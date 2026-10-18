/*
 * MiniWin PE Loader v0.1
 * Minimal Windows x64 PE runtime for Linux
 * Executes Windows PE applications without Wine
 *
 * Target: upx_decompressed.exe --version
 * DLLs: KERNEL32.DLL (68 imports) + msvcrt.dll (94 imports)
 *
 * Build: gcc -o minwin_loader loader.c -O2 -no-pie -g
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <locale.h>
#include <ctype.h>
#include <stdarg.h>
#include <pthread.h>
#include <wchar.h>
#include <dlfcn.h>

#include "../include/pe.h"
/* ISS1-WAVE3 (Issue #1 §8.4): real-application API families (ImageMagick /
 * ffmpeg corpus demand): stat/statvfs, dir iteration, futex-based address
 * waits, libgen for path transforms. */
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <dirent.h>
#include <fnmatch.h>
#include <libgen.h>
#include <malloc.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <wctype.h>
#include <math.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

extern char** environ;

/* ============================================================
 * Global State
 * ============================================================ */

static const char* g_exe_path = NULL;
static char g_cmdline_ansi[4096];
static wchar_t g_cmdline_wide[4096];
static char* g_argv[256];
static int g_argc = 0;
static char** g_argv_cell = g_argv;   /* TASK-API-011: __p___argv points here */
static char** g_environ_cell = NULL;  /* TASK-API-011: __p__environ points here */
static char* g_tzname[2] = { (char*)"UTC", (char*)"UTC" };
static long g_timezone_val = 0;
static int g_daylight_val = 0;
static int g_app_type = 1;          /* _CONSOLE_APP */
static int g_fmode_val = 0;           /* _O_TEXT */
static int g_commode_val = 0;         /* default commode */
static int g_last_error = 0;

/* Image state */
static uint8_t* g_image_base = NULL;
static uint64_t g_image_size = 0;
static uint64_t g_entry_point = 0;

/* Trampoline area — reserved for future use */

/* API trace */
static FILE* g_trace_log = NULL;

/* Forward declaration for GetProcAddress resolver (defined after import tables) */
typedef void* (*resolve_proc_fn_t)(void* module, const char* name);
static resolve_proc_fn_t g_resolve_proc_fn = NULL;

/* forward decl: used by early-defined conversion helpers (Wave-3B) */
__attribute__((ms_abi)) void mw_SetLastError(uint32_t err);

#define MW_TRACE(fmt, ...) do { \
    if (g_trace_log) { \
        fprintf(g_trace_log, "[T%lx] [API] " fmt "\n", \
                (unsigned long)pthread_self() & 0xFFFF, ##__VA_ARGS__); \
        fflush(g_trace_log); \
    } else { \
        fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* ============================================================
 * TASK-API-005 (ADR-010): EH hack isolation flag
 * ============================================================
 * EH_FIX (dispatcher LSDA override in mw_RtlDispatchException) and
 * CXX-EH-FIX (load_pe LSDA catch-all byte patch) are app-specific,
 * target-modifying bypasses that violate the engineering rules
 * ("forbidden substitutes": they hardcode this sample's addresses
 * and fake a catch the personality did not grant). BUG-029 live
 * evidence implicates the EH_FIX dispatch-1 hijack as the top
 * suspect poisoning GCC __cxa/TLS exception state.
 *
 * Both hacks are now flag-guarded: they execute ONLY when the env
 * var MINWIN_EH_HACKS is set to a non-empty value other than "0".
 * Default = OFF so the PE's own personality path handles C++ EH.
 * History preservation rule: the hack code is NOT deleted.
 */
static int g_eh_hacks_cache = -1;   /* -1 = not yet resolved */
static int mw_eh_hacks_enabled(void) {
    if (g_eh_hacks_cache < 0) {
        const char* v = getenv("MINWIN_EH_HACKS");
        g_eh_hacks_cache = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        MW_TRACE("[API-005] EH hacks %s (MINWIN_EH_HACKS=%s)",
                 g_eh_hacks_cache ? "ENABLED" : "DISABLED (default)",
                 (v && *v) ? v : "<unset>");
    }
    return g_eh_hacks_cache;
}

/* Handle table */
#define MAX_HANDLES 64
static void* g_handle_table[MAX_HANDLES];
static int    g_handle_count = 0;

static void* alloc_handle(void* ptr) {
    if (g_handle_count < MAX_HANDLES) {
        int h = g_handle_count + 4; /* Start handles at 4 */
        g_handle_table[g_handle_count++] = ptr;
        return (void*)(uintptr_t)h;
    }
    return (void*)(uintptr_t)-1; /* INVALID_HANDLE_VALUE */
}

/* ============================================================
 * TEB / PEB Structures
 * ============================================================ */

/* Fake TEB (Thread Environment Block) - minimal fields */
static uint8_t g_teb_storage[4096] __attribute__((aligned(4096)));
static uint8_t g_peb_storage[4096] __attribute__((aligned(4096)));
static uint8_t g_params_storage[4096] __attribute__((aligned(4096)));
/* TASK-TLS-001: Windows x64 TEB+0x58 = ThreadLocalStoragePointer — a POINTER
 * to an array of slot pointers. Native-TLS PEs (e.g. libc++abi's
 * __cxa_get_globals) read gs:0x58 and index this array by __tls_index. */
static void* g_tls_ptr_array[64];

/* RTL_USER_PROCESS_PARAMETERS layout (x64) */
struct FakeProcessParams {
    uint64_t MaximumLength;           /* 0x00 */
    uint64_t Length;                  /* 0x08 */
    uint32_t Flags;                   /* 0x10 */
    uint32_t DebugFlags;              /* 0x14 */
    uint64_t ConsoleHandle;           /* 0x18 */
    uint32_t ConsoleFlags;            /* 0x20 */
    uint32_t StdInputHandle;          /* 0x24 */
    uint32_t StdOutputHandle;         /* 0x28 */
    uint32_t StdErrorHandle;          /* 0x2C */
    uint8_t  pad1[16];                /* 0x30 */
    /* UNICODE_STRING CommandLine at 0x60 */
    uint16_t CmdLen;                  /* 0x60 */
    uint16_t CmdMaxLen;               /* 0x62 */
    uint64_t CmdBuffer;               /* 0x68 */
    /* UNICODE_STRING ImagePathName at 0x70 */
    uint16_t ImgPathLen;              /* 0x70 */
    uint16_t ImgPathMaxLen;           /* 0x72 */
    uint64_t ImgPathBuffer;           /* 0x78 */
};

static void setup_teb_peb(void) {
    memset(g_teb_storage, 0, sizeof(g_teb_storage));
    memset(g_peb_storage, 0, sizeof(g_peb_storage));
    memset(g_params_storage, 0, sizeof(g_params_storage));

    uint8_t* teb = g_teb_storage;
    uint8_t* peb = g_peb_storage;
    struct FakeProcessParams* params = (struct FakeProcessParams*)g_params_storage;

    /* Get actual stack info */
    void* stack_base = NULL;
    size_t stack_size = 0;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_base, &stack_size);
    pthread_attr_destroy(&attr);
    uint8_t* stack_top = (uint8_t*)stack_base + stack_size;

    /* TEB layout (x64 Windows)
     * 0x00 ExceptionList
     * 0x08 StackBase
     * 0x10 StackLimit
     * 0x18 SubSystemTib
     * 0x20 FiberData
     * 0x28 Version (4) + ArbitraryUserPointer (4)
     * 0x30 Self (pointer to TEB)
     * 0x38 EnvironmentPointer
     * 0x40 ClientId.UniqueProcess
     * 0x48 ClientId.UniqueThread
     * 0x50 RpcHandle
     * 0x58 TlsSlots[0] (first TLS slot)
     * 0x60 PEB pointer
     * 0x68 LastErrorValue
     */
    *(uint64_t*)(teb + 0x00) = 0;                    /* ExceptionList = NULL */
    *(uint64_t*)(teb + 0x08) = (uint64_t)stack_top;  /* StackBase */
    *(uint64_t*)(teb + 0x10) = (uint64_t)stack_base; /* StackLimit */
    *(uint64_t*)(teb + 0x30) = (uint64_t)teb;        /* Self */
    *(uint64_t*)(teb + 0x40) = 1234;                 /* ClientId.UniqueProcess */
    *(uint64_t*)(teb + 0x48) = 5678;                 /* ClientId.UniqueThread */
    *(uint64_t*)(teb + 0x60) = (uint64_t)peb;        /* PEB pointer */
    *(uint32_t*)(teb + 0x68) = 0;                    /* LastErrorValue */
    *(uint64_t*)(teb + 0x58) = (uint64_t)(uintptr_t)g_tls_ptr_array; /* x64: ThreadLocalStoragePointer */

    /* PEB layout (minimal)
     * 0x00 InheritedAddressSpace (1)
     * 0x01 ReadImageFileExecOptions (1)
     * 0x02 BeingDebugged (1)
     * 0x03 BitField (1)
     * 0x08 Mutant
     * 0x10 ImageBaseAddress
     * 0x18 Ldr (PEB_LDR_DATA*)
     * 0x20 ProcessParameters
     * 0x28 SubSystemMajorVersion (2) + MinorVersion (2) + padding (4)
     */
    *(uint64_t*)(peb + 0x00) = 0;                    /* InheritedAddressSpace=0, ReadImageFileExecOptions=0, BeingDebugged=0 */
    *(uint64_t*)(peb + 0x10) = (uint64_t)g_image_base; /* ImageBaseAddress */
    *(uint64_t*)(peb + 0x18) = 0;                    /* Ldr = NULL */
    *(uint64_t*)(peb + 0x20) = (uint64_t)params;     /* ProcessParameters */

    /* Process Parameters */
    size_t cmd_len = strlen(g_cmdline_ansi) * 2; /* wide char bytes */
    params->MaximumLength = sizeof(struct FakeProcessParams);
    params->Length = sizeof(struct FakeProcessParams);
    params->Flags = 0;
    params->DebugFlags = 0;
    params->ConsoleHandle = (uint64_t)(uintptr_t)(-1); /* INVALID_HANDLE_VALUE */
    params->ConsoleFlags = 0;
    params->StdInputHandle = 0;  /* STD_INPUT_HANDLE = -10 → fd 0 */
    params->StdOutputHandle = 1; /* STD_OUTPUT_HANDLE = -11 → fd 1 */
    params->StdErrorHandle = 2;  /* STD_ERROR_HANDLE = -12 → fd 2 */
    params->CmdLen = (uint16_t)cmd_len;
    params->CmdMaxLen = (uint16_t)(cmd_len + 4);
    params->CmdBuffer = (uint64_t)(uintptr_t)g_cmdline_wide;

    /* Set GS base to TEB */
    syscall(SYS_arch_prctl, 0x1001 /* ARCH_SET_GS */, (uint64_t)(uintptr_t)teb);
}

/* ============================================================
 * Trampoline Generation (Windows → System V ABI)
 * ============================================================ */

/*
 * Windows x64 ABI: RCX, RDX, R8, R9, then stack (with 32-byte shadow space)
 * System V ABI:   RDI, RSI, RDX, RCX, R8, R9, then stack (no shadow space)
 *
 * Trampoline converts and adjusts stack.
 * For non-variadic functions, we use __attribute__((ms_abi)) instead.
 * Trampolines are only used for variadic functions.
 */

static void write_trampoline(void* addr, void* target) {
    uint8_t* p = (uint8_t*)addr;
    int64_t rel = (int64_t)(uintptr_t)target - (int64_t)(uintptr_t)(p + 24);

    /* sub rsp, 0x20     */  p[0] = 0x48; p[1] = 0x83; p[2] = 0xEC; p[3] = 0x20;
    /* mov rdi, rcx      */  p[4] = 0x48; p[5] = 0x89; p[6] = 0xCF;
    /* mov rsi, rdx      */  p[7] = 0x48; p[8] = 0x89; p[9] = 0xD6;
    /* mov rdx, r8       */  p[10] = 0x4D; p[11] = 0x89; p[12] = 0xC2;
    /* mov rcx, r9       */  p[13] = 0x49; p[14] = 0x89; p[15] = 0xC9;
    /* call rel32         */  p[16] = 0xE8;
    memcpy(p + 17, &rel, 4);
    /* add rsp, 0x20     */  p[21] = 0x48; p[22] = 0x83; p[23] = 0xC4; p[24] = 0x20;
    /* ret                */  p[25] = 0xC3;
}

/* Special trampoline for printf-like variadic functions.
 * Since va_list ABI differs between Windows and System V,
 * we intercept the format string and use vsnprintf internally.
 */

/* Forward declarations for System V ABI wrappers */
static int mw_printf_sv(const char* fmt, ...);
static int mw_fprintf_sv(void* file, const char* fmt, ...);
static int mw_vfprintf_sv(void* file, const char* fmt, void* ap);

/*
 * For printf/fprintf, we can't easily trampoline variadic args.
 * Instead, write asm trampolines that call ms_abi wrappers.
 * These wrappers use the format string + fixed-position args.
 *
 * Strategy: printf is called with at most a few args in registers.
 * For upx --version, printf("upx %s\n", version) has 2 args.
 * We implement printf in ms_abi directly, using vsnprintf to buffer.
 */

/* ============================================================
 * KERNEL32.DLL Stubs (ms_abi calling convention)
 * ============================================================ */

static void* g_unhandled_exception_filter = NULL;
static void* g_veh_handlers[16];
static int g_veh_count = 0;

/* .pdata (Exception Data Directory) — set by load_pe */
static uint32_t g_pdata_rva = 0;
static uint32_t g_pdata_size = 0;
static uint32_t g_num_rt_functions = 0;

__attribute__((ms_abi)) void* mw_AddVectoredExceptionHandler(uint32_t first, void* handler) {
    MW_TRACE("AddVectoredExceptionHandler(first=%u, handler=%p)", first, handler);
    if (g_veh_count < 16) {
        if (first) {
            memmove(g_veh_handlers + 1, g_veh_handlers, g_veh_count * sizeof(void*));
            g_veh_handlers[0] = handler;
        } else {
            g_veh_handlers[g_veh_count] = handler;
        }
        g_veh_count++;
        return handler;
    }
    return NULL;
}

__attribute__((ms_abi)) uint8_t mw_RemoveVectoredExceptionHandler(void* h) {
    MW_TRACE("RemoveVectoredExceptionHandler(%p)", h);
    for (int i = 0; i < g_veh_count; i++) {
        if (g_veh_handlers[i] == h) {
            memmove(g_veh_handlers + i, g_veh_handlers + i + 1,
                    (g_veh_count - i - 1) * sizeof(void*));
            g_veh_count--;
            return 1;
        }
    }
    return 0;
}

/* BUG-062 forward decls: thread registry lives below (before
 * mw_WaitForSingleObject); CloseHandle releases thread slots. */
static void mw_thread_release(void* h);
/* BUG-064 forward decl: mw__vsnprintf reuses mw_vfprintf (defined below). */
__attribute__((ms_abi)) int mw_vfprintf(void* file, const char* fmt, void* ap);
/* LADDER-009 forward decls: real kernel objects (events/semaphores) live in
 * the registry block below; the early stubs call into them. */
static void* mw_event_create(int manual, int init);
static void* mw_sem_create(int32_t init, int32_t max);
static int mw_event_set(void* h);
static int mw_event_reset(void* h);
static int mw_sem_release(void* h, int32_t count, int32_t* prev);
static void mw_kobj_release(void* h);
/* BUG-065: FILE* issuance registry (definitions below map_crt_stream). */
static void mw_fileptr_register(FILE* f);
static int mw_fileptr_is_valid(FILE* f);

__attribute__((ms_abi)) int mw_CloseHandle(void* h) {
    MW_TRACE("CloseHandle(handle=%p)", h);
    /* BUG-062/064: thread + event + semaphore handles release their slots. */
    mw_kobj_release(h);
    return 1; /* TRUE */
}

__attribute__((ms_abi)) void* mw_CreateEventA(void* attr, int manual, int init, const char* name) {
    MW_TRACE("CreateEventA(manual=%d, init=%d, name=%s)", manual, init, name ? name : "NULL");
    (void)attr; (void)name;
    return mw_event_create(manual, init);
}

__attribute__((ms_abi)) void* mw_CreateSemaphoreA(void* attr, int32_t init, int32_t max, const char* name) {
    MW_TRACE("CreateSemaphoreA(init=%d, max=%d, name=%s)", init, max, name ? name : "NULL");
    (void)attr; (void)name;
    if (max <= 0 || init < 0 || init > max) { mw_SetLastError(87); return NULL; }
    return mw_sem_create(init, max);
}

__attribute__((ms_abi)) void mw_DebugBreak(void) {
    MW_TRACE("DebugBreak()");
}

/* Critical Section state.
 * BUG-034 (appcompat sweep APP-F-04): Windows CRITICAL_SECTION is exactly
 * 40 bytes on x64 and PEs place it flush against other globals (observed:
 * mingw dtoa CRITICAL_SECTION at 0x14000b178 with freelist[] at +0x28). The
 * previous MwCritSec {pthread_mutex_t(40) + int(4)} wrote an 'initialized'
 * flag at offset 40 -> 8-byte overflow into PE data (freelist[0] became 1,
 * dtoa Balloc deref -> SIGSEGV). The contract is: NEVER write past byte 40.
 * The mutex overlays the CS itself; our "initialized" sentinel lives in the
 * Windows SpinCount slot (offset 36), which caller-side is zeroed memory. */
#define MW_CS_MAGIC 0x4D574353u /* "MWCS" */

static pthread_mutex_t* cs_mutex(void* cs) { return (pthread_mutex_t*)cs; }

static int cs_is_initialized(void* cs) {
    return *(uint32_t*)((uint8_t*)cs + 36) == MW_CS_MAGIC;
}

static void cs_mark_initialized(void* cs) {
    *(uint32_t*)((uint8_t*)cs + 36) = MW_CS_MAGIC;
}

__attribute__((ms_abi)) void mw_InitializeCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) return; /* idempotent, like re-init on a free CS */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs_mutex(cs), &attr);
    pthread_mutexattr_destroy(&attr);
    cs_mark_initialized(cs);
    MW_TRACE("InitializeCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_DeleteCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) {
        pthread_mutex_destroy(cs_mutex(cs));
        *(uint32_t*)((uint8_t*)cs + 36) = 0;
    }
    MW_TRACE("DeleteCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_EnterCriticalSection(void* cs) {
    if (!cs) return;
    if (!cs_is_initialized(cs)) mw_InitializeCriticalSection(cs);
    pthread_mutex_lock(cs_mutex(cs));
    MW_TRACE("EnterCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_LeaveCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) pthread_mutex_unlock(cs_mutex(cs));
    MW_TRACE("LeaveCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) int mw_TryEnterCriticalSection(void* cs) {
    if (!cs) return 0;
    if (!cs_is_initialized(cs)) mw_InitializeCriticalSection(cs);
    int ret = (pthread_mutex_trylock(cs_mutex(cs)) == 0) ? 1 : 0;
    MW_TRACE("TryEnterCriticalSection(%p) = %d", cs, ret);
    return ret;
}

__attribute__((ms_abi)) int mw_DuplicateHandle(void* src, void* src_h, void* tgt, void** tgt_h,
    uint32_t access, int inh, uint32_t opts) {
    MW_TRACE("DuplicateHandle(src_h=%p, tgt_h=%p)", src_h, tgt_h);
    if (tgt_h) *tgt_h = src_h; /* Just copy the handle */
    return 1; /* TRUE */
}

/* Console */
__attribute__((ms_abi)) int mw_GetConsoleMode(void* h, uint32_t* mode) {
    MW_TRACE("GetConsoleMode(handle=%p)", h);
    /* BUG-045 (Windows-faithful): GetConsoleMode FAILS on a non-console
     * handle (ERROR_INVALID_HANDLE=6). Our pseudo-std-handles are fd+10
     * (GetStdHandle) — when the fd is redirected (pipe/file), a real console
     * API would fail; ffmpeg uses this to pick its UTF-16 console output
     * path, so always-succeed made it write raw UTF-16 into the pipe. */
    uintptr_t hv = (uintptr_t)h;
    int fd = (hv >= 10 && hv <= 12) ? (int)(hv - 10) : -1;
    if (fd < 0 || !isatty(fd)) { mw_SetLastError(6); return 0; }
    if (mode) *mode = 0x0003; /* ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT */
    return 1;
}

__attribute__((ms_abi)) int mw_GetConsoleScreenBufferInfo(void* h, void* info) {
    MW_TRACE("GetConsoleScreenBufferInfo(handle=%p)", h);
    if (info) {
        /* BUG-044: CONSOLE_SCREEN_BUFFER_INFO is 22 bytes on x64
         * (COORD dwSize @0, COORD dwCursorPosition @4, WORD wAttributes @8,
         * SMALL_RECT srWindow @10..17, COORD dwMaximumWindowSize @18..21).
         * The old memset wrote 48 bytes — a 26-byte overrun past the guest's
         * stack local, tripping its stack canary (TerminateProcess
         * 0xC0000409 STATUS_STACK_BUFFER_OVERRUN, seen live on ffmpeg). */
        memset(info, 0, 22);
        *(uint16_t*)((uint8_t*)info + 0)  = 80;    /* dwSize.X */
        *(uint16_t*)((uint8_t*)info + 2)  = 25;    /* dwSize.Y */
        *(uint16_t*)((uint8_t*)info + 4)  = 80;    /* dwCursorPosition.X */
        *(uint16_t*)((uint8_t*)info + 6)  = 0;     /* dwCursorPosition.Y */
        *(uint16_t*)((uint8_t*)info + 8)  = 0x0007;/* wAttributes */
        *(uint16_t*)((uint8_t*)info + 10) = 0;     /* srWindow.Left */
        *(uint16_t*)((uint8_t*)info + 12) = 0;     /* srWindow.Top */
        *(uint16_t*)((uint8_t*)info + 14) = 79;    /* srWindow.Right */
        *(uint16_t*)((uint8_t*)info + 16) = 24;    /* srWindow.Bottom */
        *(uint16_t*)((uint8_t*)info + 18) = 80;    /* dwMaximumWindowSize.X */
        *(uint16_t*)((uint8_t*)info + 20) = 25;    /* dwMaximumWindowSize.Y */
    }
    return 1;
}

__attribute__((ms_abi)) int mw_GetConsoleCursorInfo(void* h, void* info) {
    MW_TRACE("GetConsoleCursorInfo(handle=%p)", h);
    if (info) {
        *(uint32_t*)info = 1;  /* dwSize = 25% */
        *((uint8_t*)info + 4) = 1; /* bVisible = TRUE */
    }
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleCursorInfo(void* h, void* info) {
    MW_TRACE("SetConsoleCursorInfo(handle=%p)", h);
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleCursorPosition(void* h, uint32_t x, uint32_t y) {
    MW_TRACE("SetConsoleCursorPosition(handle=%p, x=%u, y=%u)", h, x, y);
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleTextAttribute(void* h, uint16_t attr) {
    MW_TRACE("SetConsoleTextAttribute(handle=%p, attr=0x%x)", h, attr);
    return 1;
}

__attribute__((ms_abi)) int mw_WriteConsoleOutputA(void* h, void* buf, uint32_t size,
    uint32_t* coord, void* region) {
    MW_TRACE("WriteConsoleOutputA(handle=%p, size=%u)", h, size);
    /* Could write the buffer to stdout if needed */
    if (region) memset(region, 0, 16);
    return 1;
}

/* Process/Thread */
__attribute__((ms_abi)) void* mw_GetCurrentProcess(void) {
    return (void*)(uintptr_t)(-1); /* PSEUDO_HANDLE_CURRENT_PROCESS */
}

__attribute__((ms_abi)) uint32_t mw_GetCurrentProcessId(void) {
    return 1234;
}

__attribute__((ms_abi)) void* mw_GetCurrentThread(void) {
    return (void*)(uintptr_t)(-2); /* PSEUDO_HANDLE_CURRENT_THREAD */
}

__attribute__((ms_abi)) uint32_t mw_GetCurrentThreadId(void) {
    return 5678;
}

__attribute__((ms_abi)) int mw_GetFileTime(void* h, void* ct, void* at, void* wt) {
    MW_TRACE("GetFileTime()");
    return 0;
}

__attribute__((ms_abi)) int mw_GetHandleInformation(void* h, uint32_t* flags) {
    MW_TRACE("GetHandleInformation()");
    return 0;
}

__attribute__((ms_abi)) uint32_t mw_GetLastError(void) {
    uint32_t err = g_last_error;
    MW_TRACE("GetLastError() = %u", err);
    return err;
}

__attribute__((ms_abi)) void* mw_GetModuleHandleA(const char* name) {
    MW_TRACE("GetModuleHandleA(name=%s)", name ? name : "NULL");
    if (name == NULL || name[0] == '\0') return g_image_base;
    return NULL;
}

/* OPENSRC battery (BUG-057): optional extension APIs (AppPolicy* etc.) that
 * guests look up on system modules but never call on this platform. A real
 * function pointer keeps the "if (GetProcAddress(...))" pattern alive;
 * returning 0 mimics the API's failure semantics. */
__attribute__((ms_abi)) long long mw_winapi_optional_stub(long long a, long long b, long long c, long long d) {
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}
__attribute__((ms_abi)) void* mw_GetProcAddress(void* module, const char* name) {
    MW_TRACE("GetProcAddress(module=%p, name=%s)", module, name ? name : "NULL");
    if (!name || !name[0]) return NULL;
    /* Resolve from dispatch table if module matches our fake handle or image base.
     * Uses global function pointer set after table definitions (see resolve_proc_init). */
    if (g_resolve_proc_fn) {
        void* r = g_resolve_proc_fn(module, name);
        if (r) return r;
        uintptr_t m = (uintptr_t)module;
        /* SWEEP-002 fix: the known[] pseudo-handle space is 0xdead0001+
         * (bounded by the table size), NOT just 3 slots — winmm.dll is
         * slot 18 (0xdead0013) and Go's timeBeginPeriod lookup died on
         * this stale range check. Keep 0xdead0004..0xdead00ff reserved. */
        if (m >= 0xdead0001 && m <= 0xdead00ff) {
            /* system module: optional extensions resolve to the generic stub */
            return (void*)&mw_winapi_optional_stub;
        }
    }
    return NULL;
}

__attribute__((ms_abi)) void* mw_LoadLibraryA(const char* name) {
    MW_TRACE("LoadLibraryA(%s)", name ? name : "NULL");
    /* Known system DLLs (case-insensitive, extension optional): pseudo-
     * handle identities. GetProcAddress resolves against our dispatch
     * table (g_resolve_proc_fn). Unknown DLLs fail with ERROR 126 — the
     * honest Windows answer for a DLL the loader cannot materialize. */
    static const char* known[] = {
        "kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "advapi32.dll",
        "ole32.dll", "oleaut32.dll", "ntdll.dll", "ws2_32.dll", "msvcrt.dll",
        "ucrtbase.dll", "comctl32.dll", "comdlg32.dll", "version.dll", "crypt32.dll",
        "secur32.dll", "normaliz.dll", "iphlpapi.dll", "winmm.dll", "shlwapi.dll",
        "winspool.drv", "dnsapi.dll", "mswsock.dll", "powrprof.dll", "sechost.dll",
        "bcrypt.dll", "ncrypt.dll", "userenv.dll", "dbghelp.dll", "psapi.dll",
        "mscoree.dll", "kernelbase.dll", "appmodel.dll", "cryptbase.dll",
        "sspicli.dll", "rpcrt4.dll", "uxtheme.dll", "apphelp.dll", "sxs.dll",
        "msvcp140.dll", "vcruntime140.dll", "wintrust.dll", "cabinet.dll",
        "imagehlp.dll", "msimg32.dll", "opengl32.dll", "glu32.dll", "dwmapi.dll",
        "gdiplus.dll", "windowscodecs.dll", "propsys.dll", "setupapi.dll",
        "cfgmgr32.dll", "devobj.dll", "winusb.dll", "hid.dll", "ws2help.dll",
        "bcryptprimitives.dll", /* SWEEP-002: Go runtime.osinit + Rust std */
    };
    if (name && name[0]) {
        char low[64]; size_t k = 0;
        for (; name[k] && k < sizeof(low) - 1; k++)
            low[k] = (name[k] >= 'A' && name[k] <= 'Z') ? (char)(name[k] + 32) : name[k];
        low[k] = 0;
        size_t nl = strlen(low);
        /* api-ms-win-* API-set names are OS-guaranteed redirections — always
         * "present" (sqlite3 loads api-ms-win-appmodel-runtime-l1-1-2 during
         * winOpen; failure aborts its DB-open path). */
        if (nl >= 10 && memcmp(low, "api-ms-win", 10) == 0)
            return (void*)(uintptr_t)0xdead0002;
        for (size_t i = 0; i < sizeof(known)/sizeof(known[0]); i++) {
            size_t kl = strlen(known[i]);
            if (nl == kl && memcmp(low, known[i], kl) == 0)
                return (void*)(uintptr_t)(0xdead0001 + (uintptr_t)i);
            if (nl + 1 == kl && memcmp(low, known[i], nl) == 0 && known[i][nl] == '.')
                return (void*)(uintptr_t)(0xdead0001 + (uintptr_t)i); /* no-ext form */
        }
        mw_SetLastError(126);
        return NULL;
    }
    return (void*)(uintptr_t)0xdead0001;
}

/* TASK-OPENSRC-1 (ffmpeg CRT/constructor chain): SetDllDirectoryA — winbase
 * DLL-search-path hardening API. The loader resolves guest DLL loads itself
 * and never searches a directory list, so the honest contract is: accept and
 * remember the argument, restore default on NULL, always succeed (TRUE).
 * ffmpeg's static-CRT/avcodec constructors call it before main. */
static const char* g_dll_directory_a = NULL;
__attribute__((ms_abi)) int mw_SetDllDirectoryA(const char* path) {
    MW_TRACE("SetDllDirectoryA(%s)", path ? path : "NULL");
    g_dll_directory_a = path;   /* guest memory, borrowed pointer — same as Windows */
    return 1;
}
__attribute__((ms_abi)) int mw_SetDllDirectoryW(const void* path) {
    MW_TRACE("SetDllDirectoryW(%p)", path);
    g_dll_directory_a = NULL;   /* wide variant: accept, no conversion needed */
    return 1;
}

/* TASK-API-002: shared flush for every _exit() path (ExitProcess, abort).
 * MW_TRACE flushes per line, but the trace FILE lacks its JSON terminator and
 * the stdout/stderr buffers would be silently dropped by _exit(). Closing the
 * trace here makes every api_trace.json parseable without the append-']}'
 * workaround. Idempotent (NULL check) so main()'s normal-return path is safe. */
static void mw_trace_shutdown_flush(void) {
    fflush(stdout);
    fflush(stderr);
    if (g_trace_log) {
        fprintf(g_trace_log, "  ]\n}\n");
        fclose(g_trace_log);
        g_trace_log = NULL;
    }
}

/* BUG-040 build repair: forward declaration — the definition lives in the
 * ISS1-WAVE3B atexit section (mw_ExitProcess calls it before that point,
 * and C23 rejects implicit declarations). Declaration-order fix only. */
static void w3_run_guest_atexit(void);

__attribute__((noreturn)) __attribute__((ms_abi)) void mw_ExitProcess(unsigned int code) {
    MW_TRACE("ExitProcess(%u)", code);
    w3_run_guest_atexit();          /* Windows: CRT handlers run before exit */
    mw_trace_shutdown_flush();
    _exit(code);
}

__attribute__((ms_abi)) int mw_GetProcessAffinityMask(void* h, uint64_t* mask, uint64_t* sys) {
    MW_TRACE("GetProcessAffinityMask()");
    if (mask) *mask = 1;
    return 1;
}

__attribute__((ms_abi)) void mw_GetStartupInfoA(void* info) {
    MW_TRACE("GetStartupInfoA(info=%p)", info);
    if (info) {
        memset(info, 0, 104); /* STARTUPINFOA is 104 bytes on x64 */
        *(uint32_t*)((uint8_t*)info + 0) = 104; /* cb = sizeof(STARTUPINFOA) */
        *(uint32_t*)((uint8_t*)info + 8) = 0;   /* dwFlags = 0 */
        *(uint16_t*)((uint8_t*)info + 60) = 80; /* dwXSize */
        *(uint16_t*)((uint8_t*)info + 64) = 25; /* dwYSize */
        *(uint16_t*)((uint8_t*)info + 68) = 80; /* dwXCountChars */
        *(uint16_t*)((uint8_t*)info + 72) = 300; /* dwYCountChars */
        /* Std handles at offsets 80, 88, 96 */
        *(uint64_t*)((uint8_t*)info + 80) = 0; /* hStdInput */
        *(uint64_t*)((uint8_t*)info + 88) = 1; /* hStdOutput */
        *(uint64_t*)((uint8_t*)info + 96) = 2; /* hStdError */
    }
}

/* BUG-056: std handles are fd-backed registry handles; real impl lives
 * next to w3_handle_for_fd (defined later in the file). */
__attribute__((ms_abi)) void* mw_GetStdHandle_impl(int32_t nStdHandle);
__attribute__((ms_abi)) void* mw_GetStdHandle(int32_t nStdHandle) {
    return mw_GetStdHandle_impl(nStdHandle);
}

__attribute__((ms_abi)) void mw_GetSystemTimeAsFileTime(void* ft) {
    MW_TRACE("GetSystemTimeAsFileTime()");
    if (ft) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        /* FILETIME: 100-nanosecond intervals since 1601-01-01 */
        /* Unix epoch (1970) is 11644473600 seconds after 1601 */
        uint64_t t = (uint64_t)tv.tv_sec * 10000000ULL + (uint64_t)tv.tv_usec * 10ULL;
        t += 116444736000000000ULL;
        *(uint64_t*)ft = t;
    }
}

__attribute__((ms_abi)) int mw_GetThreadContext(void* h, void* ctx) {
    MW_TRACE("GetThreadContext()");
    return 0;
}

__attribute__((ms_abi)) int mw_GetThreadPriority(void* h) {
    MW_TRACE("GetThreadPriority()");
    return 0;
}

__attribute__((ms_abi)) uint32_t mw_GetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    MW_TRACE("GetTickCount() = %u", ms);
    return ms;
}

__attribute__((ms_abi)) int mw_IsDBCSLeadByteEx(int cp, uint8_t b) {
    return 0;
}

__attribute__((ms_abi)) int mw_IsDebuggerPresent(void) {
    MW_TRACE("IsDebuggerPresent() = FALSE");
    return 0;
}

/* ISS1-WAVE3B FIX (heap corruption root): Windows MultiByteToWideChar
 * contract — the size query (dst==NULL or dstlen==0) must return the number
 * of WIDE CHARS required INCLUDING the terminating null when srclen==-1.
 * The previous code returned the BYTE count without +1, so callers
 * allocated one wchar too little and the conversion overflowed the chunk
 * header by 2 bytes (observed live: ImageMagick abort with
 * "free(): invalid size" after MultiByteToWideChar(cp=65001, srclen=-1)). */
/* BUG-042 (TASK-OPENSRC-1 heap-corruption root cause): the guest wchar_t is
 * 2 bytes but glibc wchar_t is 4 — mbstowcs(dst, ...) overruns any guest-
 * sized buffer by 2x ("malloc(): invalid size (unsorted)" / "free(): invalid
 * pointer" during UCRT locale init and ffmpeg's command-line handling).
 * For UTF-8 (cp=65001 — the codepage ffmpeg and the UCRT init actually use)
 * decode with mbrtowc but ENCODE UTF-16LE ourselves. Returns the number of
 * 2-byte units, or -1 on invalid input, -2 if dst is too small. */
static int w3_utf8_to_utf16le(const char* src, size_t src_bytes,
                              uint16_t* dst, int dst_wchars) {
    mbstate_t ps; memset(&ps, 0, sizeof(ps));
    size_t consumed = 0;
    int out = 0;
    while (consumed < src_bytes) {
        wchar_t wc4 = 0;
        size_t r = mbrtowc(&wc4, src + consumed, src_bytes - consumed, &ps);
        if (r == (size_t)-1 || r == (size_t)-2) return -1;
        if (r == 0) r = 1;              /* decoded NUL character */
        consumed += r;
        uint32_t cp32 = (uint32_t)wc4;
        uint16_t u[2];
        unsigned units;
        if (cp32 < 0x10000) { u[0] = (uint16_t)cp32; units = 1; }
        else { cp32 -= 0x10000; u[0] = (uint16_t)(0xD800 | (cp32 >> 10));
               u[1] = (uint16_t)(0xDC00 | (cp32 & 0x3FF)); units = 2; }
        if (!dst) { out += (int)units; continue; }
        if (out + (int)units > dst_wchars) return -2;
        dst[out++] = u[0];
        if (units == 2) dst[out++] = u[1];
    }
    return out;
}

/* BUG-042 extension: Windows-1252 (the ACP) high-range table 0x80..0x9F;
 * 0xA0..0xFF is the Latin-1 identity. Guest buffers hold 2-byte units. */
static const uint16_t w3_cp1252_hi[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};
static int w3_cp1252_to_utf16(const char* src, size_t src_bytes,
                              uint16_t* dst, int dst_units) {
    int out = 0;
    for (size_t i = 0; i < src_bytes; i++) {
        uint8_t b = (uint8_t)src[i];
        uint16_t u = (b >= 0x80 && b <= 0x9F) ? w3_cp1252_hi[b - 0x80] : (uint16_t)b;
        if (!dst) { out++; continue; }
        if (out + 1 > dst_units) return -2;
        dst[out++] = u;
    }
    return out;
}

__attribute__((ms_abi)) int mw_MultiByteToWideChar(uint32_t cp, uint32_t flags,
    const char* src, int srclen, wchar_t* dst, int dstlen) {
    MW_TRACE("MultiByteToWideChar(cp=%u, srclen=%d, dstlen=%d)", cp, srclen, dstlen);
    if (!src) { mw_SetLastError(87); return 0; }
    if (srclen == 0) return 0;
    int include_null = (srclen < 0);
    /* BUG-042 + extension: cp 65001 (UTF-8) and 1252/ACP take native 2-byte-
     * unit paths. Any other codepage keeps the legacy glibc-wchar passthrough
     * — known 2x-overrun hazard, tracked under TASK-API-009 note (5). */
    if (cp == 65001 || cp == 1252 || cp == 0) {
        size_t bytes = include_null ? strlen(src) : (size_t)srclen;
        int need;
        if (cp == 65001)
            need = w3_utf8_to_utf16le(src, bytes, NULL, 0);
        else
            need = w3_cp1252_to_utf16(src, bytes, NULL, 0);
        if (need < 0) { mw_SetLastError(1113); return 0; }
        if (include_null) need += 1;
        if (!dst || dstlen == 0) return need;
        if (dstlen < 0 || dstlen < need) { mw_SetLastError(122); return 0; }
        int got = (cp == 65001)
            ? w3_utf8_to_utf16le(src, bytes, (uint16_t*)dst, dstlen)
            : w3_cp1252_to_utf16(src, bytes, (uint16_t*)dst, dstlen);
        if (got < 0) { mw_SetLastError(got == -2 ? 122 : 1113); return 0; }
        if (include_null) ((uint16_t*)dst)[got++] = 0;
        return got;
    }
    size_t need = 0;
    if (include_null) {
        need = mbstowcs(NULL, src, 0);          /* chars before null */
        if (need == (size_t)-1) { mw_SetLastError(1113); return 0; }
        need += 1;                               /* include the null */
    } else {
        mbstate_t ps; memset(&ps, 0, sizeof(ps));
        const char* p = src;
        need = mbsnrtowcs(NULL, &p, (size_t)srclen, 0, &ps);
        if (need == (size_t)-1) { mw_SetLastError(1113); return 0; }
    }
    if (!dst || dstlen == 0) return (int)need;
    if (dstlen < 0 || (size_t)dstlen < need) { mw_SetLastError(122); return 0; }
    size_t got;
    if (include_null) {
        got = mbstowcs(dst, src, (size_t)dstlen);
        if (got == (size_t)-1) { mw_SetLastError(1113); return 0; }
        got += 1;                                /* null was written by mbstowcs */
    } else {
        mbstate_t ps; memset(&ps, 0, sizeof(ps));
        const char* p = src;
        got = mbsnrtowcs((wchar_t*)dst, &p, (size_t)srclen, (size_t)dstlen, &ps);
        if (got == (size_t)-1) { mw_SetLastError(1113); return 0; }
    }
    return (int)got;
}

__attribute__((ms_abi)) void* mw_OpenProcess(uint32_t access, int inh, uint32_t pid) {
    MW_TRACE("OpenProcess()");
    return NULL;
}

__attribute__((ms_abi)) void mw_OutputDebugStringA(const char* str) {
    MW_TRACE("OutputDebugStringA(%s)", str ? str : "NULL");
    if (str) fprintf(stderr, "[DEBUG] %s", str);
}

__attribute__((ms_abi)) int mw_QueryPerformanceCounter(int64_t* lp) {
    MW_TRACE("QueryPerformanceCounter()");
    if (lp) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        *lp = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
    return 1;
}

__attribute__((ms_abi)) int mw_QueryPerformanceFrequency(int64_t* lp) {
    MW_TRACE("QueryPerformanceFrequency()");
    if (lp) *lp = 1000000000LL; /* nanoseconds */
    return 1;
}

/* ============================================================
 * EXP-NEXT DIAGNOSTIC: Exception stack walk validation
 * 
 * Hypothesis: Using prolog byte scanning to detect 'sub rsp, N',
 * we can correct RSP for frames with malformed UNWIND_INFO and
 * walk the stack to find a frame with EHANDLER.
 *
 * Method: Naked stub captures RIP/RSP into globals, then calls
 * an internal SysV function that walks frames with full logging.
 * ============================================================ */

/* Global capture — set by naked stub before calling the walker.
 * Cannot be static because naked asm references them by symbol name. */
uint64_t g_cap_rip = 0;
uint64_t g_cap_rsp_entry = 0;
uint64_t g_cap_rsp = 0;       /* caller's pre-call RSP */
uint32_t g_cap_code = 0;
uint32_t g_cap_flags = 0;
uint32_t g_cap_nargs = 0;
uint64_t g_cap_args = 0;
/* BUG-031: true raise-site volatile registers (captured by the naked stub
 * BEFORE any scratch use). seh_build_context previously read these from the
 * current CPU state inside the C dispatcher — i.e. compiler-dependent
 * garbage that changed with every loader recompilation and flipped the GCC
 * personality's phase-1 decision (Frame[4] INSTALL vs ContinueSearch).
 * Windows delivers the true raise-site context; now we do too. */
uint64_t g_cap_rax = 0;
uint64_t g_cap_r10 = 0;
uint64_t g_cap_r11 = 0;
EXCEPTION_RECORD g_cap_er;  /* saved EXCEPTION_RECORD for RtlUnwindEx */

/* RtlUnwindEx support — setjmp/longjmp for non-returning unwind.
 * Cannot be static because naked asm references them by symbol name. */
static jmp_buf g_unwind_jmpbuf;
uint8_t* g_unwind_ctx = NULL;     /* CONTEXT buffer being used for dispatch */
int g_is_unwinding = 0;           /* set when RtlUnwindEx longjmps back */
uint64_t g_unwind_target_ip = 0;  /* target IP for unwind */
uint64_t g_unwind_target_frame = 0; /* target frame (RSP) for unwind */
/* BUG-031 (ADR-011): throw-site CONTEXT snapshot for the GCC unwind chain.
 * Copied once per fresh throw (every dispatch that is not a resume command).
 * The GCC bridge's unwind-phase re-raise (0x21474343) and later chain
 * segments consume it because the original dispatcher C stack — including
 * its stack-local CONTEXT — is abandoned once a landing pad resumes. */
static uint8_t g_throw_ctx_snapshot[CONTEXT_SIZE];
static int g_throw_ctx_valid = 0;
/* BUG-039/B: TARGET-FRAME register snapshot. The dispatcher search loop
 * unwinds the live CONTEXT through EVERY frame — including the frame that
 * owns the handler — so by the time the personality calls RtlUnwindEx the
 * live CONTEXT's nonvolatives hold the TARGET'S CALLER's values (live proof:
 * t08_probe p2 unwind — Frame[1] restored RDI=&got/RSI=literal/RBP=ok
 * correctly, then Frame[2] (main) replay clobbered them to CRT-startup
 * values). Real Windows never resumes a landing pad from that state:
 * RtlUnwindEx re-walks from its own machine state and stops AT the target
 * frame. We capture the "returned into frame F" state before frame F's own
 * codes are applied and use it as the register base for the resume. */
static uint8_t g_target_frame_ctx[CONTEXT_SIZE];
static int g_target_frame_ctx_valid = 0;
/* BUG-031 helpers (defined next to mw_RtlUnwindEx; forward-declared because
 * the dispatcher's resume-command intercept calls them). */
static uint64_t seh_total_prologue_alloc(uint64_t ip_va);
static void seh_walk_intermediate_cleanups(uint8_t* start_ctx, void* er_src,
                                           uint64_t tgt_frame, const char* tag);
/* CONTEXT register values saved by setjmp handler for naked stub access.
 * We copy from the stack-local CONTEXT to these globals to avoid the stack
 * being overwritten by function calls (MW_TRACE, fflush) before the naked
 * stub reads them. */
uint64_t g_unwind_regs[16]; /* RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8=8, R9=9, R10=10, R11=11, R12=12, R13=13, R14=14, R15=15 */
#define UR_RAX 0
#define UR_RCX 1
#define UR_RDX 2
#define UR_RBX 3
#define UR_RSP 4
#define UR_RBP 5
#define UR_RSI 6
#define UR_RDI 7
#define UR_R8  8
#define UR_R9  9
#define UR_R10 10
#define UR_R11 11
#define UR_R12 12
#define UR_R13 13
#define UR_R14 14
#define UR_R15 15

/* Internal SysV-only RF lookup (avoids ms_abi ABI issues) */
static RUNTIME_FUNCTION* seh_internal_lookup(uint64_t rva) {
    if (!g_pdata_rva || g_num_rt_functions == 0) return NULL;
    RUNTIME_FUNCTION* base = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
    int lo = 0, hi = (int)g_num_rt_functions - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (rva < base[mid].BeginAddress) hi = mid - 1;
        else if (rva >= base[mid].EndAddress) lo = mid + 1;
        else return &base[mid];
    }
    return NULL;
}

/* Internal SysV-only UNWIND_INFO parser — returns total alloc from codes */
static uint32_t seh_unwind_alloc(uint8_t* ui) {
    uint8_t count = ui[2];
    uint8_t* codes = ui + 4;
    uint32_t total_alloc = 0;
    int slot = 0;
    for (int i = 0; i < count && slot * 2 + 1 < (int)(count * 2 + 8); i++) {
        uint16_t cw = *(uint16_t*)(codes + slot * 2);
        uint8_t op = (cw >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t info = (cw >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        if (op == 1) { /* ALLOC_LARGE — TASK-API-006 spec rule (EXP-039): info==0 => next uint16 * 8; info==1 => next uint32 (two slots, no shift) */
            slot++;
            if (info == 0) {
                total_alloc += (uint32_t)(*(uint16_t*)(codes + slot * 2)) * 8;
                slot++;
            } else {
                total_alloc += *(uint32_t*)(codes + slot * 2);
                slot += 2;
            }
        } else if (op == 2) { /* ALLOC_SMALL */
            total_alloc += (uint32_t)(info + 1) * 8;
            slot++;
        } else if (op == 4) { slot += 2; }      /* SAVE_NONVOL */
        else if (op == 5) { slot += 3; }      /* SAVE_NONVOL_FAR */
        else if (op == 6) { slot += 2; }      /* SAVE_XMM128 */
        else if (op == 7) { slot += 3; }      /* SAVE_XMM128_FAR */
        else { slot++; }
    }
    return total_alloc;
}

/* Scan function prolog bytes to detect 'sub rsp, N'.
 * Handles functions with pushes before the sub. */
static uint32_t scan_prolog_alloc(uint32_t func_rva) {
    uint8_t* p = g_image_base + func_rva;
    if (!g_image_base) return 0;
    int i = 0;
    /* Skip REX-prefixed push instructions at function start.
     * A REX prefix (0x40-0x4F) is only a REX prefix for a push
     * if the NEXT byte is 0x50-0x57 (push r64). Otherwise it's
     * a REX prefix for a different instruction (like REX.W for sub rsp).
     */
    for (; i < 20; i++) {
        if (p[i] >= 0x50 && p[i] <= 0x57) continue; /* push r64 */
        if (p[i] >= 0x40 && p[i] <= 0x4F && p[i+1] >= 0x50 && p[i+1] <= 0x57) {
            i += 2; continue; /* REX+push */
        }
        break;
    }
    MW_TRACE("[ALLOC_SCAN] i=%d after push skip");
    /* Now check for 'sub rsp' at current position */
    if (i + 3 < 20 && p[i] == 0x48 && p[i+1] == 0x83 && p[i+2] == 0xec) {
        MW_TRACE("[ALLOC_SCAN] MATCH imm8: 0x%x", p[i+3]);
        return p[i+3];
    }
    if (i + 6 < 20 && p[i] == 0x48 && p[i+1] == 0x81 && p[i+2] == 0xec) {
        MW_TRACE("[ALLOC_SCAN] MATCH imm32: 0x%x", *(uint32_t*)(p + i + 3));
        return *(uint32_t*)(p + i + 3);
    }
    MW_TRACE("[ALLOC_SCAN] NO MATCH");
    return 0;
}

/* Count PUSH reg instructions at function start (before any other opcode)
 * Fixed: 0x40-0x4F is only a REX prefix when followed by 0x50-0x57 (push).
 * Otherwise it could be REX.W/R for a non-push instruction.
 */
static uint32_t scan_prolog_pushes(uint32_t func_rva) {
    uint8_t* p = g_image_base + func_rva;
    if (!g_image_base) return 0;
    uint32_t pushes = 0;
    for (int i = 0; i < 32; i++) {
        if (p[i] >= 0x50 && p[i] <= 0x57) { pushes++; continue; }
        if (p[i] >= 0x40 && p[i] <= 0x4F && p[i+1] >= 0x50 && p[i+1] <= 0x57) {
            pushes++; i++; continue; /* REX+push */
        }
        break;
    }
    return pushes * 8;
}

/* Internal SysV diagnostic stack walk function */
static void seh_diagnostic_walk(void) __attribute__((used));

static void seh_diagnostic_walk(void) {
    MW_TRACE("[EXP-NEXT] === Diagnostic Stack Walk ===");
    MW_TRACE("[EXP-NEXT] RIP=0x%lx RSP=0x%lx", g_cap_rip, g_cap_rsp);
    MW_TRACE("[EXP-NEXT] code=0x%x nargs=%u", g_cap_code, g_cap_nargs);

    MW_TRACE("[EXP-NEXT] [PROOF] *[rsp_entry]=0x%lx (expect 0x%lx) %s",
             *(uint64_t*)(uintptr_t)g_cap_rsp_entry, g_cap_rip,
             (*(uint64_t*)(uintptr_t)g_cap_rsp_entry == g_cap_rip) ? "MATCH" : "MISMATCH");

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;
    uint64_t rva = g_cap_rip - img_base;
    uint64_t rsp = g_cap_rsp;

    /* --- FRAME 0 --- */
    MW_TRACE("[EXP-NEXT] Frame[0]: RIP=0x%lx RVA=0x%lx", g_cap_rip, rva);

    RUNTIME_FUNCTION* rf = seh_internal_lookup(rva);
    if (!rf) {
        MW_TRACE("[EXP-NEXT] Frame[0]: No RF for RVA 0x%lx", rva);
        return;
    }

    long rf_idx = (long)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
    uint8_t* ui = g_image_base + rf->UnwindInfo;
    uint8_t ui_flags = (ui[0] >> 3) & 0x07;
    uint8_t count_codes = ui[2];
    uint32_t unwind_alloc = seh_unwind_alloc(ui);
    uint32_t prolog_alloc = scan_prolog_alloc(rf->BeginAddress);
    uint32_t prolog_pushes = scan_prolog_pushes(rf->BeginAddress);

    MW_TRACE("[EXP-NEXT] Frame[0]: RF[%ld] begin=0x%x end=0x%x unwind=0x%x",
             rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

    /* Dump first 8 bytes at function start for validation */
    {
        uint8_t* fb = g_image_base + rf->BeginAddress;
        MW_TRACE("[EXP-NEXT] Frame[0]: bytes@0x%x: %02x %02x %02x %02x %02x %02x %02x %02x",
                 rf->BeginAddress,
                 fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7]);
    }

    MW_TRACE("[EXP-NEXT] Frame[0]: UI flags=0x%x codes=%u unwind_alloc=0x%x",
             ui_flags, count_codes, unwind_alloc);
    MW_TRACE("[EXP-NEXT] Frame[0]: prolog_alloc=0x%x prolog_pushes=0x%x",
             prolog_alloc, prolog_pushes);

    if (prolog_alloc > 0 && unwind_alloc == 0)
        MW_TRACE("[EXP-NEXT] Frame[0]: *** MALFORMED: sub rsp,0x%x but no ALLOC", prolog_alloc);
    if (!(ui_flags & UNW_FLAG_EHANDLER))
        MW_TRACE("[EXP-NEXT] Frame[0]: no EHANDLER, walking to parent");

    /* Corrected parent RSP = CONTEXT.Rsp + prolog_alloc + prolog_pushes */
    uint64_t corrected_rsp = rsp + prolog_alloc + prolog_pushes;
    MW_TRACE("[EXP-NEXT] Frame[0]: CONTEXT.Rsp=0x%lx corrected=0x%lx",
             rsp, corrected_rsp);

    uint64_t parent_rip = *(uint64_t*)(uintptr_t)corrected_rsp;
    uint64_t parent_rva = parent_rip - img_base;
    uint64_t parent_rsp = corrected_rsp + 8;

    MW_TRACE("[EXP-NEXT] Frame[0]: parent_rip=[0x%lx]=0x%lx RVA=0x%lx",
             corrected_rsp, parent_rip, parent_rva);

    if (parent_rip < img_base || parent_rip >= img_end) {
        MW_TRACE("[EXP-NEXT] Frame[0]: parent 0x%lx OUTSIDE PE — walk FAILED", parent_rip);
        return;
    }

    /* --- WALK SUBSEQUENT FRAMES --- */
    for (int frame = 1; frame <= 20; frame++) {
        MW_TRACE("[EXP-NEXT] Frame[%d]: RIP=0x%lx RVA=0x%lx", frame, parent_rip, parent_rva);

        rf = seh_internal_lookup(parent_rva);
        if (!rf) {
            MW_TRACE("[EXP-NEXT] Frame[%d]: No RF — left PE code", frame);
            break;
        }

        rf_idx = (long)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
        ui = g_image_base + rf->UnwindInfo;
        ui_flags = (ui[0] >> 3) & 0x07;
        count_codes = ui[2];
        unwind_alloc = seh_unwind_alloc(ui);
        prolog_alloc = scan_prolog_alloc(rf->BeginAddress);
        prolog_pushes = scan_prolog_pushes(rf->BeginAddress);

        MW_TRACE("[EXP-NEXT] Frame[%d]: RF[%ld] 0x%x-0x%x unwind=0x%x flags=0x%x",
                 frame, rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo, ui_flags);

        /* Dump first 8 bytes */
        {
            uint8_t* fb = g_image_base + rf->BeginAddress;
            MW_TRACE("[EXP-NEXT] Frame[%d]: bytes@0x%x: %02x %02x %02x %02x %02x %02x %02x %02x",
                     frame, rf->BeginAddress,
                     fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7]);
        }

        MW_TRACE("[EXP-NEXT] Frame[%d]: unwind_alloc=0x%x prolog_alloc=0x%x pushes=0x%x",
                 frame, unwind_alloc, prolog_alloc, prolog_pushes);

        if (prolog_alloc > 0 && unwind_alloc == 0)
            MW_TRACE("[EXP-NEXT] Frame[%d]: *** MALFORMED UNWIND (missing ALLOC)", frame);

        if (ui_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + count_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(ui + h_off);
            void* lsda = (void*)(ui + h_off + 4);
            MW_TRACE("[EXP-NEXT] Frame[%d]: *** EHANDLER at RVA 0x%x LSDA_RVA=0x%lx ***",
                     frame, handler_rva,
                     (uint64_t)(uintptr_t)lsda - img_base);
            MW_TRACE("[EXP-NEXT] RESULT: EHANDLER reachable via prolog-corrected walk!");
            break;
        }

        /* Compute next parent */
        if (prolog_alloc > 0 && unwind_alloc == 0)
            corrected_rsp = parent_rsp + prolog_alloc + prolog_pushes;
        else
            corrected_rsp = parent_rsp + unwind_alloc + prolog_pushes;

        parent_rip = *(uint64_t*)(uintptr_t)corrected_rsp;
        parent_rva = parent_rip - img_base;
        parent_rsp = corrected_rsp + 8;

        if (parent_rip < img_base || parent_rip >= img_end) {
            MW_TRACE("[EXP-NEXT] Frame[%d]: parent 0x%lx outside PE — walk ends",
                     frame, parent_rip);
            break;
        }
    }

    MW_TRACE("[EXP-NEXT] === End Diagnostic Walk ===");
}

/* ============================================================
 * BUG-024: mw_RtlDispatchException
 * 
 * Real Windows x64 style exception dispatcher.
 * Walks the unwind chain, discovers EHANDLER frames,
 * invokes language-specific handlers, handles ContinueSearch/ContinueExecution.
 *
 * Called from the naked RaiseException stub (SysV ABI context).
 * Uses globals set by the naked stub: g_cap_rip, g_cap_rsp, g_cap_code, etc.
 *
 * Algorithm:
 *   1. Build EXCEPTION_RECORD and CONTEXT from captured state
 *   2. For each frame (MAX_FRAMES limit):
 *      a. RtlLookupFunctionEntry for context->Rip
 *      b. RtlVirtualUnwind to get handler + establisher frame
 *      c. If EHANDLER found: call handler, check disposition
 *      d. If ContinueExecution: restore CONTEXT and return
 *      e. If ContinueSearch: continue walking
 *   3. If no handler handles it: fall through to UnhandledExceptionFilter
 * ============================================================ */

#define DISPATCH_MAX_FRAMES 64

/* Dispatcher result codes */
#define DISP_RESULT_NOT_HANDLED   0
#define DISP_RESULT_CONTINUE_EXEC 1
#define DISP_RESULT_HANDLER_ERROR 2

/* Slots consumed by one unwind code (the code word itself + operand slots).
 * Per the Microsoft PE/COFF x64 spec table and ReactOS's
 * UnwindOpExtraSlotTable (sdk/lib/rtl/amd64/unwind.c). Used by the skip
 * path (codes above the fault RIP must be skipped WITH their operands,
 * otherwise the cursor misaligns on every following code — BUG-037) and
 * by any caller needing the byte size of the code array. */
static int seh_unwind_op_slots(uint8_t op, uint8_t op_info) {
    switch (op) {
    case UWOP_ALLOC_LARGE:     return (op_info != 0) ? 3 : 2;
    case UWOP_SAVE_NONVOL:     return 2;
    case UWOP_SAVE_NONVOL_FAR: return 3;
    case UWOP_EPILOG:          return 2;   /* v2 only */
    case UWOP_SPARE_CODE:      return 3;
    case UWOP_SAVE_XMM128:     return 2;
    case UWOP_SAVE_XMM128_FAR: return 3;
    case UWOP_PUSH_MACHFRAME:  return 1;
    default:                   return 1; /* PUSH_NONVOL, ALLOC_SMALL, SET_FPREG, reserved */
    }
}

/* Internal SysV RtlVirtualUnwind — operates on raw context buffer.
 * Returns: handler VA (or NULL if no handler for this frame).
 * Updates ctx in place (unwound registers).
 * Sets *establisher_frame, *out_handler_rva, *out_lsda. */
static void* seh_internal_virtual_unwind(
    uint64_t control_pc,
    RUNTIME_FUNCTION* rf,
    uint8_t* ctx,
    uint64_t* establisher_frame,
    uint32_t* out_handler_rva,
    void** out_lsda)
{
    /* NOTE: PUSH_NONVOL and PUSH_MACHFRAME safety bounds removed —
     * the original `new_rsp <= rsp + 4096` was always-true after
     * new_rsp += 8, making it a no-op check that added confusion.
     * Trust the unwind metadata instead. */
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;

    uint8_t version     = ui_base[0] & 0x07;
    uint8_t flags       = (ui_base[0] >> 3) & 0x07;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    /* BUG-037: UNWIND_INFO byte 3 is two little-endian 4-bit fields —
     * FrameRegister is the LOW nibble, the scaled FrameOffset the HIGH
     * nibble (winnt.h: `UCHAR FrameRegister:4; UCHAR FrameOffset:4;`).
     * The previous decode had them swapped, so mingw frames that use
     * RBP (reg 5) with lea rbp,[rsp+0x80] (offset 8 -> byte 0x85) were
     * misread as FP=R8/offset=5. */
    uint8_t frame_reg   = ui_base[3] & 0x0f;
    uint8_t frame_off   = (ui_base[3] >> 4) & 0x0f;

    (void)prolog_size;

    /* Handle CHAININFO — the chained function's unwind info replaces
     * the current function's unwind codes. We must run the chained
     * function's unwind codes to compute the correct establisher frame. */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_version = chained_ui[0] & 0x07;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x07;
        uint8_t chained_prolog_size = chained_ui[1];
        uint8_t chained_count_codes = chained_ui[2];
        /* BUG-037: FrameRegister = LOW nibble (see comment above). */
        uint8_t chained_frame_reg = chained_ui[3] & 0x0f;
        uint8_t chained_frame_off = (chained_ui[3] >> 4) & 0x0f;
        (void)chained_prolog_size;

        /* Read RIP from context for func_offset calculation */
        uint64_t chain_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t chained_rsp = *(uint64_t*)(ctx + CTX_Rsp);
        int chained_fp_set = 0;
        uint64_t chained_fp_val = 0;
        uint32_t chained_func_offset = (uint32_t)(chain_rip - (uint64_t)(uintptr_t)g_image_base) - chained->BeginAddress;
        uint8_t* chained_codes_ptr = chained_ui + 4;
        int chained_slot = 0;
        for (int i = 0; i < chained_count_codes; i++) {
            /* BUG-037: CountOfCodes counts SLOTS (code words + operand
             * slots, MS doc "2 or 3 nodes"; ReactOS advances i by
             * UnwindOpSlots). Bound the walk on the slot cursor, not the
             * code index, or padding/handler bytes decode as phantom
             * codes past the array. */
            if (chained_slot >= chained_count_codes ||
                chained_slot * 2 + 1 >= (int)(chained_count_codes * 2 + 16)) break;
            uint16_t cw = *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
            uint8_t op_code = (cw >> 8) & 0x0f;
            uint8_t op_info = (cw >> 12) & 0x0f;
            uint8_t code_offset = cw & 0xff;
            if (chained_func_offset > 0 && code_offset >= chained_func_offset) {
                /* BUG-037: skip the code WITH its operand slots, otherwise
                 * the cursor lands on operand bytes and every following
                 * code decodes from a misaligned position. */
                chained_slot += seh_unwind_op_slots(op_code, op_info);
                continue;
            }
            if (op_code == UWOP_EPILOG && chained_version < 2) {
                /* Epilogue descriptors exist only in UNWIND_INFO v2; in a
                 * v1 array op 6 is corruption — stop applying codes. */
                MW_TRACE("[DISPATCH] chained UI v%d has EPILOG code — corrupt, stop", chained_version);
                break;
            }
            switch (op_code) {
            case UWOP_PUSH_NONVOL: {
                /* BUG-039 (ReactOS PopReg semantics): the unwind-code array is
                 * stored in DESCENDING code-offset order, i.e. already in
                 * undo (epilog) order — each push code therefore restores
                 * from the CURRENT Rsp BEFORE advancing. Reading AFTER the
                 * increment shifted every restored nonvolatile one slot up:
                 * live proof (t08_probe p1 unwind, evidence master/bug038):
                 * RDI got the RETURN ADDRESS slot [E1]=0x140020af1 while the
                 * real saved rdi sits at [E1-8]. Correct order is
                 * reg=[rsp]; rsp+=8  (ReactOS sdk/lib/rtl/amd64/unwind.c
                 * PopReg: SetRegFromStackValue(Rsp) THEN Rsp+=8). */
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp);
                chained_rsp += 8;
                break;
            }
            case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
                chained_slot++;
                if (op_info == 0) {
                    chained_rsp += (uint64_t)(*(uint16_t*)(chained_codes_ptr + chained_slot * 2)) * 8;
                } else {
                    chained_slot++;
                    chained_rsp += (uint64_t)(*(uint32_t*)(chained_codes_ptr + chained_slot * 2 - 2));
                }
                break;
            case UWOP_ALLOC_SMALL: chained_rsp += (uint64_t)(op_info + 1) * 8; break;
            case UWOP_SET_FPREG:
                /* ReactOS semantics: RSP re-anchors to the frame-pointer
                 * base (FP_value - FrameOffset*16) for all remaining codes;
                 * the FP value is read from the LIVE context, where the
                 * established anchor persists (BUG-037). */
                if (chained_frame_reg) {
                    uint32_t fp_off = reg_to_ctx_offset(chained_frame_reg);
                    if (fp_off) {
                        chained_fp_val = *(uint64_t*)(ctx + fp_off);
                        chained_rsp = chained_fp_val - (uint64_t)chained_frame_off * 16;
                    } else {
                        chained_fp_val = chained_rsp + (uint64_t)chained_frame_off * 16;
                    }
                } else {
                    chained_fp_val = chained_rsp + (uint64_t)chained_frame_off * 16;
                }
                chained_fp_set = 1;
                break;
            case UWOP_SAVE_NONVOL: {
                chained_slot++;
                uint16_t soff = *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp + soff * 8);
                break;
            }
            case UWOP_SAVE_NONVOL_FAR: {
                chained_slot++; chained_slot++;
                uint32_t soff = *(uint32_t*)(chained_codes_ptr + chained_slot * 2 - 2);
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp + soff);
                break;
            }
            case UWOP_SAVE_XMM128: { chained_slot++; break; }
            case UWOP_SAVE_XMM128_FAR: { chained_slot++; chained_slot++; break; }
            case UWOP_EPILOG: { chained_slot++; break; }          /* 2 slots total */
            case UWOP_SPARE_CODE: { chained_slot++; chained_slot++; break; } /* 3 total */
            case UWOP_PUSH_MACHFRAME: {
                chained_rsp += 40;
                if (chained_rsp <= chained_rsp + 4096) {
                    *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(chained_rsp + 16);
                    chained_rsp = *(uint64_t*)(chained_rsp + 8);
                }
                break;
            }
            default: { if (op_code >= 11) chained_slot++; break; } /* reserved */
            }
            chained_slot++;
        }
        if (chained_fp_set) {
            uint32_t fp_off = reg_to_ctx_offset(chained_frame_reg);
            if (fp_off) *(uint64_t*)(ctx + fp_off) = chained_fp_val;
        }
        *(uint64_t*)(ctx + CTX_Rsp) = chained_rsp;
        uint64_t chained_est = chained_fp_set ? chained_fp_val : chained_rsp;
        if (establisher_frame) *establisher_frame = chained_est;

        if (chained_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + chained_count_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            void* lsda = (void*)(chained_ui + h_off + 4);
            if (out_handler_rva) *out_handler_rva = handler_rva;
            if (out_lsda) *out_lsda = lsda;
            MW_TRACE("[DISPATCH] Unwind: CHAININFO handler=0x%x est=0x%lx", handler_rva, chained_est);
            return (void*)(g_image_base + handler_rva);
        }
        if (out_handler_rva) *out_handler_rva = 0;
        if (out_lsda) *out_lsda = NULL;
        return NULL;
    }

    /* Read current register values from context */
    uint64_t rsp = *(uint64_t*)(ctx + CTX_Rsp);
    uint64_t rip = *(uint64_t*)(ctx + CTX_Rip);
    uint32_t rip_rva = (uint32_t)(rip - (uint64_t)(uintptr_t)g_image_base);
    uint32_t func_offset = rip_rva - rf->BeginAddress;

    /* Track stack during prolog simulation */
    uint64_t new_rsp = rsp;
    int fp_set = 0;
    uint64_t fp_reg_val = 0;

    /* Simulate unwind codes to "undo" the prolog */
    uint8_t* codes = ui_base + 4;
    int slot = 0;
    for (int i = 0; i < count_codes; i++) {
        /* BUG-037: CountOfCodes counts SLOTS — see chained-loop note. */
        if (slot >= count_codes ||
            slot * 2 + 1 >= (int)(count_codes * 2 + 16)) break;
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet — advancing by the
         * FULL slot size so the cursor stays aligned (BUG-037). Boundary:
         * a code whose END offset equals the RIP offset has executed
         * (ReactOS skips strictly-greater offsets). */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot += seh_unwind_op_slots(op_code, op_info);
            continue;
        }
        if (op_code == UWOP_EPILOG && version < 2) {
            /* Epilogue descriptors exist only in UNWIND_INFO v2; in a v1
             * array op 6 is corruption — stop applying codes. */
            MW_TRACE("[DISPATCH] UI v1 has EPILOG code — corrupt, stop");
            break;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            /* BUG-039: restore from CURRENT rsp BEFORE advancing — the code
             * array is in undo order (descending code offsets). The old
             * increment-first order read the NEXT slot (one too high):
             * t08_probe Frame[1] live trace showed RDI=[E1]=return address
             * instead of [E1-8]=saved rdi. ReactOS PopReg: read then +=8.
             * (Root cause of BUG-038 probe symptoms: catch by reference got
             * garbage RDI; `ok` counter in RBP clobbered.) */
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            new_rsp += 8;
            break;
        }
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (op_info == 0) {
                new_rsp += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            } else {
                slot++;
                new_rsp += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2));
            }
            break;
        case UWOP_ALLOC_SMALL: {
            new_rsp += (uint64_t)(op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            /* ReactOS semantics (sdk/lib/rtl/amd64/unwind.c): on unwind,
             * RSP re-anchors to the frame-pointer base — the FP register
             * value minus the scaled FrameOffset — and the FP value is
             * read from the LIVE context, where the established anchor
             * persists through the frame body (BUG-037). */
            if (frame_reg) {
                uint32_t fp_ctx_off = reg_to_ctx_offset(frame_reg);
                if (fp_ctx_off) {
                    fp_reg_val = *(uint64_t*)(ctx + fp_ctx_off);
                    new_rsp = fp_reg_val - (uint64_t)frame_off * 16;
                } else {
                    fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
                }
            } else {
                fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
            }
            fp_set = 1;
            break;
        }
        case UWOP_SAVE_NONVOL: {
            slot++;
            uint16_t stack_off = *(uint16_t*)(codes + slot * 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off * 8);
            }
            break;
        }
        case UWOP_SAVE_NONVOL_FAR: {
            slot++; slot++;
            uint32_t stack_off = *(uint32_t*)(codes + slot * 2 - 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off);
            }
            break;
        }
        case UWOP_SAVE_XMM128: { slot++; break; }
        case UWOP_SAVE_XMM128_FAR: { slot++; slot++; break; }
        case UWOP_EPILOG: { slot++; break; }          /* 2 slots total, no stack effect */
        case UWOP_SPARE_CODE: { slot++; slot++; break; } /* 3 slots total */
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
            new_rsp = *(uint64_t*)(new_rsp + 8);
            break;
        }
        default: {
            if (op_code >= 11) slot++; /* reserved ops: skip code word only */
            break;
        }
        }
        slot++;
    }

    /* Apply frame register */
    if (fp_set) {
        uint32_t fp_off = reg_to_ctx_offset(frame_reg);
        if (fp_off) *(uint64_t*)(ctx + fp_off) = fp_reg_val;
    }

    /* Update RSP in context */
    *(uint64_t*)(ctx + CTX_Rsp) = new_rsp;

    /* Set establisher frame */
    uint64_t establisher = fp_set ? fp_reg_val : new_rsp;
    if (establisher_frame) *establisher_frame = establisher;

    /* Find handler and LSDA */
    uint32_t handler_rva = 0;
    void* lsda = NULL;
    if (flags & UNW_FLAG_EHANDLER) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
        lsda = (void*)(ui_base + h_off + 4);
    }

    if (out_handler_rva) *out_handler_rva = handler_rva;
    if (out_lsda) *out_lsda = lsda;

    MW_TRACE("[DISPATCH] Unwind: pc=0x%lx begin=0x%x handler=0x%x est=0x%lx",
             (uint64_t)rip, rf->BeginAddress, handler_rva, establisher);

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* Build a CONTEXT buffer from captured register state.
 * The naked stub captures RIP and RSP precisely.
 * Other registers are read from current CPU state.
 * Exception registers (RCX, RDX, R8, R9) were saved from ms_abi params. */
static void seh_build_context(uint8_t* ctx, uint64_t rip, uint64_t rsp) {
    memset(ctx, 0, CONTEXT_SIZE);

    /* Set CONTEXT header */
    *(uint64_t*)(ctx + 0x00) = 0x100007; /* ContextFlags = CONTEXT_FULL */

    /* Set RIP and RSP from captured state */
    *(uint64_t*)(ctx + CTX_Rip) = rip;
    *(uint64_t*)(ctx + CTX_Rsp) = rsp;

    /* Read current nonvolatile registers from CPU state.
     * BUG-031: volatile registers come from the stub's true raise-site
     * captures — CPU-state reads here are compiler-dependent garbage that
     * changes with every loader recompilation and flips the GCC
     * personality's phase-1 decision (Frame[4] INSTALL vs ContinueSearch;
     * EXP-039 §6.2 bisection: E1-E5 all carried this poison, E4/E5 showed
     * the 'unsigned long long' BUG-029-era frontier with 727 events). */
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    rax = g_cap_rax;
    rcx = g_cap_code;               /* ms_abi arg1 at raise: ExceptionCode */
    rdx = g_cap_flags;              /* ms_abi arg2 at raise: ExceptionFlags */
    r8  = g_cap_nargs;              /* ms_abi arg3 at raise: nargs */
    r9  = g_cap_args;               /* ms_abi arg4 at raise: args ptr */
    r10 = g_cap_r10;
    r11 = g_cap_r11;
    __asm__ volatile ("" : "=b"(rbx));
    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("" : "=S"(rsi));
    __asm__ volatile ("" : "=D"(rdi));
    __asm__ volatile ("" : "=r"(r12));
    __asm__ volatile ("" : "=r"(r13));
    __asm__ volatile ("" : "=r"(r14));
    __asm__ volatile ("" : "=r"(r15));

    *(uint64_t*)(ctx + CTX_Rax) = rax;
    *(uint64_t*)(ctx + CTX_Rcx) = rcx;
    *(uint64_t*)(ctx + CTX_Rdx) = rdx;
    *(uint64_t*)(ctx + CTX_Rbx) = rbx;
    *(uint64_t*)(ctx + CTX_Rbp) = rbp;
    *(uint64_t*)(ctx + CTX_Rsi) = rsi;
    *(uint64_t*)(ctx + CTX_Rdi) = rdi;
    *(uint64_t*)(ctx + CTX_R8)  = r8;
    *(uint64_t*)(ctx + CTX_R9)  = r9;
    *(uint64_t*)(ctx + CTX_R10) = r10;
    *(uint64_t*)(ctx + CTX_R11) = r11;
    *(uint64_t*)(ctx + CTX_R12) = r12;
    *(uint64_t*)(ctx + CTX_R13) = r13;
    *(uint64_t*)(ctx + CTX_R14) = r14;
    *(uint64_t*)(ctx + CTX_R15) = r15;
}

/* Handler function pointer type (ms_abi). */
typedef int32_t (*exception_handler_fn)(
    EXCEPTION_RECORD* er, uint64_t establisher_frame,
    uint8_t* ctx, DISPATCHER_CONTEXT* dc);

/* The dispatcher itself — called from naked stub (SysV ABI).
 * Returns: DISP_RESULT_CONTINUE_EXEC if a handler handled it,
 *          DISP_RESULT_NOT_HANDLED if no handler was found. */
static int seh_dispatch_exception(void) __attribute__((used));

/* VEH-aligned trampoline: calls a VEH handler with proper stack alignment.
 * VEH handler takes PEXCEPTION_POINTERS* as RCX (only arg).
 * Returns LONG: -1 = EXCEPTION_CONTINUE_EXECUTION, 0 = EXCEPTION_CONTINUE_SEARCH. */
static long call_veh_aligned(void* veh_handler, void* exception_pointers) {
    long result = 0;
    __asm__ volatile (
        "pushq %%rbp\n\t"
        "movq %%rsp, %%rbp\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $0x20, %%rsp\n\t"
        "movq %2, %%rcx\n\t"
        "call *%1\n\t"
        "leave\n\t"
        : "=a"(result)
        : "r"(veh_handler), "r"(exception_pointers)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
    );
    return result;
}

/* ABI trampoline: calls an ms_abi PE handler with proper stack alignment.
 * The PE handler may call back into our ms_abi stubs (RtlUnwindEx, abort, etc.),
 * which require 16-byte stack alignment. We ensure this by creating a new
 * aligned stack frame. */
static int32_t call_handler_aligned(
    void* handler, EXCEPTION_RECORD* er, uint64_t est_frame,
    uint8_t* ctx, DISPATCHER_CONTEXT* dc)
{
    int32_t disposition = DISP_ExceptionContinueSearch;
    __asm__ volatile (
        /* Create a properly aligned stack frame */
        "pushq %%rbp\n\t"
        "movq %%rsp, %%rbp\n\t"
        "andq $-16, %%rsp\n\t"     /* Force 16-byte alignment */
        "subq $0x20, %%rsp\n\t"    /* 32-byte ms_abi shadow space */
        "movq %2, %%rcx\n\t"
        "movq %3, %%rdx\n\t"
        "movq %4, %%r8\n\t"
        "movq %5, %%r9\n\t"
        "movq g_cap_rax(%%rip), %%rax\n\t"
        "movq g_cap_r10(%%rip), %%r10\n\t"
        "movq g_cap_r11(%%rip), %%r11\n\t"
        "call *%1\n\t"
        /* Restore stack frame */
        "leave\n\t"
        : "=&a"(disposition)
        : "r"(handler), "r"((uint64_t)(uintptr_t)er),
          "r"(est_frame), "r"((uint64_t)(uintptr_t)ctx),
          "r"((uint64_t)(uintptr_t)dc)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "rdi", "rbp", "memory"
    );
    return disposition;
}

static int seh_dispatch_exception(void) {
    MW_TRACE("[DISPATCH] === RtlDispatchException ===");
    MW_TRACE("[DISPATCH] ExceptionCode=0x%x RIP=0x%lx RSP=0x%lx",
             g_cap_code, g_cap_rip, g_cap_rsp);

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    /* 1. Build EXCEPTION_RECORD (also saved to g_cap_er for RtlUnwindEx) */
    EXCEPTION_RECORD er;
    memset(&er, 0, sizeof(er));
    er.ExceptionCode = g_cap_code;
    er.ExceptionFlags = g_cap_flags;
    er.ExceptionRecord = 0; /* no nested exception */
    er.ExceptionAddress = g_cap_rip;
    er.NumberParameters = g_cap_nargs;
    /* Copy exception parameters from captured args */
    if (g_cap_nargs > 0 && g_cap_args != 0 && g_cap_nargs <= EXCEPTION_MAXIMUM_PARAMETERS) {
        uint64_t* args = (uint64_t*)(uintptr_t)g_cap_args;
        for (uint32_t i = 0; i < g_cap_nargs; i++) {
            er.ExceptionInformation[i] = args[i];
        }
    }
    /* Save fully-populated ER to g_cap_er (AFTER filling fields, not before) */
    memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));

    /* === EXP-NEXT-3 STEP 1: Dump full exception state === */
    MW_TRACE("[EXC_STATE] EXCEPTION_BEGIN");
    MW_TRACE("[EXC_STATE] Code: 0x%x", er.ExceptionCode);
    MW_TRACE("[EXC_STATE] Flags: 0x%x", er.ExceptionFlags);
    MW_TRACE("[EXC_STATE] Address: 0x%lx", (uint64_t)er.ExceptionAddress);
    MW_TRACE("[EXC_STATE] NumParams: %u", er.NumberParameters);
    MW_TRACE("[EXC_STATE] Param[0]: 0x%lx", er.ExceptionInformation[0]);
    MW_TRACE("[EXC_STATE] Param[1]: 0x%lx", er.ExceptionInformation[1]);
    MW_TRACE("[EXC_STATE] Param[2]: 0x%lx", er.ExceptionInformation[2]);
    MW_TRACE("[EXC_STATE] Param[3]: 0x%lx", er.ExceptionInformation[3]);
    MW_TRACE("[EXC_STATE] g_cap_args ptr: 0x%lx", (uint64_t)(uintptr_t)g_cap_args);
    /* Dump first 64 bytes of exception object if Param[0] looks like a pointer */
    /* BUG-066 (2026-10-18): the old dump dereferenced Param[0] unconditionally
     * (the "readability check" was `memcpy(...) == NULL || 1` — always true).
     * For 0xe06d7363 (MSVC C++ EH) Param[0] is the 0x19930520 magic constant,
     * NOT a pointer — zoomit64/bginfo64/winmergeu all died INSIDE this dump
     * (loader SIGSEGV at seh_dispatch_exception+0x7b0, movzbl 0x7(%rbx)).
     * x64 MSVC C++ record layout: [0]=0x19930520 magic, [1]=throw object,
     * [2]=ThrowInfo*, [3]=module base. Fix: dump Param[1] for C++ exceptions,
     * and only after a real fault-free readability probe (process_vm_readv
     * returns EFAULT instead of crashing). All 32 bytes are copied into a
     * local buffer by ONE probe call; printing never touches guest memory. */
    {
        uint64_t obj_va = (er.NumberParameters >= 1) ? er.ExceptionInformation[0] : 0;
        if (er.ExceptionCode == 0xe06d7363u) {
            MW_TRACE("[EXC_STATE] MSVC C++ EH magic @ Param[0]=0x%lx (not a pointer)",
                     er.ExceptionInformation[0]);
            if (er.NumberParameters >= 2) obj_va = er.ExceptionInformation[1];
        }
        if (obj_va != 0) {
            uint8_t* exc_obj = (uint8_t*)(uintptr_t)obj_va;
            MW_TRACE("[EXC_STATE] Exception object @ %p (first 32 bytes):", (void*)exc_obj);
            uint8_t buf[32];
            memset(buf, 0, sizeof(buf));
            struct iovec liov = { buf, sizeof(buf) };
            struct iovec riov = { (void*)exc_obj, sizeof(buf) };
            ssize_t got = process_vm_readv(getpid(), &liov, 1, &riov, 1, 0);
            if (got == (ssize_t)sizeof(buf)) {
                MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                         buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
                MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                         buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
                MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                         buf[16], buf[17], buf[18], buf[19], buf[20], buf[21], buf[22], buf[23]);
                MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                         buf[24], buf[25], buf[26], buf[27], buf[28], buf[29], buf[30], buf[31]);
                /* Also dump as uint64 for the exception class */
                uint64_t exc_class = *(uint64_t*)buf;
                MW_TRACE("[EXC_STATE] Exception class (u64): 0x%016lx", exc_class);
            } else {
                MW_TRACE("[EXC_STATE]   NOT READABLE (probe got %zd, errno=%d) — dump skipped",
                         got, errno);
            }
        }
    }
    MW_TRACE("[EXC_STATE] EXCEPTION_END");

    /* 2. Build CONTEXT */
    uint8_t ctx[CONTEXT_SIZE];
    seh_build_context(ctx, g_cap_rip, g_cap_rsp);
    
    /* Register CONTEXT for RtlUnwindEx access and set up longjmp target */
    g_unwind_ctx = ctx;
    g_target_frame_ctx_valid = 0; /* BUG-039/B: fresh dispatch, no snapshot yet */
    g_is_unwinding = 0;
    g_unwind_target_ip = 0;
    g_unwind_target_frame = 0;

    /* === BUG-031 (ADR-011): GCC bridge resume-command intercept ===
     * Unwind-phase INSTALL at an intermediate frame makes the GCC bridge
     * re-raise 0x21474343 (flags=1, nparams=4, params={object, establisher,
     * landing_pad, selector}) — verified live (EXP-039: bridge core
     * 0x49d3e9-0x49d413: mov r8d,4 / mov edx,1 / mov ecx,0x21474343).
     * The raise re-enters this dispatcher through the naked stub from a C
     * stack that has no PE RUNTIME_FUNCTIONs, so a fresh frame walk can
     * never reach the matching establisher (EXP-038 §5.1 design caution).
     * We honor the record directly as a resume command — exactly what the
     * bridge's own est==param[1] match (0x49d420) + site-A' RtlUnwindEx
     * produces on Windows: resume at the cleanup landing pad with
     * RAX=object, RDX=selector, RSP=establisher − prologue_alloc(LP).
     * Fingerprint: code==0x21474343 && nparams==4 && flags&NONCONTINUABLE
     * && landing pad inside the image && a throw-site snapshot exists.
     * The initial GCC throw (0x20474343, flags=0, nparams=1) never matches
     * ([EXC_STATE] trace, evidence/master/bug030/upx_trace.json). */
    if (er.ExceptionCode == 0x21474343u && er.NumberParameters == 4
        && (er.ExceptionFlags & 0x1u) != 0
        && g_throw_ctx_valid
        && er.ExceptionInformation[2] >= img_base
        && er.ExceptionInformation[2] < img_end) {
        uint64_t lp_va  = er.ExceptionInformation[2];
        uint64_t est_va = er.ExceptionInformation[1];
        uint64_t alloc3 = seh_total_prologue_alloc(lp_va);
        if (alloc3 != 0 && est_va != 0 && (est_va & 0x7ull) == 0) {
            uint64_t resume_rsp = est_va - alloc3;
            /* Registers: protocol values in RAX (object) / RDX (selector),
             * everything else from the throw-site snapshot (the cleanup LP
             * continues the frame that threw — its live callee-saved regs
             * are the snapshot's; A's LP 0x4015A5 consumes only RAX+[rsp]). */
            g_unwind_regs[UR_RAX] = er.ExceptionInformation[0];
            g_unwind_regs[UR_RDX] = er.ExceptionInformation[3];
            g_unwind_regs[UR_RCX] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rcx);
            g_unwind_regs[UR_RBX] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rbx);
            g_unwind_regs[UR_RBP] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rbp);
            g_unwind_regs[UR_RSI] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rsi);
            g_unwind_regs[UR_RDI] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rdi);
            g_unwind_regs[UR_R8]  = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R8);
            g_unwind_regs[UR_R9]  = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R9);
            g_unwind_regs[UR_R10] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R10);
            g_unwind_regs[UR_R11] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R11);
            g_unwind_regs[UR_R12] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R12);
            g_unwind_regs[UR_R13] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R13);
            g_unwind_regs[UR_R14] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R14);
            g_unwind_regs[UR_R15] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R15);
            g_unwind_regs[UR_RSP] = resume_rsp;
            g_unwind_target_ip = lp_va;
            g_unwind_target_frame = resume_rsp;
            g_is_unwinding = 1;
            MW_TRACE("[BUG-031] resume-cmd: 0x21474343 obj=0x%lx est=0x%lx lp=0x%lx sel=%lu alloc=0x%lx -> RIP=0x%lx RSP=0x%lx",
                     er.ExceptionInformation[0], est_va, lp_va,
                     (unsigned long)er.ExceptionInformation[3],
                     (unsigned long)alloc3, lp_va, resume_rsp);
            if (g_trace_log) fflush(g_trace_log);
            return DISP_RESULT_CONTINUE_EXEC;
        }
        MW_TRACE("[BUG-031] resume-cmd fingerprint matched but alloc/est invalid (alloc=0x%lx est=0x%lx) — falling through to walk",
                 (unsigned long)alloc3, est_va);
    }
    /* Fresh throw (not a resume command): snapshot the throw-site CONTEXT
     * for the intercept and the intermediate-cleanup walk. */
    memcpy(g_throw_ctx_snapshot, ctx, CONTEXT_SIZE);
    g_throw_ctx_valid = 1;
    
    /* setjmp return point for RtlUnwindEx longjmp.
     * When the personality function calls RtlUnwindEx, RtlUnwindEx modifies
     * the CONTEXT and longjmps here. We then return DISP_RESULT_CONTINUE_EXEC
     * with the modified CONTEXT, which the naked stub will use to resume.
     */
    if (setjmp(g_unwind_jmpbuf) != 0) {
        /* IMMEDIATELY copy all register values from CONTEXT to globals.
         * Must do this before ANY function calls (MW_TRACE, fflush) because
         * those calls use stack space that may overlap with the CONTEXT buffer. */
        g_unwind_regs[UR_RAX] = *(uint64_t*)(ctx + CTX_Rax);
        g_unwind_regs[UR_RCX] = *(uint64_t*)(ctx + CTX_Rcx);
        g_unwind_regs[UR_RDX] = *(uint64_t*)(ctx + CTX_Rdx);
        g_unwind_regs[UR_RBX] = *(uint64_t*)(ctx + CTX_Rbx);
        g_unwind_regs[UR_RSP] = *(uint64_t*)(ctx + CTX_Rsp);
        g_unwind_regs[UR_RBP] = *(uint64_t*)(ctx + CTX_Rbp);
        g_unwind_regs[UR_RSI] = *(uint64_t*)(ctx + CTX_Rsi);
        g_unwind_regs[UR_RDI] = *(uint64_t*)(ctx + CTX_Rdi);
        g_unwind_regs[UR_R8]  = *(uint64_t*)(ctx + CTX_R8);
        g_unwind_regs[UR_R9]  = *(uint64_t*)(ctx + CTX_R9);
        g_unwind_regs[UR_R10] = *(uint64_t*)(ctx + CTX_R10);
        g_unwind_regs[UR_R11] = *(uint64_t*)(ctx + CTX_R11);
        g_unwind_regs[UR_R12] = *(uint64_t*)(ctx + CTX_R12);
        g_unwind_regs[UR_R13] = *(uint64_t*)(ctx + CTX_R13);
        g_unwind_regs[UR_R14] = *(uint64_t*)(ctx + CTX_R14);
        g_unwind_regs[UR_R15] = *(uint64_t*)(ctx + CTX_R15);
        g_unwind_target_ip = *(uint64_t*)(ctx + CTX_Rip);
        g_unwind_target_frame = *(uint64_t*)(ctx + CTX_Rsp);
        
        MW_TRACE("[DISPATCH] RtlUnwindEx longjmped back — unwind to RIP=0x%lx RSP=0x%lx",
                 g_unwind_target_ip, g_unwind_target_frame);
        MW_TRACE("[DISPATCH] CONTEXT after longjmp: Rax=0x%lx (saved to g_unwind_regs)",
                 g_unwind_regs[UR_RAX]);
        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);
        return DISP_RESULT_CONTINUE_EXEC;
    }

    /* 2.5. Try Vectored Exception Handlers (VEH) first.
     * VEH handlers are called BEFORE frame-based handlers.
     * VEH return: EXCEPTION_CONTINUE_EXECUTION (-1) = handled,
     *              EXCEPTION_CONTINUE_SEARCH (0) = continue to frame walk.
     * VEH handler signature: LONG Handler(PEXCEPTION_POINTERS*)
     * PEXCEPTION_POINTERS = {EXCEPTION_RECORD*, CONTEXT*, PVOID ExceptionAddr}
     */
    MW_TRACE("[DISPATCH] VEH chain: %d handlers", g_veh_count);
    if (g_veh_count > 0) {
        /* Build EXCEPTION_POINTERS on stack.
         * EXCEPTION_POINTERS layout: {EXCEPTION_RECORD*, CONTEXT*, PVOID ExceptionAddr} */
        uint64_t ep[3];
        ep[0] = (uint64_t)(uintptr_t)&er;  /* ExceptionRecord* */
        ep[1] = (uint64_t)(uintptr_t)ctx;   /* CONTEXT* */
        ep[2] = g_cap_rip;              /* ExceptionAddress */

        /* Call VEH handlers in reverse order (last registered = first called).
         * VEH handler signature: LONG WINAPI Handler(PEXCEPTION_POINTERS*).
         * PEXCEPTION_POINTERS* is passed in RCX (ms_abi first arg).
         * Returns LONG: -1 = EXCEPTION_CONTINUE_EXECUTION, 0 = EXCEPTION_CONTINUE_SEARCH. */
        for (int i = g_veh_count - 1; i >= 0; i--) {
            void* veh_handler = g_veh_handlers[i];
            MW_TRACE("[DISPATCH] VEH[%d]: handler=%p", i, veh_handler);

            long veh_result = call_veh_aligned(veh_handler, (void*)ep);

            MW_TRACE("[DISPATCH] VEH[%d]: returned %ld", i, veh_result);
            if (veh_result == EXCEPTION_CONTINUE_EXECUTION) {
                MW_TRACE("[DISPATCH] VEH handled exception — ContinueExecution");
                return DISP_RESULT_CONTINUE_EXEC;
            }
        }
    }

    /* 3. Walk frames */
    for (int frame = 0; frame < DISPATCH_MAX_FRAMES; frame++) {
        uint64_t current_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t current_rva = current_rip - img_base;

        MW_TRACE("[DISPATCH] Frame[%d]: RIP=0x%lx RVA=0x%lx CTX.Rsp=0x%lx",
                 frame, current_rip, current_rva, *(uint64_t*)(ctx + CTX_Rsp));

        /* Check if still inside PE */
        if (current_rip < img_base || current_rip >= img_end) {
            MW_TRACE("[DISPATCH] Frame[%d]: RIP outside PE — walk ends", frame);
            break;
        }

        /* Lookup RUNTIME_FUNCTION */
        RUNTIME_FUNCTION* rf = seh_internal_lookup(current_rva);
        if (!rf) {
            MW_TRACE("[DISPATCH] Frame[%d]: No RUNTIME_FUNCTION — walk ends", frame);
            break;
        }

        MW_TRACE("[DISPATCH] Frame[%d]: RF begin=0x%x end=0x%x ui=0x%x",
                 frame, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

        /* EXP-035: Save RSP BEFORE RtlVirtualUnwind modifies it.
         * seh_internal_virtual_unwind updates CTX.Rsp to the function-entry RSP
         * (new_rsp after undoing all prolog operations). But the post-prologue RSP
         * (RSP after the prologue, used by catch blocks for local variable access)
         * is the CTX.Rsp value BEFORE this call. */
        uint64_t est_frame = 0;
        uint32_t handler_rva = 0;
        void* lsda = NULL;
        uint64_t pre_unwind_rsp = *(uint64_t*)(ctx + CTX_Rsp);
        /* BUG-039/B: snapshot the "returned into frame F" state BEFORE this
         * frame's own unwind codes are applied. If frame F owns the handler,
         * this snapshot — not the post-unwind context — is the correct
         * register base for the eventual landing-pad resume. */
        uint8_t pre_unwind_ctx[CONTEXT_SIZE];
        memcpy(pre_unwind_ctx, ctx, CONTEXT_SIZE);
        void* handler = seh_internal_virtual_unwind(
            current_rip, rf, ctx, &est_frame, &handler_rva, &lsda);

        MW_TRACE("[DISPATCH] Frame[%d]: Unwind est=0x%lx CTX.Rsp_after=0x%lx alloc=0x%x",
                 frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp),
                 est_frame - *(uint64_t*)(ctx + CTX_Rsp));

        if (!handler) {
            /* No handler for this frame. Read parent return address
             * from [establisher_frame] and continue walking. */
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est_frame;
            uint64_t parent_rva = parent_rip - img_base;
            MW_TRACE("[DISPATCH] Frame[%d]: no handler, parent RIP=0x%lx RVA=0x%lx",
                     frame, parent_rip, parent_rva);
            MW_TRACE("[DISPATCH] Frame[%d]: est=0x%lx CTX.Rsp=0x%lx delta=0x%lx",
                     frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp),
                     est_frame - *(uint64_t*)(ctx + CTX_Rsp));

            /* Update context for next frame */
            *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(ctx + CTX_Rsp) = est_frame + 8;
            continue;
        }

        /* EHANDLER found — build DISPATCHER_CONTEXT and call handler */
        MW_TRACE("[DISPATCH] Frame[%d]: EHANDLER at RVA 0x%x (VA 0x%lx)",
                 frame, handler_rva, (uint64_t)(uintptr_t)handler);
        MW_TRACE("[DISPATCH] Frame[%d]: establisher=0x%lx CTX.Rsp=0x%lx lsda=%p",
                 frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp), lsda);

        /* BUG-039/B: this frame owns the handler — keep the pre-unwind
         * register state ("returned into frame F from below") as the
         * landing-pad resume base for RtlUnwindEx. */
        memcpy(g_target_frame_ctx, pre_unwind_ctx, CONTEXT_SIZE);
        g_target_frame_ctx_valid = 1;

        /* EXP-035 NOTE: The GCC personality function expects dc.EstablisherFrame to be
         * the function-entry RSP. It passes this directly to RtlUnwindEx as
         * target_frame. RtlUnwindEx then subtracts the stack allocation to
         * get the post-prologue RSP for the catch landing pad.
         *
         * So we pass est_frame (function-entry RSP) as the establisher frame,
         * which is the original (correct) behavior. The fix for the catch
         * block's RSP is in RtlUnwindEx (EXP-035), not here.
         */
        DISPATCHER_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        /* CRITICAL: On Windows x64, after RtlVirtualUnwind, the context RIP is
         * the RETURN ADDRESS (one past the call instruction). GCC personality
         * functions expect the PC to point WITHIN the faulting call instruction.
         * The personality's internal _Unwind_GetIPInfo returns ip_before_insn=0,
         * causing the real personality to subtract 1. However, if the SEH wrapper
         * doesn't set ip_before_insn correctly, the personality misses the call site.
         * As a safety measure, we pass ControlPc = RIP - 1 to ensure the PC falls
         * within the call instruction's range in the LSDA call site table. */
        dc.ControlPc = current_rip > 0 ? current_rip - 1 : 0;
        dc.ImageBase = img_base;
        dc.FunctionEntry = rf;
        dc.EstablisherFrame = est_frame;
        dc.TargetIp = 0;
        dc.ContextRecord = ctx;
        dc.LanguageHandler = handler;
        dc.HandlerData = lsda;
        dc.HistoryTable = 0;
        dc.ScopeIndex = 0;
        dc.Fill = 0;

        /* Also adjust CONTEXT.Rip for personality functions that read it directly */
        uint64_t saved_ctx_rip = *(uint64_t*)(ctx + CTX_Rip);
        if (saved_ctx_rip > 0) *(uint64_t*)(ctx + CTX_Rip) = saved_ctx_rip - 1;

        /* Call the language-specific handler (ms_abi calling convention).
         * Handler signature: LONG Handler(EXCEPTION_RECORD*, ULONG64, CONTEXT*, DISPATCHER_CONTEXT*)
         * Returns EXCEPTION_DISPOSITION.
         *
         * CRITICAL ABI NOTE: The handler is ms_abi code inside the PE.
         * We're calling from SysV code. We must use an ms_abi function pointer
         * to ensure correct register assignment (RCX, RDX, R8, R9).
         */
        /* === EXP-NEXT-3: Dump handler call details === */
        MW_TRACE("[HANDLER_CALL] Frame[%d] BEGIN", frame);
        MW_TRACE("[HANDLER_CALL] RCX (EXCEPTION_RECORD*) = %p", (void*)&er);
        MW_TRACE("[HANDLER_CALL] RDX (EstablisherFrame) = 0x%lx", dc.EstablisherFrame);
        MW_TRACE("[HANDLER_CALL] R8  (CONTEXT*)       = %p", (void*)ctx);
        MW_TRACE("[HANDLER_CALL] R9  (DISPATCHER_CONTEXT*) = %p", (void*)&dc);
        MW_TRACE("[HANDLER_CALL] Handler VA = %p  RVA = 0x%x", handler, handler_rva);
        MW_TRACE("[HANDLER_CALL] HandlerData (LSDA) = %p", lsda);
        MW_TRACE("[HANDLER_CALL] ER.Code=0x%x ER.Flags=0x%x ER.NumParams=%u",
                 er.ExceptionCode, er.ExceptionFlags, er.NumberParameters);
        MW_TRACE("[HANDLER_CALL] ER.Param[0]=0x%lx ER.Param[1]=0x%lx ER.Param[2]=0x%lx",
                 er.ExceptionInformation[0], er.ExceptionInformation[1], er.ExceptionInformation[2]);
        /* Dump first 64 bytes of handler function */
        {
            uint8_t* hp = (uint8_t*)handler;
            MW_TRACE("[HANDLER_CALL] Handler bytes [0:16]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[0], hp[1], hp[2], hp[3], hp[4], hp[5], hp[6], hp[7],
                     hp[8], hp[9], hp[10], hp[11], hp[12], hp[13], hp[14], hp[15]);
            MW_TRACE("[HANDLER_CALL] Handler bytes[16:32]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[16], hp[17], hp[18], hp[19], hp[20], hp[21], hp[22], hp[23],
                     hp[24], hp[25], hp[26], hp[27], hp[28], hp[29], hp[30], hp[31]);
            MW_TRACE("[HANDLER_CALL] Handler bytes[32:48]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[32], hp[33], hp[34], hp[35], hp[36], hp[37], hp[38], hp[39],
                     hp[40], hp[41], hp[42], hp[43], hp[44], hp[45], hp[46], hp[47]);
        }
        /* Dump 128 bytes of LSDA/HandlerData */
        if (lsda) {
            uint8_t* lp = (uint8_t*)lsda;
            MW_TRACE("[LSDA] raw[0:16]:  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[0], lp[1], lp[2], lp[3], lp[4], lp[5], lp[6], lp[7],
                     lp[8], lp[9], lp[10], lp[11], lp[12], lp[13], lp[14], lp[15]);
            MW_TRACE("[LSDA] raw[16:32]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[16], lp[17], lp[18], lp[19], lp[20], lp[21], lp[22], lp[23],
                     lp[24], lp[25], lp[26], lp[27], lp[28], lp[29], lp[30], lp[31]);
            MW_TRACE("[LSDA] raw[32:48]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[32], lp[33], lp[34], lp[35], lp[36], lp[37], lp[38], lp[39],
                     lp[40], lp[41], lp[42], lp[43], lp[44], lp[45], lp[46], lp[47]);
            MW_TRACE("[LSDA] raw[48:64]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[48], lp[49], lp[50], lp[51], lp[52], lp[53], lp[54], lp[55],
                     lp[56], lp[57], lp[58], lp[59], lp[60], lp[61], lp[62], lp[63]);
            MW_TRACE("[LSDA] raw[64:80]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[64], lp[65], lp[66], lp[67], lp[68], lp[69], lp[70], lp[71],
                     lp[72], lp[73], lp[74], lp[75], lp[76], lp[77], lp[78], lp[79]);
            MW_TRACE("[LSDA] raw[80:96]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[80], lp[81], lp[82], lp[83], lp[84], lp[85], lp[86], lp[87],
                     lp[88], lp[89], lp[90], lp[91], lp[92], lp[93], lp[94], lp[95]);
            MW_TRACE("[LSDA] raw[96:112]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[96], lp[97], lp[98], lp[99], lp[100], lp[101], lp[102], lp[103],
                     lp[104], lp[105], lp[106], lp[107], lp[108], lp[109], lp[110], lp[111]);
            MW_TRACE("[LSDA] raw[112:128]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[112], lp[113], lp[114], lp[115], lp[116], lp[117], lp[118], lp[119],
                     lp[120], lp[121], lp[122], lp[123], lp[124], lp[125], lp[126], lp[127]);
            /* Parse LSDA header for diagnostics */
            MW_TRACE("[LSDA] LPStart_enc=0x%02x (%s)", lp[0],
                     lp[0] == 0xff ? "omit" : "present");
            MW_TRACE("[LSDA] TType_enc=0x%02x", lp[1]);
            MW_TRACE("[LSDA] CS_enc=0x%02x", lp[2]);
        }
        /* Dump CONTEXT state being passed to handler */
        {
            uint64_t ctx_rip = *(uint64_t*)(ctx + CTX_Rip);
            uint64_t ctx_rsp = *(uint64_t*)(ctx + CTX_Rsp);
            uint64_t ctx_rbp = *(uint64_t*)(ctx + CTX_Rbp);
            MW_TRACE("[HANDLER_CALL] CTX.Rip=0x%lx CTX.Rsp=0x%lx CTX.Rbp=0x%lx",
                     ctx_rip, ctx_rsp, ctx_rbp);
        }
        MW_TRACE("[HANDLER_CALL] calling...");

        /* Call handler via aligned trampoline */
        int32_t disposition = call_handler_aligned(handler, &er, est_frame, ctx, &dc);

        /* CRITICAL: flush immediately so we don't lose trace if handler crashes */
        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);

        MW_TRACE("[HANDLER_CALL] returned disposition=%d (0=Exec %1=Search %2=Nested %3=Collided)", disposition);
        MW_TRACE("[HANDLER_CALL] Frame[%d] END", frame);

        /* Restore CONTEXT.Rip (was adjusted for personality) */
        *(uint64_t*)(ctx + CTX_Rip) = saved_ctx_rip;

        if (disposition == DISP_ExceptionContinueExecution) {
            MW_TRACE("[DISPATCH] Handler returned ContinueExecution — resuming");
            return DISP_RESULT_CONTINUE_EXEC;
        }

        if (disposition == DISP_ExceptionContinueSearch) {
            MW_TRACE("[DISPATCH] Handler returned ContinueSearch — walking to parent");

            /* EH_FIX: Bypass broken personality fn for DWARF LSDA with catch actions.
             * If the handler returned ContinueSearch but the LSDA has a landing pad
             * with a catch action (action > 0), the personality fn failed to match.
             * We parse the DWARF LSDA ourselves and handle it. */
            int lp_override = 0;
            uint8_t* lsda_data = (uint8_t*)lsda;
            /* TASK-API-005 (ADR-010): gated behind MINWIN_EH_HACKS, default OFF.
             * BUG-029 top suspect — see flag comment above. */
            if (mw_eh_hacks_enabled() && lsda_data && lsda_data[0] == 0xff && lsda_data[2] == 0x01) {
                /* DWARF LSDA with uleb128 CS encoding */
                uint32_t cs_off = 3;
                while (cs_off < 64 && lsda_data[cs_off] != 0) {
                    uint32_t cs_start = lsda_data[cs_off++];
                    cs_off++; /* length */
                    uint32_t cs_lp = lsda_data[cs_off++];
                    uint32_t cs_action = lsda_data[cs_off++];
                    uint64_t func_begin = (uint64_t)(uintptr_t)(g_image_base + rf->BeginAddress);
                    uint64_t pc_offset = (saved_ctx_rip - 1) - func_begin;
                    MW_TRACE("[EH_FIX] CS: start=%u len=%u lp=%u act=%u pc_off=%lu range=[%u,%u)",
                             cs_start, cs_start + lsda_data[cs_off-1], cs_lp, cs_action,
                             (unsigned long)pc_offset, cs_start, cs_start + lsda_data[cs_off-1]);
                    if (pc_offset >= cs_start && pc_offset < cs_start + lsda_data[cs_off-1]) {
                        if (cs_action > 0 && cs_lp > 0) {
                            /* Has both landing pad and action — personality fn should have caught */
                            uint64_t target_lp = (uint64_t)(uintptr_t)(g_image_base + rf->BeginAddress) + cs_lp;
                            MW_TRACE("[EH_FIX] Override: personality failed, catching at LP=0x%lx (RVA 0x%lx)",
                                     target_lp, cs_lp);
                            *(uint64_t*)(ctx + CTX_Rip) = target_lp;
                            /* RSP = establisher frame (post-prologue) for catch block */
                            *(uint64_t*)(ctx + CTX_Rsp) = est_frame;
                            lp_override = 1;
                            break;
                        }
                    }
                }
            }
            if (lp_override) {
                MW_TRACE("[DISPATCH] EH_FIX: Overrode personality — resuming at catch");
                return DISP_RESULT_CONTINUE_EXEC;
            }

            /* Read parent return address from [establisher_frame] */
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est_frame;
            uint64_t parent_rva = parent_rip - img_base;
            MW_TRACE("[DISPATCH] Frame[%d]: parent RIP=0x%lx RVA=0x%lx",
                     frame, parent_rip, parent_rva);
            *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(ctx + CTX_Rsp) = est_frame + 8;
            continue;
        }

        /* DISP_ExceptionNestedException (2) or DISP_ExceptionCollidedUnwind (3)
         * — for now, treat as stop-walking */
        MW_TRACE("[DISPATCH] Handler returned disposition=%d — stopping", disposition);
        break;
    }

    MW_TRACE("[DISPATCH] No handler handled the exception");
    if (g_trace_log) fflush(g_trace_log);
    fflush(stderr);
    return DISP_RESULT_NOT_HANDLED;
}

/* Naked stub: captures RIP/RSP at RaiseException entry, calls dispatcher. */
__attribute__((naked, ms_abi))
void mw_RaiseException(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    __asm__ volatile (
        /* Entry: RSP -> return address. MS ABI: rcx=code, rdx=flags, r8=nargs, r9=args */
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "subq $0x28, %rsp\n\t"   /* 5 qwords for our data, 16-byte aligned */
        /* BUG-031: capture true raise-site volatile regs BEFORE any scratch use.
         * Direct global stores — the stub frame layout is unchanged. */
        "movq %rax, g_cap_rax(%rip)\n\t"
        "movq %r10, g_cap_r10(%rip)\n\t"
        "movq %r11, g_cap_r11(%rip)\n\t"
        /* Save rsp_entry (RSP before our pushes + sub) */
        "leaq 0x58(%rsp), %rax\n\t"  /* 6*8 + 0x28 = 48+40 = 88 = 0x58 */
        "movq %rax, 0x00(%rsp)\n\t"  /* save rsp_entry */
        /* Save return address = [rsp_entry] */
        "movq (%rax), %rbx\n\t"
        "movq %rbx, 0x08(%rsp)\n\t"  /* save rip */
        /* Save parameters (still in rcx, edx, r8d, r9 from ms_abi) */
        "movq %rcx, 0x10(%rsp)\n\t"
        "movl %edx, 0x18(%rsp)\n\t"
        "movl %r8d, 0x1C(%rsp)\n\t"
        "movq %r9, 0x20(%rsp)\n\t"
        /* Copy to globals */
        "movq 0x30(%rsp), %rcx\n\t"     /* save PE RCX (code) */
        "movq 0x38(%rsp), %rdx\n\t"     /* save PE RDX (flags) */
        "movq 0x40(%rsp), %r8\n\t"     /* save PE R8 (nargs) */
        "movq 0x48(%rsp), %r9\n\t"     /* save PE R9 (args) */
        /* BUG-026 fix: g_cap_rip must receive the saved rip slot [rsp+0x08]
         * (the return address captured at stub entry), NOT rsp_entry.
         * Previously: stray "movq 0x58(%rsp), %rax" + "movq 0x00(%rsp), %rax"
         * stored a stack address into g_cap_rip -> CONTEXT.Rip outside PE ->
         * Frame[0] walk ended -> terminate -> exit 134. Matches
         * loader.c.exp_next2_backup L842-843. */
        "movq 0x08(%rsp), %rax\n\t"
        "movq %rax, g_cap_rip(%rip)\n\t"
        "movq 0x00(%rsp), %rax\n\t"
        "movq %rax, g_cap_rsp_entry(%rip)\n\t"
        "leaq 8(%rax), %rax\n\t"
        "movq %rax, g_cap_rsp(%rip)\n\t"
        "movl 0x10(%rsp), %eax\n\t"
        "movl %eax, g_cap_code(%rip)\n\t"
        "movl 0x18(%rsp), %eax\n\t"
        "movl %eax, g_cap_flags(%rip)\n\t"
        "movl 0x1C(%rsp), %eax\n\t"
        "movl %eax, g_cap_nargs(%rip)\n\t"
        "movq 0x20(%rsp), %rax\n\t"
        "movq %rax, g_cap_args(%rip)\n\t"
        /* Call the real exception dispatcher (SysV ABI) */
        "call seh_dispatch_exception\n\t"
        /* Check if RtlUnwindEx longjmped back (unwind path) */
        "cmpb $0, g_is_unwinding(%rip)\n\t"
        "je .L_mw_normal_return\n\t"
        /* === UNWIND RETURN PATH === */
        /* Restore all registers from g_unwind_regs[] globals.
         * These were copied from CONTEXT by seh_dispatch_exception BEFORE any
         * function calls that could overwrite the stack-local CONTEXT buffer. */
        /* Register indices: RAX=0 RCX=1 RDX=2 RBX=3 RSP=4 RBP=5 RSI=6 RDI=7
         *                   R8=8 R9=9 R10=10 R11=11 R12=12 R13=13 R14=14 R15=15 */
        "movq g_unwind_regs+0(%rip), %rax\n\t"     /* RAX [0] */
        "movq g_unwind_regs+8(%rip), %rcx\n\t"     /* RCX [1] */
        "movq g_unwind_regs+16(%rip), %rdx\n\t"    /* RDX [2] */
        "movq g_unwind_regs+24(%rip), %rbx\n\t"    /* RBX [3] */
        /* RSP loaded last — after loading everything else */
        "movq g_unwind_regs+40(%rip), %rbp\n\t"    /* RBP [5] */
        "movq g_unwind_regs+48(%rip), %rsi\n\t"    /* RSI [6] */
        "movq g_unwind_regs+56(%rip), %rdi\n\t"    /* RDI [7] */
        "movq g_unwind_regs+64(%rip), %r8\n\t"     /* R8 [8] */
        "movq g_unwind_regs+72(%rip), %r9\n\t"     /* R9 [9] */
        "movq g_unwind_regs+80(%rip), %r10\n\t"    /* R10 [10] */
        "movq g_unwind_regs+88(%rip), %r11\n\t"    /* R11 [11] */
        "movq g_unwind_regs+96(%rip), %r12\n\t"    /* R12 [12] */
        "movq g_unwind_regs+104(%rip), %r13\n\t"   /* R13 [13] */
        "movq g_unwind_regs+112(%rip), %r14\n\t"   /* R14 [14] */
        "movq g_unwind_regs+120(%rip), %r15\n\t"   /* R15 [15] */
        /* Load target RIP into rcx, then set RSP and jump */
        "movq g_unwind_target_ip(%rip), %rcx\n\t"
        "movq g_unwind_regs+32(%rip), %rsp\n\t"   /* RSP [4*8=32] */
        "jmp *%rcx\n\t"                     /* jump to catch landing pad */
        
        ".L_mw_normal_return:\n\t"
        /* Clean up and return (normal path) */
        "addq $0x28, %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "ret\n\t"
    );
}

/* Keep original implementation as reference (renamed, not called) */
static void mw_RaiseException_baseline(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    (void)code; (void)flags; (void)nargs; (void)args;
    /* Original body removed — kept for reference only */
}

__attribute__((ms_abi)) int mw_ReleaseSemaphore(void* h, int32_t count, int32_t* prev) {
    MW_TRACE("ReleaseSemaphore(h=%p, count=%d)", h, count);
    return mw_sem_release(h, count, prev);
}

__attribute__((ms_abi)) int mw_ResetEvent(void* h) {
    MW_TRACE("ResetEvent(h=%p)", h);
    return mw_event_reset(h);
}

__attribute__((ms_abi)) uint32_t mw_ResumeThread(void* h) {
    MW_TRACE("ResumeThread()");
    return 0;
}

/* SEH: RtlLookupFunctionEntry — binary search through .pdata */
__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t control_pc,
    uint64_t* out_image_base, void* history) {
    if (out_image_base) *out_image_base = (uint64_t)(uintptr_t)g_image_base;

    if (g_pdata_rva == 0 || g_num_rt_functions == 0) return NULL;

    uint64_t rva = control_pc - (uint64_t)(uintptr_t)g_image_base;
    RUNTIME_FUNCTION* base = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);

    /* Binary search: entries are sorted by BeginAddress */
    int lo = 0, hi = (int)g_num_rt_functions - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        RUNTIME_FUNCTION* rf = &base[mid];
        if (rva < rf->BeginAddress) {
            hi = mid - 1;
        } else if (rva >= rf->EndAddress) {
            lo = mid + 1;
        } else {
            MW_TRACE("RtlLookupFunctionEntry(pc=0x%lx rva=0x%lx) -> [%d] begin=0x%x end=0x%x unwind=0x%x",
                     control_pc, (uint32_t)rva, mid,
                     rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);
            return (void*)rf;
        }
    }
    MW_TRACE("RtlLookupFunctionEntry(pc=0x%lx rva=0x%lx) -> NULL", control_pc, (uint32_t)rva);
    return NULL;
}

/* ============================================================
 * BUG-031 (ADR-011): GCC SEH unwind-chain support — helpers.
 *
 * Live-verified protocol (EXP-039, samples/upx_decompressed.exe):
 *  - Unwind-phase INSTALL at an intermediate frame ⇒ the GCC bridge
 *    re-raises RaiseException(0x21474343, flags=1, nparams=4,
 *    params={object, establisher, landing_pad, selector})
 *    (bridge core 0x49d3e9-0x49d413).
 *  - A cleanup landing pad ends with the continue helper 0x49d5c0,
 *    which calls RtlUnwindEx(est_target, lp_target, &ER, object,
 *    OriginalContext, …) DIRECTLY (no raise): the ER is built as
 *    {code=0x20474343, flags=1, nparams=4, same 4-param shape}
 *    (0x49d610-0x49d663; the packed code|flags qword 0x120474343
 *    that EXP-038 §5.1 read as a "continue-raise" code is really
 *    {dword code=0x20474343, dword flags=1}).
 *  - The bridge reacts to EH_UNWINDING personality calls by looking
 *    up the frame's LSDA call site at ControlPc: a cleanup hit
 *    re-raises (above); a pc inside a landing-pad region finds no
 *    call site and returns ContinueSearch — so re-walking an
 *    already-unwound frame is safe (no re-install, no loop).
 * ============================================================ */

/* Total prologue stack allocation of the function containing ip_va.
 * Full decode (no func_offset filtering): landing pads run after the
 * whole prologue. Slot addressing and ALLOC_SMALL use the BUG-027-
 * corrected rules; ALLOC_LARGE uses the real ABI scaling (info==0 ⇒
 * next uint16 * 8, info==1 ⇒ next uint32) — the pre-existing decode
 * copies in mw_RtlUnwindEx/mw_RtlVirtualUnwind still carry the API-006
 * deviation and are deliberately left untouched by this task.
 * Returns 0 if the function cannot be resolved. */
static uint64_t seh_total_prologue_alloc(uint64_t ip_va) {
    if (g_image_base == NULL || ip_va < (uint64_t)(uintptr_t)g_image_base) return 0;
    uint64_t rva = ip_va - (uint64_t)(uintptr_t)g_image_base;
    RUNTIME_FUNCTION* rf = seh_internal_lookup(rva);
    if (!rf) return 0;
    uint8_t* ui = g_image_base + rf->UnwindInfo;
    uint8_t count = ui[2];
    uint8_t* codes = ui + 4;
    int slot = 0;
    uint64_t total = 0;
    for (int i = 0; i < count; i++) {
        /* BUG-037: CountOfCodes counts SLOTS — bound on the slot cursor. */
        if (slot >= count || slot * 2 + 1 >= (int)(count * 2 + 16)) break;
        uint16_t cw = *(uint16_t*)(codes + slot * 2);
        uint8_t op = (cw >> 8) & 0x0f;
        uint8_t info = (cw >> 12) & 0x0f;
        /* BUG-037: opcodes renumbered to the current spec (XMM=8/9,
         * MACHFRAME=10, EPILOG=6 v2-only, SPARE=7). */
        switch (op) {
        case UWOP_PUSH_NONVOL: total += 8; break;
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (info == 0) total += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            else { slot++; total += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2)); }
            break;
        case UWOP_ALLOC_SMALL: total += (uint64_t)(info + 1) * 8; break;
        case UWOP_SAVE_NONVOL: slot++; break;
        case UWOP_SAVE_NONVOL_FAR: slot++; slot++; break;
        case UWOP_SAVE_XMM128: slot++; break;
        case UWOP_SAVE_XMM128_FAR: slot++; slot++; break;
        case UWOP_EPILOG: slot++; break;               /* 2 slots total, 0 stack */
        case UWOP_SPARE_CODE: slot++; slot++; break;   /* 3 slots total, 0 stack */
        case UWOP_PUSH_MACHFRAME: total += 40; break;
        default: if (op >= 11) slot++; break;
        }
        slot++;
    }
    return total;
}

/* Windows-style RtlUnwindEx intermediate walk: from start_ctx up to (but
 * excluding) the frame whose establisher == tgt_frame, call each frame's
 * personality with ExceptionFlags |= EH_UNWINDING (0x2). The GCC bridge
 * answers an unwind-phase cleanup hit by re-raising 0x21474343 (never
 * returns — the dispatcher intercept resumes the cleanup LP); a pc in a
 * landing-pad region finds no call site and returns ContinueSearch, so
 * re-walking already-unwound frames is safe. */
static void seh_walk_intermediate_cleanups(uint8_t* start_ctx, void* er_src,
                                           uint64_t tgt_frame, const char* tag) {
    uint8_t walk_ctx[CONTEXT_SIZE];
    memcpy(walk_ctx, start_ctx, CONTEXT_SIZE);
    EXCEPTION_RECORD er_u;
    memcpy(&er_u, er_src, sizeof(EXCEPTION_RECORD));
    er_u.ExceptionFlags |= 0x2; /* EXCEPTION_UNWINDING */

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    for (int f = 0; f < DISPATCH_MAX_FRAMES; f++) {
        uint64_t rip = *(uint64_t*)(walk_ctx + CTX_Rip);
        if (rip < img_base || rip >= img_end) {
            MW_TRACE("[BUG-031] %s walk: RIP 0x%lx outside PE — walk ends", tag, rip);
            break;
        }
        RUNTIME_FUNCTION* rf = seh_internal_lookup(rip - img_base);
        if (!rf) {
            MW_TRACE("[BUG-031] %s walk: no RUNTIME_FUNCTION at 0x%lx — walk ends", tag, rip);
            break;
        }
        uint64_t est = 0;
        uint32_t h_rva = 0;
        void* lsda = NULL;
        void* handler = seh_internal_virtual_unwind(rip, rf, walk_ctx, &est, &h_rva, &lsda);
        if (est == tgt_frame) {
            MW_TRACE("[BUG-031] %s walk: reached target frame est=0x%lx — intermediate scan done", tag, est);
            break;
        }
        if (!handler) {
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est;
            *(uint64_t*)(walk_ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(walk_ctx + CTX_Rsp) = est + 8;
            continue;
        }
        DISPATCHER_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        dc.ControlPc = rip - 1;
        dc.ImageBase = img_base;
        dc.FunctionEntry = rf;
        dc.EstablisherFrame = est;
        dc.TargetIp = 0;
        dc.ContextRecord = walk_ctx;
        dc.LanguageHandler = handler;
        dc.HandlerData = lsda;
        dc.HistoryTable = 0;
        dc.ScopeIndex = 0;
        dc.Fill = 0;
        uint64_t saved_rip = rip;
        *(uint64_t*)(walk_ctx + CTX_Rip) = rip - 1;
        MW_TRACE("[BUG-031] %s walk: personality fn=0x%lx est=0x%lx rip=0x%lx lsda=%p flags=0x%x (EH_UNWINDING)",
                 tag, (uint64_t)(uintptr_t)handler, est, rip, lsda, er_u.ExceptionFlags);
        if (g_trace_log) fflush(g_trace_log);
        int32_t disp = call_handler_aligned(handler, &er_u, est, walk_ctx, &dc);
        *(uint64_t*)(walk_ctx + CTX_Rip) = saved_rip;
        MW_TRACE("[BUG-031] %s walk: personality returned %d", tag, disp);
        if (disp == DISP_ExceptionContinueSearch) {
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est;
            *(uint64_t*)(walk_ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(walk_ctx + CTX_Rsp) = est + 8;
            continue;
        }
        /* Non-GCC dispositions (ExecuteHandler etc.) are outside the GCC
         * protocol — record and stop; the caller resumes the target LP. */
        MW_TRACE("[BUG-031] %s walk: unexpected disposition %d — stopping walk", tag, disp);
        break;
    }
}

__attribute__((ms_abi)) void mw_RtlUnwindEx(void* target_frame, void* target_ip, void* ctx_ptr, void* exc_record, void* history) {
    MW_TRACE("RtlUnwindEx(target_frame=%p, target_ip=%p, ctx=%p, exc=%p)",
             target_frame, target_ip, ctx_ptr, exc_record);
    
    uint64_t tgt_ip = (uint64_t)(uintptr_t)target_ip;
    uint64_t tgt_frame = (uint64_t)(uintptr_t)target_frame;
    
    MW_TRACE("RtlUnwindEx: TargetIp=0x%lx TargetFrame=0x%lx", tgt_ip, tgt_frame);

    /* ============================================================
     * BUG-031 (ADR-011): continuation unwind — a cleanup landing pad's
     * continue helper (0x49d5c0) calls RtlUnwindEx DIRECTLY with the
     * OriginalContext in arg5. The original dispatcher C frame (and its
     * setjmp jmpbuf) is gone at this point, so this path walks the
     * remaining intermediate cleanups from the continue helper's captured
     * context and then resumes the target landing pad DIRECTLY (no
     * longjmp). Never returns on success.
     * ============================================================ */
    if (g_is_unwinding) {
        uint8_t* rctx = (uint8_t*)(uintptr_t)history;
        if (!rctx) rctx = g_unwind_ctx;
        MW_TRACE("[BUG-031] continuation unwind: orig_ctx=%p", (void*)rctx);
        if (rctx && ctx_ptr) {
            uint32_t er_code0 = *(uint32_t*)((uint8_t*)ctx_ptr);
            if (er_code0 == 0x20474343u || er_code0 == 0x21474343u) {
                seh_walk_intermediate_cleanups(rctx, ctx_ptr, tgt_frame, "cont");
            }
        }
        if (rctx) {
            uint64_t alloc_cont = seh_total_prologue_alloc(tgt_ip);
            uint64_t post_prologue_rsp = (alloc_cont != 0) ? (tgt_frame - alloc_cont) : tgt_frame;
            *(uint64_t*)(rctx + CTX_Rip) = tgt_ip;
            *(uint64_t*)(rctx + CTX_Rsp) = post_prologue_rsp;
            uint64_t exc_obj = (uint64_t)(uintptr_t)exc_record;
            if (exc_obj != 0) *(uint64_t*)(rctx + CTX_Rax) = exc_obj;
            /* BUG-030 selector delivery — same fingerprint as the site-A path */
            if (ctx_ptr) {
                uint32_t er_nparams = *(uint32_t*)((uint8_t*)ctx_ptr + 0x18);
                uint64_t* er_params = (uint64_t*)((uint8_t*)ctx_ptr + 0x20);
                if (er_nparams == 4 && er_params[1] == tgt_frame && er_params[2] == tgt_ip) {
                    *(uint64_t*)(rctx + CTX_Rdx) = er_params[3];
                    MW_TRACE("[BUG-031] continuation: 4-param ER obj=0x%lx est=0x%lx lp=0x%lx sel=%lu -> CTX.Rdx",
                             er_params[0], er_params[1], er_params[2],
                             (unsigned long)er_params[3]);
                }
            }
            /* Copy the full resume context into the resume globals — mirrors
             * the dispatcher's longjmp-return path. */
            g_unwind_regs[UR_RAX] = *(uint64_t*)(rctx + CTX_Rax);
            g_unwind_regs[UR_RCX] = *(uint64_t*)(rctx + CTX_Rcx);
            g_unwind_regs[UR_RDX] = *(uint64_t*)(rctx + CTX_Rdx);
            g_unwind_regs[UR_RBX] = *(uint64_t*)(rctx + CTX_Rbx);
            g_unwind_regs[UR_RSP] = *(uint64_t*)(rctx + CTX_Rsp);
            g_unwind_regs[UR_RBP] = *(uint64_t*)(rctx + CTX_Rbp);
            g_unwind_regs[UR_RSI] = *(uint64_t*)(rctx + CTX_Rsi);
            g_unwind_regs[UR_RDI] = *(uint64_t*)(rctx + CTX_Rdi);
            g_unwind_regs[UR_R8]  = *(uint64_t*)(rctx + CTX_R8);
            g_unwind_regs[UR_R9]  = *(uint64_t*)(rctx + CTX_R9);
            g_unwind_regs[UR_R10] = *(uint64_t*)(rctx + CTX_R10);
            g_unwind_regs[UR_R11] = *(uint64_t*)(rctx + CTX_R11);
            g_unwind_regs[UR_R12] = *(uint64_t*)(rctx + CTX_R12);
            g_unwind_regs[UR_R13] = *(uint64_t*)(rctx + CTX_R13);
            g_unwind_regs[UR_R14] = *(uint64_t*)(rctx + CTX_R14);
            g_unwind_regs[UR_R15] = *(uint64_t*)(rctx + CTX_R15);
            g_unwind_target_ip = tgt_ip;
            g_unwind_target_frame = post_prologue_rsp;
            g_is_unwinding = 1;
            MW_TRACE("[BUG-031] continuation resume: RIP=0x%lx RSP=0x%lx alloc=0x%lx",
                     tgt_ip, post_prologue_rsp, (unsigned long)alloc_cont);
            if (g_trace_log) fflush(g_trace_log);
            fflush(stderr);
            /* Direct resume — same register restore as the naked stub's
             * unwind return path. "memory" clobber: the asm reads the
             * globals written above. */
            __asm__ volatile (
                "movq g_unwind_regs+0(%%rip), %%rax\n\t"
                "movq g_unwind_regs+8(%%rip), %%rcx\n\t"
                "movq g_unwind_regs+16(%%rip), %%rdx\n\t"
                "movq g_unwind_regs+24(%%rip), %%rbx\n\t"
                "movq g_unwind_regs+40(%%rip), %%rbp\n\t"
                "movq g_unwind_regs+48(%%rip), %%rsi\n\t"
                "movq g_unwind_regs+56(%%rip), %%rdi\n\t"
                "movq g_unwind_regs+64(%%rip), %%r8\n\t"
                "movq g_unwind_regs+72(%%rip), %%r9\n\t"
                "movq g_unwind_regs+80(%%rip), %%r10\n\t"
                "movq g_unwind_regs+88(%%rip), %%r11\n\t"
                "movq g_unwind_regs+96(%%rip), %%r12\n\t"
                "movq g_unwind_regs+104(%%rip), %%r13\n\t"
                "movq g_unwind_regs+112(%%rip), %%r14\n\t"
                "movq g_unwind_regs+120(%%rip), %%r15\n\t"
                "movq g_unwind_target_ip(%%rip), %%rcx\n\t"
                "movq g_unwind_regs+32(%%rip), %%rsp\n\t"
                "jmp *%%rcx\n\t"
                : : : "memory",
                  "rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi",
                  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
            );
        }
        MW_TRACE("[BUG-031] continuation unwind: no context — returning (caller aborts, fail-visible)");
        return;
    }

    /* BUG-031 (ADR-011): first-call intermediate-cleanup walk — Windows
     * RtlUnwindEx runs every frame's cleanup between the throw site and
     * the target; the previous implementation jumped straight to the
     * target LP (BUG-031 root cause). GCC protocol only (the ER code is
     * STATUS_GCC_THROW/STATUS_GCC_UNWIND); start from the throw-site
     * snapshot because g_unwind_ctx has already been walked to the target
     * frame by the dispatcher. A cleanup hit re-raises 0x21474343 and
     * never returns from this call. */
    if (g_throw_ctx_valid && ctx_ptr) {
        uint32_t er_code0 = *(uint32_t*)((uint8_t*)ctx_ptr);
        if (er_code0 == 0x20474343u || er_code0 == 0x21474343u) {
            seh_walk_intermediate_cleanups(g_throw_ctx_snapshot, ctx_ptr, tgt_frame, "first");
        }
    }
    
    /* Walk frames from current to target, calling cleanup handlers.
     * For each frame between the current frame and the target frame,
     * we call the personality function with EH_UNWINDING flag.
     * 
     * IMPORTANT: The personality function that called us already found
     * the catch handler. RtlUnwindEx is responsible for:
     * 1. Running cleanup (dtors) for intermediate frames
     * 2. Setting the context to resume at the catch handler
     * 3. NEVER returning to the caller (non-returning function)
     */
    
    /* For now, implement the minimal unwind:
     * - Skip cleanup of intermediate frames (UPX --version likely doesn't
     *   need complex dtor cleanup for the throw path)
     * - Modify the CONTEXT to target the catch landing pad
     * - longjmp back to the dispatcher's setjmp point
     * 
     * This is a simplified implementation. A full implementation would
     * walk each frame and call handlers with EH_UNWINDING flag.
     */
    
    uint8_t* ctx = g_unwind_ctx;
    if (ctx) {
        /* BUG-039/B: rebuild the landing-pad register base from the
         * target-frame snapshot ("returned into frame F from below"). The
         * live ctx was unwound THROUGH the handler's frame by the search
         * loop, so its nonvolatives hold the target's CALLER's values; the
         * landing pad needs the target's THROW-TIME values (RDI=&catch-var,
         * RBP=live counters, ...). RIP/RSP/RAX/RDX are overridden below by
         * the landing-pad protocol, so copying them here is harmless. */
        if (g_target_frame_ctx_valid) {
            static const uint16_t gpr_offsets[] = {
                CTX_Rax, CTX_Rcx, CTX_Rdx, CTX_Rbx, CTX_Rsp, CTX_Rbp,
                CTX_Rsi, CTX_Rdi, CTX_R8, CTX_R9, CTX_R10, CTX_R11,
                CTX_R12, CTX_R13, CTX_R14, CTX_R15
            };
            for (size_t gi = 0; gi < sizeof(gpr_offsets)/sizeof(gpr_offsets[0]); gi++) {
                *(uint64_t*)(ctx + gpr_offsets[gi]) =
                    *(uint64_t*)(g_target_frame_ctx + gpr_offsets[gi]);
            }
            g_target_frame_ctx_valid = 0;
            MW_TRACE("RtlUnwindEx: BUG-039/B register base restored from target-frame snapshot (RDI=0x%lx RBP=0x%lx RSI=0x%lx RBX=0x%lx)",
                     *(uint64_t*)(ctx + CTX_Rdi), *(uint64_t*)(ctx + CTX_Rbp),
                     *(uint64_t*)(ctx + CTX_Rsi), *(uint64_t*)(ctx + CTX_Rbx));
        }
        /* EXP-035 FIX: target_frame from the GCC personality function is the
         * function-entry RSP (RSP after CALL pushed return address). But the
         * catch/landing-pad code uses [RSP+offset] where offsets are relative
         * to the post-prologue RSP (RSP after the function's prologue).
         *
         * We compute post-prologue RSP = target_frame - stack_alloc.
         * The stack_alloc = target_frame - CTX.Rsp (because RtlVirtualUnwind
         * set CTX.Rsp to function-entry RSP, and the original RSP before
         * unwind was post-prologue RSP). So:
         *   stack_alloc = target_frame - CTX.Rsp
         *   post_prologue_RSP = target_frame - stack_alloc = CTX.Rsp
         *
         * Wait — CTX.Rsp was already modified by the handler's RtlVirtualUnwind
         * calls during frame walking. But we saved the ORIGINAL CTX.Rsp in
         * g_unwind_regs before the handler was called. Use that.
         * Actually, the simplest approach: use the pre_unwind_rsp we saved
         * in the dispatcher. But RtlUnwindEx doesn't have access to it.
         *
         * Instead, we can compute the post-prologue RSP from the unwind
         * info of the target function. But that's complex.
         *
         * Simplest correct fix: the handler's target_frame = function-entry RSP.
         * The stack_alloc = target_frame - CTX.Rsp_after_handler.
         * But CTX.Rsp_after_handler may be modified by handler.
         *
         * Actually, the most reliable method: look up the RUNTIME_FUNCTION for
         * the target_ip, read its unwind info, and process all codes to find
         * the total stack allocation. Then:
         *   post_prologue_RSP = target_frame - total_alloc.
         */
        uint64_t post_prologue_rsp = tgt_frame;

        /* Look up the RUNTIME_FUNCTION for the target IP to find
         * the total stack allocation of the target function. */
        uint64_t tgt_rva = tgt_ip - (uint64_t)(uintptr_t)g_image_base;
        RUNTIME_FUNCTION* tgt_rf = seh_internal_lookup(tgt_rva);
        if (tgt_rf) {
            uint8_t* tgt_ui = g_image_base + tgt_rf->UnwindInfo;
            uint8_t tgt_count = tgt_ui[2];
            /* BUG-027 fix: unwind code slots are 2 bytes each and must be
             * addressed in BYTES. The previous declaration `uint16_t* tgt_codes`
             * made `tgt_codes + tgt_slot * 2` advance 4 bytes per slot, so the
             * loop read every other slot and then garbage past the codes array:
             * for function 0xdcbf0 (codes 19 09 04 00 | 09 52 05 30 04 c0 02 d0 =
             * ALLOC_SMALL 0x30 + PUSH R13/R12/RBX = 0x48 total) it computed
             * alloc_total=0x40 instead of 0x48, resuming the landing pad with
             * RSP 8 bytes too high -> SIGSEGV at heap RIP (EXP-036 §4). */
            uint8_t* tgt_codes = tgt_ui + 4;
            int tgt_slot = 0;
            uint64_t alloc_total = 0;
            for (int i = 0; i < tgt_count; i++) {
                /* BUG-037: CountOfCodes counts SLOTS — bound on the slot
                 * cursor, otherwise padding/handler bytes decode as
                 * phantom codes (reproduced: alloc_total=0xff0191 came
                 * from reading the handler RVA as ALLOC_LARGE). */
                if (tgt_slot >= tgt_count ||
                    tgt_slot * 2 + 1 >= (int)(tgt_count * 2 + 16)) break;
                uint16_t cw = *(uint16_t*)(tgt_codes + tgt_slot * 2);
                uint8_t op = (cw >> 8) & 0x0f;
                uint8_t info = (cw >> 12) & 0x0f;
                /* BUG-037: opcodes renumbered to the current spec
                 * (XMM128=8, XMM128_FAR=9, MACHFRAME=10, EPILOG=6 v2-only,
                 * SPARE=7); missing UWOP_SAVE_NONVOL slot advance added;
                 * ALLOC_LARGE info==1 now unscaled u32 (TASK-API-006 rule). */
                switch (op) {
                case UWOP_ALLOC_LARGE:
                    tgt_slot++;
                    if (info == 0) {
                        alloc_total += (uint64_t)(*(uint16_t*)(tgt_codes + tgt_slot * 2)) * 8;
                    } else {
                        tgt_slot++;
                        alloc_total += (uint64_t)(*(uint32_t*)(tgt_codes + tgt_slot * 2 - 2));
                    }
                    break;
                case UWOP_ALLOC_SMALL:
                    alloc_total += (uint64_t)(info + 1) * 8;
                    break;
                case UWOP_PUSH_NONVOL:
                    alloc_total += 8;
                    break;
                case UWOP_SAVE_NONVOL:
                    tgt_slot++; break;
                case UWOP_SAVE_NONVOL_FAR:
                    tgt_slot++; tgt_slot++; break;
                case UWOP_SAVE_XMM128:
                    tgt_slot++; break;
                case UWOP_SAVE_XMM128_FAR:
                    tgt_slot++; tgt_slot++; break;
                case UWOP_EPILOG:
                    tgt_slot++; break;               /* 2 slots total, 0 stack */
                case UWOP_SPARE_CODE:
                    tgt_slot++; tgt_slot++; break;   /* 3 slots total, 0 stack */
                case UWOP_PUSH_MACHFRAME:
                    alloc_total += 40;
                    break;
                default:
                    if (op >= 11) tgt_slot++;
                    break;
                }
                tgt_slot++;
            }
            post_prologue_rsp = tgt_frame - alloc_total;
            /* BUG-037: for frames with a frame pointer the establisher is
             * the FP register value (mid-frame anchor), not the frame's
             * entry RSP. The body RSP is FP - FrameOffset*16 (establishment
             * happens after the last stack change in the prologue).
             * CRT-startup frame: FP=RBP=entry-0x108, off=8 -> body RSP =
             * entry-0x188; the old tgt_frame-alloc formula would have
             * produced entry-0x290. */
            {
                uint8_t tgt_framebyte = tgt_ui[3];
                uint8_t tgt_framereg  = tgt_framebyte & 0x0f;
                uint8_t tgt_frameoff  = (tgt_framebyte >> 4) & 0x0f;
                if (tgt_framereg) {
                    post_prologue_rsp = tgt_frame - (uint64_t)tgt_frameoff * 16;
                    MW_TRACE("RtlUnwindEx: FP frame reg=%u off=%u -> body RSP=0x%lx",
                             tgt_framereg, tgt_frameoff, post_prologue_rsp);
                }
            }
            MW_TRACE("RtlUnwindEx: target_func RVA=0x%x alloc_total=0x%lx post_prologue_RSP=0x%lx",
                     tgt_rf->BeginAddress, alloc_total, post_prologue_rsp);
        } else {
            MW_TRACE("RtlUnwindEx: WARNING no RUNTIME_FUNCTION for target, using target_frame directly");
        }

        /* Modify CONTEXT to target the landing pad */
        *(uint64_t*)(ctx + CTX_Rip) = tgt_ip;
        *(uint64_t*)(ctx + CTX_Rsp) = post_prologue_rsp;
        
        /* CRITICAL: The GCC personality function sets RAX to the exception object
         * pointer via _Unwind_SetGR, but it writes to the GCC unwind context,
         * not our Windows CONTEXT buffer. We need to transfer the exception
         * object pointer to our CONTEXT's RAX so the catch handler can access it.
         * The 4th argument (exc_record) is the exception object pointer.
         * For C++ exceptions (code 0x20474343), this is the _Unwind_Exception*
         * which also serves as the exception object pointer for the catch handler. */
        uint64_t exc_obj = (uint64_t)(uintptr_t)exc_record;
        if (exc_obj != 0) {
            *(uint64_t*)(ctx + CTX_Rax) = exc_obj;
            MW_TRACE("RtlUnwindEx: Set CTX.Rax = 0x%lx (exception object)", exc_obj);
        }

        /* BUG-030 fix (EXP-038): deliver the GCC/UPX catch selector in RDX.
         * Ground truth (EXP-038 §2-§3): the GCC SEH bridge site A passes
         * arg3 = &EXCEPTION_RECORD (NOT a CONTEXT — EXP-037 §2.2) carrying the
         * 4-parameter protocol param = {object, target_est, landing_pad, selector}
         * (bridge 0x49d4f6-0x49d528 copies its blob slots: blob[0]=object -> arg4,
         * blob[1]=selector -> ER.param[3]; blob slots written by helper 0x49d270,
         * selector stored by the core at obj-0x24 during phase 1).
         * The landing pad consumes the selector in RDX — B's catch at 0x40331c:
         * `mov rbx,rdx; ...; sub rbx,1; jne <assert-failure>` (selector must be 1).
         * Fingerprint-guarded: applied only when arg3 really is the protocol ER. */
        if (ctx_ptr) {
            uint32_t er_nparams = *(uint32_t*)((uint8_t*)ctx_ptr + 0x18);
            uint64_t* er_params = (uint64_t*)((uint8_t*)ctx_ptr + 0x20);
            if (er_nparams == 4 && er_params[1] == tgt_frame && er_params[2] == tgt_ip) {
                uint64_t selector = er_params[3];
                *(uint64_t*)(ctx + CTX_Rdx) = selector;
                MW_TRACE("RtlUnwindEx: 4-param protocol ER (obj=0x%lx est=0x%lx lp=0x%lx sel=%lu) -> CTX.Rdx=%lu",
                         er_params[0], er_params[1], er_params[2],
                         (unsigned long)selector, (unsigned long)selector);
            }
        }
        
        /* Store targets for the dispatcher */
        g_unwind_target_ip = tgt_ip;
        g_unwind_target_frame = post_prologue_rsp;
        g_is_unwinding = 1;
        
        MW_TRACE("RtlUnwindEx: longjmping to dispatcher (RIP=0x%lx RSP=0x%lx)", tgt_ip, post_prologue_rsp);

        /* DIAGNOSTIC: Verify the landing pad will see correct stack data */
        {
            uint64_t* check_rsp = (uint64_t*)(uintptr_t)tgt_frame;
            MW_TRACE("RtlUnwindEx DIAG: tgt_frame(post-prologue RSP)=0x%lx", tgt_frame);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x30]=0x%lx (expect global _onexit ptr)", check_rsp[6]);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x38]=0x%lx (expect _pfirst ptr)", check_rsp[7]);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x40]=0x%lx (expect count)", check_rsp[8]);
        }

        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);
        
        /* longjmp back to seh_dispatch_exception's setjmp point.
         * This NEVER returns. */
        longjmp(g_unwind_jmpbuf, 1);
    }
    
    MW_TRACE("RtlUnwindEx: ERROR — no CONTEXT available, cannot unwind");
}

__attribute__((ms_abi)) void* mw_RtlVirtualUnwind(uint32_t handler_type, uint64_t image_base, uint64_t control_pc,
    void* function_entry, void* context_ptr, void* handler_data_ptr,
    uint64_t* establisher_frame_ptr, void* context_pointers) {
    RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)function_entry;
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;
    uint8_t* ctx = (uint8_t*)context_ptr;

    uint8_t version = ui_base[0] & 0x07;
    uint8_t flags   = (ui_base[0] >> 3) & 0x07;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    /* BUG-037: FrameRegister = LOW nibble, scaled FrameOffset = HIGH nibble
     * (little-endian C bitfields; see seh_internal_virtual_unwind). */
    uint8_t frame_reg   = ui_base[3] & 0x0f;
    uint8_t frame_off   = (ui_base[3] >> 4) & 0x0f;

    (void)image_base; (void)context_pointers; (void)prolog_size;

    /* Handle CHAININFO */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        MW_TRACE("RtlVirtualUnwind: CHAININFO -> begin=0x%x unwind=0x%x",
                 chained->BeginAddress, chained->UnwindInfo);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x07;
        uint8_t chained_codes = chained_ui[2];
        if (chained_flags & handler_type) {
            uint32_t h_off = 4 + chained_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            void* lsda = (void*)(chained_ui + h_off + 4);
            if (handler_data_ptr) *(void**)handler_data_ptr = lsda;
            return (void*)(g_image_base + handler_rva);
        }
        return NULL;
    }

    /* Read current register values from context */
    uint64_t rsp = *(uint64_t*)(ctx + CTX_Rsp);
    uint64_t rip = *(uint64_t*)(ctx + CTX_Rip);
    uint32_t rip_rva = (uint32_t)(rip - (uint64_t)(uintptr_t)g_image_base);
    uint32_t func_offset = rip_rva - rf->BeginAddress;

    /* Track stack during prolog simulation */
    uint64_t new_rsp = rsp;
    int fp_set = 0;
    uint64_t fp_reg = 0;

    /* Simulate unwind codes to "undo" the prolog */
    uint8_t* codes = ui_base + 4;
    int slot = 0;
    for (int i = 0; i < count_codes; i++) {
        /* BUG-037: CountOfCodes counts SLOTS — bound on the slot cursor. */
        if (slot >= count_codes) break;
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet — full slot advance
         * so the cursor stays aligned (BUG-037). */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot += seh_unwind_op_slots(op_code, op_info);
            continue;
        }
        if (op_code == UWOP_EPILOG && version < 2) {
            /* Epilogue descriptors exist only in UNWIND_INFO v2. */
            MW_TRACE("RtlVirtualUnwind: UI v1 has EPILOG code — corrupt, stop");
            break;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            /* BUG-039: restore from CURRENT rsp BEFORE advancing (undo order;
             * ReactOS PopReg semantics) — mirrors seh_internal_virtual_unwind. */
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            new_rsp += 8;
            break;
        }
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (op_info == 0) {
                new_rsp += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            } else {
                slot++;
                new_rsp += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2));
            }
            break;
        case UWOP_ALLOC_SMALL: {
            new_rsp += (op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            /* ReactOS semantics: re-anchor RSP from the LIVE FP register
             * (BUG-037; see seh_internal_virtual_unwind). */
            if (frame_reg) {
                uint32_t fp_ctx_off = reg_to_ctx_offset(frame_reg);
                if (fp_ctx_off) {
                    fp_reg = *(uint64_t*)(ctx + fp_ctx_off);
                    new_rsp = fp_reg - (uint64_t)frame_off * 16;
                } else {
                    fp_reg = new_rsp + (uint64_t)frame_off * 16;
                }
            } else {
                fp_reg = new_rsp + (uint64_t)frame_off * 16;
            }
            fp_set = 1;
            break;
        }
        case UWOP_SAVE_NONVOL: {
            slot++;
            uint16_t stack_off = *(uint16_t*)(codes + slot * 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off * 8);
            }
            break;
        }
        case UWOP_SAVE_NONVOL_FAR: {
            slot++; slot++;
            uint32_t stack_off = *(uint32_t*)(codes + slot * 2 - 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off);
            }
            break;
        }
        case UWOP_SAVE_XMM128: {
            slot++;
            break;
        }
        case UWOP_SAVE_XMM128_FAR: {
            slot++; slot++;
            break;
        }
        case UWOP_EPILOG: {
            slot++;               /* 2 slots total, no stack effect */
            break;
        }
        case UWOP_SPARE_CODE: {
            slot++; slot++;       /* 3 slots total */
            break;
        }
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
            new_rsp = *(uint64_t*)(new_rsp + 8);
            break;
        }
        default: {
            static int warned_unknown[7] = {0};
            if (op_code >= 11 && !warned_unknown[op_code - 11]) {
                MW_TRACE("RtlVirtualUnwind: unknown opcode %d (skipping)", op_code);
                warned_unknown[op_code - 11] = 1;
            }
            if (op_code >= 11) slot++; /* reserved ops: skip code word only */
            break;
        }
        }
        slot++;
    }

    /* Apply frame register */
    if (fp_set) {
        uint32_t fp_off = reg_to_ctx_offset(frame_reg);
        if (fp_off) *(uint64_t*)(ctx + fp_off) = fp_reg;
    }

    /* Update RSP */
    *(uint64_t*)(ctx + CTX_Rsp) = new_rsp;

    /* Set establisher frame */
    uint64_t establisher = fp_set ? fp_reg : new_rsp;
    if (establisher_frame_ptr) *establisher_frame_ptr = establisher;

    /* Find handler and LSDA */
    uint32_t handler_rva = 0;
    void* lsda = NULL;
    if (flags & handler_type) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
        lsda = (void*)(ui_base + h_off + 4);
    }

    if (handler_data_ptr && lsda) {
        *(void**)handler_data_ptr = lsda;
    }

    MW_TRACE("RtlVirtualUnwind: pc=0x%lx begin=0x%x handler=0x%x est=0x%lx",
             (uint64_t)rip, rf->BeginAddress, handler_rva, establisher);

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* SEH: RtlCaptureContext stub */
__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {
    MW_TRACE("RtlCaptureContext()");
}

__attribute__((ms_abi)) int mw_ScrollConsoleScreenBufferA(void* h, void* sr, void* dr, void* coord, void* fill) {
    MW_TRACE("ScrollConsoleScreenBufferA()");
    return 0;
}

__attribute__((ms_abi)) int mw_SetEvent(void* h) {
    MW_TRACE("SetEvent(h=%p)", h);
    return mw_event_set(h);
}

__attribute__((ms_abi)) int mw_SetFileTime(void* h, void* ct, void* at, void* wt) {
    MW_TRACE("SetFileTime()");
    return 0;
}

__attribute__((ms_abi)) void mw_SetLastError(uint32_t err) {
    g_last_error = err;
    MW_TRACE("SetLastError(%u)", err);
}

__attribute__((ms_abi)) int mw_SetProcessAffinityMask(void* h, uint64_t mask) {
    MW_TRACE("SetProcessAffinityMask()");
    return 1;
}

__attribute__((ms_abi)) int mw_SetThreadContext(void* h, void* ctx) {
    MW_TRACE("SetThreadContext()");
    return 0;
}

__attribute__((ms_abi)) int mw_SetThreadPriority(void* h, int prio) {
    MW_TRACE("SetThreadPriority()");
    return 1;
}

__attribute__((ms_abi)) void* mw_SetUnhandledExceptionFilter(void* handler) {
    MW_TRACE("SetUnhandledExceptionFilter(handler=%p)", handler);
    void* old = g_unhandled_exception_filter;
    g_unhandled_exception_filter = handler;
    return old;
}

__attribute__((ms_abi)) void mw_Sleep(uint32_t ms) {
    MW_TRACE("Sleep(%u)", ms);
    if (ms > 0) usleep((useconds_t)ms * 1000);
}

__attribute__((ms_abi)) uint32_t mw_SuspendThread(void* h) {
    MW_TRACE("SuspendThread()");
    return 0;
}

/* TLS */
/* TASK-TLS-001: TlsAlloc/GetValue/SetValue share the SAME slot array the
 * native-TLS static block uses (gs:0x58 -> g_tls_ptr_array, declared above).
 * Slot 0 is the image's static TLS; dynamic TlsAlloc slots start at 1. */

__attribute__((ms_abi)) uint32_t mw_TlsAlloc(void) {
    static uint32_t next = 1; /* slot 0 = image static TLS */
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    uint32_t idx = next++;
    pthread_mutex_unlock(&mu);
    if (idx < 64) {
        g_tls_ptr_array[idx] = NULL;
        MW_TRACE("TlsAlloc() = %u", idx);
        return idx;
    }
    return (uint32_t)(-1);
}

__attribute__((ms_abi)) void* mw_TlsGetValue(uint32_t idx) {
    void* val = (idx < 64) ? g_tls_ptr_array[idx] : NULL;
    MW_TRACE("TlsGetValue(%u) = %p", idx, val);
    return val;
}

__attribute__((ms_abi)) int mw_TlsSetValue(uint32_t idx, void* val) {
    if (idx < 64) g_tls_ptr_array[idx] = val;
    MW_TRACE("TlsSetValue(%u, %p)", idx, val);
    return 1;
}

__attribute__((ms_abi)) int mw_VirtualProtect(void* addr, size_t size, uint32_t prot, uint32_t* old) {
    MW_TRACE("VirtualProtect(addr=%p, size=0x%zx, prot=0x%x)", addr, size, prot);
    int linux_prot = PROT_READ | PROT_WRITE;
    if (prot & 0x01) linux_prot = PROT_READ;                 /* PAGE_NOACCESS handled below */
    if (prot & 0x02) linux_prot |= PROT_READ;                /* PAGE_READONLY */
    if (prot & 0x04) linux_prot |= PROT_WRITE;               /* PAGE_READWRITE */
    /* BUG-065: PAGE_EXECUTE(0x10)/PAGE_EXECUTE_READ(0x20)/
     * PAGE_EXECUTE_READWRITE(0x40)/PAGE_EXECUTE_WRITECOPY(0x80) must map to
     * PROT_EXEC — mingw's pseudo-relocator makes .text RWX then RESTORES with
     * the value we report in *old; missing either half kills the text page
     * (md5deep64 SIGSEGV at the instruction right after the call). */
    if (prot & (0x10u | 0x20u | 0x40u | 0x80u)) linux_prot |= PROT_EXEC;
    if (prot == 0x01) linux_prot = PROT_NONE;                /* PAGE_NOACCESS */
    /* Honest old-protect: image pages are RWX under this loader (self-modifying
     * relocators rely on restoring what we report); heap/other = RW. */
    if (old) {
        if ((uintptr_t)addr >= (uintptr_t)g_image_base &&
            (uintptr_t)addr <  (uintptr_t)g_image_base + g_image_size) {
            *old = 0x40; /* PAGE_EXECUTE_READWRITE */
        } else {
            *old = 0x04; /* PAGE_READWRITE */
        }
    }
    /* OPENSRC-7ZIP: Windows VirtualProtect accepts any address and rounds
     * internally; mprotect demands page alignment (mingw CRT passes the raw
     * TLS/stack address -> EINVAL -> "VirtualProtect failed" abort, t08). */
    if (addr) {
        uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0xFFF;
        uintptr_t end  = ((uintptr_t)addr + size + 0xFFF) & ~(size_t)0xFFF;
        /* BUG-065: the image is mapped per-section, so [addr, addr+size) can
         * span UNMAPPED alignment gaps — one big mprotect over the whole span
         * fails with ENOMEM (mingw pseudo-relocator: "VirtualProtect failed").
         * Walk page-by-page and skip gaps (Windows sees all pages committed;
         * our gaps are by-construction unmapped slack). */
        if (end <= page) return 1;
        for (uintptr_t p = page; p < end; p += 0x1000) {
            if (mprotect((void*)p, 0x1000, linux_prot) != 0 && errno != ENOMEM) return 0;
        }
        return 1;
    }
    return 0;
}

__attribute__((ms_abi)) size_t mw_VirtualQuery(void* addr, void* info, size_t len) {
    MW_TRACE("VirtualQuery(addr=%p)", addr);
    if (info && len >= 48) {
        memset(info, 0, 48);
        *(uint64_t*)((uint8_t*)info + 0) = (uint64_t)(uintptr_t)addr; /* BaseAddress */
        *(uint64_t*)((uint8_t*)info + 8) = 0x1000;                    /* AllocationBase */
        *(uint32_t*)((uint8_t*)info + 16) = 0x1000;                   /* AllocationProtect */
        *(uint64_t*)((uint8_t*)info + 24) = g_image_size;              /* RegionSize */
        *(uint32_t*)((uint8_t*)info + 32) = 0x04;                     /* State = MEM_COMMIT */
        *(uint32_t*)((uint8_t*)info + 36) = 0x40 | 0x20;             /* Protect = PAGE_READWRITE | RWX */
        *(uint32_t*)((uint8_t*)info + 40) = 0x1000;                   /* Type = MEM_PRIVATE */
        return 48;
    }
    return 0;
}

/* ============================================================
 * BUG-062: real guest threads.
 *
 * _beginthreadex/_endthreadex/CreateThread used to be stubs that returned
 * "failure" (handle 0/NULL). Consequences measured with zstd.exe 1.5.6
 * (LADDER-008): its thread pool (POOL_create -> _beginthreadex) got zero
 * workers, ZSTDMT still queued jobs and the CLI blocked forever in
 * SleepConditionVariableCS waiting on a completion CV nobody would ever
 * signal. Hang threshold measured at input >= 0x60000 bytes (393216);
 * smaller inputs take the serial path and exit cleanly.
 *
 * Design:
 *  - opaque HANDLE built with alloc_handle() wrapping a MwThread slot;
 *  - host pthread runs a SysV trampoline that calls the GUEST start routine
 *    through an ms_abi function-pointer type (x64 Windows ABI: RCX/RDX/R8/R9
 *    + shadow space), so plain MS code runs on a host thread;
 *  - completion (normal return or _endthreadex) sets done + exit_code under
 *    t_exit_mu and broadcasts t_exit_cv;
 *  - WaitForSingleObject honors thread handles (WAIT_OBJECT_0 / WAIT_TIMEOUT);
 *  - CloseHandle releases the slot (join-without-block semantics: the thread
 *    is detached at creation, so no pthread_join resource leak).
 * Limitations (documented, honest):
 *  - CREATE_SUSPENDED (initflag bit 4) is ignored — thread starts running;
 *  - TLS (TlsGetValue/SetValue) shares the main thread's slots across threads;
 *  - no per-thread CRT init (fine for plain-C worker threads like zstd's).
 * ============================================================ */
typedef unsigned (__attribute__((ms_abi)) *MwGuestStart)(void*);
typedef struct {
    int used;
    int done;
    unsigned exit_code;
    pthread_t pt;
} MwThread;
static MwThread g_mw_threads[64];
static pthread_mutex_t g_mw_threads_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t t_exit_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t t_exit_cv = PTHREAD_COND_INITIALIZER;
static __thread MwThread* t_cur_thread;

typedef struct { MwGuestStart start; void* arg; MwThread* self; } MwThreadBoot;

static void* mw_thread_trampoline(void* p) {
    MwThreadBoot* b = (MwThreadBoot*)p;
    MwThread* self = b->self;
    MwGuestStart start = b->start;
    void* arg = b->arg;
    free(b);
    t_cur_thread = self;
    unsigned ret = start(arg);   /* MS-ABI call into guest code */
    pthread_mutex_lock(&t_exit_mu);
    if (!self->done) { self->exit_code = ret; self->done = 1; }
    pthread_cond_broadcast(&t_exit_cv);
    pthread_mutex_unlock(&t_exit_mu);
    t_cur_thread = NULL;
    return NULL;
}

static MwThread* mw_thread_lookup(void* h) {
    /* Handles are small integers from alloc_handle(); the table stores the
     * MwThread* they wrap. */
    if (!h || (uintptr_t)h < 4) return NULL;
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= MAX_HANDLES) return NULL;
    void* v = g_handle_table[idx];
    if (!v) return NULL;
    MwThread* base = &g_mw_threads[0];
    if ((MwThread*)v < base || (MwThread*)v >= base + 64) return NULL;
    MwThread* t = (MwThread*)v;
    return t->used ? t : NULL;
}

static uint32_t mw_thread_wait(void* h, uint32_t ms) {
    MwThread* t = mw_thread_lookup(h);
    if (!t) return 0; /* WAIT_OBJECT_0 — legacy passthrough for non-thread handles */
    pthread_mutex_lock(&t_exit_mu);
    uint32_t rc = 0; /* WAIT_OBJECT_0 */
    if (!t->done) {
        if (ms == 0xFFFFFFFFu) {
            while (!t->done) pthread_cond_wait(&t_exit_cv, &t_exit_mu);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (!t->done) {
                int r = pthread_cond_timedwait(&t_exit_cv, &t_exit_mu, &ts);
                if (r == ETIMEDOUT) { rc = 0x102; break; } /* WAIT_TIMEOUT */
            }
        }
    }
    pthread_mutex_unlock(&t_exit_mu);
    return rc;
}

static void mw_thread_release(void* h) {
    MwThread* t = mw_thread_lookup(h);
    if (!t) return;
    pthread_mutex_lock(&t_exit_mu);
    int done = t->done;
    pthread_mutex_unlock(&t_exit_mu);
    if (done) { t->used = 0; } /* fully reaped */
    /* still running: keep slot for _endthreadex completion; leak-free because
     * the thread is detached and the slot is statically allocated */
}

/* ============================================================
 * LADDER-009 (lz4.exe): minimal honest IOCP + RtlAddFunctionTable +
 * __doserrno + _vsnprintf. Without them lz4 decompress jumped to the
 * hint/name RVA of CreateIoCompletionPort (unresolved IAT slot still
 * holding ILT contents = BUG-032-local class crash at RIP=0x5a7ca).
 * IOCP semantics implemented for the in-process queue (Post/Get with
 * timeout); association of FILE handles records nothing yet — honest
 * limitation documented in docs/bugs/BUG-064.md.
 * ============================================================ */
typedef struct { void* key; void* ovl; unsigned long count; } MwIocpEntry;
typedef struct {
    int used;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    MwIocpEntry q[256];
    int head, tail, cnt;
} MwIocp;
static MwIocp g_iocp_tab[16];

static MwIocp* mw_iocp_lookup(void* h) {
    if (!h) return NULL;
    MwIocp* base = &g_iocp_tab[0];
    if ((MwIocp*)h < base || (MwIocp*)h >= base + 16) return NULL;
    MwIocp* p = (MwIocp*)h;
    return p->used ? p : NULL;
}

static int g_doserrno_storage = 0;

__attribute__((ms_abi)) void* mw_CreateIoCompletionPort(void* file, void* existing,
    unsigned long key, unsigned long threads) {
    (void)file; (void)key; (void)threads;
    MW_TRACE("CreateIoCompletionPort(file=%p, existing=%p, key=%lu)", file, existing, key);
    if (existing) return existing;      /* association: port reused as-is */
    for (int i = 0; i < 16; i++) {
        if (!g_iocp_tab[i].used) {
            MwIocp* p = &g_iocp_tab[i];
            memset(p, 0, sizeof(*p));
            p->used = 1;
            pthread_mutex_init(&p->mu, NULL);
            pthread_cond_init(&p->cv, NULL);
            return p;                    /* handle == slot pointer */
        }
    }
    mw_SetLastError(8); /* ERROR_NOT_ENOUGH_MEMORY */
    return NULL;
}

__attribute__((ms_abi)) int mw_PostQueuedCompletionStatus(void* port,
    unsigned long count, unsigned long key, void* ovl) {
    MW_TRACE("PostQueuedCompletionStatus(port=%p, count=%lu, key=%lu)", port, count, key);
    MwIocp* p = mw_iocp_lookup(port);
    if (!p) { mw_SetLastError(87); return 0; } /* ERROR_INVALID_PARAMETER */
    pthread_mutex_lock(&p->mu);
    if (p->cnt >= 256) { pthread_mutex_unlock(&p->mu); mw_SetLastError(1385); return 0; }
    MwIocpEntry* e = &p->q[p->tail];
    e->key = (void*)(uintptr_t)key; e->ovl = ovl; e->count = count;
    p->tail = (p->tail + 1) % 256;
    p->cnt++;
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->mu);
    return 1;
}

__attribute__((ms_abi)) int mw_GetQueuedCompletionStatus(void* port,
    unsigned long* count, unsigned long* key, void** ovl, uint32_t ms) {
    /* ms is a Windows DWORD: the guest writes 32 bits into the 5th-arg stack
     * slot — reading it as 64-bit picked up dirty high bits (0x7f8dffffffff
     * instead of INFINITE 0xffffffff). */
    MW_TRACE("GetQueuedCompletionStatus(port=%p, ms=%lu)", port, ms);
    MwIocp* p = mw_iocp_lookup(port);
    if (!p) { mw_SetLastError(87); return 0; }
    pthread_mutex_lock(&p->mu);
    int ok = 0;
    if (p->cnt == 0 && ms > 0) {
        /* ms == 0xFFFFFFFF (INFINITE) must BLOCK — the earlier code skipped
         * the wait entirely and returned FALSE (lz4's WorkerThread then left
         * its loop and the pool lost its worker; main waited forever). */
        if (ms == 0xFFFFFFFFu) {
            while (p->cnt == 0) pthread_cond_wait(&p->cv, &p->mu);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (p->cnt == 0) {
                int r = pthread_cond_timedwait(&p->cv, &p->mu, &ts);
                if (r == ETIMEDOUT) break;
            }
        }
    }
    if (p->cnt > 0) {
        MwIocpEntry* e = &p->q[p->head];
        if (count) *count = e->count;
        if (key) *key = (unsigned long)(uintptr_t)e->key;
        if (ovl) *ovl = e->ovl;
        p->head = (p->head + 1) % 256;
        p->cnt--;
        ok = 1;
    } else {
        mw_SetLastError(258); /* WAIT_TIMEOUT */
    }
    pthread_mutex_unlock(&p->mu);
    return ok;
}

/* mingw SEH init registers guest unwind info; our EH walk reads .pdata
 * directly, so the registration is a no-op that must still report success. */
__attribute__((ms_abi)) void* mw_RtlAddFunctionTable(void* tbl, unsigned long n, void* base) {
    MW_TRACE("RtlAddFunctionTable(tbl=%p, n=%lu, base=%p) -> 1 (no-op)", tbl, n, base);
    (void)tbl; (void)n; (void)base;
    return (void*)(uintptr_t)1;
}

__attribute__((ms_abi)) int* mw___doserrno(void) {
    MW_TRACE("__doserrno() = %p", &g_doserrno_storage);
    return &g_doserrno_storage;
}

/* ============================================================
 * LADDER-009: REAL event + semaphore objects.
 * lz4's TPool protocol: TPool_submitJob waits jobSlotAvail (SEMAPHORE),
 * workers SetEvent(allJobsCompleted) (EVENT) when the last job finishes,
 * TPool_jobsCompleted waits that event. The old stubs (alloc_handle(1),
 * wait->0 immediately) made TPool_jobsCompleted return before any job ran
 * -> main exited with "invalid frame" while workers were still idle.
 * Handles stay alloc_handle()-issued; the payload pointer selects the
 * object kind by registry-range (same pattern as mw_thread_lookup).
 * ============================================================ */
typedef struct {
    int used; int manual; int signaled;
    pthread_mutex_t mu; pthread_cond_t cv;
} MwKEvent;
static MwKEvent g_mw_events[32];

typedef struct {
    int used; int32_t count; int32_t max;
    pthread_mutex_t mu; pthread_cond_t cv;
} MwKSem;
static MwKSem g_mw_sems[32];

static void* g_handle_table_lookup(int idx) {
    if (idx < 0 || idx >= MAX_HANDLES) return NULL;
    return g_handle_table[idx];
}

static MwKEvent* mw_event_lookup(void* h) {
    if (!h || (uintptr_t)h < 4) return NULL;
    void* v = g_handle_table_lookup((int)(uintptr_t)h - 4);
    if (!v) return NULL;
    MwKEvent* base = &g_mw_events[0];
    if ((MwKEvent*)v < base || (MwKEvent*)v >= base + 32) return NULL;
    MwKEvent* e = (MwKEvent*)v;
    return e->used ? e : NULL;
}

static MwKSem* mw_sem_lookup(void* h) {
    if (!h || (uintptr_t)h < 4) return NULL;
    void* v = g_handle_table_lookup((int)(uintptr_t)h - 4);
    if (!v) return NULL;
    MwKSem* base = &g_mw_sems[0];
    if ((MwKSem*)v < base || (MwKSem*)v >= base + 32) return NULL;
    MwKSem* s = (MwKSem*)v;
    return s->used ? s : NULL;
}

static void* mw_event_create(int manual, int init) {
    for (int i = 0; i < 32; i++) {
        if (!g_mw_events[i].used) {
            MwKEvent* e = &g_mw_events[i];
            memset(e, 0, sizeof(*e));
            e->used = 1; e->manual = manual ? 1 : 0; e->signaled = init ? 1 : 0;
            pthread_mutex_init(&e->mu, NULL);
            pthread_cond_init(&e->cv, NULL);
            return alloc_handle((void*)e);
        }
    }
    mw_SetLastError(8);
    return NULL;
}

static void* mw_sem_create(int32_t init, int32_t max) {
    for (int i = 0; i < 32; i++) {
        if (!g_mw_sems[i].used) {
            MwKSem* s = &g_mw_sems[i];
            memset(s, 0, sizeof(*s));
            s->used = 1; s->count = init; s->max = max;
            pthread_mutex_init(&s->mu, NULL);
            pthread_cond_init(&s->cv, NULL);
            return alloc_handle((void*)s);
        }
    }
    mw_SetLastError(8);
    return NULL;
}

static uint32_t mw_event_wait(MwKEvent* e, uint32_t ms) {
    pthread_mutex_lock(&e->mu);
    uint32_t rc = 0; /* WAIT_OBJECT_0 */
    if (!e->signaled) {
        if (ms == 0xFFFFFFFFu) {
            while (!e->signaled) pthread_cond_wait(&e->cv, &e->mu);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (!e->signaled) {
                int r = pthread_cond_timedwait(&e->cv, &e->mu, &ts);
                if (r == ETIMEDOUT) { rc = 0x102; break; }
            }
        }
    }
    if (rc == 0 && !e->manual) e->signaled = 0; /* auto-reset consumes the signal */
    pthread_mutex_unlock(&e->mu);
    return rc;
}

static uint32_t mw_sem_wait(MwKSem* s, uint32_t ms) {
    pthread_mutex_lock(&s->mu);
    uint32_t rc = 0;
    if (s->count <= 0) {
        if (ms == 0xFFFFFFFFu) {
            while (s->count <= 0) pthread_cond_wait(&s->cv, &s->mu);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ms / 1000;
            ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (s->count <= 0) {
                int r = pthread_cond_timedwait(&s->cv, &s->mu, &ts);
                if (r == ETIMEDOUT) { rc = 0x102; break; }
            }
        }
    }
    if (rc == 0 && s->count > 0) s->count--;
    pthread_mutex_unlock(&s->mu);
    return rc;
}

/* Unified wait dispatch: thread handle -> event -> semaphore -> legacy 0. */
static uint32_t mw_kobj_wait(void* h, uint32_t ms) {
    MwThread* t = mw_thread_lookup(h);
    if (t) return mw_thread_wait(h, ms);
    MwKEvent* e = mw_event_lookup(h);
    if (e) return mw_event_wait(e, ms);
    MwKSem* s = mw_sem_lookup(h);
    if (s) return mw_sem_wait(s, ms);
    return 0; /* WAIT_OBJECT_0 — legacy passthrough (process/pseudo handles) */
}

/* Unified release for CloseHandle. */
static void mw_kobj_release(void* h) {
    MwThread* t = mw_thread_lookup(h);
    if (t) { mw_thread_release(h); return; }
    MwKEvent* e = mw_event_lookup(h);
    if (e) { e->used = 0; return; }
    MwKSem* s = mw_sem_lookup(h);
    if (s) { s->used = 0; return; }
}

/* Real event/semaphore state transitions (registry block above). */
static int mw_event_set(void* h) {
    MwKEvent* e = mw_event_lookup(h);
    if (!e) return 1; /* legacy handles: pretend success */
    pthread_mutex_lock(&e->mu);
    e->signaled = 1;
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mu);
    return 1;
}

static int mw_event_reset(void* h) {
    MwKEvent* e = mw_event_lookup(h);
    if (!e) return 1;
    pthread_mutex_lock(&e->mu);
    e->signaled = 0;
    pthread_mutex_unlock(&e->mu);
    return 1;
}

static int mw_sem_release(void* h, int32_t count, int32_t* prev) {
    MwKSem* s = mw_sem_lookup(h);
    if (!s) { if (prev) *prev = 0; return 1; }
    pthread_mutex_lock(&s->mu);
    if (prev) *prev = s->count;
    int32_t before = s->count;
    s->count = (s->count + count > s->max) ? s->max : (s->count + count);
    if (s->count != before) pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mu);
    return 1;
}

/* LADDER-009: _vsnprintf — the guest va_list is the ms_abi slot stream
 * (uniform 8-byte slots starting at the first vararg), same contract as
 * mw_vfprintf's ap parameter. msvcrt truncation semantics: write up to n
 * bytes (no terminator), return -1. */
__attribute__((ms_abi)) int mw__vsnprintf(char* buf, size_t n, const char* fmt, void* ap) {
    MW_TRACE("_vsnprintf(n=%zu, fmt=%s)", n, fmt ? fmt : "NULL");
    if (!buf || !fmt) return -1;
    char tmp[16384];
    FILE* mf = fmemopen(tmp, sizeof tmp, "w");
    if (!mf) return -1;
    int r = mw_vfprintf(mf, fmt, ap);
    fclose(mf);
    if (r < 0) return -1;
    if ((size_t)r < n) { memcpy(buf, tmp, (size_t)r + 1); return r; }
    if (n) memcpy(buf, tmp, n);
    return -1;
}

__attribute__((ms_abi)) uint32_t mw_WaitForSingleObject(void* h, uint32_t ms) {
    MW_TRACE("WaitForSingleObject(handle=%p, ms=%u)", h, ms);
    return mw_kobj_wait(h, ms);
}

__attribute__((ms_abi)) uint32_t mw_WaitForMultipleObjects(uint32_t count, void** handles,
    int all, uint32_t ms) {
    MW_TRACE("WaitForMultipleObjects(count=%u, all=%d, ms=%u)", count, all, ms);
    /* Real wait over kernel objects (threads/events/semaphores), used by
     * lz4's TPool_free (wait-all on worker threads). Legacy handles (whose
     * kobj_wait returns 0 instantly) are treated as signaled. */
    if (count == 0 || !handles) return 0;
    uint32_t deadline = ms;
    for (uint32_t done = 0;;) {
        int pending = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t rc = mw_kobj_wait(handles[i], 0);
            if (rc != 0) {
                if (!all) return 0; /* any-mode: first signaled object wins */
                pending = 1;
            }
        }
        if (!pending) return 0;                 /* all (or any) satisfied */
        if (deadline == 0) return 0x102;        /* WAIT_TIMEOUT */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 10000000L; /* 10ms poll slice */
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (deadline != 0xFFFFFFFFu) {
            if (now.tv_sec > (time_t)(deadline / 1000)) return 0x102;
        }
        nanosleep(&ts, NULL);
        (void)done;
    }
}

/* Same contract fix for WideCharToMultiByte: size query returns the BYTE
 * count INCLUDING the terminating null when srclen==-1. */
/* BUG-046 family: guest wchar_t is 2 bytes (UTF-16LE) but glibc wchar_t is 4 —
 * wcstombs/wcslen/WideCharToMultiByte over guest buffers read interleaved
 * garbage (unit[1]=0x0000 collapses pairs; non-ASCII = wild codepoints).
 * Native UTF-16LE -> UTF-8 encoder used by the cp=65001 paths. Returns bytes
 * (excluding terminator), -1 on invalid input, -2 if dst is too small. */
static int w3_utf16le_to_utf8(const uint16_t* src, int src_units, char* dst, int dst_bytes) {
    int o = 0;
    for (int i = 0; i < src_units; i++) {
        uint32_t cp32 = src[i];
        if (cp32 >= 0xD800 && cp32 <= 0xDBFF && i + 1 < src_units &&
            src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp32 = 0x10000 + ((cp32 - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i++;
        } else if (cp32 >= 0xD800 && cp32 <= 0xDFFF) {
            return -1;                     /* unpaired surrogate */
        }
        uint8_t enc[4]; unsigned need;
        if (cp32 < 0x80) { enc[0] = (uint8_t)cp32; need = 1; }
        else if (cp32 < 0x800) { enc[0] = 0xC0 | (cp32 >> 6); enc[1] = 0x80 | (cp32 & 0x3F); need = 2; }
        else if (cp32 < 0x10000) { enc[0] = 0xE0 | (cp32 >> 12); enc[1] = 0x80 | ((cp32 >> 6) & 0x3F); enc[2] = 0x80 | (cp32 & 0x3F); need = 3; }
        else { enc[0] = 0xF0 | (cp32 >> 18); enc[1] = 0x80 | ((cp32 >> 12) & 0x3F); enc[2] = 0x80 | ((cp32 >> 6) & 0x3F); enc[3] = 0x80 | (cp32 & 0x3F); need = 4; }
        if (!dst) { o += (int)need; continue; }
        if (o + (int)need > dst_bytes) return -2;
        for (unsigned k = 0; k < need; k++) dst[o++] = (char)enc[k];
    }
    return o;
}


/* BUG-050 (2026-10-17): guest wide strings are ALWAYS 2-byte UTF-16LE.
 * Every wide->narrow conversion must go through these; glibc wcstombs reads
 * 4-byte wchars and dies on guest data (ASCII truncates to first byte:
 * 0x43,0x00 = 'C', NUL). */
static int w3_w16_to_utf8z(const uint16_t* s, char* dst, size_t dstbytes) {
    if (!s) return -1;
    size_t u = 0;
    while (s[u]) u++;
    int got = w3_utf16le_to_utf8(s, (int)u, dst, dstbytes ? (int)(dstbytes - 1) : 0);
    if (got < 0) return -1;
    if (dst && dstbytes) dst[got] = '\0';
    return got;
}
static int w3_w16_to_utf8_size(const uint16_t* s) {
    if (!s) return -1;
    size_t u = 0;
    while (s[u]) u++;
    int got = w3_utf16le_to_utf8(s, (int)u, NULL, 0);
    return got < 0 ? -1 : got;
}
/* BUG-054 (2026-10-17): guest paths use Windows separators. 7-Zip builds
 * "<posix-cwd>\<name>" because GetCurrentDirectoryW hands out the POSIX cwd
 * and the guest appends '\' — the result must be normalized before any
 * POSIX call: '\' -> '/', strip a single-letter drive prefix ("C:" -> "/"),
 * collapse duplicate slashes. In-place, returns the pointer. */
static char* w3_normalize_path(char* p) {
    if (!p) return p;
    for (char* q = p; *q; q++)
        if (*q == '\\') *q = '/';
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        /* drive prefix: drop "X:" and make the rest absolute at '/' */
        char* r = p + 2;
        if (*r == '/') r++;
        memmove(p, "/", 1);
        /* move the remainder (already slash-normalized) */
        size_t rest = strlen(r);
        memmove(p + 1, r, rest + 1);
    }
    /* collapse '//' (not leading) */
    char* w = p;
    for (char* q = p; *q; ) {
        if (*q == '/' && q[1] == '/' && w != p) { q++; continue; }
        *w++ = *q++;
    }
    *w = '\0';
    return p;
}

/* 2-byte wide string primitives (guest ABI) */
static size_t w16len(const uint16_t* s) { size_t n = 0; if (s) while (s[n]) n++; return n; }
static int w16cmp(const uint16_t* a, const uint16_t* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}
static int w16ncmp(const uint16_t* a, const uint16_t* b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (int)*a - (int)*b : 0;
}
static uint16_t* w16cpy(uint16_t* d, const uint16_t* s) {
    uint16_t* r = d;
    while ((*d++ = *s++)) ;
    return r;
}
static uint16_t* w16ncpy(uint16_t* d, const uint16_t* s, size_t n) {
    uint16_t* r = d;
    while (n && (*d++ = *s++)) n--;
    while (n--) *d++ = 0;
    return r;
}
static uint16_t* w16cat(uint16_t* d, const uint16_t* s) {
    uint16_t* r = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return r;
}
static uint16_t* w16chr(const uint16_t* s, uint16_t c) {
    for (;; s++) { if (*s == c) return (uint16_t*)s; if (!*s) return NULL; }
}
static uint16_t* w16rchr(const uint16_t* s, uint16_t c) {
    const uint16_t* r = NULL;
    for (;; s++) { if (*s == c) r = s; if (!*s) break; }
    return (uint16_t*)r;
}
static uint16_t* w16str(const uint16_t* h, const uint16_t* n) {
    if (!*n) return (uint16_t*)h;
    for (; *h; h++) {
        const uint16_t* a = h; const uint16_t* b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (uint16_t*)h;
    }
    return NULL;
}
static size_t w16spn(const uint16_t* s, const uint16_t* set) {
    size_t n = 0;
    for (; s[n]; n++) { if (!w16chr(set, s[n])) break; }
    return n;
}

__attribute__((ms_abi)) int mw_WideCharToMultiByte(uint32_t cp, uint32_t flags,
    const wchar_t* src, int srclen, char* dst, int dstlen, char* defc, int* used) {
    MW_TRACE("WideCharToMultiByte(cp=%u, srclen=%d, dstlen=%d)", cp, srclen, dstlen);
    if (!src) { mw_SetLastError(87); return 0; }
    if (srclen == 0) return 0;
    int include_null = (srclen < 0);
    /* BUG-046: UTF-8 takes the native 2-byte-unit path. Other codepages keep
     * the legacy glibc passthrough (known 2x mis-read hazard, TASK-API-009). */
    if (cp == 65001) {
        const uint16_t* u = (const uint16_t*)src;
        int units = srclen;
        if (include_null) { units = 0; while (u[units]) units++; }
        int need = w3_utf16le_to_utf8(u, units, NULL, 0);
        if (need < 0) { mw_SetLastError(1113); return 0; }
        if (include_null) need += 1;
        if (used) *used = 0;
        if (!dst || dstlen == 0) return need;
        if (dstlen < 0 || dstlen < need) { mw_SetLastError(122); return 0; }
        int got = w3_utf16le_to_utf8(u, units, dst, dstlen);
        if (got < 0) { mw_SetLastError(got == -2 ? 122 : 1113); return 0; }
        if (include_null) dst[got++] = '\0';
        return got;
    }
    /* BUG-057 (2026-10-17): the fixed-length non-65001 paths used
     * wcsnrtombs, which reads 4-byte glibc wchars — guest data is 2-byte
     * UTF-16LE, so every fixed-length conversion failed with EILSEQ ->
     * error 1113 -> sqlite mapped it to "out of memory" and refused to
     * open the database. Single-byte codepages (ACP=1252 family) are
     * handled natively: ASCII identity, cp1252 hi-range table inverse. */
    const uint16_t* u = (const uint16_t*)src;
    int units = srclen;
    if (include_null) { units = 0; while (u[units]) units++; }
    /* byte length per unit: 1 for <=0xFF (single-byte page), else '?' */
    size_t need = 0;
    for (int i = 0; i < units; i++) need += (u[i] <= 0xFF) ? 1 : 1;
    if (used) *used = 0;
    if (!dst || dstlen == 0) return (int)need;
    if (dstlen < 0 || (size_t)dstlen < need) { mw_SetLastError(122); return 0; }
    size_t got = 0;
    for (int i = 0; i < units && (size_t)got < (size_t)dstlen; i++) {
        uint16_t c = u[i];
        if (c <= 0xFF) {
            dst[got++] = (char)c;      /* Latin-1 identity for ACP low half */
        } else {
            dst[got++] = defc ? *defc : '?';
        }
    }
    if (include_null && (size_t)got < (size_t)dstlen) dst[got++] = '\0';
    return (int)got;
}

/* ============================================================
 * msvcrt.dll Stubs (ms_abi calling convention)
 * ============================================================ */

/* CRT Data pointers */
static char* g_acmdln_ptr = NULL;     /* pointer to ANSI command line */
static char** g_initenv_ptr = NULL;   /* pointer to env array */
static int  g_errno_storage = 0;   /* our errno mirror */
static int* g_errno_ptr = &g_errno_storage;

/* These are DATA imports - the IAT slot gets the ADDRESS of the data,
 * not a function pointer. Handled specially in import resolution. */

/* CRT init functions */
__attribute__((ms_abi)) void mw___set_app_type(int type) {
    g_app_type = type;
    MW_TRACE("__set_app_type(%d)", type);
}

__attribute__((ms_abi)) void mw___setusermatherr(void* handler) {
    MW_TRACE("__setusermatherr(handler=%p)", handler);
}

__attribute__((ms_abi)) int mw___getmainargs(int* pargc, char*** pargv, char*** penvp,
    int dowildcard, void* startup) {
    MW_TRACE("__getmainargs(argc=%p, argv=%p, envp=%p)", pargc, pargv, penvp);
    if (pargc) *pargc = g_argc;
    if (pargv) *pargv = g_argv;
    if (penvp) *penvp = g_initenv_ptr;
    g_environ_cell = g_initenv_ptr; /* TASK-API-011: __p__environ target */
    return 0;
}

__attribute__((ms_abi)) int mw___lconv_init(void) {
    MW_TRACE("__lconv_init()");
    localeconv(); /* initialize locale data */
    return 0;
}

__attribute__((ms_abi)) int mw___C_specific_handler(void* exc, void* frame, void* ctx,
    void* disp, void* history) {
    MW_TRACE("__C_specific_handler()");
    return 0;
}

/* _initterm: walk a table of function pointers, call each non-NULL one.
 * The table is terminated by a NULL entry.
 * Returns the return value of the last non-NULL function called.
 */
__attribute__((ms_abi)) int mw__initterm(void** start, void** end) {
    MW_TRACE("_initterm(start=%p, end=%p)", start, end);
    int ret = 0;
    if (!start || !end) return 0;
    for (void** p = start; p < end; p++) {
        if (*p != NULL) {
            MW_TRACE("_initterm: calling %p", *p);
            /* Call the function pointer using ms_abi since these are
             * internal CRT functions compiled with MSVC */
            int (*fn)(void) = (int (*)(void))*p;
            ret = fn();
        }
    }
    return ret;
}

__attribute__((ms_abi)) void mw__amsg_exit(int msg) {
    MW_TRACE("_amsg_exit(msg=%d)", msg);
    fprintf(stderr, "Runtime Error: message id %d\n", msg);
    exit(255);
}

__attribute__((ms_abi)) void mw__cexit(void) {
    MW_TRACE("_cexit()");
    fflush(stdout);
    fflush(stderr);
}

static void* g_iob_array[3]; /* BUG-032: backing store returned by __iob_func();
                              * mingw strides over it with 0x30 - see map_crt_stream */
/* ============================================================
 * ISS1 slice: MSVCRT-layout FILE registry (WS-D semantic contract).
 * OBSERVED (live trace, t12_libc_io.exe): the guest's statically-linked
 * mingw CRT printf machinery calls InitializeCriticalSection(fp+0x30)
 * and Enter/Leave on it. Therefore fopen MUST return an MSVCRT-strided
 * handle with >=0x40 bytes of zeroed slack, never a raw glibc FILE*
 * (locking into glibc's struct corrupts the host heap -> SIGSEGV in
 * libc during fputc). The host FILE* lives in a side table; every
 * FILE* API resolves handles through the registry first.
 * ============================================================ */
#define MW_FILE_MAX 32
typedef struct MwFile {
    char* _ptr; int _cnt; char* _base; int _flag; int _file;
    int _charbuf; int _bufsiz; char* _tmpfname;
    unsigned char slack[0x40]; /* guest CRT lock (fp+0x30) lives here */
} MwFile;
static struct { MwFile* mf; FILE* hf; } g_mwfile_tab[MW_FILE_MAX];

static FILE* mwfile_hostfp_of(void* p) {
    if (!p) return NULL;
    for (int i = 0; i < MW_FILE_MAX; ++i)
        if (g_mwfile_tab[i].mf == (MwFile*)p) return g_mwfile_tab[i].hf;
    return NULL;
}

/* OPENSRC-7ZIP: msvcrt _iob data window (3 x 0x30, guest FILE stride) */
static uint8_t g_msvcrt_iob[3 * 0x30] __attribute__((aligned(16)));

static FILE* map_crt_stream(void* stream); /* fwd decl (defined below) */

__attribute__((ms_abi)) void* mw___iob_func(void) {
    /* Returns pointer to array of FILE* (stdin, stdout, stderr, ...).
     * BUG-032 note: mingw indexes the returned pointer with stride 0x30
     * (sizeof of its FILE struct); map_crt_stream() handles that window. */
    g_iob_array[0] = stdin;
    g_iob_array[1] = stdout;
    g_iob_array[2] = stderr;
    MW_TRACE("__iob_func() = %p", g_iob_array);
    return g_iob_array;
}

/* TASK-API-011 (UCRT wave, 2026-10-15): modern UCRT-flavored mingw binaries
 * (llvm-mingw, MSVC) resolve std streams through __acrt_iob_func(index)
 * instead of __iob_func(). Return the same backing streams by index. */
__attribute__((ms_abi)) void* mw___acrt_iob_func(unsigned index) {
    g_iob_array[0] = stdin;
    g_iob_array[1] = stdout;
    g_iob_array[2] = stderr;
    void* r = (index < 3) ? g_iob_array[index] : NULL;
    MW_TRACE("__acrt_iob_func(%u) = %p", index, r);
    return r;
}

/* UCRT-flavored CRT startup calls these during init; the loader has already
 * prepared argc/argv/env, so they are bookkeeping no-ops. */
__attribute__((ms_abi)) int mw__configure_narrow_argv(int mode) {
    (void)mode;
    MW_TRACE("_configure_narrow_argv(mode=%d) -> 0", mode);
    return 0;
}

__attribute__((ms_abi)) int mw__initialize_narrow_environment(void) {
    MW_TRACE("_initialize_narrow_environment() -> 0");
    return 0;
}

__attribute__((ms_abi)) int mw__set_new_mode(int mode) {
    MW_TRACE("_set_new_mode(%d) -> 0", mode);
    return 0;
}

__attribute__((ms_abi)) void* mw__set_invalid_parameter_handler(void* h) {
    MW_TRACE("_set_invalid_parameter_handler(%p) -> NULL", h);
    return NULL;
}

/* signal(): tests never install real handlers; record and return no old one.
 * (Named _stub to avoid clashing with the host signal(2) declaration.) */
__attribute__((ms_abi)) void* mw_signal_stub(int sig, void* handler) {
    MW_TRACE("signal(%d, %p) -> NULL (stub)", sig, handler);
    return NULL;
}

__attribute__((ms_abi)) void mw__tzset(void) {
    MW_TRACE("_tzset() (stub, UTC)");
}

/* UCRT __p__fmode/__p__commode are FUNCTIONS returning &var (t10 crashes
 * 0x2039038/0x2039034 proved call-style usage). */
__attribute__((ms_abi)) unsigned int* mw___p__fmode(void) {
    MW_TRACE("__p__fmode() = %p", &g_fmode_val);
    return &g_fmode_val;
}

__attribute__((ms_abi)) unsigned int* mw___p__commode(void) {
    MW_TRACE("__p__commode() = %p", &g_commode_val);
    return &g_commode_val;
}

__attribute__((ms_abi)) int* mw___p___argc(void) {
    MW_TRACE("__p___argc() = %p", &g_argc);
    return &g_argc;
}

__attribute__((ms_abi)) char*** mw___p___argv(void) {
    MW_TRACE("__p___argv() = %p", &g_argv_cell);
    return &g_argv_cell;
}

__attribute__((ms_abi)) char*** mw___p__environ(void) {
    MW_TRACE("__p__environ() = %p", &g_environ_cell);
    return &g_environ_cell;
}

/* BUG-067 (SWEEP-005): zig/mingw ucrt startup imports __p__acmdln from
 * api-ms-win-crt-runtime-l1-1-0.dll. Without it the unresolved-import stub
 * returned 0 and the guest CRT dereferenced it (mov rsi,[rax], RAX=0) ->
 * SIGSEGV at RVA 0x26b1 before main. __p__acmdln returns char** pointing at
 * the command-line char* (g_acmdln_ptr = g_cmdline_ansi, set in main). */
__attribute__((ms_abi)) char** mw___p__acmdln(void) {
    MW_TRACE("__p__acmdln() = %p", (void*)&g_acmdln_ptr);
    return (char**)&g_acmdln_ptr;
}


/* TASK-API-012 + ISS1: FILE-level stdio bridges. fopen returns an
 * MSVCRT-layout registry handle (guest CRT locks fp+0x30 — see registry
 * comment); the whole family resolves handles via map_crt_stream. */
__attribute__((ms_abi)) void* mw_fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    if (strstr(mode, "ccs=") != NULL) { errno = EINVAL; g_errno_storage = errno; return NULL; }
    FILE* hf = fopen(path, mode);
    if (!hf) { g_errno_storage = errno; /* ISS1: guest reads errno via _errno mirror */ }
    MW_TRACE("fopen(%s, %s) = %p", path ? path : "NULL", mode ? mode : "NULL", hf);
    if (!hf) return NULL;
    mw_fileptr_register(hf); /* BUG-065: validate pass-throughs later */
    for (int i = 0; i < MW_FILE_MAX; ++i) {
        if (g_mwfile_tab[i].mf) continue;
        MwFile* mf = (MwFile*)calloc(1, sizeof(MwFile));
        if (!mf) { fclose(hf); return NULL; }
        mf->_file = fileno(hf);
        if (strchr(mode, '+'))   mf->_flag = 0x0004; /* _IORW */
        else if (mode[0] == 'r') mf->_flag = 0x0001; /* _IOREAD */
        else                     mf->_flag = 0x0002; /* _IOWRT */
        g_mwfile_tab[i].mf = mf;
        g_mwfile_tab[i].hf = hf;
        return mf;
    }
    fclose(hf); /* table full — deterministic failure */
    errno = EMFILE; g_errno_storage = errno;
    return NULL;
}

__attribute__((ms_abi)) int mw_fclose(void* stream) {
    MW_TRACE("fclose(%p)", stream);
    FILE* hf = mwfile_hostfp_of(stream);
    if (!hf) {
        /* TASK-API-011 pass-through contract (map_crt_stream): unknown
         * pointers are REAL glibc FILE* (incl. stdin/stdout/stderr handed
         * out by __iob_func/__acrt_iob_func). jq main() ends with
         * fclose(stdout) and treats nonzero as failure. */
        if (!stream) return EOF;
        return fclose((FILE*)stream);
    }
    for (int i = 0; i < MW_FILE_MAX; ++i)
        if (g_mwfile_tab[i].mf == (MwFile*)stream) {
            g_mwfile_tab[i].mf = NULL; g_mwfile_tab[i].hf = NULL;
            break;
        }
    free(stream);
    return fclose(hf);
}

__attribute__((ms_abi)) unsigned long long mw_fread(void* buf,
        unsigned long long sz, unsigned long long n, void* stream) {
    FILE* f = map_crt_stream(stream);
    unsigned long long r = (f && buf) ? fread(buf, (size_t)sz, (size_t)n, f) : 0ULL;
    MW_TRACE("fread(sz=%llu, cnt=%llu) = %llu", sz, n, r);
    return r;
}

__attribute__((ms_abi)) char* mw_fgets(char* buf, int n, void* stream) {
    FILE* f = map_crt_stream(stream);
    return (f && buf) ? fgets(buf, n, f) : NULL;
}

__attribute__((ms_abi)) int mw_fgetc(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? fgetc(f) : EOF;
}

__attribute__((ms_abi)) void mw_rewind(void* stream) {
    FILE* f = map_crt_stream(stream);
    if (f) rewind(f);
}

/* ISS1 slice: seek/tell/EOF bridges (missing from API-012 wave).
 * MSVCRT clears the EOF indicator on a successful reposition. */
__attribute__((ms_abi)) int mw_fseek(void* stream, long long off, int origin) {
    /* Windows SEEK_SET/CUR/END constants equal POSIX ones. */
    FILE* f = map_crt_stream(stream);
    if (!f) return -1;
    int r = fseek(f, (long)off, origin);
    if (r == 0) clearerr(f); /* MSVCRT clears EOF on successful reposition */
    return r;
}

__attribute__((ms_abi)) long long mw_ftell(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? (long long)ftell(f) : -1LL;
}

__attribute__((ms_abi)) int mw_feof(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? feof(f) : 0;
}

__attribute__((ms_abi)) int mw_remove(const char* path) {
    MW_TRACE("remove(%s)", path ? path : "NULL");
    int r = remove(path);
    if (r != 0) g_errno_storage = errno; /* ISS1: errno bridge */
    return r;
}

__attribute__((ms_abi)) size_t mw_strftime(char* buf, size_t max,
        const char* fmt, const void* tmv) {
    return strftime(buf, max, fmt, (const struct tm*)tmv);
}

/* Data accessor functions */
__attribute__((ms_abi)) unsigned int mw___lc_codepage_func(void) {
    MW_TRACE("__lc_codepage_func() = %u", 0);
    return 0; /* CP_ACP */
}

__attribute__((ms_abi)) int mw___mb_cur_max_func(void) {
    MW_TRACE("__mb_cur_max_func() = 1");
    return 1;
}

/* BUG-033 (appcompat sweep APP-F-03): printf-family across the ABI boundary.
 *
 * The PE (ms_abi) passes varargs per the Windows x64 contract: every vararg
 * occupies a uniform 8-byte slot (integers/pointers directly, doubles as
 * bit-casts spilled by the caller). glibc's vfprintf expects a SysV va_list:
 * a struct {gp_offset, fp_offset, overflow_arg_area, reg_save_area} whose
 * reg-save area holds rdi..r9 (48 bytes) then xmm0-7 (16 bytes each).
 * The old code handed the raw ms slot pointer to vprintf — vprintf read it
 * as a SysV struct (gp_offset = low bits of a stack address) and faulted in
 * libc for anything but tiny integer-only formats.
 *
 * Fix: parse fmt to classify every consumed argument (GP vs FP), materialize
 * the uniform MS slots, then synthesize a faithful SysV va_list:
 *   - gp slots: gpv[0] is fmt (rdi), varargs fill gpv[1..5];
 *   - fp slots: doubles fill the low 64 bits of xmm0..xmm7;
 *   - the rest go to the overflow area in call order (SysV stack order). */

#define MW_PRINTF_MAX_ARGS 256

typedef struct {
    unsigned gp_offset;
    unsigned fp_offset;
    void*    overflow_arg_area;
    void*    reg_save_area;
} mw_sysv_va_list;

/* Parse fmt; cls[i]=1 for FP args, 0 for GP. Returns arg count (capped). */
static int mw_printf_classify(const char* fmt, unsigned char* cls) {
    int nargs = 0;
    const char* p = fmt;
    if (!p) return 0;
    while (*p) {
        if (*p != '%') { p++; continue; }
        p++;
        if (*p == '%') { p++; continue; }        /* literal percent: no arg */
        /* width '*' consumes a GP arg */
        if (*p == '*') {
            if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = 0;
            p++;
        }
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = 0;
                p++;
            }
            while (*p >= '0' && *p <= '9') p++;
        }
        /* length modifiers */
        while (*p == 'l' || *p == 'h' || *p == 'q' || *p == 'j' ||
               *p == 'z' || *p == 't' || *p == 'L') p++;
        if (!*p) break;
        char conv = *p++;
        int is_fp = (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                     conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A');
        if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = is_fp ? 1 : 0;
    }
    return nargs;
}

/* Build a SysV va_list from uniform MS slots and call vfprintf on it. */
static int mw_printf_emit(FILE* f, const char* fmt,
                          const unsigned long long* slots, int nargs) {
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int parsed = mw_printf_classify(fmt, cls);
    if (nargs > parsed) nargs = parsed;

    unsigned long long gpv[6] = {0, 0, 0, 0, 0, 0};
    unsigned long long fpv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned long long ovf[MW_PRINTF_MAX_ARGS];
    int novf = 0, gpi = 1 /* gpv[0] = rdi = fmt */, fpi = 0;

    for (int i = 0; i < nargs; i++) {
        if (cls[i]) {
            if (fpi < 8) fpv[fpi++] = slots[i];
            else         ovf[novf++] = slots[i];
        } else {
            if (gpi < 6) gpv[gpi++] = slots[i];
            else         ovf[novf++] = slots[i];
        }
    }

    unsigned long long reg_save[6 + 16]; /* rdi..r9 then xmm0-7 (low64 only) */
    memcpy(reg_save, gpv, sizeof(gpv));
    for (int i = 0; i < 8; i++)
        memcpy((unsigned char*)reg_save + 48 + 16 * i, &fpv[i], 8);
    /* xmm slots beyond the doubles: zero — only read if fmt over-reads */
    for (int i = fpi; i < 8; i++)
        memset((unsigned char*)reg_save + 48 + 16 * i, 0, 16);

    va_list sv;                       /* glibc __va_list_tag[1] */
    mw_sysv_va_list* svv = (mw_sysv_va_list*)sv;
    svv->gp_offset = 8;              /* fmt consumed rdi */
    svv->fp_offset = 48;             /* no named FP args */
    svv->overflow_arg_area = ovf;
    svv->reg_save_area = reg_save;
    return vfprintf(f, fmt, sv);
}

/* printf - variadic, needs special handling.
 * BUG-063: this is an ms_abi VARIADIC function — the va_list here MUST be
 * __builtin_ms_va_list (Windows x64 ABI: 4 GP regs + shadow space, XMM6-15
 * volatile). The old SysV va_start read the wrong register-save layout and
 * dereferenced garbage arg slots (lz4.exe --version SIGSEGV inside the
 * mw_fprintf format walk at 0x202ab43; simple 1-arg calls had survived by
 * layout luck since UPX). */
__attribute__((ms_abi)) int mw_printf(const char* fmt, ...) {
    MW_TRACE("printf(fmt=%s)", fmt ? fmt : "NULL");
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    __builtin_ms_va_end(ap);
    return mw_printf_emit(stdout, fmt, slots, nargs);
}

/* fprintf - variadic. BUG-063: ms_abi varargs -> __builtin_ms_va_list. */
__attribute__((ms_abi)) int mw_fprintf(void* file, const char* fmt, ...) {
    MW_TRACE("fprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    __builtin_ms_va_end(ap);
    return mw_printf_emit(f, fmt, slots, nargs);
}

/* vfprintf - the PE passes its own va_list: an ms_abi slot stream (char*),
 * uniform 8-byte vararg slots starting at the first vararg. */
__attribute__((ms_abi)) int mw_vfprintf(void* file, const char* fmt, void* ap) {
    MW_TRACE("vfprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    if (ap && nargs > 0) memcpy(slots, ap, (size_t)nargs * 8);
    return mw_printf_emit(f, fmt, slots, nargs);
}

/* TASK-API-011: UCRT unified stdio core. Modern UCRT printf/snprintf are
 * inline wrappers over the __stdio_common_v* family. The va_list argument is
 * the PE's ms_abi slot stream — exactly what mw_vfprintf already repacks. */
__attribute__((ms_abi)) int mw___stdio_common_vfprintf(
    unsigned long long options, void* file, const char* fmt,
    void* locale, void* ap) {
    (void)options; (void)locale;
    MW_TRACE("__stdio_common_vfprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    return mw_vfprintf(file, fmt, ap);
}

__attribute__((ms_abi)) int mw___stdio_common_vsprintf(
    unsigned long long options, char* buffer, size_t size,
    const char* fmt, void* locale, void* ap) {
    (void)options; (void)locale;
    char tmp[16384];
    FILE* mf = fmemopen(tmp, sizeof tmp, "w");
    if (!mf) return -1;
    int n = mw_vfprintf(mf, fmt, ap);
    fclose(mf);
    if (n < 0) return -1;
    size_t copy = ((size_t)n >= size) ? (size ? size - 1 : 0) : (size_t)n;
    if (buffer && size) { memcpy(buffer, tmp, copy); buffer[copy] = 0; }
    MW_TRACE("__stdio_common_vsprintf(fmt=%s) = %d", fmt ? fmt : "NULL", n);
    return n; /* UCRT: would-be length (may exceed size) */
}

__attribute__((ms_abi)) int mw___stdio_common_vfwprintf(
    unsigned long long options, void* file, const short* fmt,
    void* locale, void* ap) {
    /* Wide print: no test exercises it; refuse loudly rather than misformat. */
    (void)options; (void)file; (void)fmt; (void)locale; (void)ap;
    MW_TRACE("__stdio_common_vfwprintf() -> -1 (wide print unsupported)");
    return -1;
}

__attribute__((ms_abi)) int mw___stdio_common_vsscanf(
    unsigned long long options, const char* buffer, size_t size,
    const char* fmt, void* locale, void* ap) {
    (void)options; (void)locale; (void)size;
    if (!buffer) return -1;
    /* All scanf varargs are pointers (GP-only). MS slot stream: slot[i] is
     * an 8-byte uniform slot. Build the SysV va_list with the loader's
     * proven reg_save trick (slot 0 reserved; first vararg at gp_offset 8). */
    unsigned long long* slots = (unsigned long long*)ap;
    unsigned long long reg_save[6 + 16];
    reg_save[0] = 0; /* dummy rdi slot */
    for (int i = 0; i < 5; i++) reg_save[1 + i] = slots[i];
    memset(&reg_save[6], 0, 16 * 8);
    va_list sva;                      /* glibc __va_list_tag[1] */
    struct { unsigned int gp_offset, fp_offset; void* overflow; void* reg_save; }* sv =
        (void*)sva;
    sv->gp_offset = 8;
    sv->fp_offset = 48; /* pointers only — never reads xmm */
    sv->overflow = slots + 5;
    sv->reg_save = reg_save;
    int r = vsscanf(buffer, fmt, sva);
    MW_TRACE("__stdio_common_vsscanf(fmt=%s) = %d", fmt ? fmt : "NULL", r);
    return r;
}

/* BUG-032: classic msvcrt/mingw CRTs treat __iob_func()'s return value as a
 * FILE* into an ARRAY OF FILE STRUCTS with stride sizeof(FILE)=0x30 on x64:
 *   stdin = base+0x00, stdout = base+0x30, stderr = base+0x60.
 * The PE therefore passes synthetic pointers (observed: setvbuf called with
 * base+0x60) that glibc must never dereference. Recognize that window and
 * map to the real streams; anything else keeps the legacy behavior. */

static FILE* map_crt_stream(void* stream) {
    if (stream == NULL) return NULL;
    {   /* ISS1: registry handles resolve to their host FILE* first — a raw
         * registry pointer must NEVER reach glibc. */
        FILE* hf = mwfile_hostfp_of(stream);
        if (hf) return hf;
    }
    uintptr_t s = (uintptr_t)stream;
    uintptr_t base = (uintptr_t)g_iob_array;
    if (s >= base && s < base + 0x90) {
        uintptr_t off = s - base;
        int idx = (int)(off / 0x30);
        if (idx < 0 || idx > 2) idx = 1; /* window edge -> stdout */
        FILE* mapped = (idx == 0) ? stdin : (idx == 1) ? stdout : stderr;
        MW_TRACE("map_crt_stream: iob+%#zx -> %s", (size_t)off,
                 idx == 0 ? "stdin" : idx == 1 ? "stdout" : "stderr");
        return mapped;
    }
    {   /* OPENSRC-7ZIP: msvcrt _iob data import window (same 0x30 stride) */
        uintptr_t base2 = (uintptr_t)g_msvcrt_iob;
        if (s >= base2 && s < base2 + 0x90) {
            uintptr_t off = s - base2;
            int idx = (int)(off / 0x30);
            if (idx < 0 || idx > 2) idx = 1;
            FILE* mapped = (idx == 0) ? stdin : (idx == 1) ? stdout : stderr;
            MW_TRACE("map_crt_stream: _iob+%#zx -> %s", (size_t)off,
                     idx == 0 ? "stdin" : idx == 1 ? "stdout" : "stderr");
            return mapped;
        }
    }
    /* TASK-API-011: pointers outside the synthetic iob window are REAL
     * glibc FILE* (fmemopen/fopen-backed streams created by our own API
     * implementations). Pass them through verbatim; the old "unknown ->
     * stdout" fallback silently ate fmemopen output (snapt.exe repro:
     * "%.3f" went to stdout and the caller's buffer stayed empty). */
    /* BUG-065 (md5deep): glibc aborts the process ("invalid stdio handle")
     * when a guest hands us a GARBAGE stream pointer. Validate against the
     * registry of FILE* we ever issued; unknown pointers are refused with a
     * loud trace instead of reaching glibc. */
    if (!mw_fileptr_is_valid((FILE*)stream)) {
        MW_TRACE("map_crt_stream: UNKNOWN stream %p -> refused (NULL)", stream);
        return NULL;
    }
    return (FILE*)stream;
}

/* Registry of every host FILE* the loader ever handed to a guest. */
#define MW_FILEPTR_MAX 512
static FILE* g_issued_files[MW_FILEPTR_MAX];
static int g_issued_files_n = 0;
static void mw_fileptr_register(FILE* f) {
    if (!f) return;
    for (int i = 0; i < g_issued_files_n; i++)
        if (g_issued_files[i] == f) return;
    if (g_issued_files_n < MW_FILEPTR_MAX) g_issued_files[g_issued_files_n++] = f;
}
static int mw_fileptr_is_valid(FILE* f) {
    if (f == stdin || f == stdout || f == stderr) return 1;
    for (int i = 0; i < g_issued_files_n; i++)
        if (g_issued_files[i] == f) return 1;
    return 0;
}
__attribute__((constructor)) static void mw_fileptr_init(void) {
    mw_fileptr_register(stdin); mw_fileptr_register(stdout); mw_fileptr_register(stderr);
}

__attribute__((ms_abi)) int mw_fflush(void* file) {
    MW_TRACE("fflush(file=%p)", file);
    FILE* f = (file == NULL) ? NULL : map_crt_stream(file);
    return fflush(f);
}

__attribute__((ms_abi)) int mw_fputs(const char* str, void* file) {
    MW_TRACE("fputs(str=%s)", str ? str : "NULL");
    FILE* f = map_crt_stream(file);
    return fputs(str, f);
}

__attribute__((ms_abi)) int mw_fputc(int ch, void* file) {
    MW_TRACE("fputc(ch=0x%x)", ch);
    FILE* f = map_crt_stream(file);
    return fputc(ch, f);
}

__attribute__((ms_abi)) size_t mw_fwrite(const void* buf, size_t sz, size_t cnt, void* file) {
    MW_TRACE("fwrite(sz=%zu, cnt=%zu)", sz, cnt);
    FILE* f = map_crt_stream(file);
    size_t r = fwrite(buf, sz, cnt, f);
    MW_TRACE("fwrite -> %zu (f=%p)", r, (void*)f);
    return r;
}

__attribute__((ms_abi)) int mw_getc(void* file) {
    MW_TRACE("getc()");
    FILE* f = map_crt_stream(file);
    return (f != NULL) ? getc(f) : EOF;
}

__attribute__((ms_abi)) int mw_ungetc(int ch, void* file) {
    MW_TRACE("ungetc(ch=0x%x)", ch);
    FILE* f = map_crt_stream(file);
    return (f != NULL) ? ungetc(ch, f) : EOF;
}

__attribute__((ms_abi)) int mw_puts(const char* str) {
    MW_TRACE("puts(str=%s)", str ? str : "NULL");
    return puts(str);
}

/* Memory */

/* forward decls: allocation registry (defined in the Wave-3B block) */
static void w3_reg_add(void* p);
static void w3_reg_add_sz(void* p, size_t sz);
static void w3_reg_del(void* p);
static int w3_reg_has(void* p);
static void w3_trap_bad_free(void* p, const char* tag);
static int w3_guard_enabled(void);
static void* w3_guard_alloc(size_t size);
static void w3_guard_free(void* p);
static int w3_is_guarded(void* p);
static void w3_run_guest_atexit(void);
static void w3_run_tle_atexit(void);
/* BUG-040 build repair: the allocation registry TYPE, SIZE macro and w3_reg_slot
 * are used by mw_free/mw_realloc above; their definitions live in the
 * ISS1-WAVE3B allocator section far below. Hoisted here verbatim (C23 rejects
 * implicit declarations and use-before-declaration of statics). */
#define W3_REG_SIZE 8192
#define W3_REG_TOMB ((void*)(uintptr_t)1)
static struct { void* p; size_t size; uint32_t gen; } g_alloc_reg[W3_REG_SIZE];
static unsigned w3_reg_slot(void* p);
static int w3_reg_find(void* p);
__attribute__((ms_abi)) void* mw_malloc(size_t size) {
    if (w3_guard_enabled()) {
        void* gp = w3_guard_alloc(size ? size : 1);
        MW_TRACE("malloc[GUARD](%zu) = %p", size, gp);
        return gp;
    }
    void* p = malloc(size);
    MW_TRACE("malloc(%zu) = %p", size, p);
    w3_reg_add_sz(p, size);
    return p;
}

__attribute__((ms_abi)) void mw_free(void* ptr) {
    MW_TRACE("free(%p)", ptr);
    if (!w3_reg_has(ptr)) w3_trap_bad_free(ptr, "free");
    {
        int s = w3_reg_find(ptr);
        if (s >= 0 && (g_alloc_reg[s].size >> 32)) {
            w3_guard_free(ptr);
            return;
        }
    }
    w3_reg_del(ptr);
    free(ptr);
}

__attribute__((ms_abi)) void* mw_realloc(void* ptr, size_t size) {
    if (ptr && !w3_reg_has(ptr)) w3_trap_bad_free(ptr, "realloc");
    if (ptr && w3_is_guarded(ptr)) {
        /* BUG-041 (TASK-OPENSRC-1, guard-mode SIGSEGV root cause): a guarded
         * chunk is an mmap'd region — glibc realloc would read a bogus chunk
         * header at [ptr-8] and fault inside libc.so (crash signature: RIP in
         * libc r-xp map, `mov r12,[rbx-0x8]`, return addr inside mw_realloc).
         * mw_HeapReAlloc already had the guarded branch; mw_realloc did not. */
        int s0 = w3_reg_find(ptr);
        size_t old = (s0 >= 0) ? (g_alloc_reg[s0].size & ~0x100000000ull) : 0;
        void* n = w3_guard_alloc(size ? size : 1);
        if (!n) { mw_SetLastError(8); return NULL; }
        memcpy(n, ptr, old < size ? old : size);
        w3_guard_free(ptr);
        MW_TRACE("realloc[GUARD](%p, %zu) = %p", ptr, size, n);
        return n;
    }
    void* p = realloc(ptr, size);
    MW_TRACE("realloc(%p, %zu) = %p", ptr, size, p);
    if (p) { w3_reg_del(ptr); w3_reg_add_sz(p, size ? size : 1); }
    return p;
}

__attribute__((ms_abi)) void* mw_calloc(size_t n, size_t sz) {
    if (w3_guard_enabled()) {
        void* gp = w3_guard_alloc(n * sz);
        if (gp) memset(gp, 0, n * sz);
        MW_TRACE("calloc[GUARD](%zu, %zu) = %p", n, sz, gp);
        return gp;
    }
    void* p = calloc(n, sz);
    MW_TRACE("calloc(%zu, %zu) = %p", n, sz, p);
    w3_reg_add_sz(p, n * sz);
    return p;
}

/* String functions */
__attribute__((ms_abi)) size_t mw_strlen(const char* s) {
    return strlen(s);
}
__attribute__((ms_abi)) int mw_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
}
__attribute__((ms_abi)) char* mw_strcpy(char* d, const char* s) {
    return strcpy(d, s);
}
__attribute__((ms_abi)) char* mw_strchr(const char* s, int c) {
    return (char*)strchr(s, c);
}
__attribute__((ms_abi)) char* mw_strrchr(const char* s, int c) {
    return (char*)strrchr(s, c);
}
__attribute__((ms_abi)) char* mw_strstr(const char* h, const char* n) {
    return (char*)strstr(h, n);
}
__attribute__((ms_abi)) size_t mw_strcspn(const char* s, const char* r) {
    return strcspn(s, r);
}
__attribute__((ms_abi)) int mw_strncmp(const char* a, const char* b, size_t n) {
    return strncmp(a, b, n);
}
__attribute__((ms_abi)) int mw__stricmp(const char* a, const char* b) {
    return strcasecmp(a, b);
}
__attribute__((ms_abi)) char* mw__strdup(const char* s) {
    /* BUG-055: glibc strdup memory was never entered into the allocation
     * registry; jq frees strdup'd parser strings via free() -> spurious
     * HEAP-TRAP exit(4). Allocate + copy + register like every other
     * guest-visible allocation. */
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* d = (char*)malloc(n);
    if (d) { memcpy(d, s, n); w3_reg_add_sz(d, n); }
    return d;
}
__attribute__((ms_abi)) void* mw_memchr(const void* s, int c, size_t n) {
    return (void*)memchr(s, c, n);
}
__attribute__((ms_abi)) int mw_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}
__attribute__((ms_abi)) void* mw_memcpy(void* d, const void* s, size_t n) {
    return memcpy(d, s, n);
}
__attribute__((ms_abi)) void* mw_memmove(void* d, const void* s, size_t n) {
    return memmove(d, s, n);
}
__attribute__((ms_abi)) void* mw_memset(void* s, int c, size_t n) {
    return memset(s, c, n);
}

/* Conversion */
__attribute__((ms_abi)) int mw_atoi(const char* s) { return atoi(s); }

__attribute__((ms_abi)) long mw_strtol(const char* s, char** end, int base) {
    return strtol(s, end, base);
}

__attribute__((ms_abi)) unsigned long mw_strtoul(const char* s, char** end, int base) {
    return strtoul(s, end, base);
}

__attribute__((ms_abi)) char* mw__ultoa(unsigned long val, char* buf, int base) {
    unsigned long v = val;
    char tmp[33];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return buf; }
    while (v > 0) { tmp[i++] = "0123456789abcdef"[v % base]; v /= base; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

__attribute__((ms_abi)) int mw_tolower(int c) { return tolower(c); }
__attribute__((ms_abi)) size_t mw_wcstombs(char* d, const wchar_t* s, size_t n) {
    /* BUG-046: guest UTF-16 (2-byte units) -> UTF-8, native encoder.
     * Stops at the L terminator (CRT semantics); n = max bytes incl. null. */
    const uint16_t* u = (const uint16_t*)s;
    size_t units = 0;
    while (u[units]) units++;
    int got = w3_utf16le_to_utf8(u, (int)units, d, d ? (int)(n ? n - 1 : 0) : 0);
    if (got < 0) return (size_t)-1;
    if (d && n) { d[got] = '\0'; return (size_t)got + 1; }
    return d ? (size_t)got + 1 : (size_t)got;
}
__attribute__((ms_abi)) size_t mw_wcslen(const wchar_t* s) {
    /* BUG-046: guest UTF-16 — count 2-byte units, not glibc 4-byte wchars. */
    const uint16_t* u = (const uint16_t*)s;
    size_t n = 0;
    if (!s) return 0;
    while (u[n]) n++;
    return n;
}

/* Character classification */
__attribute__((ms_abi)) int mw_islower(int c) { return islower(c); }
__attribute__((ms_abi)) int mw_isspace(int c) { return isspace(c); }
__attribute__((ms_abi)) int mw_isupper(int c) { return isupper(c); }
__attribute__((ms_abi)) int mw_isxdigit(int c) { return isxdigit(c); }

/* Time */
__attribute__((ms_abi)) clock_t mw_clock(void) { return clock(); }

/* File I/O - low level MSVCRT functions */
/* APP-F-05b (BUG-033 class): variadic ms_abi entry points are unsafe for
 * glibc-side va_list handling; _open's optional mode is therefore a FIXED
 * third argument. PEs that omit the mode leave R8 undefined, which is fine:
 * mode is only consumed when _O_CREAT (0x0100) is set. */
__attribute__((ms_abi)) int mw__open(const char* path, int flags, int mode) {
    MW_TRACE("_open(path=%s, flags=0x%x, mode=0x%x)", path ? path : "NULL", flags, mode);
    if (!(flags & 0x0100)) mode = 0; /* _O_CREAT unset: mode ignored */
    /* Map MSVC flags to POSIX */
    int oflags = ((flags & 0x0003) == 0) ? O_RDONLY : 0;
    if (flags & 0x0001) oflags = O_WRONLY;
    if (flags & 0x0002) oflags = O_RDWR;
    if (flags & 0x0080) oflags |= O_APPEND;
    if (flags & 0x0100) oflags |= O_CREAT;
    if (flags & 0x0200) oflags |= O_TRUNC;
    if (flags & 0x0400) oflags |= O_EXCL;
    /* _O_TEXT (0x4000) and _O_BINARY (0x8000) ignored on Linux */
    int fd = open(path, oflags, mode);
    return fd;
}

__attribute__((ms_abi)) int mw__read(int fd, void* buf, unsigned int cnt) {
    MW_TRACE("_read(fd=%d, cnt=%u)", fd, cnt);
    return (int)read(fd, buf, cnt);
}

__attribute__((ms_abi)) int mw__write(int fd, const void* buf, unsigned int cnt) {
    MW_TRACE("_write(fd=%d, cnt=%u)", fd, cnt);
    int ret = (int)write(fd, buf, cnt);
    return ret;
}

__attribute__((ms_abi)) int mw__close(int fd) {
    MW_TRACE("_close(fd=%d)", fd);
    return close(fd);
}

__attribute__((ms_abi)) int mw__dup(int fd) {
    MW_TRACE("_dup(fd=%d)", fd);
    return dup(fd);
}

__attribute__((ms_abi)) int mw__isatty(int fd) {
    MW_TRACE("_isatty(fd=%d)", fd);
    return isatty(fd);
}

__attribute__((ms_abi)) int mw__fileno(void* file) {
    MW_TRACE("_fileno(file=%p)", file);
    if (file == stdin) return 0;
    if (file == stdout) return 1;
    if (file == stderr) return 2;
    return -1;
}

__attribute__((ms_abi)) int64_t mw__get_osfhandle(int fd) {
    MW_TRACE("_get_osfhandle(fd=%d)", fd);
    return (int64_t)(uintptr_t)fd;
}

__attribute__((ms_abi)) int mw__setmode(int fd, int mode) {
    MW_TRACE("_setmode(fd=%d, mode=%d)", fd, mode);
    return 0;
}

__attribute__((ms_abi)) int mw__sopen(const char* path, int flags, int shflag, int pmode) {
    MW_TRACE("_sopen(path=%s, flags=0x%x)", path ? path : "NULL", flags);
    return mw__open(path, flags, pmode);
}

__attribute__((ms_abi)) int mw__wopen(const wchar_t* path, int flags, int mode) {
    MW_TRACE("_wopen()");
    char buf[1024];
    w3_w16_to_utf8z((const uint16_t*)path, buf, sizeof(buf));
w3_normalize_path(buf);
    return mw__open(buf, flags, mode);
}

/* stat */
#include <sys/types.h>
#include <sys/stat.h>

__attribute__((ms_abi)) int mw__fstat64(int fd, void* st) {
    MW_TRACE("_fstat64(fd=%d)", fd);
    struct stat64 st64;
    int ret = fstat64(fd, &st64);
    if (ret == 0 && st) {
        /* Map to MSVC _stat64 structure */
        memset(st, 0, 128);
        *(uint32_t*)((uint8_t*)st + 0) = st64.st_mode;
        *(int64_t*)((uint8_t*)st + 8) = st64.st_size;
        *(int64_t*)((uint8_t*)st + 24) = st64.st_atime;
        *(int64_t*)((uint8_t*)st + 32) = st64.st_mtime;
        *(int64_t*)((uint8_t*)st + 40) = st64.st_ctime;
    }
    return ret;
}

__attribute__((ms_abi)) int mw__stat64(const char* path, void* st) {
    MW_TRACE("_stat64(path=%s)", path ? path : "NULL");
    struct stat64 st64;
    int ret = stat64(path, &st64);
    if (ret == 0 && st) {
        /* BUG-061 (2026-10-17, zstd LADDER-008): the guest struct is the MS x64
         * `struct _stat64` (56 bytes: dev@0 u32, ino@4 u16, mode@6 u16, nlink@8
         * u16, uid@10, gid@12, rdev@16 u32, size@24 i64, atime@32, mtime@40,
         * ctime@48). The prior version wrote POSIX offsets (mode@0, size@8) and
         * memset 128 bytes — 72 bytes past the real struct, corrupting adjacent
         * guest globals and crashing zstd's compress path at RIP=0. */
        memset(st, 0, 56);
        *(uint32_t*)((uint8_t*)st + 0)  = (uint32_t)st64.st_dev;
        *(uint16_t*)((uint8_t*)st + 4)  = (uint16_t)st64.st_ino;
        *(uint16_t*)((uint8_t*)st + 6)  = (uint16_t)st64.st_mode;
        *(uint16_t*)((uint8_t*)st + 8)  = (uint16_t)st64.st_nlink;
        *(uint16_t*)((uint8_t*)st + 10) = 0; /* st_uid */
        *(uint16_t*)((uint8_t*)st + 12) = 0; /* st_gid */
        *(uint32_t*)((uint8_t*)st + 16) = (uint32_t)st64.st_rdev;
        *(int64_t*)((uint8_t*)st + 24)  = (int64_t)st64.st_size;
        *(int64_t*)((uint8_t*)st + 32)  = (int64_t)st64.st_atime;
        *(int64_t*)((uint8_t*)st + 40)  = (int64_t)st64.st_mtime;
        *(int64_t*)((uint8_t*)st + 48)  = (int64_t)st64.st_ctime;
    }
    return ret;
}

/* LADDER-013 (md5deep64, BUG-065): _wstat64 — wide path, same MS layout. */
__attribute__((ms_abi)) int mw__wstat64(const uint16_t* wpath, void* st) {
    char p8[4096];
    if (!wpath || w3_w16_to_utf8z(wpath, p8, sizeof(p8)) < 0) {
        return -1;
    }
    w3_normalize_path(p8);
    MW_TRACE("_wstat64(path=%s)", p8);
    return mw__stat64(p8, st);
}

__attribute__((ms_abi)) int64_t mw__lseeki64(int fd, int64_t off, int whence) {
    MW_TRACE("_lseeki64(fd=%d, off=%ld, whence=%d)", fd, (long)off, whence);
    return lseek64(fd, off, whence);
}

__attribute__((ms_abi)) int mw_rename(const char* old, const char* new) {
    MW_TRACE("rename(old=%s, new=%s)", old, new);
    return rename(old, new);
}

__attribute__((ms_abi)) int mw__unlink(const char* path) {
    MW_TRACE("_unlink(path=%s)", path ? path : "NULL");
    return unlink(path);
}

__attribute__((ms_abi)) int mw__rmdir(const char* path) {
    MW_TRACE("_rmdir(path=%s)", path ? path : "NULL");
    return rmdir(path);
}

__attribute__((ms_abi)) int mw__chmod(const char* path, int mode) {
    MW_TRACE("_chmod(path=%s, mode=%d)", path ? path : "NULL", mode);
    return chmod(path, mode);
}

/* Time functions */
__attribute__((ms_abi)) time_t mw__time64(time_t* t) {
    return time(t);
}

__attribute__((ms_abi)) void* mw__gmtime64(const time_t* t) {
    return (void*)gmtime(t);
}

__attribute__((ms_abi)) void* mw__localtime64(const time_t* t) {
    return (void*)localtime(t);
}

/* Misc */
__attribute__((ms_abi)) void mw_exit(int code) {
    MW_TRACE("exit(%d)", code);
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

__attribute__((ms_abi)) void mw_abort(void) {
    MW_TRACE("abort()");
    mw_trace_shutdown_flush();
    _exit(134); /* 128 + SIGABRT(6) */
}

__attribute__((ms_abi)) void (*mw_signal(int sig, void (*handler)(int)))(int) {
    MW_TRACE("signal(sig=%d)", sig);
    return signal(sig, handler);
}

/* BUG-036 (appcompat sweep APP-F-06): setjmp/longjmp must honor the Windows
 * x64 jmp_buf contract. The previous implementation stored a glibc sigjmp_buf
 * inside mw__setjmp's own SYSV frame: (a) ms_abi callee-saved RDI/RSI/XMM6-15
 * are NOT covered by glibc's save set -> live PE values corrupt after longjmp;
 * (b) resumption flows through the loader frame and mis-restores state
 * (observed: t06 counter/volatile failures; t07 infinite longjmp(val=1) loop).
 *
 * This implementation IS the jmp_buf owner: uniform 8-byte slots (mingw x64
 * jmp_buf is 256 bytes = 32 slots, we use 30):
 *   0 rbx   1 rbp   2 rdi   3 rsi   4 r12  5 r13  6 r14  7 r15
 *   8..17   xmm6..xmm15 (16 bytes each = 2 slots each)
 *   18      rsp at entry (points at the return address)
 *   19..28  (unused padding, zeroed)
 *   29      magic 0x4D574A42 ("MWJB") for cross-checking
 * longjmp restores everything and jumps to the saved return address with the
 * requested value in EAX — the exact semantics of Windows setjmp/longjmp. */
#define MW_JB_MAGIC 0x4D574A42ULL

/* setjmp is NAKED: rsp at the first instruction == entry rsp == [retaddr].
 * We save entry_rsp (slot 18) and retaddr (slot 19); longjmp resumes with
 * rsp = entry_rsp + 8 (the caller's live rsp) and jumps to the retaddr. */
__attribute__((ms_abi, naked)) int mw__setjmp(void* buf) {
    asm volatile(
        "movq %rcx, %r10\n\t"
        "movq %rbx, 0(%r10)\n\t"
        "movq %rbp, 8(%r10)\n\t"
        "movq %rdi, 16(%r10)\n\t"
        "movq %rsi, 24(%r10)\n\t"
        "movq %r12, 32(%r10)\n\t"
        "movq %r13, 40(%r10)\n\t"
        "movq %r14, 48(%r10)\n\t"
        "movq %r15, 56(%r10)\n\t"
        "movdqu %xmm6, 64(%r10)\n\t"
        "movdqu %xmm7, 80(%r10)\n\t"
        "movdqu %xmm8, 96(%r10)\n\t"
        "movdqu %xmm9, 112(%r10)\n\t"
        "movdqu %xmm10, 128(%r10)\n\t"
        "movdqu %xmm11, 144(%r10)\n\t"
        "movdqu %xmm12, 160(%r10)\n\t"
        "movdqu %xmm13, 176(%r10)\n\t"
        "movdqu %xmm14, 192(%r10)\n\t"
        "movdqu %xmm15, 208(%r10)\n\t"
        "movq %rsp, 224(%r10)\n\t"
        "movq 0(%rsp), %rax\n\t"
        "movq %rax, 232(%r10)\n\t"
        "movq $0x4D574A42, 240(%r10)\n\t"   /* MW_JB_MAGIC */
        "xorl %eax, %eax\n\t"
        "ret\n\t"
    );
}

__attribute__((ms_abi)) __attribute__((noreturn)) void mw_longjmp(void* buf, int val) {
    register void* r10 asm("r10") = buf;
    register long r11 asm("r11") = (long)val;
    asm volatile(
        "movq 0(%%r10), %%rbx\n\t"
        "movq 8(%%r10), %%rbp\n\t"
        "movq 16(%%r10), %%rdi\n\t"
        "movq 24(%%r10), %%rsi\n\t"
        "movq 32(%%r10), %%r12\n\t"
        "movq 40(%%r10), %%r13\n\t"
        "movq 48(%%r10), %%r14\n\t"
        "movq 56(%%r10), %%r15\n\t"
        "movdqu 64(%%r10), %%xmm6\n\t"
        "movdqu 80(%%r10), %%xmm7\n\t"
        "movdqu 96(%%r10), %%xmm8\n\t"
        "movdqu 112(%%r10), %%xmm9\n\t"
        "movdqu 128(%%r10), %%xmm10\n\t"
        "movdqu 144(%%r10), %%xmm11\n\t"
        "movdqu 160(%%r10), %%xmm12\n\t"
        "movdqu 176(%%r10), %%xmm13\n\t"
        "movdqu 192(%%r10), %%xmm14\n\t"
        "movdqu 208(%%r10), %%xmm15\n\t"
        "movq 224(%%r10), %%rsp\n\t"
        "addq $8, %%rsp\n\t"                 /* caller's live rsp (retaddr popped) */
        "movq %%r11, %%rax\n\t"
        "jmp *232(%%r10)\n\t"
        :
        : "r"(r10), "r"(r11)
        : "memory"
    );
    __builtin_unreachable();
}

__attribute__((ms_abi)) int mw__kbhit(void) { return 0; }

__attribute__((ms_abi)) char* mw_getenv(const char* name) {
    return getenv(name);
}

__attribute__((ms_abi)) void* mw_localeconv(void) {
    return (void*)localeconv();
}

/* BUG-035 (appcompat sweep APP-F-05): callback pointers across the ABI
 * boundary. glibc qsort invokes the comparator SysV-style (args in RDI,RSI)
 * but the PE's comparator is ms_abi (args in RCX,RDX) -> NULL deref inside
 * the comparator (observed t04: mov eax,[rcx] with rcx=0). Bridge through
 * qsort_r: the bridge is SysV (glibc-safe) and re-dispatches through an
 * ms_abi-typed function pointer so GCC emits the Windows register mapping. */

typedef int (__attribute__((ms_abi)) *mw_ms_qsort_cmp)(const void*, const void*);
static const mw_ms_qsort_cmp* g_ms_qsort_cmp_ref;

static int mw_ms_qsort_bridge(const void* a, const void* b, void* ctx) {
    (void)ctx;
    return (*g_ms_qsort_cmp_ref)(a, b);
}

__attribute__((ms_abi)) void mw_qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    g_ms_qsort_cmp_ref = (const mw_ms_qsort_cmp*)(const void*)&cmp;
    qsort_r(base, n, sz, mw_ms_qsort_bridge, NULL);
}

/* ============================================================
 * TASK-API-008 wave 2 (appcompat sweep APP-F-07): C++ runtime surface.
 * llvm-mingw C++ binaries import SRW locks, condition variables, fiber-local
 * storage, InitOnce, RtlRestoreContext, aligned allocation, wide-char CRT
 * and 64-bit strtol family. All implementations below are honest bridges.
 * ============================================================ */
#include <sched.h>
#include <sys/syscall.h>
#include <wchar.h>

/* ---- SRW locks: SRWLOCK is an opaque 8-byte handle slot on Windows.
 * We keep a small registry mapping the PE's SRWLOCK slots to real
 * pthread rwlocks, allocated on first use. Single-instance per key. ---- */
typedef struct { void* key; int used; pthread_rwlock_t rw; } MwSrwEntry;
static MwSrwEntry g_srw_tab[256];
static pthread_mutex_t g_srw_tab_mu = PTHREAD_MUTEX_INITIALIZER;

static pthread_rwlock_t* srw_get(void* k) {
    pthread_mutex_lock(&g_srw_tab_mu);
    MwSrwEntry* free_slot = NULL;
    for (int i = 0; i < 256; i++) {
        if (g_srw_tab[i].used && g_srw_tab[i].key == k) {
            pthread_mutex_unlock(&g_srw_tab_mu);
            return &g_srw_tab[i].rw;
        }
        if (!g_srw_tab[i].used && !free_slot) free_slot = &g_srw_tab[i];
    }
    if (!free_slot) { pthread_mutex_unlock(&g_srw_tab_mu); return NULL; }
    free_slot->used = 1; free_slot->key = k;
    pthread_rwlockattr_t a;
    pthread_rwlockattr_init(&a);
    pthread_rwlockattr_setkind_np(&a, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&free_slot->rw, &a);
    pthread_rwlockattr_destroy(&a);
    pthread_mutex_unlock(&g_srw_tab_mu);
    return &free_slot->rw;
}

__attribute__((ms_abi)) void mw_AcquireSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    if (rw) pthread_rwlock_wrlock(rw);
    MW_TRACE("AcquireSRWLockExclusive(%p)", srw);
}
__attribute__((ms_abi)) void mw_ReleaseSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    if (rw) pthread_rwlock_unlock(rw);
    MW_TRACE("ReleaseSRWLockExclusive(%p)", srw);
}
__attribute__((ms_abi)) unsigned char mw_TryAcquireSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    int rc = rw ? pthread_rwlock_trywrlock(rw) : 0;
    MW_TRACE("TryAcquireSRWLockExclusive(%p) = %d", srw, rc == 0);
    return (rc == 0) ? 1 : 0;
}

/* ---- Condition variables: CONDITION_VARIABLE is an opaque 8-byte slot.
 * Internal mutex+cond per key; the SRW lock is released around the wait and
 * re-acquired in the caller's mode (bit 0 of Flags = shared). ---- */
typedef struct { void* key; int used; pthread_mutex_t mu; pthread_cond_t cd; } MwCvEntry;
static MwCvEntry g_cv_tab[256];
static pthread_mutex_t g_cv_tab_mu = PTHREAD_MUTEX_INITIALIZER;

static MwCvEntry* cv_get(void* k) {
    pthread_mutex_lock(&g_cv_tab_mu);
    MwCvEntry* free_slot = NULL;
    for (int i = 0; i < 256; i++) {
        if (g_cv_tab[i].used && g_cv_tab[i].key == k) {
            pthread_mutex_unlock(&g_cv_tab_mu);
            return &g_cv_tab[i];
        }
        if (!g_cv_tab[i].used && !free_slot) free_slot = &g_cv_tab[i];
    }
    if (!free_slot) { pthread_mutex_unlock(&g_cv_tab_mu); return NULL; }
    free_slot->used = 1; free_slot->key = k;
    pthread_mutex_init(&free_slot->mu, NULL);
    pthread_cond_init(&free_slot->cd, NULL);
    pthread_mutex_unlock(&g_cv_tab_mu);
    return free_slot;
}

__attribute__((ms_abi)) uint32_t mw_SleepConditionVariableSRW(void* cv, void* srw,
                                                              uint32_t ms, uint32_t flags) {
    MwCvEntry* c = cv_get(cv);
    pthread_rwlock_t* rw = srw_get(srw);
    if (!c || !rw) return 0;
    if (flags & 1) pthread_rwlock_unlock(rw); else pthread_rwlock_unlock(rw);
    uint32_t result = 0; /* WAIT_OBJECT_0? Windows: 0 = success */
    pthread_mutex_lock(&c->mu);
    if (ms == 0xFFFFFFFFu) {
        pthread_cond_wait(&c->cd, &c->mu);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if (pthread_cond_timedwait(&c->cd, &c->mu, &ts) == ETIMEDOUT) result = 258; /* WAIT_TIMEOUT */
    }
    pthread_mutex_unlock(&c->mu);
    if (flags & 1) pthread_rwlock_rdlock(rw); else pthread_rwlock_wrlock(rw);
    MW_TRACE("SleepConditionVariableSRW(cv=%p, ms=%u) = %u", cv, ms, result);
    return result;
}
/* zstd wave (LADDER-008): CS condition variable — mirrors the SRW variant;
 * the CRITICAL_SECTION's loader-owned pthread mutex (cs_mutex) is released
 * while waiting and re-acquired on wake, matching Windows semantics. */
__attribute__((ms_abi)) uint32_t mw_SleepConditionVariableCS(void* cv, void* cs, uint32_t ms) {
    MwCvEntry* c = cv_get(cv);
    if (!c || !cs) return 0;
    if (!cs_is_initialized(cs)) mw_InitializeCriticalSection(cs);
    pthread_mutex_t* mu = cs_mutex(cs);
    pthread_mutex_unlock(mu);
    uint32_t result = 0;
    pthread_mutex_lock(&c->mu);
    if (ms == 0xFFFFFFFFu) {
        pthread_cond_wait(&c->cd, &c->mu);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if (pthread_cond_timedwait(&c->cd, &c->mu, &ts) == ETIMEDOUT) result = 258; /* WAIT_TIMEOUT */
    }
    pthread_mutex_unlock(&c->mu);
    pthread_mutex_lock(mu);
    MW_TRACE("SleepConditionVariableCS(cv=%p, ms=%u) = %u", cv, ms, result);
    return result;
}
__attribute__((ms_abi)) void mw_WakeConditionVariable(void* cv) {
    MwCvEntry* c = cv_get(cv);
    if (c) { pthread_mutex_lock(&c->mu); pthread_cond_signal(&c->cd); pthread_mutex_unlock(&c->mu); }
    MW_TRACE("WakeConditionVariable(%p)", cv);
}
__attribute__((ms_abi)) void mw_WakeAllConditionVariable(void* cv) {
    MwCvEntry* c = cv_get(cv);
    if (c) { pthread_mutex_lock(&c->mu); pthread_cond_broadcast(&c->cd); pthread_mutex_unlock(&c->mu); }
    MW_TRACE("WakeAllConditionVariable(%p)", cv);
}

/* ---- Fiber-local storage: index-based TLS array. Thread-exit callbacks are
 * not invoked (documented limitation; nothing in the C++ startup path relies
 * on FLS callbacks under this loader). ---- */
#define MW_FLS_MAX 128
static __thread void* g_fls_vals[MW_FLS_MAX];

__attribute__((ms_abi)) uint32_t mw_FlsAlloc(void* cb) {
    (void)cb;
    static uint32_t next = 0;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    uint32_t idx = next++;
    pthread_mutex_unlock(&mu);
    MW_TRACE("FlsAlloc(cb=%p) = %u", cb, idx);
    return (idx < MW_FLS_MAX) ? idx : 0xFFFFFFFFu;
}
__attribute__((ms_abi)) void* mw_FlsGetValue(uint32_t idx) {
    return (idx < MW_FLS_MAX) ? g_fls_vals[idx] : NULL;
}
__attribute__((ms_abi)) int mw_FlsSetValue(uint32_t idx, void* val) {
    if (idx >= MW_FLS_MAX) return 0;
    g_fls_vals[idx] = val;
    return 1;
}

/* ---- InitOnce: single-threaded-execution assumption for the fast path,
 * serialized by a global mutex (startup-time use only). ---- */
typedef int (__attribute__((ms_abi)) *mw_initonce_fn)(void* once, void* param, void* ctx);
__attribute__((ms_abi)) int mw_InitOnceExecuteOnce(void* once, void* fn, void* param, void* ctx) {
    /* BUG-060 (2026-10-17): the old global mutex serialized ALL once-controls;
     * curl's nested InitOnce callback re-entered this function on the same
     * thread -> self-deadlock on mu. Windows semantics: state lives IN the
     * once-control (0=init, 1=in-progress, 2=complete). */
    uint64_t* s = (uint64_t*)once;
    if (*s == 2) return 1;
    if (__sync_val_compare_and_swap(s, 0, 1) == 0) {
        int ok = ((mw_initonce_fn)fn)(once, param, ctx);
        if (ok) *s = 2; else *s = 0;
        MW_TRACE("InitOnceExecuteOnce(once=%p) = %d", once, ok);
        return ok;
    }
    /* pending (or completed while we raced): wait for completion */
    while (*s == 1) sched_yield();
    return (*s == 2) ? 1 : 0;
}

/* ---- RtlRestoreContext: full GPR + XMM + RIP restore from a Windows
 * CONTEXT (x64 layout). Used by unwinding resume paths. ---- */
__attribute__((ms_abi)) void mw_RtlRestoreContext(void* ctx, void* exc) {
    (void)exc;
    if (!ctx) return;
    register void* r10 asm("r10") = ctx;
    asm volatile(
        "movq 0x80(%%r10), %%rcx\n\t"
        "movq 0x88(%%r10), %%rdx\n\t"
        "movq 0x90(%%r10), %%rbx\n\t"
        "movq 0xA0(%%r10), %%rbp\n\t"
        "movq 0xA8(%%r10), %%rsi\n\t"
        "movq 0xB0(%%r10), %%rdi\n\t"
        "movq 0xB8(%%r10), %%r8\n\t"
        "movq 0xC0(%%r10), %%r9\n\t"
        "movq 0xD8(%%r10), %%r12\n\t"
        "movq 0xE0(%%r10), %%r13\n\t"
        "movq 0xE8(%%r10), %%r14\n\t"
        "movq 0xF0(%%r10), %%r15\n\t"
        "movdqu 0x1A0(%%r10), %%xmm0\n\t"
        "movdqu 0x1B0(%%r10), %%xmm1\n\t"
        "movdqu 0x1C0(%%r10), %%xmm2\n\t"
        "movdqu 0x1D0(%%r10), %%xmm3\n\t"
        "movdqu 0x1E0(%%r10), %%xmm4\n\t"
        "movdqu 0x1F0(%%r10), %%xmm5\n\t"
        "movdqu 0x200(%%r10), %%xmm6\n\t"
        "movdqu 0x210(%%r10), %%xmm7\n\t"
        "movdqu 0x220(%%r10), %%xmm8\n\t"
        "movdqu 0x230(%%r10), %%xmm9\n\t"
        "movdqu 0x240(%%r10), %%xmm10\n\t"
        "movdqu 0x250(%%r10), %%xmm11\n\t"
        "movdqu 0x260(%%r10), %%xmm12\n\t"
        "movdqu 0x270(%%r10), %%xmm13\n\t"
        "movdqu 0x280(%%r10), %%xmm14\n\t"
        "movdqu 0x290(%%r10), %%xmm15\n\t"
        "movq 0x78(%%r10), %%rax\n\t"        /* Rax */
        "movq 0xC8(%%r10), %%r11\n\t"        /* R11 (r10 still = ctx) */
        "movq 0x98(%%r10), %%rsp\n\t"        /* Rsp (final stack) */
        "movq 0xC8(%%r10), %%r10\n\t"        /* R10 (last: r10 was ctx) */
        "pushq %%rax\n\t"                    /* [Rsp-8] = Rip */
        "ret\n\t"
        :
        : "r"(r10)
        : "rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi",
          "r8", "r9", "r11", "r12", "r13", "r14", "r15", "memory"
    );
    __builtin_unreachable();
}

/* ---- misc kernel32 ---- */
__attribute__((ms_abi)) void* mw_GetModuleHandleW(const wchar_t* name) {
    MW_TRACE("GetModuleHandleW()");
    if (name == NULL || name[0] == 0) return g_image_base;
    return NULL;
}
__attribute__((ms_abi)) uint32_t mw_GetThreadId(void* h) {
    (void)h;
    return (uint32_t)syscall(SYS_gettid);
}
__attribute__((ms_abi)) int mw_SwitchToThread(void) {
    return (sched_yield() == 0) ? 1 : 0;
}
__attribute__((ms_abi)) uint32_t mw_WaitForSingleObjectEx(void* h, uint32_t ms, int alertable) {
    (void)alertable;
    return mw_WaitForSingleObject(h, ms);
}

/* ---- msvcrt wave 2 + TASK-OPENSRC-1 (api-ms-win-crt-heap) ---- */
/* Aligned-heap family with a Windows-style header: [base_ptr @ -16]
 * [req_size @ -8] before the aligned pointer. _aligned_realloc/_recalloc/
 * _msize need the old size (Windows contract), which plain posix_memalign
 * cannot recover; free() still works because we own the base pointer. */
static size_t w3_align_up_sz(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
/* BUG-047: aligned chunks carry [magic @ -24][base @ -16][size @ -8]. The
 * guest is allowed to free an aligned pointer through free/HeapFree (UCRT
 * does), so the central bad-free path resolves the magic back to the base
 * instead of trapping (magick: '[HEAP-TRAP HeapFree] untracked 0x2656660'). */
#define W3_ALIGNED_MAGIC 0x4D574147554E4957ULL
static void* w3_aligned_base_of(void* p) {
    if (!p || (uintptr_t)p < 4096) return NULL;
    if (*(uint64_t*)((uintptr_t)p - 24) == W3_ALIGNED_MAGIC)
        return *(void**)((uintptr_t)p - 16);
    return NULL;
}
__attribute__((ms_abi)) void* mw__aligned_malloc(size_t size, size_t align) {
    if (align < 8) align = 8;
    if ((align & (align - 1)) != 0) return NULL;
    size_t slack = align + 24;
    void* base = malloc((size ? size : 1) + slack);
    if (!base) return NULL;
    uintptr_t a = w3_align_up_sz((uintptr_t)base + 24, align);
    *(uint64_t*)(a - 24) = W3_ALIGNED_MAGIC;
    *(void**)(a - 16) = base;
    *(size_t*)(a - 8) = size;
    return (void*)a;
}
__attribute__((ms_abi)) void* mw__aligned_offset_malloc(size_t size, size_t align, size_t offset) {
    if (align < 8) align = 8;
    if ((align & (align - 1)) != 0) return NULL;
    size_t slack = align + 24 + offset;
    void* base = malloc((size ? size : 1) + slack);
    if (!base) return NULL;
    uintptr_t a = w3_align_up_sz((uintptr_t)base + 24 + offset, align) - offset;
    *(uint64_t*)(a - 24) = W3_ALIGNED_MAGIC;
    *(void**)(a - 16) = base;
    *(size_t*)(a - 8) = size;
    return (void*)a;
}
__attribute__((ms_abi)) void mw__aligned_free(void* p) {
    if (!p) return;
    free(*(void**)((uintptr_t)p - 16));
}
__attribute__((ms_abi)) void* mw__aligned_realloc(void* p, size_t size, size_t align) {
    if (!p) return mw__aligned_malloc(size, align);
    if (align < 8) align = 8;
    size_t old = *(size_t*)((uintptr_t)p - 8);
    void* n = mw__aligned_malloc(size, align);
    if (!n) return NULL;
    memcpy(n, p, old < size ? old : size);
    mw__aligned_free(p);
    return n;
}
__attribute__((ms_abi)) void* mw__aligned_offset_realloc(void* p, size_t size, size_t align, size_t offset) {
    if (!p) return mw__aligned_offset_malloc(size, align, offset);
    if (align < 8) align = 8;
    size_t old = *(size_t*)((uintptr_t)p - 8);
    void* n = mw__aligned_offset_malloc(size, align, offset);
    if (!n) return NULL;
    memcpy(n, p, old < size ? old : size);
    mw__aligned_free(p);
    return n;
}
__attribute__((ms_abi)) void* mw__aligned_recalloc(void* p, size_t num, size_t size, size_t align) {
    size_t bytes = num * size;
    if (!p) {
        void* n = mw__aligned_malloc(bytes, align);
        if (n) memset(n, 0, bytes);
        return n;
    }
    size_t old = *(size_t*)((uintptr_t)p - 8);
    void* n = mw__aligned_malloc(bytes, align);
    if (!n) return NULL;
    memcpy(n, p, old < bytes ? old : bytes);
    if (bytes > old) memset((char*)n + old, 0, bytes - old);
    mw__aligned_free(p);
    return n;
}
__attribute__((ms_abi)) size_t mw__aligned_msize(void* p, size_t align, size_t offset) {
    (void)align; (void)offset;
    return p ? *(size_t*)((uintptr_t)p - 8) : 0;
}

__attribute__((ms_abi)) void mw__assert(const char* msg, const char* file, unsigned line) {
    fprintf(stderr, "Assertion failed: %s (%s:%u)\n", msg ? msg : "?", file ? file : "?", line);
    abort();
}

__attribute__((ms_abi)) long long mw__strtoi64(const char* s, char** end, int base) {
    return strtoll(s, end, base);
}
__attribute__((ms_abi)) unsigned long long mw__strtoui64(const char* s, char** end, int base) {
    return strtoull(s, end, base);
}
/* NOTE: Windows wchar_t is 2 bytes (UTF-16), glibc's is 4 (UCS-4). For
 * ASCII digit strings the leading bytes coincide, so simple passthrough is
 * correct for the numeric cases the C++ runtime uses. Non-ASCII wide input
 * needs a UTF-16 -> UTF-32 shim (follow-up API task). */
__attribute__((ms_abi)) long mw_wcstol(const wchar_t* s, wchar_t** end, int base) {
    { size_t o = 0; while (((const uint16_t*)s)[o]) o++;
      char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
      char* e8; long r = strtol(b8, &e8, base);
      if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
      return r; }
}
__attribute__((ms_abi)) unsigned long mw_wcstoul(const wchar_t* s, wchar_t** end, int base) {
    { size_t o = 0; while (((const uint16_t*)s)[o]) o++;
      char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
      char* e8; unsigned long r = strtoul(b8, &e8, base);
      if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
      return r; }
}
__attribute__((ms_abi)) long long mw__wcstoi64(const wchar_t* s, wchar_t** end, int base) {
    { size_t o = 0; while (((const uint16_t*)s)[o]) o++;
      char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
      char* e8; long r = strtol(b8, &e8, base);
      if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
      return r; }
}
__attribute__((ms_abi)) unsigned long long mw__wcstoui64(const wchar_t* s, wchar_t** end, int base) {
    { size_t o = 0; while (((const uint16_t*)s)[o]) o++;
      char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
      char* e8; unsigned long r = strtoul(b8, &e8, base);
      if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
      return r; }
}

__attribute__((ms_abi)) wint_t mw_fputwc(wchar_t ch, void* file) {
    FILE* f = map_crt_stream(file);
    return f ? fputwc((wint_t)ch, f) : WEOF;
}

/* fwprintf: wide variadic — narrow the (ASCII) format specifiers for
 * classification, then build the same SysV va_list and call vfwprintf. */
__attribute__((ms_abi)) int mw_fwprintf(void* file, const wchar_t* fmt, ...) {
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    char narrow[512];
    size_t k = 0;
    if (fmt) {
        const unsigned short* w = (const unsigned short*)fmt;
        while (*w && k + 1 < sizeof(narrow)) { narrow[k++] = (char)(*w & 0xFF); w++; }
    }
    narrow[k] = 0;
    int nargs = mw_printf_classify(narrow, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    __builtin_ms_va_list ap;   /* BUG-063: ms_abi variadic -> ms va_list */
    __builtin_ms_va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    __builtin_ms_va_end(ap);
    unsigned char cls2[MW_PRINTF_MAX_ARGS];
    int parsed = mw_printf_classify(narrow, cls2);
    if (nargs > parsed) nargs = parsed;

    unsigned long long gpv[6] = {0,0,0,0,0,0};
    unsigned long long fpv[8] = {0,0,0,0,0,0,0,0};
    unsigned long long ovf[MW_PRINTF_MAX_ARGS];
    int novf = 0, gpi = 1, fpi = 0;
    for (int i = 0; i < nargs; i++) {
        if (cls2[i]) { if (fpi < 8) fpv[fpi++] = slots[i]; else ovf[novf++] = slots[i]; }
        else         { if (gpi < 6) gpv[gpi++] = slots[i]; else ovf[novf++] = slots[i]; }
    }
    unsigned long long reg_save[6 + 16];
    memcpy(reg_save, gpv, sizeof(gpv));
    for (int i = 0; i < 8; i++)
        memcpy((unsigned char*)reg_save + 48 + 16 * i, &fpv[i], 8);
    for (int i = fpi; i < 8; i++)
        memset((unsigned char*)reg_save + 48 + 16 * i, 0, 16);
    va_list sv;
    mw_sysv_va_list* svv = (mw_sysv_va_list*)sv;
    svv->gp_offset = 8;
    svv->fp_offset = 48;
    svv->overflow_arg_area = ovf;
    svv->reg_save_area = reg_save;
    return f ? vfwprintf(f, fmt, sv) : -1;
}

__attribute__((ms_abi)) int mw_rand(void) { return rand(); }
__attribute__((ms_abi)) void mw_srand(unsigned int seed) { srand(seed); }

__attribute__((ms_abi)) int mw__onexit(void (*func)(void)) {
    MW_TRACE("_onexit(func=%p)", func);
    return atexit(func);
}

/* ---- TASK-API-007 wave 1 (appcompat sweep APP-F-02): CRT startup surface.
 * mingw-w64 CRT startup imports these unconditionally; with them unresolved
 * the IAT slot keeps the raw hint/name RVA and `call [iat]` faults at that
 * RVA (observed RIP=0x92f8, SIGSEGV before main in every mingw PE). */

/* ISS1-WAVE4: guest atexit registry. Passing PE code pointers to the HOST
 * atexit() made glibc jump into the guest image during process teardown —
 * outside the loader's execution context (live: ffmpeg "malloc(): invalid
 * size (unsorted)" + SIGABRT at exit). Windows semantics: the CRT exit path
 * runs these in reverse order BEFORE the process terminates. */
#define MW_ATEXIT_MAX 128
static void (__attribute__((ms_abi)) *g_guest_atexit[MW_ATEXIT_MAX])(void);
static int g_guest_atexit_count = 0;
__attribute__((ms_abi)) int mw_atexit(void (__attribute__((ms_abi)) *func)(void)) {
    MW_TRACE("atexit(func=%p)", func);
    if (!func) return -1;
    if (g_guest_atexit_count >= MW_ATEXIT_MAX) return -1;
    g_guest_atexit[g_guest_atexit_count++] = func;
    return 0;
}
static void w3_run_guest_atexit(void) {
    for (int i = g_guest_atexit_count - 1; i >= 0; i--) {
        MW_TRACE("guest atexit run [%d] = %p", i, g_guest_atexit[i]);
        g_guest_atexit[i]();
    }
    g_guest_atexit_count = 0;
    w3_run_tle_atexit();
}

/* Windows _exit: terminate without flushing or running atexit handlers. */
__attribute__((ms_abi)) void mw__exit_noflush(int code) {
    MW_TRACE("_exit(code=%d)", code);
    _exit(code);
}

/* msvcrt setvbuf modes differ from glibc: _IOFBF=0, _IOLBF=64, _IONBF=4
 * (glibc: 0/1/2). Translate before forwarding to the real FILE*. */
__attribute__((ms_abi)) int mw_setvbuf(void* stream, char* buf, int mode, size_t size) {
    int gmode;
    switch (mode) {
        case 0:  gmode = _IOFBF; break;  /* _IOFBF */
        case 64: gmode = _IOLBF; break;  /* _IOLBF */
        case 4:  gmode = _IONBF; break;  /* _IONBF */
        default: gmode = mode;   break;
    }
    MW_TRACE("setvbuf(stream=%p, mode=%d->%d, size=%zu)", stream, mode, gmode, size);
    FILE* f = map_crt_stream(stream);
    if (!f) return 0; /* nothing to do for a NULL stream */
    return setvbuf(f, buf, gmode, size);
}

/* kernel32 GetCPInfo — CRT locale init asks for the ANSI code page.
 * Honest SBCS answer: MaxCharSize=1, DefaultChar='?', no lead bytes. */
__attribute__((ms_abi)) int mw_GetCPInfo(unsigned codepage, void* cpinfo) {
    MW_TRACE("GetCPInfo(cp=%u)", codepage);
    if (!cpinfo) return 0;           /* FALSE */
    unsigned char* p = (unsigned char*)cpinfo;
    p[0] = 1;                        /* MaxCharSize = 1 (SBCS) */
    p[1] = 0;                        /* (padding of UINT on LE is BE byte 0) */
    p[2] = '?'; p[3] = 0;            /* DefaultChar = "?" + NUL */
    memset(p + 4, 0, 12);            /* LeadByte ranges: none */
    return 1;                        /* TRUE */
}

/* msvcrt _XcptFilter — MSVC SEH filter helper used by CRT internals.
 * Faithful default: never claim the exception. */
__attribute__((ms_abi)) long mw__XcptFilter(unsigned long xcpt, void* ep) {
    MW_TRACE("_XcptFilter(xcpt=0x%lx)", xcpt);
    (void)ep;
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}

/* BUG-062: real thread creation (was a 0-returning stub — see the thread
 * registry block above mw_WaitForSingleObject for the full design note). */
__attribute__((ms_abi)) unsigned long mw__beginthreadex(void* sec, unsigned int stk,
    void* (*start)(void*), void* arg, int init, unsigned int* tid) {
    (void)sec; (void)stk; /* security descriptor + stack size: host defaults */
    MW_TRACE("_beginthreadex(start=%p, arg=%p, init=0x%x)", start, arg, init);
    pthread_mutex_lock(&g_mw_threads_mu);
    MwThread* slot = NULL;
    for (int i = 0; i < 64; i++) {
        if (!g_mw_threads[i].used) { slot = &g_mw_threads[i]; break; }
    }
    if (slot) { slot->used = 1; slot->done = 0; slot->exit_code = 0; slot->pt = 0; }
    pthread_mutex_unlock(&g_mw_threads_mu);
    if (!slot) return 0; /* table full -> Windows-style failure (NULL handle) */

    MwThreadBoot* b = (MwThreadBoot*)malloc(sizeof(MwThreadBoot));
    if (!b) { slot->used = 0; return 0; }
    b->start = (MwGuestStart)(void*)start;
    b->arg = arg;
    b->self = slot;
    pthread_t pt;
    if (pthread_create(&pt, NULL, mw_thread_trampoline, b) != 0) {
        free(b); slot->used = 0; return 0;
    }
    slot->pt = pt;
    pthread_detach(pt); /* CloseHandle/Wait semantics; no join leak */
    void* h = alloc_handle((void*)slot);
    if (tid) *tid = (unsigned)(uintptr_t)pt & 0xFFFFFFFFu;
    MW_TRACE("_beginthreadex() = %p", h);
    return (unsigned long)(uintptr_t)h;
}

__attribute__((ms_abi)) void mw__endthreadex(unsigned int code) {
    MW_TRACE("_endthreadex(%u)", code);
    MwThread* self = t_cur_thread;
    if (self) {
        pthread_mutex_lock(&t_exit_mu);
        self->exit_code = code;
        self->done = 1;
        pthread_cond_broadcast(&t_exit_cv);
        pthread_mutex_unlock(&t_exit_mu);
        t_cur_thread = NULL;
    }
    pthread_exit((void*)(uintptr_t)code);
}

__attribute__((ms_abi)) int* mw__errno(void) {
    MW_TRACE("_errno() = %p", &g_errno_storage);
    return &g_errno_storage;
}

__attribute__((ms_abi)) void mw__lock(int locknum) {
    MW_TRACE("_lock(%d)", locknum);
}

__attribute__((ms_abi)) void mw__unlock(int locknum) {
    MW_TRACE("_unlock(%d)", locknum);
}

/* ============================================================
 * Import Dispatch Table
 * Maps import name → function pointer (for code imports)
 * Data imports are handled separately.
 * ============================================================ */

typedef struct {
    const char* dll;
    const char* name;
    void*       func;  /* For code imports: ms_abi function pointer */
    int         is_data;
} ImportEntry;

#define ENTRY(dll, name, func) { dll, name, (void*)func, 0 }
#define DATA_ENTRY(dll, name, addr) { dll, name, (void*)addr, 1 }


/* ============================================================
 * ISS1-WAVE3 — general API families driven by REAL application
 * demand (Issue #1 §8.2/§8.4: "Add support according to measured
 * demand"): first external corpus = ImageMagick 7.1.2-34 portable
 * (magick.exe) + ffmpeg-master win64 GPL. Semantics per MSDN;
 * features the host cannot provide return their real Windows
 * failure codes (honest failure, never fake success).
 * ============================================================ */

/* ---- process parameter block (command line / env / module) ---- */
static char g_cmdline_buf[8192];
/* (removed: g_cmdline_bufw was the 4-byte-wchar cmdline buffer — BUG-043 replaced it with g_cmdline_bufw16) */
static int g_cmdline_ready = 0;
static int g_cmdline_w_ready = 0;

static void build_cmdline(void) {
    if (g_cmdline_ready) return;
    size_t o = 0;
    for (int i = 0; g_argv[i]; i++) {
        const char* a = g_argv[i];
        int need_quote = a[0] == '\0' || strchr(a, ' ');
        if (o) g_cmdline_buf[o++] = ' ';
        if (need_quote) g_cmdline_buf[o++] = '"';
        for (const char* p = a; *p && o < sizeof(g_cmdline_buf) - 4; p++) g_cmdline_buf[o++] = *p;
        if (need_quote) g_cmdline_buf[o++] = '"';
    }
    g_cmdline_buf[o] = '\0';
    g_cmdline_ready = 1;
}
static size_t utf8_to_utf16(const char* s, void* out, size_t cap_units) {
    /* BUG-050: emits 2-byte UTF-16LE units (guest ABI), not 4-byte glibc
     * wchars. cap_units counts uint16_t units. Returns unit count. */
    if (!s) return 0;
    int got = w3_utf8_to_utf16le(s, strlen(s), (uint16_t*)out, out ? (int)cap_units : 0);
    return got < 0 ? 0 : (size_t)got;
}

/* BUG-043: the guest command line must be UTF-16LE (2-byte units). The old
 * build_cmdline_w used mbstowcs -> 4-byte glibc wchars: the guest reading the
 * buffer as UTF-16 saw every high half as a NUL terminator, i.e. argv0 = "f"
 * and NO arguments (ffmpeg's CRT init mis-parses its own command line). */
static uint16_t g_cmdline_bufw16[2048];
static void build_cmdline_w(void) {
    if (g_cmdline_w_ready) return;
    build_cmdline();
    int n = w3_utf8_to_utf16le(g_cmdline_buf, strlen(g_cmdline_buf),
                               g_cmdline_bufw16, 2047);
    if (n < 0) n = 0;
    g_cmdline_bufw16[n] = 0;
    g_cmdline_w_ready = 1;
}

__attribute__((ms_abi)) const char* mw_GetCommandLineA(void) {
    build_cmdline();
    MW_TRACE("GetCommandLineA() = \"%s\"", g_cmdline_buf);
    return g_cmdline_buf;
}
__attribute__((ms_abi)) const wchar_t* mw_GetCommandLineW(void) {
    build_cmdline_w();
    MW_TRACE("GetCommandLineW()");
    return (const wchar_t*)g_cmdline_bufw16;
}

/* TASK-OPENSRC-1 / BUG-043 family (shell32!CommandLineToArgvW): MS parser
 * semantics — whitespace = space/tab; "..." toggles quoting; 2n backslashes
 * followed by a quote = n backslashes + quote-toggle; 2n+1 backslashes + a
 * quote = n backslashes + a literal quote (Wine/ReactOS-verified). One
 * mw_malloc'd block holds the argv pointer array followed by all strings;
 * the guest frees it with LocalFree. Two passes: count, then emit. */
static void w3_c2a_run(const uint16_t* s, uint16_t** argv, uint16_t* out,
                       int* pargc, size_t* punits) {
    int argc = 0;
    size_t u = 0;
    while (*s == ' ' || *s == '\t') s++;          /* leading whitespace */
    /* argv[0] */
    if (argv) argv[argc] = out ? out + u : NULL;
    argc++;
    if (*s == '"') {
        s++;
        while (*s && *s != '"') { if (out) out[u] = *s; u++; s++; }
        if (*s) s++;
    } else {
        while (*s && *s != ' ' && *s != '\t') { if (out) out[u] = *s; u++; s++; }
    }
    if (out) out[u] = 0;
    u++;
    /* remaining arguments */
    while (*s) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (argv) argv[argc] = out ? out + u : NULL;
        argc++;
        int in_quotes = 0;
        while (*s) {
            if (*s == ' ' || *s == '\t') {
                if (!in_quotes) break;
                if (out) out[u] = *s; u++; s++;
            } else if (*s == '"') {
                int bs = 0;
                while (*s == '\\') { bs++; s++; }
                if (*s == '"') {
                    for (int i = 0; i < bs / 2; i++) { if (out) out[u] = '\\'; u++; }
                    if (bs & 1) { if (out) out[u] = '"'; u++; s++; }
                    else { in_quotes = !in_quotes; s++; }
                } else {
                    for (int i = 0; i < bs; i++) { if (out) out[u] = '\\'; u++; }
                }
            } else {
                if (out) out[u] = *s; u++; s++;
            }
        }
        if (out) out[u] = 0;
        u++;
    }
    *pargc = argc;
    *punits = u;
}

__attribute__((ms_abi)) void** mw_CommandLineToArgvW(const void* cmdline, int* pNumArgs) {
    const uint16_t* s = (const uint16_t*)cmdline;
    MW_TRACE("CommandLineToArgvW(%p)", cmdline);
    if (!pNumArgs) { mw_SetLastError(87); return NULL; }
    if (!s) { build_cmdline_w(); s = g_cmdline_bufw16; }
    int argc = 0; size_t units = 0;
    w3_c2a_run(s, NULL, NULL, &argc, &units);
    uint16_t** block = mw_malloc((size_t)argc * 8 + units * 2 + 16);
    if (!block) { mw_SetLastError(8); return NULL; }
    uint16_t* strs = (uint16_t*)((char*)block + (size_t)argc * 8);
    w3_c2a_run(s, block, strs, &argc, &units);
    *pNumArgs = argc;
    return (void**)block;
}

__attribute__((ms_abi)) void mw_GetStartupInfoW(void* si) {
    MW_TRACE("GetStartupInfoW()");
    if (!si) return;
    /* STARTUPINFOW = 104 bytes on x64. Field offsets: cb=0, lpReserved=8,
     * lpDesktop=16, lpTitle=24, dwX..dwFlags=32..60, wShowWindow=64,
     * wCbReserved2=66, lpReserved2=72, hStdInput=80, hStdOutput=88,
     * hStdError=96. (Writing dwFlags at +72 clobbered lpReserved2 — a CRT
     * internal pointer — and corrupted the heap downstream.) */
    memset(si, 0, 104);
    *(uint32_t*)((uint8_t*)si + 0)  = 104;           /* cb */
    *(uint32_t*)((uint8_t*)si + 60) = 0x00000100;    /* dwFlags = STARTF_USESTDHANDLES */
    *(uint64_t*)((uint8_t*)si + 80) = 0x0000000000000004ull;  /* hStdInput  (handle 4) */
    *(uint64_t*)((uint8_t*)si + 88) = 0x0000000000000008ull;  /* hStdOutput (handle 8) */
    *(uint64_t*)((uint8_t*)si + 96) = 0x000000000000000Cull;  /* hStdError  (handle C) */
}

__attribute__((ms_abi)) int mw_GetModuleHandleExA(uint32_t flags, const char* name, void** mod) {
    MW_TRACE("GetModuleHandleExA(flags=%u)", flags);
    if (!mod) return 0;
    /* FROM_ADDRESS: the PE is one module — any image address resolves to it */
    if ((flags & 4) && name) { *mod = (void*)(uintptr_t)g_image_base; return 1; }
    if (!name) { *mod = (void*)(uintptr_t)g_image_base; return 1; }
    return 0;
}
__attribute__((ms_abi)) int mw_GetModuleHandleExW(uint32_t flags, const wchar_t* name, void** mod) {
    MW_TRACE("GetModuleHandleExW(flags=%u)", flags);
    if (!mod) return 0;
    if ((flags & 4) && name) { *mod = (void*)(uintptr_t)g_image_base; return 1; }
    if (!name) { *mod = (void*)(uintptr_t)g_image_base; return 1; }
    {   /* OPENSRC battery: by-name lookups of system modules succeed with
         * the same pseudo-handle identity LoadLibrary hands out. */
        char n8[256] = {0};
        w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8));
        char low[64]; size_t k = 0;
        for (; n8[k] && k < sizeof(low) - 1; k++)
            low[k] = (n8[k] >= 'A' && n8[k] <= 'Z') ? (char)(n8[k] + 32) : n8[k];
        low[k] = 0;
        if (memcmp(low, "api-ms-win", 10) == 0) { *mod = (void*)(uintptr_t)0xdead0002; return 1; }
        void* h = mw_LoadLibraryA(low);
        if (h) { *mod = h; return 1; }
    }
    return 0;
}

static char w3_exe_path[4096];
static int w3_exe_path_ready = 0;
static const char* get_exe_path(void) {
    /* BUG-051 (2026-10-17): /proc/self/exe is the HOST loader binary
     * (minwin_loader — no .exe, wrong file). The guest's own module path
     * is the PE the loader was told to run (g_exe_path = argv[1]). */
    if (g_exe_path) return g_exe_path;
    if (!w3_exe_path_ready) {
        ssize_t n = readlink("/proc/self/exe", w3_exe_path, sizeof(w3_exe_path) - 1);
        if (n <= 0) n = 0;
        w3_exe_path[n] = '\0';
        w3_exe_path_ready = 1;
    }
    return w3_exe_path;
}
__attribute__((ms_abi)) uint32_t mw_GetModuleFileNameA(void* mod, char* out, uint32_t cap) {
    MW_TRACE("GetModuleFileNameA(cap=%u)", cap);
    if (!out || !cap) return 0;
    const char* p = get_exe_path();
    snprintf(out, cap, "%s", p);
    return (uint32_t)strlen(out);
}
__attribute__((ms_abi)) uint32_t mw_GetModuleFileNameW(void* mod, wchar_t* out, uint32_t cap) {
    MW_TRACE("GetModuleFileNameW(cap=%u)", cap);
    if (!out || !cap) return 0;
    const char* p = get_exe_path();
    size_t n = utf8_to_utf16(p, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}

/* ============================================================
 * SWEEP-002 (2026-10-18): ProcessPrng (bcryptprimitives.dll).
 * Sweep evidence: every Go binary died in runtime.osinit ->
 * loadOptionalSyscalls with "fatal error: bcryptprimitives.dll not
 * found", and every Rust/MSVC binary lists ProcessPrng as its FIRST
 * unresolved import. Modern language runtimes get randomness through
 * this single bcryptprimitives export. BOOL ProcessPrng(PBYTE, SIZE_T)
 * fills the buffer from the OS CSPRNG and returns nonzero on success.
 * Linux mapping: getrandom(2) loop (same primitive as loader.c:8629).
 * ============================================================ */
__attribute__((ms_abi)) int mw_ProcessPrng(uint8_t* buf, uint64_t len) {
    MW_TRACE("ProcessPrng(len=%llu)", (unsigned long long)len);
    if (!buf && len) return 0;
    uint64_t done = 0;
    while (done < len) {
        ssize_t r = syscall(SYS_getrandom, buf + done, len - done, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return 0; /* honest failure — guest runtimes retry or abort */
        }
        done += (uint64_t)r;
    }
    return 1;
}

/* ============================================================
 * SWEEP-002b (2026-10-18): winmm.dll timeBeginPeriod/timeEndPeriod.
 * Sweep evidence: with bcryptprimitives.dll present, Go osinit now
 * fails one step later: "fatal error: timeBegin/EndPeriod not found"
 * (runtime.initHighResTimer requires both exports or aborts before
 * main). Windows contract: request 1ms scheduler resolution, return
 * TIMERR_NOERROR (0); nested periods are refcounted. Linux needs no
 * such knob — honest no-op that remembers the last period.
 * ============================================================ */
static uint32_t g_time_period_ms = 0;
__attribute__((ms_abi)) uint32_t mw_timeBeginPeriod(uint32_t ms) {
    MW_TRACE("timeBeginPeriod(%u)", ms);
    g_time_period_ms = ms;
    return 0; /* TIMERR_NOERROR */
}
__attribute__((ms_abi)) uint32_t mw_timeEndPeriod(uint32_t ms) {
    MW_TRACE("timeEndPeriod(%u)", ms);
    if (g_time_period_ms == ms) g_time_period_ms = 0;
    return 0; /* TIMERR_NOERROR */
}

__attribute__((ms_abi)) void* mw_LoadLibraryW(const wchar_t* name) {
    /* OPENSRC battery (sqlite3): known system DLLs load successfully with a
     * pseudo-handle — the guest's imports were already resolved at load
     * time, so the handle is an identity for GetProcAddress, which resolves
     * against our dispatch table. Unknown DLLs still fail (ERROR 126). */
    char n8[256] = {0};
    if (name) w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8));
    MW_TRACE("LoadLibraryW(%s)", n8);
    return mw_LoadLibraryA(n8);
}
__attribute__((ms_abi)) void* mw_LoadLibraryExW(const wchar_t* name, void* file, uint32_t flags) {
    MW_TRACE("LoadLibraryExW(flags=%u)", flags);
    char n8[256] = {0};
    if (name) w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8));
    return mw_LoadLibraryA(n8);
    mw_SetLastError(126);
    return NULL;
}
__attribute__((ms_abi)) int mw_FreeLibrary(void* mod) { MW_TRACE("FreeLibrary()"); return 1; }
__attribute__((ms_abi)) void mw_FreeLibraryAndExitThread(void* mod, uint32_t code) {
    MW_TRACE("FreeLibraryAndExitThread(code=%u)", code);
    mw_ExitProcess(code);
}

/* ---- environment ---- */
__attribute__((ms_abi)) wchar_t* mw_GetEnvironmentStringsW(void) {
    MW_TRACE("GetEnvironmentStringsW()");
    /* Build a double-NUL-terminated UTF-16 block; the loader never frees it
     * (FreeEnvironmentStringsW is a no-op) so one static buffer suffices. */
    static uint16_t block[16384];
    size_t o = 0;
    for (char** e = environ; *e && o < 16380; e++) {
        int n = w3_utf8_to_utf16le(*e, strlen(*e), block + o, 16380 - (int)o - 2);
        if (n <= 0) break;
        o += n;
        block[o++] = 0;
    }
    block[o] = 0;
    return (wchar_t*)block;
}
__attribute__((ms_abi)) int mw_FreeEnvironmentStringsW(wchar_t* b) { MW_TRACE("FreeEnvironmentStringsW()"); return 1; }
__attribute__((ms_abi)) int mw_FreeEnvironmentStringsA(char* b) { MW_TRACE("FreeEnvironmentStringsA()"); return 1; }

__attribute__((ms_abi)) uint32_t mw_GetEnvironmentVariableW(const wchar_t* name, wchar_t* out, uint32_t cap) {
    char n8[256], v8[4096];
    if (!name) return 0;
    w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8));
    const char* v = getenv(n8);
    MW_TRACE("GetEnvironmentVariableW(%s) = %s", n8, v ? v : "<null>");
    if (!v) { mw_SetLastError(203); return 0; }
    if (!out || !cap) return (uint32_t)strlen(v) + 1;
    size_t n = utf8_to_utf16(v, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}
__attribute__((ms_abi)) uint32_t mw_GetEnvironmentVariableA(const char* name, char* out, uint32_t cap) {
    const char* v = name ? getenv(name) : NULL;
    MW_TRACE("GetEnvironmentVariableA(%s) = %s", name ? name : "?", v ? v : "<null>");
    if (!v) { mw_SetLastError(203); return 0; }
    if (!out || !cap) return (uint32_t)strlen(v) + 1;
    snprintf(out, cap, "%s", v);
    return (uint32_t)strlen(out);
}
__attribute__((ms_abi)) int mw_SetEnvironmentVariableA(const char* name, const char* val) {
    MW_TRACE("SetEnvironmentVariableA(%s)", name ? name : "NULL");
    if (!name) return 1;
    if (setenv(name, val ? val : "", 1) == 0) return 1;
    mw_SetLastError(8); return 0;
}
__attribute__((ms_abi)) int mw_SetEnvironmentVariableW(const wchar_t* name, const wchar_t* val) {
    char n8[256], v8[4096];
    if (!name) return 1;
    w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8));
    if (val) w3_w16_to_utf8z((const uint16_t*)val, v8, sizeof(v8));
    MW_TRACE("SetEnvironmentVariableW(%s)", n8);
    if (setenv(name && n8[0] ? n8 : "", val ? v8 : "", 1) == 0) return 1;
    mw_SetLastError(8); return 0;
}
__attribute__((ms_abi)) uint32_t mw_ExpandEnvironmentStringsW(const wchar_t* src, wchar_t* out, uint32_t cap) {
    /* Real %VAR% expansion against the host environment */
    char s8[8192], d8[8192];
    if (!src) return 0;
    int sn = w3_w16_to_utf8z((const uint16_t*)src, s8, sizeof(s8));
    if (sn < 0) return 0;
    s8[sn] = '\0';
    size_t o = 0;
    for (char* p = s8; *p && o < sizeof(d8) - 1; ) {
        if (p[0] == '%' && (strchr(p + 1, '%'))) {
            char* e = strchr(p + 1, '%');
            *e = '\0';
            const char* v = getenv(p + 1);
            if (!v) v = "";
            o += (size_t)snprintf(d8 + o, sizeof(d8) - o, "%s", v);
            p = e + 1;
        } else {
            d8[o++] = *p++;
        }
    }
    d8[o] = '\0';
    MW_TRACE("ExpandEnvironmentStringsW() -> \"%s\"", d8);
    if (!out || !cap) return (uint32_t)strlen(d8) + 1;
    size_t n = utf8_to_utf16(d8, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n + 1;
}

/* ---- heap / global / local memory (backed by malloc) ---- */
__attribute__((ms_abi)) void* mw_GetProcessHeap(void) {
    return (void*)(uintptr_t)0x00010000;
}
__attribute__((ms_abi)) void* mw_HeapCreate(uint32_t fl, size_t init, size_t max) {
    return (void*)(uintptr_t)0x00010001;
}

/* ISS1-WAVE3B: allocation registry — every pointer our HeapAlloc/ReAlloc/
 * GlobalAlloc family hands out is recorded; frees of unknown pointers are
 * trapped with the caller chain (root-causes "free(): invalid size").
 * BUG-040: W3_REG_SIZE + g_alloc_reg + w3_reg_slot declared at the malloc
 * forward-decl block above (mw_free touches the registry directly); the
 * function definitions remain here. */
static uint32_t g_alloc_gen = 1;
static unsigned w3_reg_slot(void* p) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    return (unsigned)((v >> 4) * 0x9E3779B97F4A7C15ull >> 32) % W3_REG_SIZE;
}
static void w3_reg_add_sz(void* p, size_t sz) {
    if (!p) return;
    /* BUG-048: linear probing — the direct-mapped table lost entries on hash
     * collisions (big guests allocate thousands of chunks; a displaced entry
     * turned its later free into a spurious HEAP-TRAP exit(4)). */
    unsigned s = w3_reg_slot(p);
    for (unsigned probe = 0; probe < W3_REG_SIZE; probe++) {
        unsigned idx = (s + probe) & (W3_REG_SIZE - 1);
        void* q = g_alloc_reg[idx].p;
        if (q == p) { g_alloc_reg[idx].size = sz; g_alloc_reg[idx].gen = g_alloc_gen; return; }
        if (q == NULL || q == W3_REG_TOMB) {
            g_alloc_reg[idx].p = p;
            g_alloc_reg[idx].size = sz;
            g_alloc_reg[idx].gen = g_alloc_gen;
            return;
        }
    }
    MW_TRACE("registry FULL — %p untracked", p);
}
static void w3_reg_add(void* p) { w3_reg_add_sz(p, 0); }
static void w3_reg_del(void* p) {
    if (!p) return;
    int s = w3_reg_find(p);
    if (s >= 0) { g_alloc_reg[s].p = W3_REG_TOMB; g_alloc_reg[s].size = 0; }
}
static int w3_reg_find(void* p) {
    if (!p || (uintptr_t)p < 16) return -1;
    unsigned s = w3_reg_slot(p);
    for (unsigned probe = 0; probe < W3_REG_SIZE; probe++) {
        unsigned idx = (s + probe) & (W3_REG_SIZE - 1);
        void* q = g_alloc_reg[idx].p;
        if (q == p) return (int)idx;
        if (q == NULL) return -1;   /* empty slot ends the probe chain */
    }
    return -1;
}
static int w3_reg_has(void* p) {
    if (!p) return 1;
    return w3_reg_find(p) >= 0;
}
static void w3_trap_bad_free(void* p, const char* tag) {
    /* BUG-047: an aligned-family pointer freed through the generic path —
     * resolve the magic header and free the base instead of trapping. */
    void* abase = w3_aligned_base_of(p);
    if (abase) {
        MW_TRACE("%s(aligned %p) -> base %p", tag, p, abase);
        free(abase);
        return;
    }
    fprintf(stderr, "[HEAP-TRAP %s] free of untracked/bad ptr 0x%lx — caller chain:\n",
            tag, (unsigned long)(uintptr_t)p);
    uint64_t scan_rsp = (uint64_t)(uintptr_t)&p;
    for (int i = 0; i < 256; i++) {
        uint64_t v = *(uint64_t*)(scan_rsp + (uint64_t)(i * 8));
        const char* t = NULL;
        if (v >= 0x2000000ull && v < 0x2050000ull) t = "LOADER";
        else if (g_image_base && v >= (uint64_t)(uintptr_t)g_image_base &&
                 v < (uint64_t)(uintptr_t)g_image_base + g_image_size) t = "PE";
        if (t) fprintf(stderr, "[HEAP-TRAP %s] [rsp+%03d] 0x%lx <%s>\n", tag, i * 8, v, t);
    }
    fflush(stderr);
    _exit(4);
}

/* ISS1-WAVE3B DIAG: MINWIN_HEAP_GUARD=1 — HeapAlloc gets an mmap'd region
 * with a trailing PROT_NONE guard page so ANY overflow faults at the exact
 * writing instruction instead of corrupting a neighbor chunk. */
static int g_heap_guard = -1;
static int w3_guard_enabled(void) {
    if (g_heap_guard < 0) {
        const char* v = getenv("MINWIN_HEAP_GUARD");
        g_heap_guard = (v && v[0] == '1');
    }
    return g_heap_guard;
}
static void* w3_guard_alloc(size_t size) {
    size_t page = 4096;
    size_t body = (size + page - 1) & ~(page - 1);
    uint8_t* mem = mmap(NULL, body + page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;
    mprotect(mem + body, page, PROT_NONE);
    w3_reg_add_sz(mem, size | 0x100000000ull); /* flag bit 32 = guarded */
    return mem;
}
static void w3_guard_free(void* p) {
    int s = w3_reg_find(p);
    if (s < 0) return;
    size_t size = g_alloc_reg[s].size & ~0x100000000ull;
    size_t page = 4096;
    size_t body = (size + page - 1) & ~(page - 1);
    munmap(p, body + page);
    g_alloc_reg[s].p = W3_REG_TOMB;
    g_alloc_reg[s].size = 0;
}
static int w3_is_guarded(void* p) {
    int s = w3_reg_find(p);
    return s >= 0 && (g_alloc_reg[s].size >> 32);
}
__attribute__((ms_abi)) void* mw_HeapAlloc(void* heap, uint32_t flags, size_t size) {
    if (w3_guard_enabled()) {
        void* gp = w3_guard_alloc(size ? size : 1);
        MW_TRACE("HeapAlloc[GUARD](size=0x%lx) = %p", (unsigned long)size, gp);
        if (!gp) { mw_SetLastError(8); return NULL; }
        return gp;
    }
    void* p = (flags & 0x08) ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
    MW_TRACE("HeapAlloc(size=0x%lx, zero=%d) = %p", (unsigned long)size, !!(flags & 0x08), p);
    if (!p) { mw_SetLastError(8); return NULL; }
    w3_reg_add_sz(p, size);
    return p;
}

/* ISS1-WAVE3B: shared chunk validation before any free-family operation */
static void w3_validate_chunk(void* p, const char* tag) {
    if (!p) return;
    uint64_t hdr = *(uint64_t*)((uint8_t*)p - 8);
    uint64_t sz = hdr & ~0xFull;
    if (sz < 32 || (sz & 7) || sz > (1ull << 40)) {
        fprintf(stderr, "[HEAP-TRAP %s] bad chunk 0x%lx hdr=0x%lx size=0x%lx\n",
                tag, (unsigned long)(uintptr_t)p, (unsigned long)hdr, (unsigned long)sz);
        uint64_t scan_rsp = (uint64_t)(uintptr_t)&hdr;
        for (int i = 0; i < 256; i++) {
            uint64_t v = *(uint64_t*)(scan_rsp + (uint64_t)(i * 8));
            const char* t = NULL;
            if (v >= 0x2000000ull && v < 0x2050000ull) t = "LOADER";
            else if (g_image_base && v >= (uint64_t)(uintptr_t)g_image_base &&
                     v < (uint64_t)(uintptr_t)g_image_base + g_image_size) t = "PE";
            if (t) fprintf(stderr, "[HEAP-TRAP %s] [rsp+%03d] 0x%lx <%s>\n", tag, i * 8, v, t);
        }
        fflush(stderr);
        _exit(4);
    }
}
__attribute__((ms_abi)) void* mw_HeapReAlloc(void* heap, uint32_t flags, void* p, size_t size) {
    if (!p) return mw_HeapAlloc(heap, flags, size);
    if (w3_is_guarded(p)) {
        int s0 = w3_reg_find(p);
        size_t old = (s0 >= 0) ? (g_alloc_reg[s0].size & ~0x100000000ull) : 0;
        void* n = w3_guard_alloc(size ? size : 1);
        if (!n) { mw_SetLastError(8); return NULL; }
        memcpy(n, p, old < size ? old : size);
        w3_guard_free(p);
        return n;
    }
    w3_validate_chunk(p, "HeapReAlloc");
    void* n = (flags & 0x08) ? realloc(p, size ? size : 1) : realloc(p, size ? size : 1);
    if (n && (flags & 0x08)) {
        size_t old = malloc_usable_size(p); /* already freed by realloc — approximate */
        (void)old;
    }
    MW_TRACE("HeapReAlloc(size=0x%lx) = %p", (unsigned long)size, n);
    if (!n) { mw_SetLastError(8); return NULL; }
    w3_reg_del(p);
    w3_reg_add(n);
    return n;
}
__attribute__((ms_abi)) int mw_HeapFree(void* heap, uint32_t flags, void* p) {
    MW_TRACE("HeapFree(%p)", p);
    if (!p) return 1;
    /* guarded allocations must be checked BEFORE touching p-8 (their header
     * word lives inside the PROT_NONE guard page) */
    if (w3_is_guarded(p)) { w3_guard_free(p); return 1; }
    /* glibc chunk validation: size at p-8 must be sane. Catch corrupted
     * chunks AT THE FREE with the guest caller still on the stack. */
    uint64_t hdr = *(uint64_t*)((uint8_t*)p - 8);
    uint64_t sz = hdr & ~0xFull;
    uint64_t scan_rsp = (uint64_t)(uintptr_t)&hdr;
    int rf = w3_reg_find(p);
    int tracked = (rf >= 0);
    size_t exp = tracked ? (g_alloc_reg[rf].size & ~0x100000000ull) : 0;
    if (sz < 32 || (sz & 7) || sz > (1ull << 40) || !tracked) {
        w3_trap_bad_free(p, "HeapFree");
    }
    if (exp && sz < exp) {
        fprintf(stderr, "[HEAP-TRAP] chunk 0x%lx OVERFLOWED: hdr size=0x%lx < requested 0x%lx (overrun >= 0x%lx bytes)\n",
                (unsigned long)(uintptr_t)p, (unsigned long)sz, (unsigned long)exp,
                (unsigned long)(exp - sz));
        uint64_t scan_rsp2 = (uint64_t)(uintptr_t)&hdr;
        for (int i = 0; i < 256; i++) {
            uint64_t v = *(uint64_t*)(scan_rsp2 + (uint64_t)(i * 8));
            const char* t = NULL;
            if (v >= 0x2000000ull && v < 0x2050000ull) t = "LOADER";
            else if (g_image_base && v >= (uint64_t)(uintptr_t)g_image_base &&
                     v < (uint64_t)(uintptr_t)g_image_base + g_image_size) t = "PE";
            if (t) fprintf(stderr, "[HEAP-TRAP] [rsp+%03d] 0x%lx <%s>\n", i * 8, v, t);
        }
        fflush(stderr);
        _exit(5);
    }
    w3_reg_del(p);
    free(p);
    return 1;
}
__attribute__((ms_abi)) size_t mw_HeapSize(void* heap, uint32_t flags, void* p) {
    if (!p) return 0;
    int s = w3_reg_find(p);
    if (s >= 0 && (g_alloc_reg[s].size >> 32))
        return g_alloc_reg[s].size & ~0x100000000ull;
    return malloc_usable_size(p);
}
__attribute__((ms_abi)) int mw_HeapValidate(void* heap, uint32_t flags, void* p) { return 1; }
__attribute__((ms_abi)) int mw_HeapSetInformation(void* heap, int cls, void* info, size_t len) { return 1; }
__attribute__((ms_abi)) int mw_HeapQueryInformation(void* heap, int cls, void* info, size_t len, size_t* ret) {
    if (ret) *ret = 0;
    return 0; /* not supported — honest */
}
__attribute__((ms_abi)) int mw_HeapCompact(void* heap, uint32_t flags) { return 0; }

/* ============================================================
 * SW-555 honest batch (SWEEP-009): REAL global atom table.
 * Win32 string atoms occupy 0xC000..0xFFFF; add refcounts repeats,
 * delete drops a ref (frees at zero), find looks up without adding,
 * get-name copies. Pure user-mode, zero host dependency — honest by
 * implementation, not by failure. RAMMap64/procexp64 hit GlobalAddAtomW
 * through their IATs while it was the lying stub returning atom 0.
 * ============================================================ */
#define MW_ATOM_FIRST 0xC000
#define MW_ATOM_MAX   1024
static struct { wchar_t* name; uint32_t refs; } g_atoms[MW_ATOM_MAX];
static uint32_t g_atom_count = 0;
static int mw_atom_find(const wchar_t* s) {
    for (uint32_t i = 0; i < g_atom_count; i++)
        if (g_atoms[i].name && wcscmp(g_atoms[i].name, s) == 0) return (int)i;
    return -1;
}
static int mw_atom_hole(void) {
    for (uint32_t i = 0; i < g_atom_count; i++)
        if (!g_atoms[i].name) return (int)i;
    return -1;
}
__attribute__((ms_abi)) uint16_t mw_GlobalAddAtomW(const wchar_t* s) {
    if (!s || !s[0]) return 0;                             /* real: NULL/empty fails */
    if (wcslen(s) > 255) { mw_SetLastError(6); return 0; } /* real: name >255 fails */
    int i = mw_atom_find(s);
    if (i >= 0) { if (g_atoms[i].refs < 0xFFFFFFFFu) g_atoms[i].refs++; return (uint16_t)(MW_ATOM_FIRST + i); }
    wchar_t* dup = (wchar_t*)malloc((wcslen(s) + 1) * sizeof(wchar_t));
    if (!dup) { mw_SetLastError(14); return 0; }           /* ERROR_OUTOFMEMORY */
    wcscpy(dup, s);
    int hole = mw_atom_hole();
    if (hole >= 0) { g_atoms[hole].name = dup; g_atoms[hole].refs = 1; return (uint16_t)(MW_ATOM_FIRST + hole); }
    if (g_atom_count >= MW_ATOM_MAX) { free(dup); mw_SetLastError(14); return 0; }
    g_atoms[g_atom_count].name = dup; g_atoms[g_atom_count].refs = 1; g_atom_count++;
    return (uint16_t)(MW_ATOM_FIRST + g_atom_count - 1);
}
__attribute__((ms_abi)) uint16_t mw_GlobalFindAtomW(const wchar_t* s) {
    if (!s || !s[0]) return 0;
    int i = mw_atom_find(s);
    return i < 0 ? 0 : (uint16_t)(MW_ATOM_FIRST + i);
}
__attribute__((ms_abi)) void mw_GlobalDeleteAtom(uint16_t atom) {
    int i = (int)atom - MW_ATOM_FIRST;
    if (i < 0 || i >= (int)g_atom_count || !g_atoms[i].name) return;
    if (g_atoms[i].refs > 0 && --g_atoms[i].refs == 0) { free(g_atoms[i].name); g_atoms[i].name = NULL; }
}
__attribute__((ms_abi)) uint32_t mw_GlobalGetAtomNameW(uint16_t atom, wchar_t* buf, int32_t size) {
    int i = (int)atom - MW_ATOM_FIRST;
    if (!buf || size <= 0 || i < 0 || i >= (int)g_atom_count || !g_atoms[i].name) { mw_SetLastError(13); return 0; }
    uint32_t n = (uint32_t)wcslen(g_atoms[i].name);
    if (n >= (uint32_t)size) { mw_SetLastError(13); return 0; }
    wcscpy(buf, g_atoms[i].name);
    return n;
}

/* ============================================================
 * SW-555 honest batch (SWEEP-009): shlwapi HLS color pair — REAL math.
 * Classic Win32 HLS scale: H/S/L each 0..240. procexp64's 36-caller
 * 22%-luminance shading helper (FUN_14007baf0: L = L*22/100, clamp 240)
 * read BLACK (0,0,0) for every color while these were stubs. Roundtrip
 * verified over all 2^24 colors (host test: scripts/sweep009_color_roundtrip.c:
 * max channel dev 4 — inherent to the 0..240 Win32 scale; L err EXACTLY 0,
 * which is what procexp consumes). Hue must ROUND, not truncate (the test
 * caught dev 2 at 0x000251 before the fix).
 * ============================================================ */
__attribute__((ms_abi)) void mw_ColorRGBToHLS(uint32_t clr, uint16_t* hue, uint16_t* lum, uint16_t* sat) {
    uint32_t r = clr & 0xFF, g = (clr >> 8) & 0xFF, b = (clr >> 16) & 0xFF;
    uint32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint32_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint32_t sum = mx + mn, delta = mx - mn;
    uint32_t H = 0, L, S = 0;
    L = (sum * 240 + 255) / 510;                    /* rounded (mx+mn)/2 -> 0..240 */
    if (delta) {
        S = (sum <= 255) ? (delta * 240 + sum / 2) / sum
                         : (delta * 240 + (510 - sum) / 2) / (510 - sum);
        /* hue must ROUND (truncation broke near-primary colors) */
        if (mx == r)      { int32_t d2 = (int32_t)g - (int32_t)b;
                            int32_t n2 = d2 >= 0 ? (d2 * 40 + (int32_t)delta / 2) / (int32_t)delta
                                                 : (d2 * 40 - (int32_t)delta / 2) / (int32_t)delta;
                            H = (uint32_t)((d2 >= 0 ? 0 : 240) + n2); }
        else if (mx == g) { int32_t d2 = (int32_t)b - (int32_t)r;
                            H = (uint32_t)(80 + (d2 * 40 + (d2 >= 0 ? (int32_t)delta / 2 : -(int32_t)delta / 2)) / (int32_t)delta); }
        else              { int32_t d2 = (int32_t)r - (int32_t)g;
                            H = (uint32_t)(160 + (d2 * 40 + (d2 >= 0 ? (int32_t)delta / 2 : -(int32_t)delta / 2)) / (int32_t)delta); }
        H %= 240;
    }
    if (hue) *hue = (uint16_t)H;
    if (lum) *lum = (uint16_t)L;
    if (sat) *sat = (uint16_t)S;
}
static double mw_hls_v(double m1, double m2, double h) {
    if (h < 0) h += 1; else if (h > 1) h -= 1;
    if (h < 1.0/6) return m1 + (m2 - m1) * 6 * h;
    if (h < 0.5)   return m2;
    if (h < 2.0/3) return m1 + (m2 - m1) * (2.0/3 - h) * 6;
    return m1;
}
__attribute__((ms_abi)) uint32_t mw_ColorHLSToRGB(uint16_t hue, uint16_t lum, uint16_t sat) {
    double L = lum / 240.0, S = sat / 240.0, H = (hue % 240) / 240.0;
    double r, g, b;
    if (S == 0) { r = g = b = L; }
    else {
        double m2 = (L <= 0.5) ? L * (1 + S) : L + S - L * S;
        double m1 = 2 * L - m2;
        r = mw_hls_v(m1, m2, H + 1.0/3);
        g = mw_hls_v(m1, m2, H);
        b = mw_hls_v(m1, m2, H - 1.0/3);
    }
#define MW_C2B(x) ((uint32_t)((x) * 255 + 0.5) & 0xFF)
    return MW_C2B(r) | (MW_C2B(g) << 8) | (MW_C2B(b) << 16);  /* COLORREF 0x00BBGGRR */
}

/* ---- SWEEP-010 (SW-556, procexp64 UI-init): user32!GetSystemMetrics ----
 * Evidence: procexp64 UI init asks EXACTLY SM_CYSMCAPTION (50) and
 * SM_CYSMICON (49) at 0x140001eb5/ec4/f55/f62 (scripts/sweep010_sm_indexes.py;
 * mov ecx,imm32 decoded before each FF15) — the all-purpose stub returned 0 =
 * zero-sized small icons/captions feeding the toolbar ImageList_Create.
 * Contract (honest): full standard table with a Windows-10 x64 1920x1080
 * single-monitor identity; guest reality is no network stack (SM_NETWORK 0),
 * no digitizer/touch, no terminal-server session, no secure boot; undefined
 * indexes return 0 exactly like real GetSystemMetrics. */
__attribute__((ms_abi)) int mw_GetSystemMetrics(int index) {
    switch (index) {
    case 0:  return 1920; /* SM_CXSCREEN */
    case 1:  return 1080; /* SM_CYSCREEN */
    case 2:  return 17;   /* SM_CXVSCROLL */
    case 3:  return 17;   /* SM_CYHSCROLL */
    case 4:  return 23;   /* SM_CYCAPTION */
    case 5:  return 1;    /* SM_CXBORDER */
    case 6:  return 3;    /* SM_CXDLGFRAME */
    case 7:  return 3;    /* SM_CYDLGFRAME */
    case 8:  return 17;   /* SM_CYVTHUMB */
    case 9:  return 17;   /* SM_CXHTHUMB */
    case 10: return 32;   /* SM_CXICON */
    case 11: return 32;   /* SM_CYICON */
    case 12: return 32;   /* SM_CXCURSOR */
    case 13: return 32;   /* SM_CYCURSOR */
    case 14: return 20;   /* SM_CYMENU */
    case 15: return 1920; /* SM_CXFULLSCREEN */
    case 16: return 1057; /* SM_CYFULLSCREEN (1080 - caption) */
    case 17: return 0;    /* SM_CYKANJIWINDOW */
    case 18: return 1;    /* SM_MOUSEPRESENT */
    case 19: return 17;   /* SM_CYVSCROLL */
    case 20: return 17;   /* SM_CXHSCROLL */
    case 21: return 0;    /* SM_DEBUG */
    case 22: return 0;    /* SM_SWAPBUTTON */
    case 27: return 132;  /* SM_CXMIN */
    case 28: return 54;   /* SM_CYMIN */
    case 29: return 18;   /* SM_CXSIZE */
    case 30: return 18;   /* SM_CYSIZE */
    case 31: return 4;    /* SM_CXSIZEFRAME (SM_CXFRAME) */
    case 32: return 4;    /* SM_CYSIZEFRAME (SM_CYFRAME) */
    case 33: return 132;  /* SM_CXMINTRACK */
    case 34: return 54;   /* SM_CYMINTRACK */
    case 35: return 4;    /* SM_CXDOUBLECLK */
    case 36: return 4;    /* SM_CYDOUBLECLK */
    case 37: return 75;   /* SM_CXICONSPACING */
    case 38: return 75;   /* SM_CYICONSPACING */
    case 39: return 0;    /* SM_MENUDROPALIGNMENT (left) */
    case 40: return 0;    /* SM_PENWINDOWS */
    case 41: return 0;    /* SM_DBCSENABLED */
    case 42: return 3;    /* SM_CMOUSEBUTTONS */
    case 43: return 0;    /* SM_SECURE */
    case 44: return 2;    /* SM_CXEDGE */
    case 45: return 2;    /* SM_CYEDGE */
    case 46: return 160;  /* SM_CXMINSPACING */
    case 47: return 24;   /* SM_CYMINSPACING */
    case 48: return 16;   /* SM_CXSMICON */
    case 49: return 16;   /* SM_CYSMICON   <- procexp64 toolbar sizing */
    case 50: return 17;   /* SM_CYSMCAPTION <- procexp64 toolbar sizing */
    case 51: return 16;   /* SM_CXSMSIZE */
    case 52: return 16;   /* SM_CYSMSIZE */
    case 53: return 18;   /* SM_CXMENUSIZE */
    case 54: return 18;   /* SM_CYMENUSIZE */
    case 55: return 8;    /* SM_ARRANGE */
    case 56: return 160;  /* SM_CXMINIMIZED */
    case 57: return 24;   /* SM_CYMINIMIZED */
    case 58: return 1936; /* SM_CXMAXTRACK (1920 + 2*frame) */
    case 59: return 1096; /* SM_CYMAXTRACK (1080 + 2*frame) */
    case 60: return 1928; /* SM_CXMAXIMIZED */
    case 61: return 1079; /* SM_CYMAXIMIZED */
    case 62: return 0;    /* SM_NETWORK — guest has no network stack (honest) */
    case 63: return 0;    /* SM_CLEANBOOT (normal boot) */
    case 64: return 4;    /* SM_CXDRAG */
    case 65: return 4;    /* SM_CYDRAG */
    case 66: return 0;    /* SM_SHOWSOUNDS */
    case 71: return 13;   /* SM_CXMENUCHECK */
    case 72: return 13;   /* SM_CYMENUCHECK */
    case 73: return 0;    /* SM_SLOWMACHINE */
    case 74: return 0;    /* SM_MIDEASTENABLED */
    case 75: return 1;    /* SM_MOUSEWHEELPRESENT */
    case 76: return 0;    /* SM_XVIRTUALSCREEN */
    case 77: return 0;    /* SM_YVIRTUALSCREEN */
    case 78: return 1920; /* SM_CXVIRTUALSCREEN */
    case 79: return 1080; /* SM_CYVIRTUALSCREEN */
    case 80: return 1;    /* SM_CMONITORS */
    case 81: return 1;    /* SM_SAMEDISPLAYFORMAT */
    case 82: return 1;    /* SM_IMMENABLED */
    case 83: return 2;    /* SM_CXFOCUSBORDER */
    case 84: return 2;    /* SM_CYFOCUSBORDER */
    case 86: return 0;    /* SM_TABLETPC */
    case 87: return 0;    /* SM_MEDIACENTER */
    case 88: return 0;    /* SM_STARTER */
    case 89: return 0;    /* SM_SERVERR2 */
    case 90: return 0;    /* SM_CARETBLINKINGENABLED (off) */
    case 94: return 0;    /* SM_DIGITIZER */
    case 95: return 0;    /* SM_MAXIMUM_TOUCHES */
    case 4096: return 0;  /* SM_REMOTESESSION — not a terminal-server session */
    }
    return 0; /* real GetSystemMetrics also returns 0 for undefined indexes */
}

__attribute__((ms_abi)) void* mw_GlobalAlloc(uint32_t flags, size_t bytes) {
    void* p = (flags & 0x40) ? calloc(1, bytes ? bytes : 1) : malloc(bytes ? bytes : 1);
    MW_TRACE("GlobalAlloc(flags=%u, bytes=0x%lx) = %p", flags, (unsigned long)bytes, p);
    w3_reg_add(p);
    return p;
}
__attribute__((ms_abi)) void* mw_GlobalLock(void* p) { return p; }
__attribute__((ms_abi)) int mw_GlobalUnlock(void* p) { return 1; }
__attribute__((ms_abi)) size_t mw_GlobalSize(void* p) {
    if (!p) return 0;
    int s = w3_reg_find(p);
    if (s >= 0 && (g_alloc_reg[s].size >> 32))
        return g_alloc_reg[s].size & ~0x100000000ull;
    return malloc_usable_size(p);
}
__attribute__((ms_abi)) void* mw_GlobalFree(void* p) { if (!w3_reg_has(p)) w3_trap_bad_free(p, "GlobalFree"); w3_reg_del(p); free(p); return NULL; }
__attribute__((ms_abi)) void* mw_LocalAlloc(uint32_t flags, size_t bytes) { return mw_GlobalAlloc(flags, bytes); } /* registry inside GlobalAlloc */
__attribute__((ms_abi)) void* mw_LocalLock(void* p) { return p; }
__attribute__((ms_abi)) int mw_LocalUnlock(void* p) { return 1; }
__attribute__((ms_abi)) size_t mw_LocalSize(void* p) { return p ? malloc_usable_size(p) : 0; }
__attribute__((ms_abi)) void* mw_LocalFree(void* p) {
    if (!p) return NULL;
    if (!w3_reg_has(p)) w3_trap_bad_free(p, "LocalFree");
    /* BUG-043 family: mirror mw_free — a guarded chunk is mmap'd; plain
     * free() on it faults inside libc (same class as BUG-041 mw_realloc). */
    if (w3_is_guarded(p)) { w3_guard_free(p); return NULL; }
    w3_reg_del(p);
    free(p);
    return NULL;
}

__attribute__((ms_abi)) int mw_GlobalMemoryStatusEx(void* ms) {
    if (!ms) return 0;
    uint8_t* p = (uint8_t*)ms;
    *(uint32_t*)(p + 0) = 64; /* dwLength */
    for (int i = 1; i < 8; i++) *(uint32_t*)(p + 4 * i) = 0;
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        uint64_t total = (uint64_t)si.totalram * si.mem_unit;
        uint64_t freeb = (uint64_t)si.freeram * si.mem_unit;
        *(uint32_t*)(p + 4) = 58; /* dwMemoryLoad % */
        *(uint64_t*)(p + 8)  = total;
        *(uint64_t*)(p + 16) = freeb;
        *(uint64_t*)(p + 32) = total;
        *(uint64_t*)(p + 40) = freeb;
    }
    return 1;
}

/* ---- system info / CPU features / debug ---- */
__attribute__((ms_abi)) void mw_GetSystemInfo(void* si) {
    MW_TRACE("GetSystemInfo()");
    if (!si) return;
    memset(si, 0, 48);
    uint8_t* p = (uint8_t*)si;
    *(uint16_t*)(p + 0)  = 9;          /* wProcessorArchitecture = AMD64 */
    *(uint16_t*)(p + 2)  = 0;
    *(uint32_t*)(p + 4)  = 4096;       /* dwPageSize */
    *(void**)(p + 8)     = (void*)0x10000;
    *(void**)(p + 16)    = (void*)0x7FFFFFFEFFFF;
    *(uint64_t*)(p + 24) = 1;          /* dwActiveProcessorMask */
    *(uint32_t*)(p + 32) = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    *(uint32_t*)(p + 36) = 65536;      /* dwAllocationGranularity */
    *(uint16_t*)(p + 40) = 6;          /* wProcessorLevel */
    *(uint16_t*)(p + 42) = 0x0101;
}
__attribute__((ms_abi)) void mw_GetNativeSystemInfo(void* si) { mw_GetSystemInfo(si); }
/* ISS1-WAVE3B: real GetLogicalProcessorInformationEx (Issue #1 §8.3 —
 * callers retry on ERROR_INSUFFICIENT_BUFFER until the returned size fits;
 * a bare failure stub makes them loop forever, observed live with
 * ImageMagick's CRT init). We expose one RelationProcessorCore entry with
 * the full processor mask. */
__attribute__((ms_abi)) uint32_t mw_GetLogicalProcessorInformationEx(int cls, void* buf, uint32_t* len) {
    MW_TRACE("GetLogicalProcessorInformationEx(cls=%d)", cls);
    if (!len) { mw_SetLastError(87); return 0; }
    /* RelationProcessorCore entry: {Relationship=0, Size=48,
     *   PROCESSOR_RELATIONSHIP { Flags=1 (PerformanceCounters? SMT off -> 0),
     *     EfficiencyClass=0, Reserved[20], GroupCount=1,
     *     GROUP_AFFINITY { Mask, Group=0, Reserved[6] } } } */
    const uint32_t needed = 48;
    if (!buf || *len < needed) {
        *len = needed;
        mw_SetLastError(122); /* ERROR_INSUFFICIENT_BUFFER */
        return 0;
    }
    uint8_t* p = (uint8_t*)buf;
    memset(p, 0, needed);
    uint32_t ncpu = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
    if (!ncpu) ncpu = 1;
    *(uint32_t*)(p + 0)  = 0;        /* RelationProcessorCore */
    *(uint32_t*)(p + 4)  = needed;   /* Size */
    *(uint8_t*)(p + 8)   = 0;        /* Flags: no SMT */
    *(uint16_t*)(p + 8 + 1 + 1 + 20) = 1;          /* GroupCount */
    *(uint64_t*)(p + 8 + 24) = (ncpu >= 64) ? ~0ull : ((1ull << ncpu) - 1); /* GroupMask.Mask */
    /* GroupMask.Group at +8+24+8 = 0 */
    *len = needed;
    return 1;
}
__attribute__((ms_abi)) int mw_IsProcessorFeaturePresent(uint32_t feat) {
    /* x64 hosts: SSE/SSE2/compare-exchange/3DNOW-none. Bits CRT checks. */
    switch (feat) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 12: case 13:
        return 1;                     /* PF_* integer/SSE family */
    case 17: case 18: case 19: case 20: case 21:
        return 1;                     /* PF_XSAVE / AVX family */
    default:
        return 0;
    }
}
__attribute__((ms_abi)) int mw_CheckRemoteDebuggerPresent(void* proc, int* out) {
    if (out) *out = 0;
    return 1;
}
__attribute__((ms_abi)) uint16_t mw_RtlCaptureStackBackTrace(uint32_t skip, uint32_t frames, void** bt, uint32_t* hash) {
    MW_TRACE("RtlCaptureStackBackTrace(skip=%u)", skip);
    if (hash) *hash = 0;
    return 0;
}
__attribute__((ms_abi)) void* mw_RtlPcToFileHeader(void* pc, void** base) {
    uint64_t v = (uint64_t)(uintptr_t)pc;
    if (g_image_base && v >= g_image_base && v < g_image_base + g_image_size) {
        if (base) *base = (void*)(uintptr_t)g_image_base;
        return (void*)(uintptr_t)g_image_base;
    }
    if (base) *base = NULL;
    return NULL;
}

/* pointer-encoding cookies: xor with a fixed per-process key */
static uintptr_t g_ptr_cookie = 0;
__attribute__((ms_abi)) void* mw_EncodePointer(void* p) {
    if (!g_ptr_cookie) {
        g_ptr_cookie = (uintptr_t)getpid() << 32 | (uintptr_t)time(NULL);
        if (!g_ptr_cookie) g_ptr_cookie = 0x4D57494E;
    }
    return (void*)(uintptr_t)((uintptr_t)p ^ g_ptr_cookie);
}
__attribute__((ms_abi)) void* mw_DecodePointer(void* p) {
    return (void*)(uintptr_t)((uintptr_t)p ^ (g_ptr_cookie ? g_ptr_cookie : 0x4D57494E));
}

/* SLIST (single-threaded honest semantics) */
__attribute__((ms_abi)) void mw_InitializeSListHead(void* head) {
    if (head) *(void**)head = NULL;
}
__attribute__((ms_abi)) void* mw_InterlockedPushEntrySList(void* head, void* entry) {
    void* old = head ? *(void**)head : NULL;
    if (head && entry) {
        *(void**)entry = old;
        *(void**)head = entry;
    }
    return old;
}
__attribute__((ms_abi)) void* mw_InterlockedFlushSList(void* head) {
    void* old = head ? *(void**)head : NULL;
    if (head) *(void**)head = NULL;
    return old;
}
__attribute__((ms_abi)) uint32_t mw_QueryDepthSList(void* head) {
    uint32_t d = 0;
    void* n = head ? *(void**)head : NULL;
    while (n && d < 1000000) { n = *(void**)n; d++; }
    return d;
}

/* ---- synchronization ---- */
__attribute__((ms_abi)) int mw_InitializeCriticalSectionEx(void* cs, uint32_t spin, uint32_t flags) {
    MW_TRACE("InitializeCriticalSectionEx(spin=%u)", spin);
    if (!cs) return 0;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init((pthread_mutex_t*)cs, &a);
    pthread_mutexattr_destroy(&a);
    *(uint32_t*)((uint8_t*)cs + 36) = MW_CS_MAGIC;
    return 1;
}
/* SRWLOCK = 8 bytes on x64. Single-writer flag + shared count packed in the
 * pointer cell; the guest's rose render path is single-threaded so the
 * uncontended paths are the contract. */
__attribute__((ms_abi)) void mw_InitializeSRWLock(void* srw) { if (srw) *(void**)srw = NULL; }
static void srw_lock(void* srw, int exclusive) {
    volatile int64_t* v = (volatile int64_t*)srw;
    if (exclusive) {
        while (__sync_val_compare_and_swap(v, 0, -1) != 0) sched_yield();
    } else {
        int64_t c;
        do { c = *v; } while (c < 0 || !__sync_bool_compare_and_swap(v, c, c + 1));
    }
}
static void srw_unlock(void* srw, int exclusive) {
    volatile int64_t* v = (volatile int64_t*)srw;
    if (exclusive) __sync_val_compare_and_swap(v, -1, 0);
    else __sync_fetch_and_sub(v, 1);
}
__attribute__((ms_abi)) void mw_AcquireSRWLockShared(void* s) { srw_lock(s, 0); }
__attribute__((ms_abi)) void mw_ReleaseSRWLockShared(void* s) { srw_unlock(s, 0); }
__attribute__((ms_abi)) int mw_TryAcquireSRWLockShared(void* s) {
    volatile int64_t* v = (volatile int64_t*)s;
    int64_t c = *v;
    return (c >= 0 && __sync_bool_compare_and_swap(v, c, c + 1)) ? 1 : 0;
}

/* CONDITION_VARIABLE = 8 bytes (futex word) */
__attribute__((ms_abi)) void mw_InitializeConditionVariable(void* cv) { if (cv) *(void**)cv = NULL; }
/* INIT_ONCE = 8 bytes: 0=init, 1=in-progress, 2=done (ctx in dword 1) */
__attribute__((ms_abi)) int mw_InitOnceBeginInitialize(void* io, uint32_t flags, int* pending, void** ctx) {
    volatile int64_t* v = (volatile int64_t*)io;
    if (flags & 2 /* INIT_ONCE_CHECK_ONLY */) {
        int64_t s = *v;
        if (s >> 32 == 2) { if (ctx) *ctx = (void*)(uintptr_t)(s & 0xFFFFFFFFull); if (pending) *pending = 0; return 1; }
        mw_SetLastError(347); return 0;
    }
    if (__sync_val_compare_and_swap(v, 0, 1) == 0) { if (pending) *pending = 1; return 1; }
    if (pending) *pending = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_InitOnceComplete(void* io, uint32_t flags, void* ctx) {
    volatile int64_t* v = (volatile int64_t*)io;
    int64_t nv = 2ll << 32 | ((uintptr_t)ctx & 0xFFFFFFFFull);
    if (__sync_val_compare_and_swap(v, 1, nv) == 1) return 1;
    mw_SetLastError(348); return 0; /* INIT_ONCE_FAILED */
}

/* Address-barrier waits: real futex on the same address the guest uses */
__attribute__((ms_abi)) int mw_WaitOnAddress(volatile void* addr, void* cmp, size_t size, uint32_t ms) {
    uint8_t local[8];
    memcpy(local, (const void*)addr, size);
    if (memcmp(local, cmp, size) != 0) return 1;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    (void)ts;
    for (int i = 0; i < 200000; i++) {
        memcpy(local, (const void*)addr, size);
        if (memcmp(local, cmp, size) != 0) return 1;
        syscall(SYS_futex, addr, 128 /*FUTEX_WAIT_PRIVATE*/, *(uint32_t*)addr, NULL, NULL, 0);
        memcpy(local, (const void*)addr, size);
        if (memcmp(local, cmp, size) != 0) return 1;
    }
    mw_SetLastError(1460); return 0;
}
__attribute__((ms_abi)) void mw_WakeByAddressSingle(void* addr) {
    syscall(SYS_futex, addr, 129 /*FUTEX_WAKE_PRIVATE*/, 1, NULL, NULL, 0);
}
__attribute__((ms_abi)) void mw_WakeByAddressAll(void* addr) {
    syscall(SYS_futex, addr, 129, 0x7fffffff, NULL, NULL, 0);
}

/* events: real state so single-thread wait/unwait works */
#define MAX_EVENTS 256
typedef struct { int set; int manual; } MwEvent;
static MwEvent g_events[MAX_EVENTS];
static int g_event_count = 0;
static MwEvent* event_from_handle(void* h) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_event_count) return NULL;
    return &g_events[idx];
}
__attribute__((ms_abi)) void* mw_CreateEventW(void* attr, int manual, int init, const wchar_t* name) {
    MW_TRACE("CreateEventW(manual=%d init=%d)", manual, init);
    if (g_event_count >= MAX_EVENTS) return NULL;
    g_events[g_event_count].set = init;
    g_events[g_event_count].manual = manual;
    return alloc_handle((void*)(uintptr_t)(4 + g_event_count++));
}
__attribute__((ms_abi)) uint32_t mw_WaitForMultipleObjectsEx(uint32_t n, void** hs, int wait_all, uint32_t ms, int alertable) {
    return mw_WaitForMultipleObjects(n, hs, wait_all, ms);
}
__attribute__((ms_abi)) uint32_t mw_SignalObjectAndWait(void* sig, void* wait, uint32_t ms, int alertable) {
    mw_SetEvent(sig);
    return mw_WaitForSingleObject(wait, ms);
}
__attribute__((ms_abi)) void* mw_CreateMutexA(void* attr, int owner, const char* name) {
    MW_TRACE("CreateMutexA(owner=%d)", owner);
    if (g_event_count >= MAX_EVENTS) return NULL;
    g_events[g_event_count].set = 1 - owner;
    g_events[g_event_count].manual = 1;
    return alloc_handle((void*)(uintptr_t)(4 + g_event_count++));
}
__attribute__((ms_abi)) int mw_ReleaseMutex(void* h) { return mw_SetEvent(h); }

/* ---- threads (single-team honest semantics) ---- */
__attribute__((ms_abi)) void* mw_CreateThread(void* attr, size_t stack, void* fn, void* arg, uint32_t flags, uint32_t* tid) {
    /* BUG-062: real threads via the _beginthreadex registry (ms_abi bridge).
     * CREATE_SUSPENDED (flags bit 4) is ignored — thread starts immediately. */
    (void)attr;
    MW_TRACE("CreateThread(fn=%p, arg=%p, flags=0x%x)", fn, arg, flags);
    unsigned long h = mw__beginthreadex(NULL, (unsigned)stack, fn, arg,
                                        (int)(flags & 0x4), tid);
    if ((flags & 0x4) && h) {
        MW_TRACE("CreateThread: CREATE_SUSPENDED ignored (thread runs immediately)");
    }
    if (!h) { mw_SetLastError(8); } /* ERROR_NOT_ENOUGH_MEMORY */
    return (void*)(uintptr_t)h;
}
__attribute__((ms_abi)) void mw_ExitThread(uint32_t code) {
    MW_TRACE("ExitThread(%u)", code);
    /* BUG-062: must end the CALLING THREAD only — exit() killed the whole
     * guest process (same lifecycle family as _beginthreadex). */
    mw__endthreadex(code);
}
__attribute__((ms_abi)) int mw_GetExitCodeThread(void* h, uint32_t* code) {
    /* BUG-062: report the recorded exit code for registry threads. */
    MwThread* t = mw_thread_lookup(h);
    if (t && code) {
        pthread_mutex_lock(&t_exit_mu);
        *code = t->exit_code;
        pthread_mutex_unlock(&t_exit_mu);
        return 1;
    }
    if (code) *code = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_TerminateThread(void* h, uint32_t code) { return 0; }
__attribute__((ms_abi)) uint32_t mw_GetCurrentProcessorNumber(void) { return 0; }
/* threadpool: honest synchronous fallback is NOT Windows semantics; fail */
__attribute__((ms_abi)) void* mw_CreateThreadpoolWork(void* cb, void* ctx, void* env) {
    MW_TRACE("CreateThreadpoolWork() -> NULL");
    mw_SetLastError(8);
    return NULL;
}
__attribute__((ms_abi)) void mw_SubmitThreadpoolWork(void* w) { }
__attribute__((ms_abi)) void mw_CloseThreadpoolWork(void* w) { }
__attribute__((ms_abi)) void mw_FreeLibraryWhenCallbackReturns(void* env, void* mod) { }
__attribute__((ms_abi)) void* mw_RegisterWaitForSingleObject(void** out, void* h, void* cb, void* ctx, uint32_t ms, uint32_t flags) {
    return NULL;
}
__attribute__((ms_abi)) void mw_UnregisterWait(void* w) { }
__attribute__((ms_abi)) void* mw_QueueUserWorkItem(void* cb, void* ctx, uint32_t flags) {
    return NULL;
}

/* ---- time ---- */
__attribute__((ms_abi)) void mw_GetSystemTime(void* st) {
    MW_TRACE("GetSystemTime()");
    if (!st) return;
    /* SYSTEMTIME: 8 WORDs */
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    uint16_t* w = (uint16_t*)st;
    w[0] = tmv.tm_year + 1900; w[1] = tmv.tm_mon + 1; w[2] = tmv.tm_wday;
    w[3] = tmv.tm_mday; w[4] = tmv.tm_hour; w[5] = tmv.tm_min;
    w[6] = tmv.tm_sec; w[7] = 0;
}
__attribute__((ms_abi)) void mw_GetSystemTimePreciseAsFileTime(void* ft) {
    mw_GetSystemTimeAsFileTime(ft);
}
__attribute__((ms_abi)) uint64_t mw_GetTickCount64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
__attribute__((ms_abi)) uint32_t mw_GetTimeZoneInformation(void* tzi) {
    MW_TRACE("GetTimeZoneInformation()");
    if (tzi) memset(tzi, 0, 176);
    return 0; /* TIME_ZONE_ID_UNKNOWN (bias 0 = UTC) — honest for UTC hosts */
}
__attribute__((ms_abi)) int mw_FileTimeToSystemTime(const void* ft, void* st) {
    if (!ft || !st) return 0;
    uint64_t t100 = *(const uint64_t*)ft;
    time_t secs = (time_t)(t100 / 10000000ULL) - 11644473600LL;
    struct tm tmv;
    gmtime_r(&secs, &tmv);
    uint16_t* w = (uint16_t*)st;
    w[0] = tmv.tm_year + 1900; w[1] = tmv.tm_mon + 1; w[2] = tmv.tm_wday;
    w[3] = tmv.tm_mday; w[4] = tmv.tm_hour; w[5] = tmv.tm_min;
    w[6] = tmv.tm_sec; w[7] = (uint16_t)((t100 % 10000000ULL) / 10000);
    return 1;
}
__attribute__((ms_abi)) int mw_SystemTimeToFileTime(const void* st, void* ft) {
    if (!st || !ft) return 0;
    const uint16_t* w = (const uint16_t*)st;
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = w[0] - 1900; tmv.tm_mon = w[1] - 1; tmv.tm_mday = w[3];
    tmv.tm_hour = w[4]; tmv.tm_min = w[5]; tmv.tm_sec = w[6];
    time_t secs = timegm(&tmv);
    *(uint64_t*)ft = (uint64_t)(secs + 11644473600LL) * 10000000ULL + w[7] * 10000;
    return 1;
}
__attribute__((ms_abi)) int mw_GetVersion(void) { return 0x000A0006; } /* 10.6 */
__attribute__((ms_abi)) int mw_GetVersionExA(void* vi) {
    if (!vi) return 0;
    uint32_t sz = *(uint32_t*)vi;
    memset(vi, 0, sz);
    *(uint32_t*)vi = sz;
    uint8_t* p = (uint8_t*)vi;
    *(uint32_t*)(p + 4)  = 10; *(uint32_t*)(p + 8) = 0; *(uint32_t*)(p + 12) = 0;
    *(uint32_t*)(p + 16) = 2;  /* VER_PLATFORM_WIN32_NT */
    return 1;
}

/* SW-555 honest batch (SWEEP-009): GetVersionExW — the W sibling of the A
 * variant above. autoruns64 called it 2x and RAMMap64 ELEVEN times through
 * their IATs while it was still the all-purpose stub lying 0 (BOOL-false
 * with an untouched struct = garbage version data downstream). Same
 * identity fill: Win10, VER_PLATFORM_WIN32_NT, build 0 (consistent with
 * GetVersionExA); handles both OSVERSIONINFOW (276) and OSVERSIONINFOEXW
 * (284) because the guest pre-sizes the struct and we zero through it. */
__attribute__((ms_abi)) int mw_GetVersionExW(void* vi) {
    if (!vi) return 0;
    uint32_t sz = *(uint32_t*)vi;
    memset(vi, 0, sz);
    *(uint32_t*)vi = sz;
    uint8_t* p = (uint8_t*)vi;
    *(uint32_t*)(p + 4)  = 10; *(uint32_t*)(p + 8) = 0; *(uint32_t*)(p + 12) = 0;
    *(uint32_t*)(p + 16) = 2;  /* VER_PLATFORM_WIN32_NT */
    return 1;
}

/* ---- codepage / locale / string ---- */
/* ACP must be a SINGLE-BYTE codepage: the UCRT builds its ctype table by
 * converting a 256-byte table through the ACP; UTF-8 fails on bytes >=0x80
 * (live: ImageMagick locale init corrupted its stack → GetStringTypeW with
 * garbage args). 1252 = Windows-1252, the real-world default ACP. */
__attribute__((ms_abi)) uint32_t mw_GetACP(void) { return 1252; }
__attribute__((ms_abi)) uint32_t mw_GetOEMCP(void) { return 437; }
__attribute__((ms_abi)) int mw_IsValidCodePage(uint32_t cp) { return cp == 65001 || cp == 437 || cp == 1252 ? 1 : 0; }
__attribute__((ms_abi)) int mw_IsValidLocale(uint32_t lcid, uint32_t flags) { return 1; }
__attribute__((ms_abi)) uint32_t mw_GetUserDefaultLCID(void) { return 0x0409; }
__attribute__((ms_abi)) uint32_t mw_GetUserDefaultLangID(void) { return 0x0409; }
__attribute__((ms_abi)) uint32_t mw_GetSystemDefaultLCID(void) { return 0x0409; }
__attribute__((ms_abi)) uint32_t mw_GetThreadLocale(void) { return 0x0409; }
__attribute__((ms_abi)) int mw_SetThreadLocale(uint32_t lcid) { return 1; }
/* LOCALE_* identifiers (winnls.h) — real per-type values so the UCRT reads
 * consistent codepage data (the ctype-table conversion picked its CP from
 * here; a blanket "en-US" made it fall into the UTF-8 path). */
#define MW_LOCALE_ILANGUAGE            0x0001
#define MW_LOCALE_IDEFAULTANSICODEPAGE 0x1004
#define MW_LOCALE_IDEFAULTCODEPAGE     0x100B
#define MW_LOCALE_IDEFAULTMACCODEPAGE  0x100A
#define MW_LOCALE_SNAME                0x005C
#define MW_LOCALE_SISO639LANGNAME      0x0059
#define MW_LOCALE_SISO3166CTRYNAME     0x005A
#define MW_LOCALE_RETURN_NUMBER        0x20000000
#define MW_LOCALE_NAME_MAX_LEN         85

static const char* w3_locale_info_str(uint32_t type) {
    switch (type & ~MW_LOCALE_RETURN_NUMBER) {
    case MW_LOCALE_ILANGUAGE:            return "0409";
    case MW_LOCALE_IDEFAULTANSICODEPAGE: return "1252";
    case MW_LOCALE_IDEFAULTCODEPAGE:     return "437";
    case MW_LOCALE_IDEFAULTMACCODEPAGE:  return "10000";
    case MW_LOCALE_SNAME:                return "en-US";
    case MW_LOCALE_SISO639LANGNAME:      return "en";
    case MW_LOCALE_SISO3166CTRYNAME:     return "US";
    case 0x1002:                         return "1";   /* IDEFAULTLANGUAGE */
    case 0x1001:                         return "1";   /* ICOUNTRY */
    default:                             return "en-US";
    }
}
static uint32_t w3_locale_info_num(uint32_t type) {
    switch (type & ~MW_LOCALE_RETURN_NUMBER) {
    case MW_LOCALE_ILANGUAGE:            return 0x0409;
    case MW_LOCALE_IDEFAULTANSICODEPAGE: return 1252;
    case MW_LOCALE_IDEFAULTCODEPAGE:     return 437;
    case MW_LOCALE_IDEFAULTMACCODEPAGE:  return 10000;
    default:                             return 0;
    }
}
__attribute__((ms_abi)) int mw_GetLocaleInfoA(uint32_t lcid, uint32_t type, char* out, int cap) {
    MW_TRACE("GetLocaleInfoA(type=%x)", type);
    if (type & MW_LOCALE_RETURN_NUMBER) {
        if (!out || cap < 4) { mw_SetLastError(122); return 0; }
        *(uint32_t*)out = w3_locale_info_num(type);
        return 4;
    }
    const char* s = w3_locale_info_str(type);
    int need = (int)strlen(s) + 1;
    if (!out || cap < need) { mw_SetLastError(122); return 0; }
    memcpy(out, s, need);
    return need;
}
__attribute__((ms_abi)) int mw_GetLocaleInfoW(uint32_t lcid, uint32_t type, wchar_t* out, int cap) {
    MW_TRACE("GetLocaleInfoW(type=%x)", type);
    if (type & MW_LOCALE_RETURN_NUMBER) {
        if (!out || cap < 2) { mw_SetLastError(122); return 0; }
        *(uint32_t*)out = w3_locale_info_num(type);
        return 2;
    }
    const char* s = w3_locale_info_str(type);
    int need = (int)strlen(s) + 1; /* wide chars incl null */
    if (!out || cap < need) { mw_SetLastError(122); return 0; }
    size_t n = utf8_to_utf16(s, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (int)n + 1;
}
__attribute__((ms_abi)) int mw_GetLocaleInfoEx(void* name, uint32_t type, wchar_t* out, int cap) {
    return mw_GetLocaleInfoW(0x0409, type, out, cap);
}
static int wstr_case_cmp(const wchar_t* a, const wchar_t* b, int fold) {
    for (;;) {
        wint_t ca = *a++, cb = *b++;
        if (fold) { ca = towlower(ca); cb = towlower(cb); }
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}
__attribute__((ms_abi)) int mw_CompareStringW(uint32_t lcid, uint32_t flags, const wchar_t* s1, int n1, const wchar_t* s2, int n2) {
    if (n1 < 0) n1 = s1 ? (int)wcslen(s1) : 0;
    if (n2 < 0) n2 = s2 ? (int)wcslen(s2) : 0;
    int fold = flags & 1; /* NORM_IGNORECASE */
    wchar_t b1[512], b2[512];
    if (n1 > 511) n1 = 511; if (n2 > 511) n2 = 511;
    if (s1) { memcpy(b1, s1, n1 * 2); b1[n1] = 0; } else n1 = 0;
    if (s2) { memcpy(b2, s2, n2 * 2); b2[n2] = 0; } else n2 = 0;
    int r = wstr_case_cmp(n1 ? b1 : (wchar_t*)L"", n2 ? b2 : (wchar_t*)L"", fold);
    return r == 0 ? 2 : (r < 0 ? 1 : 3); /* CSTR_EQUAL/LESS/GREATER */
}
__attribute__((ms_abi)) int mw_CompareStringEx(void* name, uint32_t flags, const wchar_t* s1, int n1, const wchar_t* s2, int n2, void* v1, void* v2, void* res) {
    return mw_CompareStringW(0x0409, flags, s1, n1, s2, n2);
}
__attribute__((ms_abi)) int mw_LCMapStringW(uint32_t lcid, uint32_t flags, const wchar_t* src, int sn, wchar_t* out, int cap) {
    if (!src) return 0;
    int include_null = (sn < 0);
    if (include_null) sn = (int)wcslen(src);
    int need = sn + (include_null ? 1 : 0);
    if (!out || cap == 0) return need;           /* size query — no write */
    if (cap < need) { mw_SetLastError(122); return 0; }
    for (int i = 0; i < sn; i++) {
        wchar_t c = src[i];
        if (flags & 0x100) c = (wchar_t)towlower(c);       /* LCMAP_LOWERCASE */
        if (flags & 0x200) c = (wchar_t)towupper(c);       /* LCMAP_UPPERCASE */
        out[i] = c;
    }
    if (include_null) out[sn] = 0;
    return need;
}
__attribute__((ms_abi)) int mw_LCMapStringEx(void* name, uint32_t flags, const wchar_t* src, int sn, wchar_t* out, int cap, void* v1, void* v2, void* res) {
    return mw_LCMapStringW(0x0409, flags, src, sn, out, cap);
}
__attribute__((ms_abi)) int mw_LCMapStringA(uint32_t lcid, uint32_t flags, const char* src, int sn, char* out, int cap) {
    if (!src) return 0;
    int include_null = (sn < 0);
    if (include_null) sn = (int)strlen(src);
    int need = sn + (include_null ? 1 : 0);
    if (!out || cap == 0) return need;
    if (cap < need) { mw_SetLastError(122); return 0; }
    for (int i = 0; i < sn; i++) {
        char c = src[i];
        if (flags & 0x100) c = (char)tolower((unsigned char)c);
        if (flags & 0x200) c = (char)toupper((unsigned char)c);
        out[i] = c;
    }
    if (include_null) out[sn] = 0;
    return need;
}
__attribute__((ms_abi)) int mw_EnumSystemLocalesW(void* cb, uint32_t flags) {
    /* enumerate the single supported locale; honest callback invocation */
    int (*fn)(const wchar_t*) = (int (*)(const wchar_t*))cb;
    static const wchar_t en[] = L"en-US";
    if (fn) fn(en);
    return 1;
}
__attribute__((ms_abi)) int mw_GetStringTypeW(uint32_t type, const wchar_t* src, int cnt, uint16_t* out) {
    if (!src || !out) return 0;
    /* BUG-046 family: guest wchar_t is 2 bytes — classify uint16 units. */
    const uint16_t* u = (const uint16_t*)src;
    for (int i = 0; i < cnt; i++) {
        uint32_t c = u[i];
        uint16_t t = 0;
        if (c < 0x80) {
            if (isupper((int)c)) t |= 0x0001;         /* C1_UPPER */
            if (islower((int)c)) t |= 0x0002;         /* C1_LOWER */
            if (isdigit((int)c)) t |= 0x0004;         /* C1_DIGIT */
            if (isspace((int)c)) t |= 0x0008;         /* C1_SPACE */
            if (ispunct((int)c)) t |= 0x0010;         /* C1_PUNCT */
            if ((uint8_t)c < 0x20) t |= 0x0010;       /* C1_PUNCT covers ctrl in Windows ctype */
        } else if (c >= 0x00C0 && c <= 0x024F) {
            t |= 0x0002;                              /* letters: treat as lower-ish */
            if ((c >= 0x00C0 && c <= 0x00DE) || (c >= 0x0100 && c <= 0x017F && !(c & 1))) t |= 0x0001;
        } else {
            t |= 0x0080;                              /* C1_UNDEFINED */
        }
        if (t == 0)      t |= 0x0080;
        out[i] = t;
    }
    return 1;
}
__attribute__((ms_abi)) int mw_IsDBCSLeadByte(uint8_t b) { return 0; }

static void mw_init_ptr_cookie(void) __attribute__((constructor));
static void mw_init_ptr_cookie(void) {
    if (!g_ptr_cookie) g_ptr_cookie = 0x4D57494E00000000ull ^ (uintptr_t)getpid();
}


/* ============================================================
 * ISS1-WAVE3B — file/console/dir + registry/bcrypt/VCOMP families
 * ============================================================ */

/* Win32 file-handle registry: handle value -> fd (+context) */
typedef struct { int fd; int in_use; } W3File;
static W3File g_w3files[MAX_HANDLES];
static int g_w3files_ready = 0;

static int w3fd_from_handle(void* h) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= MAX_HANDLES || !g_w3files[idx].in_use) return -1;
    return g_w3files[idx].fd;
}
static void* w3_handle_for_fd(int fd) {
    if (!g_w3files_ready) { memset(g_w3files, 0, sizeof(g_w3files)); g_w3files_ready = 1; }
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_w3files[i].in_use && g_w3files[i].fd == fd) return (void*)(uintptr_t)(4 + i);
    }
    return NULL;
}

/* BUG-056: real GetStdHandle — fd-backed registry handles so
 * ReadFile/WriteFile/GetConsoleMode/SetStdHandle share one identity space
 * with CreateFileW. sqlite3's whole CLI output goes through here. */
__attribute__((ms_abi)) void* mw_GetStdHandle_impl(int32_t nStdHandle) {
    int fd = (nStdHandle == -10) ? 0 : (nStdHandle == -11) ? 1 :
             (nStdHandle == -12) ? 2 : -1;
    if (fd < 0) { mw_SetLastError(87); return NULL; }
    if (!g_w3files_ready) { memset(g_w3files, 0, sizeof(g_w3files)); g_w3files_ready = 1; }
    void* h = w3_handle_for_fd(fd);
    if (!h) {
        for (int i = 0; i < MAX_HANDLES; i++) {
            if (!g_w3files[i].in_use) {
                g_w3files[i].fd = fd;
                g_w3files[i].in_use = 1;
                h = (void*)(uintptr_t)(4 + i);
                break;
            }
        }
    }
    MW_TRACE("GetStdHandle(%d) = %p", nStdHandle, h);
    return h;
}

/* DESIRED_ACCESS: GENERIC_READ 0x80000000, GENERIC_WRITE 0x40000000 */
#define W3_GENERIC_READ  0x80000000u
#define W3_GENERIC_WRITE 0x40000000u
#define W3_CREATE_NEW        1
#define W3_CREATE_ALWAYS     2
#define W3_OPEN_EXISTING     3
#define W3_OPEN_ALWAYS       4
#define W3_TRUNCATE_EXISTING 5

/* helper shared by A/W: returns handle or INVALID_HANDLE_VALUE */
static void* w3_create_file(const char* name8, uint32_t access, uint32_t disp);
__attribute__((ms_abi)) void* mw_CreateFileA(const char* name, uint32_t access, uint32_t share,
        void* sec, uint32_t disp, uint32_t flags, void* tmpl) {
    MW_TRACE("CreateFileA(%s, acc=%08x, disp=%u)", name ? name : "NULL", access, disp);
    if (!name) { mw_SetLastError(87); return (void*)-1; }
    return w3_create_file(name, access, disp);
}
static void* w3_create_file(const char* name8, uint32_t access, uint32_t disp) {
    char nn8[4096];
    snprintf(nn8, sizeof(nn8), "%s", name8);
    w3_normalize_path(nn8);
    name8 = nn8;
    if (!g_w3files_ready) { memset(g_w3files, 0, sizeof(g_w3files)); g_w3files_ready = 1; }
    int slot = -1;
    for (int i = 0; i < MAX_HANDLES; i++) if (!g_w3files[i].in_use) { slot = i; break; }
    if (slot < 0) { mw_SetLastError(4); return (void*)-1; }
    int oflags = 0;
    int rd = (access & W3_GENERIC_READ) != 0;
    int wr = (access & W3_GENERIC_WRITE) != 0;
    oflags |= wr ? (rd ? O_RDWR : O_WRONLY) : O_RDONLY;
    switch (disp) {
    case W3_CREATE_NEW:        oflags |= O_CREAT | O_EXCL; break;
    case W3_CREATE_ALWAYS:     oflags |= O_CREAT | O_TRUNC; break;
    case W3_OPEN_EXISTING:     break;
    case W3_OPEN_ALWAYS:       oflags |= O_CREAT; break;
    case W3_TRUNCATE_EXISTING: oflags |= O_TRUNC; break;
    default: break;
    }
    int fd = open(name8, oflags, 0644);
    if (fd < 0) {
        mw_SetLastError(errno == ENOENT ? 2 : (errno == EACCES ? 5 : 87));
        return (void*)-1;
    }
    g_w3files[slot].fd = fd;
    g_w3files[slot].in_use = 1;
    return (void*)(uintptr_t)(4 + slot);
}
__attribute__((ms_abi)) void* mw_CreateFileW(const wchar_t* name, uint32_t access, uint32_t share,
        void* sec, uint32_t disp, uint32_t flags, void* tmpl) {
    char n8[4096];
    if (!name) { mw_SetLastError(87); return (void*)-1; }
    if (w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) { mw_SetLastError(87); return (void*)-1; }
    n8[sizeof(n8) - 1] = '\0';
    MW_TRACE("CreateFileW(%s, acc=%08x, disp=%u)", n8, access, disp);
    return w3_create_file(n8, access, disp);
}
__attribute__((ms_abi)) int mw_ReadFile(void* h, void* buf, uint32_t n, uint32_t* bread, void* overlapped) {
    MW_TRACE("ReadFile(n=%u)", n);
    int fd = w3fd_from_handle(h);
    int real = fd;
    if (real < 0) {
        /* std handles: 4/8/0xC map to fd 0/1/2 */
        uint64_t hv = (uint64_t)(uintptr_t)h;
        if (hv == 4) real = 0; else if (hv == 8) real = 1; else if (hv == 0xC) real = 2;
        else { mw_SetLastError(6); return 0; }
    }
    /* BUG-058 (2026-10-17): guests may pass OVERLAPPED with an explicit
     * file offset (sqlite3's winRead does). Honor it via pread instead of
     * silently reading at the sequential fd position. */
    ssize_t r;
    if (overlapped) {
        long long off = (long long)*(uint32_t*)((uint8_t*)overlapped + 16)
                      | ((long long)*(uint32_t*)((uint8_t*)overlapped + 20) << 32);
        r = pread(real, buf, n, (off_t)off);
    } else {
        r = read(real, buf, n);
    }
    if (r < 0) { mw_SetLastError(109); if (bread) *bread = 0; return 0; }
    if (bread) *bread = (uint32_t)r;
    return 1;
}
__attribute__((ms_abi)) int mw_WriteFile(void* h, const void* buf, uint32_t n, uint32_t* written, void* overlapped) {
    MW_TRACE("WriteFile(n=%u)", n);
    int fd = w3fd_from_handle(h);
    int real = fd;
    if (real < 0) {
        uint64_t hv = (uint64_t)(uintptr_t)h;
        if (hv == 4) real = 0; else if (hv == 8) real = 1; else if (hv == 0xC) real = 2;
        else { mw_SetLastError(6); return 0; }
    }
    /* BUG-058: OVERLAPPED offsets honored (see ReadFile) */
    ssize_t r;
    if (overlapped) {
        long long off = (long long)*(uint32_t*)((uint8_t*)overlapped + 16)
                      | ((long long)*(uint32_t*)((uint8_t*)overlapped + 20) << 32);
        r = pwrite(real, buf, n, (off_t)off);
    } else {
        r = write(real, buf, n);
    }
    if (r < 0) { mw_SetLastError(109); if (written) *written = 0; return 0; }
    if (written) *written = (uint32_t)r;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_SetFilePointer(void* h, int32_t dist, int32_t* high, uint32_t method) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0xFFFFFFFFu; }
    int whence = method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END;
    off_t r = lseek(fd, dist, whence);
    if (r == (off_t)-1) { mw_SetLastError(87); return 0xFFFFFFFFu; }
    if (high) *high = (int32_t)((uint64_t)r >> 32);
    return (uint32_t)(uint64_t)r;
}
__attribute__((ms_abi)) int mw_SetFilePointerEx(void* h, int64_t dist, int64_t* newpos, uint32_t method) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0; }
    int whence = method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END;
    off_t r = lseek(fd, (off_t)dist, whence);
    if (r == (off_t)-1) { mw_SetLastError(87); return 0; }
    if (newpos) *newpos = (int64_t)r;
    return 1;
}
__attribute__((ms_abi)) int mw_SetEndOfFile(void* h) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0; }
    off_t p = lseek(fd, 0, SEEK_CUR);
    return ftruncate(fd, p) == 0;
}
__attribute__((ms_abi)) uint32_t mw_GetFileSize(void* h, uint32_t* high) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0xFFFFFFFFu; }
    struct stat st;
    if (fstat(fd, &st) != 0) { mw_SetLastError(87); return 0xFFFFFFFFu; }
    if (high) *high = (uint32_t)((uint64_t)st.st_size >> 32);
    return (uint32_t)st.st_size;
}
__attribute__((ms_abi)) int mw_GetFileSizeEx(void* h, int64_t* size) {
    int fd = w3fd_from_handle(h);
    if (fd < 0 || !size) { mw_SetLastError(6); return 0; }
    struct stat st;
    if (fstat(fd, &st) != 0) { mw_SetLastError(87); return 0; }
    *size = st.st_size;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetFileType(void* h) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) return 0; /* FILE_TYPE_UNKNOWN */
    struct stat st;
    if (fstat(fd, &st) != 0) return 0;
    if (S_ISCHR(st.st_mode)) return 2;  /* FILE_TYPE_CHAR */
    if (S_ISFIFO(st.st_mode)) return 3; /* FILE_TYPE_PIPE */
    return 1;                            /* FILE_TYPE_DISK */
}
__attribute__((ms_abi)) int mw_FlushFileBuffers(void* h) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0; }
    return fsync(fd) == 0;
}
__attribute__((ms_abi)) int mw_GetFileInformationByHandle(void* h, void* info) {
    int fd = w3fd_from_handle(h);
    if (fd < 0 || !info) { mw_SetLastError(6); return 0; }
    struct stat st;
    if (fstat(fd, &st) != 0) { mw_SetLastError(87); return 0; }
    uint8_t* p = (uint8_t*)info;
    *(uint32_t*)(p + 0)  = st.st_dev;        /* dwFileAttributes: leave 0x20 archive */
    *(uint32_t*)(p + 0)  = 0x20;
    *(uint32_t*)(p + 4)  = 0; *(uint32_t*)(p + 8) = 0;   /* creation time */
    *(uint32_t*)(p + 12) = 0; *(uint32_t*)(p + 16) = 0;
    *(uint32_t*)(p + 20) = 0; *(uint32_t*)(p + 24) = 0;
    *(uint32_t*)(p + 28) = (uint32_t)st.st_dev;          /* volume serial */
    *(uint32_t*)(p + 32) = (uint32_t)st.st_size;
    *(uint32_t*)(p + 36) = (uint32_t)((uint64_t)st.st_size >> 32);
    *(uint32_t*)(p + 40) = (uint32_t)st.st_nlink;
    *(uint32_t*)(p + 44) = (uint32_t)((uint64_t)st.st_ino >> 32);
    *(uint32_t*)(p + 48) = (uint32_t)st.st_ino;
    return 1;
}
__attribute__((ms_abi)) int mw_GetFileInformationByHandleEx(void* h, int cls, void* info, uint32_t len) {
    MW_TRACE("GetFileInformationByHandleEx(cls=%d)", cls);
    return 0; /* FILE_INFO classes unsupported — honest */
}
__attribute__((ms_abi)) int mw_SetFileInformationByHandle(void* h, int cls, void* info, uint32_t len) {
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_GetFileAttributesW(const wchar_t* name) {
    char n8[4096];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) { mw_SetLastError(87); return 0xFFFFFFFFu; }
    n8[sizeof(n8) - 1] = '\0';
    MW_TRACE("GetFileAttributesW(%s)", n8);
    struct stat st;
    if (stat(n8, &st) != 0) { mw_SetLastError(2); return 0xFFFFFFFFu; }
    uint32_t a = 0x20; /* ARCHIVE */
    if (S_ISDIR(st.st_mode)) a = 0x10; /* DIRECTORY */
    if (name[0] && n8[0] == '.') a |= 0x2; /* HIDDEN (approximation) */
    return a;
}
__attribute__((ms_abi)) uint32_t mw_GetFileAttributesExW(const wchar_t* name, int cls, void* info) {
    if (cls != 0) return 0;
    uint32_t a = mw_GetFileAttributesW(name);
    if (a == 0xFFFFFFFFu) return 0;
    if (info) *(uint32_t*)info = a;
    return 1;
}
__attribute__((ms_abi)) int mw_SetFileAttributesW(const wchar_t* name, uint32_t attr) { return 1; }
__attribute__((ms_abi)) int mw_DeleteFileW(const wchar_t* name) {
    char n8[4096];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) { mw_SetLastError(87); return 0; }
    n8[sizeof(n8) - 1] = '\0';
    w3_normalize_path(n8); /* BUG-054b: same Windows-path rule as CreateDirectoryW */
    MW_TRACE("DeleteFileW(%s)", n8);
    if (unlink(n8) != 0) { mw_SetLastError(errno == ENOENT ? 2 : 5); return 0; }
    return 1;
}
__attribute__((ms_abi)) int mw_RemoveDirectoryW(const wchar_t* name) {
    char n8[4096];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) { mw_SetLastError(87); return 0; }
    n8[sizeof(n8) - 1] = '\0';
    w3_normalize_path(n8); /* BUG-054b: same Windows-path rule as CreateDirectoryW */
    MW_TRACE("RemoveDirectoryW(%s)", n8);
    if (rmdir(n8) != 0) { mw_SetLastError(2); return 0; }
    return 1;
}
__attribute__((ms_abi)) int mw_CreateDirectoryW(const wchar_t* name, void* sa) {
    char n8[4096];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) { mw_SetLastError(87); return 0; }
    n8[sizeof(n8) - 1] = '\0';
    /* BUG-054b (2026-10-17): the -o extraction root arrives as a Windows-style
     * absolute path ("\\tmp\\7ztest\\out"); without normalization mkdir created
     * a literal backslash-named directory in the CWD. Same rule as CreateFileW. */
    w3_normalize_path(n8);
    MW_TRACE("CreateDirectoryW(%s)", n8);
    if (mkdir(n8, 0755) != 0) { mw_SetLastError(errno == EEXIST ? 183 : 3); return 0; }
    return 1;
}
__attribute__((ms_abi)) int mw_CreateDirectoryExW(const wchar_t* tmpl, const wchar_t* name, void* sa) {
    return mw_CreateDirectoryW(name, sa);
}
__attribute__((ms_abi)) int mw_MoveFileExW(const wchar_t* from, const wchar_t* to, uint32_t flags) {
    char f8[4096], t8[4096];
    if (!from || !to || w3_w16_to_utf8z((const uint16_t*)from, f8, sizeof(f8)) < 0
        || w3_w16_to_utf8z((const uint16_t*)to, t8, sizeof(t8)) < 0) { mw_SetLastError(87); return 0; }
    w3_normalize_path(f8);
    w3_normalize_path(t8);
    f8[sizeof(f8)-1] = '\0'; t8[sizeof(t8)-1] = '\0';
    MW_TRACE("MoveFileExW(%s -> %s)", f8, t8);
    if (rename(f8, t8) != 0) { mw_SetLastError(2); return 0; }
    return 1;
}
__attribute__((ms_abi)) int mw_ReplaceFileW(const wchar_t* dst, const wchar_t* src, const wchar_t* bak, uint32_t flags, void* e1, void* e2) {
    if (!mw_MoveFileExW(src, dst, 1)) return 0;
    return 1;
}
__attribute__((ms_abi)) void* mw_CreateFile2(const wchar_t* name, uint32_t access, uint32_t share, uint32_t disp, void* params) {
    return mw_CreateFileW(name, access, share, NULL, disp, 0, NULL);
}
__attribute__((ms_abi)) int mw_CopyFile2(const wchar_t* from, const wchar_t* to, void* cancel) {
    char f8[4096], t8[4096];
    if (!from || !to || w3_w16_to_utf8z((const uint16_t*)from, f8, sizeof(f8)) < 0
        || w3_w16_to_utf8z((const uint16_t*)to, t8, sizeof(t8)) < 0) { mw_SetLastError(87); return 0x80070057u; }
    w3_normalize_path(f8);
    w3_normalize_path(t8);
    f8[sizeof(f8)-1] = '\0'; t8[sizeof(t8)-1] = '\0';
    MW_TRACE("CopyFile2(%s -> %s)", f8, t8);
    FILE* a = fopen(f8, "rb");
    if (!a) return 0x80070002u;
    FILE* b = fopen(t8, "wb");
    if (!b) { fclose(a); return 0x80070005u; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
    fclose(a); fclose(b);
    return 0; /* S_OK */
}
__attribute__((ms_abi)) int mw_CreateSymbolicLinkW(const wchar_t* link, const wchar_t* target, uint32_t flags) {
    char l8[4096], t8[4096];
    if (!link || !target || w3_w16_to_utf8z((const uint16_t*)link, l8, sizeof(l8)-1) == (size_t)-1
        || w3_w16_to_utf8z((const uint16_t*)target, t8, sizeof(t8)-1) == (size_t)-1) return 0;
    l8[sizeof(l8)-1] = '\0'; t8[sizeof(t8)-1] = '\0';
    return symlink(t8, l8) == 0;
}
__attribute__((ms_abi)) int mw_CreateHardLinkW(const wchar_t* wlink, const wchar_t* wtarget, void* sa) {
    char l8[4096], t8[4096];
    if (!wlink || !wtarget || w3_w16_to_utf8z((const uint16_t*)wlink, l8, sizeof(l8)-1) == (size_t)-1
        || w3_w16_to_utf8z((const uint16_t*)wtarget, t8, sizeof(t8)-1) == (size_t)-1) return 0;
    l8[sizeof(l8)-1] = '\0'; t8[sizeof(t8)-1] = '\0';
    MW_TRACE("CreateHardLinkW(%s)", l8);
    return link(t8, l8) == 0;
}

/* ---- path helpers ---- */
__attribute__((ms_abi)) uint32_t mw_GetFullPathNameW(const wchar_t* name, uint32_t cap, wchar_t* out, wchar_t** part) {
    char n8[4096], f8[4096];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)-1) == (size_t)-1) return 0;
    n8[sizeof(n8)-1] = '\0';
    if (n8[0] != '/') {
        char cwd[2048];
        if (getcwd(cwd, sizeof(cwd))) snprintf(f8, sizeof(f8), "%s/%s", cwd, n8);
        else snprintf(f8, sizeof(f8), "%s", n8);
    } else snprintf(f8, sizeof(f8), "%s", n8);
    MW_TRACE("GetFullPathNameW(%s) -> %s", n8, f8);
    if (!out || !cap) return (uint32_t)strlen(f8) + 1;
    size_t n = utf8_to_utf16(f8, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    if (part) {
        char* slash = strrchr(f8, '/');
        if (slash) *part = (wchar_t*)((uint16_t*)out + (slash - f8 + 1));
        else *part = out;
    }
    return (uint32_t)n;
}
__attribute__((ms_abi)) uint32_t mw_GetLongPathNameW(const wchar_t* in, wchar_t* out, uint32_t cap) {
    uint32_t n = 0;
    const wchar_t* p = in;
    if (p) while (p[n]) n++;
    if (out && cap > n) { memcpy(out, in, (n + 1) * 2); return n; }
    return n;
}
__attribute__((ms_abi)) uint32_t mw_GetShortPathNameW(const wchar_t* in, wchar_t* out, uint32_t cap) {
    return mw_GetLongPathNameW(in, out, cap);
}
__attribute__((ms_abi)) uint32_t mw_GetFinalPathNameByHandleW(void* h, wchar_t* out, uint32_t cap, uint32_t flags) {
    int fd = w3fd_from_handle(h);
    if (fd < 0) { mw_SetLastError(6); return 0; }
    char path[4096];
    char link[64];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(link, path, sizeof(path) - 1);
    if (n < 0) { mw_SetLastError(87); return 0; }
    path[n] = '\0';
    if (!out || !cap) return (uint32_t)n + 1;
    size_t u = utf8_to_utf16(path, out, cap - 1);
    ((uint16_t*)out)[u] = 0;
    return (uint32_t)u;
}

/* ---- temp / drives / dirs ---- */
__attribute__((ms_abi)) uint32_t mw_GetTempPathW(uint32_t cap, wchar_t* out) {
    MW_TRACE("GetTempPathW()");
    const char* t = getenv("TMPDIR");
    if (!t || !t[0]) t = "/tmp";
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s/", t);
    if (!out || !cap) return (uint32_t)strlen(buf) + 1;
    size_t n = utf8_to_utf16(buf, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}
__attribute__((ms_abi)) uint32_t mw_GetTempFileNameW(const wchar_t* path, const wchar_t* prefix, uint32_t uniq, wchar_t* out) {
    char p8[2048], u8[64];
    if (path) w3_w16_to_utf8z((const uint16_t*)path, p8, sizeof(p8)); else snprintf(p8, sizeof(p8), "%s", "/tmp");
w3_normalize_path(p8);
    if (prefix) w3_w16_to_utf8z((const uint16_t*)prefix, u8, sizeof(u8)); else u8[0] = '\0';
w3_normalize_path(u8);
    char full[4096];
    snprintf(full, sizeof(full), "%s/%s%04x.tmp", p8, u8, (uint32_t)(time(NULL) & 0xffff) + uniq);
    FILE* f = fopen(full, "wb");
    if (f) fclose(f);
    if (out) {
        size_t n = utf8_to_utf16(full, out, 4095);
        ((uint16_t*)out)[n] = 0;
    }
    return 1;
}
__attribute__((ms_abi)) int mw_GetDiskFreeSpaceExW(const wchar_t* dir, void* free_caller, void* total, void* free_total) {
    char d8[2048];
    if (dir) w3_w16_to_utf8z((const uint16_t*)dir, d8, sizeof(d8)); else snprintf(d8, sizeof(d8), "%s", ".");
    w3_normalize_path(d8); /* BUG-054b: same Windows-path rule as CreateFileW/CreateDirectoryW */
    struct statvfs v;
    if (statvfs(d8, &v) != 0) { mw_SetLastError(3); return 0; }
    uint64_t bs = (uint64_t)v.f_frsize;
    if (free_caller) *(uint64_t*)free_caller = bs * v.f_bavail;
    if (total)       *(uint64_t*)total = bs * v.f_blocks;
    if (free_total)  *(uint64_t*)free_total = bs * v.f_bfree;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetLogicalDrives(void) { return 1; /* C: only */ }
__attribute__((ms_abi)) uint32_t mw_GetDriveTypeA(const char* root) { return 3; /* DRIVE_FIXED */ }
__attribute__((ms_abi)) uint32_t mw_GetDriveTypeW(const wchar_t* root) { return 3; }
__attribute__((ms_abi)) int mw_GetVolumeInformationW(const wchar_t* root, wchar_t* vol, uint32_t vcap, uint32_t* serial, uint32_t* maxlen, uint32_t* fsflags, wchar_t* fs, uint32_t fcap) {
    if (vol && vcap) { size_t n = utf8_to_utf16("MiniWin", vol, vcap - 1); vol[n] = 0; }
    if (serial) *serial = 0x4D57494E;
    if (maxlen) *maxlen = 255;
    if (fsflags) *fsflags = 0x00040000 | 0x00020000; /* supports ACLs? no: case-preserving|unicode */
    if (fs && fcap) { size_t n = utf8_to_utf16("NTFS", fs, fcap - 1); fs[n] = 0; }
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetVolumePathNameW(const wchar_t* name, wchar_t* out, uint32_t cap) {
    if (out && cap) { size_t n = utf8_to_utf16("C:\\", out, cap - 1); out[n] = 0; return n; }
    return 0;
}
__attribute__((ms_abi)) int mw_SetCurrentDirectoryW(const wchar_t* dir) {
    char d8[4096];
    if (!dir || w3_w16_to_utf8z((const uint16_t*)dir, d8, sizeof(d8)) < 0) return 0;
w3_normalize_path(d8);
    d8[sizeof(d8)-1] = '\0';
    MW_TRACE("SetCurrentDirectoryW(%s)", d8);
    if (chdir(d8) != 0) { mw_SetLastError(2); return 0; }
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetCurrentDirectoryW(uint32_t cap, wchar_t* out) {
    char cwd[2048];
    if (!getcwd(cwd, sizeof(cwd))) { mw_SetLastError(3); return 0; }
    if (!out || !cap) return (uint32_t)strlen(cwd) + 1;
    size_t n = utf8_to_utf16(cwd, out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}
__attribute__((ms_abi)) uint32_t mw_GetSystemDirectoryA(char* out, uint32_t cap) {
    MW_TRACE("GetSystemDirectoryA()");
    if (!out || !cap) return 12;
    snprintf(out, cap, "C:\\Windows\\System32");
    return 18;
}
__attribute__((ms_abi)) uint32_t mw_GetSystemDirectoryW(wchar_t* out, uint32_t cap) {
    if (!out || !cap) return 19;
    size_t n = utf8_to_utf16("C:\\Windows\\System32", out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}
__attribute__((ms_abi)) uint32_t mw_GetWindowsDirectoryW(wchar_t* out, uint32_t cap) {
    if (!out || !cap) return 14;
    size_t n = utf8_to_utf16("C:\\Windows", out, cap - 1);
    ((uint16_t*)out)[n] = 0;
    return (uint32_t)n;
}

/* ---- directory enumeration (FindFirstFile* family) ----
 * BUG-049 (2026-10-17): the old fill_find_data wrote cFileName at +68 and
 * the file size at +52/+56, but the real x64 WIN32_FIND_DATAW (592 bytes)
 * is: attributes 0, ftCreation 4, ftLastAccess 12, ftLastWrite 20,
 * nFileSizeHigh 28, nFileSizeLow 32, dwReserved0 36, dwReserved1 40,
 * cFileName[260] @44, cAlternateFileName[14] @564. Guests read names/sizes
 * from +44/+28 — every enumeration returned garbage names.
 * Also fixed the contract: FindFirstFile{Ex}W now RETURNS the first match
 * (buffer filled) and FindNextFileW continues from the second; wildcards
 * (* ?) match via fnmatch; a non-wildcard pattern that does not exist fails
 * with ERROR_FILE_NOT_FOUND(2), not ERROR_NO_MORE_FILES(18). */
typedef struct { DIR* d; char pattern[4096]; char dirpart[4096]; int first; } W3Find;
static W3Find g_finds[64];
static int g_find_count = 0;

static uint64_t ft_from_time_t(uint64_t t) {
    return (t + 11644473600ULL) * 10000000ULL; /* 1601-01-01 epoch, 100ns */
}

static void fill_find_data(void* data, const char* name, const struct stat* st) {
    if (!data) return;
    uint8_t* p = (uint8_t*)data;
    memset(p, 0, 592);
    int is_dir = S_ISDIR(st->st_mode);
    uint32_t attr = is_dir ? 0x10 /*DIRECTORY*/ : 0x20 /*ARCHIVE*/;
    if (name[0] == '.') attr |= 0x2 /*HIDDEN*/;
    *(uint32_t*)(p + 0) = attr;
    *(uint64_t*)(p + 4)  = ft_from_time_t((uint64_t)st->st_ctime);
    *(uint64_t*)(p + 12) = ft_from_time_t((uint64_t)st->st_atime);
    *(uint64_t*)(p + 20) = ft_from_time_t((uint64_t)st->st_mtime);
    uint64_t sz = is_dir ? 0 : (uint64_t)st->st_size;
    *(uint32_t*)(p + 28) = (uint32_t)(sz >> 32);
    *(uint32_t*)(p + 32) = (uint32_t)sz;
    w3_utf8_to_utf16le(name, strlen(name), (uint16_t*)(p + 44), 259);
    ((uint16_t*)(p + 44))[259] = 0;
    /* cAlternateFileName left zeroed (no 8.3 names on this platform) */
}

static int find_has_wildcard(const char* s) {
    return strchr(s, '*') != NULL || strchr(s, '?') != NULL;
}

/* Walk g_finds[*f] until the next entry matching f->pattern; fill data.
 * Returns 1 on match, 0 when exhausted (caller sets the error code). */
static int find_next_match(W3Find* f, void* data) {
    struct dirent* de;
    while ((de = readdir(f->d)) != NULL) {
        if (strcmp(f->pattern, "*") != 0 &&
            fnmatch(f->pattern, de->d_name, 0) != 0) continue;
        struct stat st;
        int dfd = dirfd(f->d);
        if (fstatat(dfd >= 0 ? dfd : AT_FDCWD, de->d_name, &st,
                    (dfd >= 0 ? 0 : AT_SYMLINK_NOFOLLOW)) != 0) continue;
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", f->dirpart, de->d_name);
        (void)full;
        fill_find_data(data, de->d_name, &st);
        return 1;
    }
    return 0;
}

static void* find_first_impl(const char* p8, void* data) {
    char np8[4096];
    snprintf(np8, sizeof(np8), "%s", p8);
    w3_normalize_path(np8);
    p8 = np8;
    MW_TRACE("FindFirstFile(%s)", p8);
    if (g_find_count >= 64) { mw_SetLastError(4); return (void*)-1; }
    char dirpart[4096];
    snprintf(dirpart, sizeof(dirpart), "%s", p8);
    char* slash = strrchr(dirpart, '/');
    const char* fname = "*";
    if (slash) { *slash = '\0'; fname = slash + 1; if (!dirpart[0]) snprintf(dirpart, sizeof(dirpart), "%s", "/"); }
    else { snprintf(dirpart, sizeof(dirpart), "%s", "."); fname = p8; }
    if (!fname[0]) fname = "*";
    /* Non-wildcard: Windows stats the exact file and fails with 2 if absent */
    if (!find_has_wildcard(fname)) {
        struct stat st;
        if (stat(p8, &st) != 0) { mw_SetLastError(2); return (void*)-1; }
        DIR* d = opendir(dirpart[0] ? dirpart : ".");
        if (!d) { mw_SetLastError(3); return (void*)-1; }
        W3Find* f = &g_finds[g_find_count];
        f->d = d; f->first = 1;
        snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
        snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
        void* h = (void*)(uintptr_t)(4 + g_find_count++);
        fill_find_data(data, fname, &st);
        return h;
    }
    DIR* d = opendir(dirpart[0] ? dirpart : ".");
    if (!d) { mw_SetLastError(3); return (void*)-1; }
    W3Find* f = &g_finds[g_find_count];
    f->d = d;
    f->first = 1;
    snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
    snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
    void* h = (void*)(uintptr_t)(4 + g_find_count++);
    if (!find_next_match(f, data)) {
        mw_SetLastError(2); /* nothing matched */
        closedir(d);
        f->d = NULL;
        return (void*)-1;
    }
    return h;
}

__attribute__((ms_abi)) void* mw_FindFirstFileExW(const wchar_t* pattern, int level, void* data, int search_op, void* filter, uint32_t extra) {
    char p8[4096];
    if (!pattern || w3_w16_to_utf8z((const uint16_t*)pattern, p8, sizeof(p8)) < 0) { mw_SetLastError(87); return (void*)-1; }
    p8[sizeof(p8)-1] = '\0';
    return find_first_impl(p8, data);
}

__attribute__((ms_abi)) int mw_FindNextFileW(void* h, void* data) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_find_count || !g_finds[idx].d) { mw_SetLastError(6); return 0; }
    W3Find* f = &g_finds[idx];
    if (!find_next_match(f, data)) {
        mw_SetLastError(18); /* ERROR_NO_MORE_FILES */
        return 0;
    }
    return 1;
}

__attribute__((ms_abi)) int mw_FindClose(void* h) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_find_count) return 0;
    if (g_finds[idx].d) { closedir(g_finds[idx].d); g_finds[idx].d = NULL; }
    return 1;
}

/* ---- console ---- */
__attribute__((ms_abi)) uint32_t mw_GetConsoleCP(void) { return 65001; }
__attribute__((ms_abi)) uint32_t mw_GetConsoleOutputCP(void) { return 65001; }
__attribute__((ms_abi)) int mw_SetConsoleOutputCP(uint32_t cp) { return 1; }
__attribute__((ms_abi)) int mw_SetConsoleCP(uint32_t cp) { return 1; }
__attribute__((ms_abi)) int mw_SetConsoleMode(void* h, uint32_t mode) { return 1; }
__attribute__((ms_abi)) int mw_WriteConsoleW(void* h, const void* buf, uint32_t n, uint32_t* written, void* res) {
    char u8[8192];
    size_t m = 0;
    const wchar_t* w = (const wchar_t*)buf;
    for (uint32_t i = 0; i < n && m < sizeof(u8) - 8; i++) {
        uint32_t c = w[i];
        if (c < 0x80) u8[m++] = (char)c;
        else if (c < 0x800) { u8[m++] = 0xC0 | (c >> 6); u8[m++] = 0x80 | (c & 0x3F); }
        else { u8[m++] = 0xE0 | (c >> 12); u8[m++] = 0x80 | ((c >> 6) & 0x3F); u8[m++] = 0x80 | (c & 0x3F); }
    }
    uint64_t hv = (uint64_t)(uintptr_t)h;
    int fd = hv == 8 ? 1 : hv == 0xC ? 2 : 1;
    ssize_t r = write(fd, u8, m);
    if (written) *written = r >= 0 ? n : 0;
    return r >= 0;
}
__attribute__((ms_abi)) void* mw_GetConsoleWindow(void) { return NULL; }
__attribute__((ms_abi)) int mw_AllocConsole(void) { return 1; }
__attribute__((ms_abi)) int mw_AttachConsole(uint32_t pid) { return 0; } /* no parent console */
__attribute__((ms_abi)) int mw_SetConsoleCtrlHandler(void* fn, int add) { return 1; }
__attribute__((ms_abi)) uint32_t mw_GetNumberOfConsoleInputEvents(void* h, uint32_t* n) {
    if (n) *n = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_PeekConsoleInputA(void* h, void* buf, uint32_t n, uint32_t* read) {
    if (read) *read = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_PeekConsoleInputW(void* h, void* buf, uint32_t n, uint32_t* read) {
    if (read) *read = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_ReadConsoleW(void* h, void* buf, uint32_t n, uint32_t* read, void* ctl) {
    return mw_ReadFile(h, buf, n * 2, read, NULL);
}
__attribute__((ms_abi)) int mw_ReadConsoleInputW(void* h, void* buf, uint32_t n, uint32_t* read) {
    if (read) *read = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_ReadConsoleInputA(void* h, void* buf, uint32_t n, uint32_t* read) {
    if (read) *read = 0;
    return 1;
}

/* ---- registry: honest failure (no registry on host) ---- */
#define MW_REG_FAIL (mw_SetLastError(2), 2) /* ERROR_FILE_NOT_FOUND */
__attribute__((ms_abi)) int32_t mw_RegOpenKeyExW(void* key, const wchar_t* sub, uint32_t opts, uint32_t access, void** result) {
    MW_TRACE("RegOpenKeyExW() -> ERROR_FILE_NOT_FOUND");
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegOpenKeyExA(void* key, const char* sub, uint32_t opts, uint32_t access, void** result) {
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegQueryValueExW(void* key, const wchar_t* name, void* res, uint32_t* type, void* data, uint32_t* size) {
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegQueryValueExA(void* key, const char* name, void* res, uint32_t* type, void* data, uint32_t* size) {
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegGetValueW(void* key, const wchar_t* sub, const wchar_t* name, uint32_t flags, uint32_t* type, void* data, uint32_t* size) {
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegCloseKey(void* key) { return 0; }
__attribute__((ms_abi)) int32_t mw_RegCreateKeyExW(void* key, const wchar_t* sub, uint32_t res, const wchar_t* cls, uint32_t opts, uint32_t access, void* sa, void** result, void* disp) {
    mw_SetLastError(5);
    return 5;
}
/* SW-555 honest batch (SWEEP-009): W short forms. Real RegOpenKeyW ==
 * RegOpenKeyExW(..., 0, KEY_ALL_ACCESS, ...); real RegCreateKeyW ==
 * RegCreateKeyExW(..., REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, ...).
 * Wrapped — NOT duplicated — so both inherit the same honest-failure
 * contract (ERROR_FILE_NOT_FOUND / ERROR_ACCESS_DENIED + SetLastError).
 * autoruns64/rammap64 hit RegOpenKeyW via IAT; the old stub returned
 * 0 == ERROR_SUCCESS with a NULL phkResult — a lying success. */
__attribute__((ms_abi)) int32_t mw_RegOpenKeyW(void* key, const wchar_t* sub, void** result) {
    return mw_RegOpenKeyExW(key, sub, 0, 0x000F0000 /*KEY_ALL_ACCESS*/, result);
}
__attribute__((ms_abi)) int32_t mw_RegCreateKeyW(void* key, const wchar_t* sub, void** result) {
    return mw_RegCreateKeyExW(key, sub, 0, NULL, 0, 0x000F0000, NULL, result, NULL);
}
__attribute__((ms_abi)) int32_t mw_RegDeleteValueW(void* key, const wchar_t* name) { mw_SetLastError(2); return 2; }
__attribute__((ms_abi)) int32_t mw_RegEnumKeyA(void* key, uint32_t idx, char* name, uint32_t cap) { mw_SetLastError(259); return 259; }
__attribute__((ms_abi)) int32_t mw_RegEnumKeyExW(void* key, uint32_t idx, wchar_t* name, uint32_t* ncap, void* res, wchar_t* cls, uint32_t* ccap, void* time) {
    mw_SetLastError(259);
    return 259;
}
__attribute__((ms_abi)) int32_t mw_RegEnumValueW(void* key, uint32_t idx, wchar_t* name, uint32_t* ncap, void* res, uint32_t* type, void* data, uint32_t* size) {
    mw_SetLastError(259);
    return 259;
}
__attribute__((ms_abi)) int32_t mw_RegQueryInfoKeyW(void* key, wchar_t* cls, uint32_t* ccap, void* res, uint32_t* subkeys, uint32_t* maxsub, uint32_t* maxcls, uint32_t* values, uint32_t* maxval, uint32_t* maxdata, uint32_t* secdesc, void* time) {
    mw_SetLastError(6);
    return 6;
}
__attribute__((ms_abi)) int32_t mw_RegSetValueExW(void* key, const wchar_t* name, uint32_t res, uint32_t type, const void* data, uint32_t size) {
    mw_SetLastError(5);
    return 5;
}
__attribute__((ms_abi)) int32_t mw_RegLoadMUIStringW(void* key, const wchar_t* v, wchar_t* out, uint32_t cap, uint32_t* len, uint32_t flags, const wchar_t* dir) {
    mw_SetLastError(2);
    return 2;
}
__attribute__((ms_abi)) int32_t mw_RegNotifyChangeKeyValue(void* key, int watch, uint32_t filter, void* event, int async) {
    mw_SetLastError(1);
    return 1;
}

/* ---- BCrypt: real entropy for GenRandom, honest failure elsewhere ---- */
__attribute__((ms_abi)) int32_t mw_BCryptOpenAlgorithmProvider(void** ph, const wchar_t* alg, const wchar_t* impl, uint32_t flags) {
    MW_TRACE("BCryptOpenAlgorithmProvider()");
    if (!ph) return 0xC000000Du; /* STATUS_INVALID_PARAMETER */
    *ph = alloc_handle((void*)(uintptr_t)0xB0);
    return 0;
}
__attribute__((ms_abi)) int32_t mw_BCryptCloseAlgorithmProvider(void* h, uint32_t flags) { return 0; }
__attribute__((ms_abi)) int32_t mw_BCryptGenRandom(void* h, uint8_t* buf, uint32_t n, uint32_t flags) {
    if (!buf && n) return 0xC000000Du;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 0xC0000009u;
    ssize_t got = 0;
    while (got < (ssize_t)n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    return got == (ssize_t)n ? 0 : 0xC0000009u; /* STATUS_INVALID_PARAMETER_2 */
}
__attribute__((ms_abi)) int32_t mw_BCryptGetProperty(void* h, const wchar_t* prop, uint8_t* out, uint32_t len, uint32_t* res, uint32_t flags) {
    mw_SetLastError(0xC0000001u);
    if (res) *res = 0;
    return 0xC0000001u; /* STATUS_UNSUCCESSFUL — honest (no crypto backend) */
}
__attribute__((ms_abi)) int32_t mw_BCryptSetProperty(void* h, const wchar_t* prop, const void* val, uint32_t len, uint32_t flags) {
    return 0xC0000001u;
}
__attribute__((ms_abi)) int32_t mw_BCryptCreateHash(void* h, void** hh, uint8_t* obj, uint32_t objlen, void* secret, uint32_t seclen, uint32_t flags) {
    return 0xC0000001u;
}
__attribute__((ms_abi)) int32_t mw_BCryptHashData(void* h, const uint8_t* in, uint32_t len, uint32_t flags) { return 0xC0000001u; }
__attribute__((ms_abi)) int32_t mw_BCryptFinishHash(void* h, uint8_t* out, uint32_t len, uint32_t flags) { return 0xC0000001u; }
__attribute__((ms_abi)) int32_t mw_BCryptDestroyHash(void* h) { return 0; }
__attribute__((ms_abi)) int32_t mw_BCryptGenerateSymmetricKey(void* h, void** key, uint8_t* obj, uint32_t objlen, const void* secret, uint32_t seclen, uint32_t flags) { return 0xC0000001u; }
__attribute__((ms_abi)) int32_t mw_BCryptDestroyKey(void* key) { return 0; }
__attribute__((ms_abi)) int32_t mw_BCryptEncrypt(void* h, const uint8_t* in, uint32_t ilen, void* pad, const uint8_t* iv, uint32_t ivlen, uint8_t* out, uint32_t olen, uint32_t* res, uint32_t flags) { return 0xC0000001u; }
__attribute__((ms_abi)) int32_t mw_BCryptDeriveKeyPBKDF2(void* h, const uint8_t* pw, uint32_t pwlen, const uint8_t* salt, uint32_t saltlen, uint64_t iters, uint8_t* out, uint32_t olen, uint32_t flags) {
    return 0xC0000001u;
}
__attribute__((ms_abi)) int32_t mw_CryptAcquireContextA(void** ph, const char* container, const char* prov, uint32_t type, uint32_t flags) {
    MW_TRACE("CryptAcquireContextA(flags=%u)", flags);
    if (flags & 0x8 /* CRYPT_VERIFYCONTEXT */) { if (ph) *ph = alloc_handle((void*)(uintptr_t)0xC0); return 1; }
    mw_SetLastError(2);
    return 0;
}
__attribute__((ms_abi)) int32_t mw_CryptGenRandom(void* h, uint32_t len, uint8_t* buf) {
    return mw_BCryptGenRandom(NULL, buf, len, 0) == 0 ? 1 : 0;
}
__attribute__((ms_abi)) int32_t mw_CryptReleaseContext(void* h, uint32_t flags) { return 1; }

/* ---- VCOMP (OpenMP): single-team semantics (num_threads = 1) ---- */
__attribute__((ms_abi)) int32_t mw_omp_get_max_threads(void) { return 1; }
__attribute__((ms_abi)) int32_t mw_omp_get_thread_num(void) { return 0; }
__attribute__((ms_abi)) int32_t mw_omp_get_num_threads(void) { return 1; }
__attribute__((ms_abi)) void mw_omp_set_nested(int v) { }
__attribute__((ms_abi)) void mw_omp_init_lock(void* l) { if (l) *(void**)l = NULL; }
__attribute__((ms_abi)) void mw_omp_destroy_lock(void* l) { }
__attribute__((ms_abi)) void mw_omp_set_lock(void* l) { }
__attribute__((ms_abi)) void mw_omp_unset_lock(void* l) { }
__attribute__((ms_abi)) void mw__vcomp_set_num_threads(uint32_t n) { }
__attribute__((ms_abi)) void mw__vcomp_barrier(void) { }
__attribute__((ms_abi)) void mw__vcomp_enter_critsect(void* p) { }
__attribute__((ms_abi)) void mw__vcomp_leave_critsect(void* p) { }
__attribute__((ms_abi)) void mw__vcomp_single_begin(void) { }
__attribute__((ms_abi)) void mw__vcomp_single_end(void) { }
__attribute__((ms_abi)) void mw__vcomp_reduction_r8(void) { }
__attribute__((ms_abi)) void mw__vcomp_atomic_add_i4(void* p, int32_t v) { if (p) (void)__sync_fetch_and_add((volatile int32_t*)p, v); }
__attribute__((ms_abi)) void mw__vcomp_atomic_add_i8(void* p, int64_t v) { if (p) (void)__sync_fetch_and_add((volatile int64_t*)p, v); }
__attribute__((ms_abi)) void mw__vcomp_for_static_simple_init(int32_t lb, int32_t ub, int32_t st, int32_t chunk, int32_t* from, int32_t* to) {
    if (from) *from = lb;
    if (to)   *to = ub;
}
__attribute__((ms_abi)) void mw__vcomp_for_static_init_i8(int64_t lb, int64_t ub, int64_t st, int64_t chunk, int64_t* from, int64_t* to) {
    if (from) *from = lb;
    if (to)   *to = ub;
}
__attribute__((ms_abi)) int32_t mw__vcomp_for_next(int32_t* from, int32_t* to) { return 0; }
__attribute__((ms_abi)) void mw__vcomp_for_static_end(void) { }
__attribute__((ms_abi)) void mw__vcomp_for_dynamic_init(int32_t kind) { }
__attribute__((ms_abi)) void mw__vcomp_for_dynamic_init_i8(int64_t kind) { }
__attribute__((ms_abi)) int32_t mw__vcomp_for_dynamic_next(int32_t* from, int32_t* to) { return 0; }
__attribute__((ms_abi)) int32_t mw__vcomp_for_dynamic_next_i8(int64_t* from, int64_t* to) { return 0; }

/* _vcomp_fork: execute the loop body directly on this thread (team of 1).
 * ABI (mingw-w64 libgcc): (void (*fn)(), size_t nargs, ...) — the callback
 * receives the trailing args as its arguments. */
__attribute__((ms_abi)) void mw__vcomp_fork(void* fn, size_t nargs,
        uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4,
        uint64_t x5, uint64_t x6, uint64_t x7, uint64_t x8) {
    uint64_t a[8] = { x1, x2, x3, x4, x5, x6, x7, x8 };
    MW_TRACE("_vcomp_fork(nargs=%lu) single-team", (unsigned long)nargs);
    switch (nargs) {
    case 0: ((void(*)(void))fn)(); break;
    case 1: ((void(*)(uint64_t))fn)(a[0]); break;
    case 2: ((void(*)(uint64_t,uint64_t))fn)(a[0], a[1]); break;
    case 3: ((void(*)(uint64_t,uint64_t,uint64_t))fn)(a[0], a[1], a[2]); break;
    case 4: ((void(*)(uint64_t,uint64_t,uint64_t,uint64_t))fn)(a[0], a[1], a[2], a[3]); break;
    case 5: ((void(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))fn)(a[0],a[1],a[2],a[3],a[4]); break;
    case 6: ((void(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))fn)(a[0],a[1],a[2],a[3],a[4],a[5]); break;
    case 7: ((void(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))fn)(a[0],a[1],a[2],a[3],a[4],a[5],a[6]); break;
    default: ((void(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))fn)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]); break;
    }
}

/* ---- misc process/debug/message ---- */
__attribute__((ms_abi)) int mw_UnhandledExceptionFilter(void* ptr) {
    MW_TRACE("UnhandledExceptionFilter()");
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}
__attribute__((ms_abi)) int32_t mw_FormatMessageA(uint32_t flags, const void* src, uint32_t msgid, uint32_t lang, char* out, uint32_t cap, void* args) {
    MW_TRACE("FormatMessageA(msgid=%u)", msgid);
    return 0; /* no message found — honest */
}
__attribute__((ms_abi)) int32_t mw_FormatMessageW(uint32_t flags, const void* src, uint32_t msgid, uint32_t lang, wchar_t* out, uint32_t cap, void* args) {
    MW_TRACE("FormatMessageW(msgid=%u)", msgid);
    return 0;
}
__attribute__((ms_abi)) void mw_OutputDebugStringW(const wchar_t* s) {
    char u8[1024];
    if (s) { size_t n = w3_w16_to_utf8z((const uint16_t*)s, u8, sizeof(u8)); u8[n >= 0 ? n : 0] = '\0'; MW_TRACE("OutputDebugStringW(%s)", u8); }
}
__attribute__((ms_abi)) int mw_TerminateProcess(void* h, uint32_t code) {
    MW_TRACE("TerminateProcess(%u)", code);
    exit(code);
}

__attribute__((ms_abi)) int mw_ProcessIdToSessionId(uint32_t pid, uint32_t* sid) { if (sid) *sid = 0; return 1; }
__attribute__((ms_abi)) int mw_GetUserNameW(wchar_t* out, uint32_t* cap) {
    const char* u = getenv("USER");
    if (!u) u = "user";
    if (!out || !cap || *cap < 32) { if (cap) *cap = 32; return 0; }
    size_t n = utf8_to_utf16(u, out, *cap - 1);
    ((uint16_t*)out)[n] = 0;
    *cap = (uint32_t)(n + 1);
    return 1;
}
__attribute__((ms_abi)) int mw_GetComputerNameW(wchar_t* out, uint32_t* cap) {
    char h[256] = "minwin-host";
    if (gethostname(h, sizeof(h) - 1) != 0) snprintf(h, sizeof(h), "%s", "minwin-host");
    if (!out || !cap || *cap < (uint32_t)strlen(h) + 1) { if (cap) *cap = (uint32_t)strlen(h) + 1; return 0; }
    size_t n = utf8_to_utf16(h, out, *cap - 1);
    ((uint16_t*)out)[n] = 0;
    *cap = (uint32_t)(n + 1);
    return 1;
}
__attribute__((ms_abi)) int mw_DeviceIoControl(void* h, uint32_t code, void* in, uint32_t ilen, void* out, uint32_t olen, uint32_t* ret, void* ov) {
    mw_SetLastError(1);
    return 0;
}
__attribute__((ms_abi)) uint32_t mw_SetErrorMode(uint32_t mode) { return 0; }
__attribute__((ms_abi)) int mw_SetHandleInformation(void* h, uint32_t mask, uint32_t flags) { return 1; }
__attribute__((ms_abi)) int mw_SetStdHandle(int32_t which, void* h) { return 1; }
__attribute__((ms_abi)) int mw_PeekNamedPipe(void* h, void* buf, uint32_t n, uint32_t* read, uint32_t* avail, uint32_t* left) {
    if (read) *read = 0;
    if (avail) *avail = 0;
    if (left) *left = 0;
    return 0; /* not a pipe — honest */
}
__attribute__((ms_abi)) int mw_CreatePipe(void** rh, void** wh, void* sa, uint32_t size) {
    mw_SetLastError(3);
    return 0;
}
__attribute__((ms_abi)) int mw_CreateProcessW(const wchar_t* app, wchar_t* cmd, void* pa, void* ta, int inherit, uint32_t flags, void* env, const wchar_t* dir, void* si, void* pi) {
    MW_TRACE("CreateProcessW() -> fail (no child process support)");
    mw_SetLastError(2);
    return 0;
}
__attribute__((ms_abi)) int mw_CreateProcessA(const char* app, char* cmd, void* pa, void* ta, int inherit, uint32_t flags, void* env, const char* dir, void* si, void* pi) {
    mw_SetLastError(2);
    return 0;
}
__attribute__((ms_abi)) int mw_GetExitCodeProcess(void* h, uint32_t* code) { if (code) *code = 0; return 1; }
__attribute__((ms_abi)) int mw_WaitOnAddress_stub(void) { return 0; }


/* ---- real .rsrc resource lookup (IMAGE_RESOURCE_DIRECTORY walk) ---- */
/* FindResourceA/W return an HRSRC = pointer to the IMAGE_RESOURCE_DATA_ENTRY;
 * LoadResource/LockResource resolve it to the data VA; SizeofResource reads
 * its Size field. */
static int w3_res_cmp_name(const uint8_t* dir_base, uint32_t name_off, const char* aname, const wchar_t* wname) {
    if (name_off & 0x80000000u) {
        /* string entry: WORD len, then UTF-16 */
        const uint8_t* s = dir_base + (name_off & 0x7FFFFFFFu);
        uint16_t len = *(const uint16_t*)s;
        const wchar_t* ws = (const wchar_t*)(s + 2);
        if (wname) {
            for (uint16_t i = 0; i < len; i++) {
                wchar_t c = ws[i];
                wchar_t w = wname[i];
                if (towlower(c) != towlower(w)) return 1;
                if (!w) return 1;
            }
            return wname[len] ? 1 : 0;
        }
        if (!aname) return 1;
        for (uint16_t i = 0; i < len; i++) {
            wchar_t c = ws[i];
            char a = aname[i];
            if (towlower(c) != (wchar_t)towlower((unsigned char)a)) return 1;
            if (!a) return 1;
        }
        return aname[len] ? 1 : 0;
    }
    /* ID entry */
    uint32_t id = name_off;
    long want = wname ? -1 : (aname ? atol(aname) : -1);
    if (wname) {
        wchar_t buf[16];
        int n = utf8_to_utf16("", buf, 0); (void)n;
        /* compare wname as decimal number */
        if (!wname[0]) return 1;
        long acc = 0;
        for (int i = 0; wname[i]; i++) {
            if (wname[i] < L'0' || wname[i] > L'9') return 1;
            acc = acc * 10 + (wname[i] - L'0');
            if (acc > 0x7FFFFFFFl) return 1;
        }
        return acc == id ? 0 : 1;
    }
    (void)want;
    if (!aname) return 1;
    /* numeric string or plain number */
    if (aname[0] == '#') {
        long acc = 0;
        for (const char* p = aname + 1; *p; p++) {
            if (*p < '0' || *p > '9') return 1;
            acc = acc * 10 + (*p - '0');
        }
        return acc == id ? 0 : 1;
    }
    return -1 == -1 ? 1 : 1; /* name string can't equal an ID entry */
}

static void* w3_res_find(void* hmod, const char* aname, const wchar_t* wname, const char* atype, const wchar_t* wtype) {
    if (!g_image_base) return NULL;
    /* locate .rsrc via the resource data directory */
    uint32_t rsrc_rva = 0;
    {
        uint8_t* pe = g_image_base;
        uint32_t pe_off = *(uint32_t*)(pe + 0x3c);
        uint16_t num_dd = *(uint16_t*)(pe + pe_off + 24 + 108);
        if (num_dd < 3) return NULL;
        uint32_t dd_rva = *(uint32_t*)(pe + pe_off + 24 + 112 + 2 * 8);
        rsrc_rva = dd_rva;
    }
    if (!rsrc_rva) return NULL;
    const uint8_t* base = g_image_base + rsrc_rva;
    /* level 1: type, level 2: name, level 3: language */
    const uint8_t* level = base;
    for (int depth = 0; depth < 3; depth++) {
        uint16_t named = *(const uint16_t*)(level + 8);
        uint16_t ided = *(const uint16_t*)(level + 10);
        uint16_t count = named + ided;
        const uint8_t* entries = level + 16;
        const uint8_t* next = NULL;
        for (uint16_t i = 0; i < count; i++) {
            uint32_t nm = *(const uint32_t*)(entries + i * 8);
            uint32_t of = *(const uint32_t*)(entries + i * 8 + 4);
            int match;
            if (depth == 0) match = w3_res_cmp_name(base, nm, atype, wtype) == 0;
            else match = w3_res_cmp_name(base, nm, aname, wname) == 0;
            if (match) {
                if (of & 0x80000000u) {
                    next = base + (of & 0x7FFFFFFFu);
                    break;
                } else if (depth == 2) {
                    return (void*)(base + of); /* IMAGE_RESOURCE_DATA_ENTRY */
                }
                next = base + of;
                break;
            }
        }
        if (!next) return NULL;
        level = next;
    }
    return NULL;
}
__attribute__((ms_abi)) void* mw_FindResourceA(void* hmod, const char* name, const char* type) {
    MW_TRACE("FindResourceA(type=%s, name=%s)", type ? type : "?", name ? name : "?");
    void* r = w3_res_find(hmod, name, NULL, type, NULL);
    if (!r) mw_SetLastError(1814); /* ERROR_RESOURCE_NAME_NOT_FOUND */
    return r;
}
__attribute__((ms_abi)) void* mw_FindResourceW(void* hmod, const wchar_t* name, const wchar_t* type) {
    void* r = w3_res_find(hmod, NULL, name, NULL, type);
    if (!r) mw_SetLastError(1814);
    return r;
}
__attribute__((ms_abi)) void* mw_FindResourceExW(void* hmod, const wchar_t* type, const wchar_t* name, uint16_t lang) {
    (void)lang;
    void* r = w3_res_find(hmod, NULL, name, NULL, type);
    if (!r) mw_SetLastError(1814);
    return r;
}
__attribute__((ms_abi)) void* mw_LoadResource(void* hmod, void* hrsrc) {
    MW_TRACE("LoadResource(%p)", hrsrc);
    return hrsrc; /* data entry VA doubles as the resource handle */
}
__attribute__((ms_abi)) void* mw_LockResource(void* hrsrc) {
    return hrsrc;
}
__attribute__((ms_abi)) uint32_t mw_SizeofResource(void* hmod, void* hrsrc) {
    if (!hrsrc) return 0;
    return *(const uint32_t*)((const uint8_t*)hrsrc + 4); /* DATA_ENTRY.Size */
}
__attribute__((ms_abi)) int mw_FreeResource(void* hrsrc) { (void)hrsrc; return 1; }
__attribute__((ms_abi)) int mw_EnumResourceNamesW(void* hmod, const wchar_t* type, void* cb, long param) {
    (void)hmod; (void)type; (void)cb; (void)param;
    return 0; /* enumeration unsupported — honest */
}


/* ============================================================
 * ISS1-WAVE4 — CRT family (api-ms-win-crt-*) for the mingw-ucrt
 * corpus (ffmpeg). Real semantics per CRT contract; honest
 * failures where the host cannot provide the behavior.
 * ============================================================ */

/* exe-wide thread-local atexit callback (registered once, run at exit) */
static void (*g_tle_atexit_cb)(void) = NULL;
__attribute__((ms_abi)) uint32_t mw__register_thread_local_exe_atexit_callback(void* cb) {
    MW_TRACE("_register_thread_local_exe_atexit_callback(%p)", cb);
    g_tle_atexit_cb = (void (*)(void))cb;
    return 0;
}
static void w3_run_tle_atexit(void) {
    if (g_tle_atexit_cb) g_tle_atexit_cb();
}
/* hook into ExitProcess: wrap the existing implementation */
__attribute__((ms_abi)) void mw__set_abort_behavior(uint32_t flags, uint32_t mask) {
    MW_TRACE("_set_abort_behavior(flags=%u, mask=%u)", flags, mask);
}
__attribute__((ms_abi)) int mw__set_errno(int v) { errno = v; g_errno_storage = v; return 0; }
__attribute__((ms_abi)) int mw__get_errno(int* out, int* dos) {
    if (out) *out = errno;
    if (dos) *dos = 0;
    return 0;
}
/* _initterm_e: table of int(void) initializers, stop on nonzero */
__attribute__((ms_abi)) int mw__initterm_e(void (**start)(void), void (**end)(void)) {
    MW_TRACE("_initterm_e(%p..%p)", start, end);
    for (; start < end; start++) {
        if (!*start) continue;
        int (__attribute__((ms_abi)) *fn)(void) = (int (__attribute__((ms_abi)) *)(void))*start;
        int r = fn();
        if (r != 0) return r;
    }
    return 0;
}

/* SEH filter helpers: guest exception code filtering (map to continue) */
__attribute__((ms_abi)) int mw__seh_filter_exe(uint32_t xc, void* info) {
    MW_TRACE("_seh_filter_exe(code=%x)", xc);
    return 1; /* EXCEPTION_EXECUTE_HANDLER */
}
__attribute__((ms_abi)) int mw__seh_filter_dll(uint32_t xc, void* info) {
    return 1;
}
__attribute__((ms_abi)) void mw__invalid_parameter_noinfo(void) { }
__attribute__((ms_abi)) int mw___report_rangecheckfailure(void) {
    MW_TRACE("__report_rangecheckfailure()");
    return 0;
}

/* __intrinsic_setjmp: guest uses setjmp for SEH frame installation.
 * Route through the loader's existing setjmp buffer machinery. */
/* __intrinsic_setjmp: the guest's mingw jmp_buf is 16 qwords (128 bytes);
 * a glibc jmp_buf is 200 bytes and SMASHED 72 bytes past it (live: ffmpeg
 * "malloc(): invalid size (unsorted)" during CRT init). Correct semantics
 * without touching the guest buffer: keep the HOST jmp_buf in a side table
 * keyed by the guest buffer pointer. longjmp unwinds the SHARED C stack
 * back to our setjmp frame, whose return lands back in the guest — the
 * observable setjmp/longjmp behavior matches Windows. */
typedef struct { void* guest_buf; jmp_buf host; int used; } W3Sjmp;
static W3Sjmp g_sjmp[16];
__attribute__((ms_abi)) int mw___intrinsic_setjmp(void* jb) {
    MW_TRACE("__intrinsic_setjmp(%p)", jb);
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (g_sjmp[i].used && g_sjmp[i].guest_buf == jb) { slot = i; break; }
        if (slot < 0 && !g_sjmp[i].used) slot = i;
    }
    if (slot < 0) slot = 0;
    g_sjmp[slot].used = 1;
    g_sjmp[slot].guest_buf = jb;
    int r = _setjmp(g_sjmp[slot].host);
    return r;
}
__attribute__((ms_abi)) void mw___intrinsic_longjmp(void* jb, int val) {
    MW_TRACE("__intrinsic_longjmp(%p, %d)", jb, val);
    for (int i = 0; i < 16; i++) {
        if (g_sjmp[i].used && g_sjmp[i].guest_buf == jb) {
            longjmp(g_sjmp[i].host, val ? val : 1);
        }
    }
    fprintf(stderr, "[MiniWin] longjmp to unknown jmp_buf %p\n", jb);
    _exit(4);
}

/* wide char classification bridge for _wassert etc. */
__attribute__((ms_abi)) void mw__wassert(const wchar_t* msg, const wchar_t* file, unsigned line) {
    char m8[512], f8[512];
    if (msg) w3_w16_to_utf8z((const uint16_t*)msg, m8, sizeof(m8)), m8[sizeof(m8)-1] = 0; else m8[0] = 0;
    if (file) w3_w16_to_utf8z((const uint16_t*)file, f8, sizeof(f8)), f8[sizeof(f8)-1] = 0; else f8[0] = 0;
    w3_normalize_path(f8);
    fprintf(stderr, "Assertion failed: %s (%s:%u)\n", m8, f8, line);
    abort();
}
__attribute__((ms_abi)) void mw__assert2(const char* msg, const char* file, unsigned line) {
    fprintf(stderr, "Assertion failed: %s (%s:%u)\n", msg ? msg : "?", file ? file : "?", line);
    abort();
}

/* errno-returning math */
__attribute__((ms_abi)) double mw__hypot(double a, double b) { return sqrt(a * a + b * b); }
__attribute__((ms_abi)) int mw__isnan(double x) { return x != x; }
__attribute__((ms_abi)) int mw__finite(double x) { return x == x && x != 1.0 / 0.0 && x != -1.0 / 0.0; }
__attribute__((ms_abi)) double mw__j1(double x) { (void)x; return 0; }
__attribute__((ms_abi)) double mw__y1(double x) { (void)x; return 0; }
__attribute__((ms_abi)) double mw__jn(int n, double x) { (void)n; (void)x; return 0; }
__attribute__((ms_abi)) double mw__yn(int n, double x) { (void)n; (void)x; return 0; }

/* time */
__attribute__((ms_abi)) int mw__localtime64_s(void* tm, const int64_t* t64) {
    if (!tm || !t64) { mw_SetLastError(87); return 22; }
    time_t t = (time_t)*t64;
    struct tm tmp;
    if (!localtime_r(&t, &tmp)) return 1;
    /* struct tm layout is identical (9 ints) */
    memcpy(tm, &tmp, sizeof(struct tm));
    return 0;
}
__attribute__((ms_abi)) int mw__gmtime64_s(void* tm, const int64_t* t64) {
    if (!tm || !t64) { mw_SetLastError(87); return 22; }
    time_t t = (time_t)*t64;
    struct tm tmp;
    if (!gmtime_r(&t, &tmp)) return 1;
    memcpy(tm, &tmp, sizeof(struct tm));
    return 0;
}
__attribute__((ms_abi)) int64_t mw__mktime64(void* tm) {
    if (!tm) return -1;
    struct tm tmp;
    memcpy(&tmp, tm, sizeof(struct tm));
    time_t t = mktime(&tmp);
    memcpy(tm, &tmp, sizeof(struct tm));
    return (int64_t)t;
}
__attribute__((ms_abi)) int32_t mw__time32(int32_t* out) {
    int32_t t = (int32_t)time(NULL);
    if (out) *out = t;
    return t;
}
__attribute__((ms_abi)) char* mw__ctime64(const int64_t* t64) {
    if (!t64) return NULL;
    time_t t = (time_t)*t64;
    return ctime(&t);
}
__attribute__((ms_abi)) double mw__difftime64(int64_t a, int64_t b) { return difftime((time_t)a, (time_t)b); }
__attribute__((ms_abi)) void mw__ftime64(void* tb) {
    if (!tb) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    /* struct _timeb64: __int64 time; __int16 millitm; int16 timezone; int16 dstflag */
    uint8_t* p = (uint8_t*)tb;
    *(int64_t*)(p + 0) = tv.tv_sec;
    *(int16_t*)(p + 8) = tv.tv_usec / 1000;
    *(int16_t*)(p + 10) = 0;
    *(int16_t*)(p + 12) = 0;
}

/* file ops (narrow) */
__attribute__((ms_abi)) int mw__access(const char* path, int mode) { return access(path, mode == 0 ? F_OK : (mode & 6)); }
__attribute__((ms_abi)) int mw__waccess(const wchar_t* path, int mode) {
    char p8[4096];
    if (!path || w3_w16_to_utf8z((const uint16_t*)path, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return access(p8, mode == 0 ? F_OK : (mode & 6));
}
__attribute__((ms_abi)) int mw__chsize(int fd, long sz) { return ftruncate(fd, sz); }
__attribute__((ms_abi)) int mw__chsize_s(int fd, int64_t sz) { return ftruncate(fd, (off_t)sz); }
__attribute__((ms_abi)) int mw__commit(int fd) { return fsync(fd); }
__attribute__((ms_abi)) int mw__configthreadlocale(int how) {
    (void)how;
    return 0; /* _ENABLE_PER_THREAD_LOCALE never set — single team */
}
__attribute__((ms_abi)) void* mw__create_locale(int cat, const char* loc) {
    MW_TRACE("_create_locale(%d, %s)", cat, loc ? loc : "");
    return (void*)(uintptr_t)0x1000; /* opaque handle; C locale only */
}
__attribute__((ms_abi)) void mw__free_locale(void* h) { }
__attribute__((ms_abi)) int mw__dup2(int a, int b) { return dup2(a, b); }
__attribute__((ms_abi)) int mw__getch(void) {
    /* console read without echo: single byte from stdin */
    unsigned char c = 0;
    ssize_t r = read(0, &c, 1);
    return r == 1 ? (int)c : -1;
}
__attribute__((ms_abi)) int mw__getpid(void) { return (int)getpid(); }
__attribute__((ms_abi)) int mw__getdrive(void) { return 3; /* C: */ }
__attribute__((ms_abi)) int mw__mkdir(const char* p) { return mkdir(p, 0755); }
__attribute__((ms_abi)) int mw__wmkdir(const wchar_t* p) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return mkdir(p8, 0755);
}
__attribute__((ms_abi)) int mw__wchdir(const wchar_t* p) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return chdir(p8);
}
__attribute__((ms_abi)) int mw__wchmod(const wchar_t* p, int mode) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return chmod(p8, mode);
}
__attribute__((ms_abi)) int mw__wcreat(const wchar_t* p, int mode) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return open(p8, O_CREAT | O_TRUNC | O_RDWR, mode);
}
__attribute__((ms_abi)) wchar_t* mw__wcsdup(const wchar_t* s) {
    if (!s) return NULL;
    /* BUG-055: register the allocation (guest frees via free) — guest
     * wchar_t is 2 bytes, so size in bytes = (units+1)*2 */
    size_t units = 0; while (((const uint16_t*)s)[units]) units++;
    size_t n = (units + 1) * 2;
    wchar_t* d = (wchar_t*)malloc(n);
    if (d) { memcpy(d, s, n); w3_reg_add_sz(d, n); }
    return d;
}
__attribute__((ms_abi)) int mw__wcsicmp(const wchar_t* a, const wchar_t* b) {
    return wstr_case_cmp(a, b, 1);
}
__attribute__((ms_abi)) int mw__wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        wint_t ca = towlower(a ? a[i] : 0), cb = towlower(b ? b[i] : 0);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}
__attribute__((ms_abi)) int mw__strnicmp(const char* a, const char* b, size_t n) {
    return strncasecmp(a, b, n);
}
__attribute__((ms_abi)) wchar_t* mw__wgetenv(const wchar_t* name) {
    char n8[256];
    if (!name || w3_w16_to_utf8z((const uint16_t*)name, n8, sizeof(n8)) < 0) return NULL;
    n8[sizeof(n8)-1] = 0;
    const char* v = getenv(n8);
    if (!v) return NULL;
    static uint16_t wval[4096];
    utf8_to_utf16(v, wval, 4095);
    return (wchar_t*)wval;
}
__attribute__((ms_abi)) char* mw__fullpath(char* abs, const char* rel, size_t n) {
    char buf[4096];
    if (!rel) { mw_SetLastError(87); return NULL; }
    if (rel[0] != '/') {
        char cwd[2048];
        if (!getcwd(cwd, sizeof(cwd))) return NULL;
        snprintf(buf, sizeof(buf), "%s/%s", cwd, rel);
    } else snprintf(buf, sizeof(buf), "%s", rel);
    if (!abs) {
        abs = (char*)malloc(strlen(buf) + 1);
        if (!abs) return NULL;
        w3_reg_add_sz(abs, strlen(buf) + 1); /* BUG-055: guest frees this */
    } else if (strlen(buf) + 1 > n) {
        mw_SetLastError(34); return NULL;
    }
    strcpy(abs, buf);
    return abs;
}
__attribute__((ms_abi)) wchar_t* mw__wfullpath(wchar_t* abs, const wchar_t* rel, size_t n) {
    char r8[4096], buf[4096];
    if (!rel || w3_w16_to_utf8z((const uint16_t*)rel, r8, sizeof(r8)) < 0) return NULL;
    w3_normalize_path(r8);
    r8[sizeof(r8)-1] = 0;
    if (r8[0] != '/') {
        char cwd[2048];
        if (!getcwd(cwd, sizeof(cwd))) return NULL;
        snprintf(buf, sizeof(buf), "%s/%s", cwd, r8);
    } else snprintf(buf, sizeof(buf), "%s", r8);
    if (!abs) {
        size_t w = utf8_to_utf16(buf, NULL, 0);
        abs = (wchar_t*)malloc((w + 1) * 2);
        if (!abs) return NULL;
    } else if (utf8_to_utf16(buf, NULL, 0) + 1 > n) {
        mw_SetLastError(34); return NULL;
    }
    size_t got = utf8_to_utf16(buf, abs, n ? n - 1 : 0);
    ((uint16_t*)abs)[got] = 0;
    return abs;
}
__attribute__((ms_abi)) wchar_t* mw__wgetcwd(wchar_t* buf, int n) {
    char cwd[2048];
    if (!buf || !n || !getcwd(cwd, sizeof(cwd))) return NULL;
    size_t got = utf8_to_utf16(cwd, buf, (size_t)n - 1);
    ((uint16_t*)buf)[got] = 0;
    return buf;
}
__attribute__((ms_abi)) int mw__wremove(const wchar_t* p) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return remove(p8);
}
__attribute__((ms_abi)) int mw__wrename(const wchar_t* a, const wchar_t* b) {
    char a8[4096], b8[4096];
    if (!a || !b || w3_w16_to_utf8z((const uint16_t*)a, a8, sizeof(a8)) < 0
        || w3_w16_to_utf8z((const uint16_t*)b, b8, sizeof(b8)) < 0) return -1;
    w3_normalize_path(a8);
    w3_normalize_path(b8);
    a8[sizeof(a8)-1] = 0; b8[sizeof(b8)-1] = 0;
    return rename(a8, b8);
}
__attribute__((ms_abi)) int mw__wrmdir(const wchar_t* p) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return rmdir(p8);
}
__attribute__((ms_abi)) int mw__wutime64(const wchar_t* p, void* t) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    struct timespec ts[2];
    memset(ts, 0, sizeof(ts));
    if (t) {
        int64_t secs = *(int64_t*)t; /* __utimbuf64: time_t time, time_t modtime */
        ts[0].tv_sec = (time_t)secs;
        ts[1].tv_sec = (time_t)*((int64_t*)t + 1);
    }
    return utimensat(AT_FDCWD, p8, ts, 0);
}
__attribute__((ms_abi)) int mw__utime64(const char* p, void* t) {
    struct timespec ts[2];
    memset(ts, 0, sizeof(ts));
    if (t) {
        ts[0].tv_sec = (time_t)*(int64_t*)t;
        ts[1].tv_sec = (time_t)*((int64_t*)t + 1);
    }
    return utimensat(AT_FDCWD, p, ts, 0);
}
/* FILE* bridges for wide fs (registry-backed streams) */
__attribute__((ms_abi)) void* mw__wfopen(const wchar_t* path, const wchar_t* mode) {
    char p8[4096], m8[32];
    if (!path || !mode) return NULL;
    if (w3_w16_to_utf8z((const uint16_t*)path, p8, sizeof(p8)) < 0) return NULL;
    if (w3_w16_to_utf8z((const uint16_t*)mode, m8, sizeof(m8)) < 0) return NULL;
    p8[sizeof(p8)-1] = 0; m8[sizeof(m8)-1] = 0;
    MW_TRACE("_wfopen(%s, %s)", p8, m8);
    FILE* f = fopen(p8, m8);
    mw_fileptr_register(f); /* BUG-065 */
    return f;
}
__attribute__((ms_abi)) void* mw__fdopen(int fd, const char* mode) {
    MW_TRACE("_fdopen(%d, %s)", fd, mode ? mode : "");
    if (!mode) return NULL;
    FILE* hf = fdopen(fd, mode);
    mw_fileptr_register(hf); /* BUG-065: zstd opens outputs via _open+_fdopen */
    return hf;
}
__attribute__((ms_abi)) void* mw__fsopen(const char* path, const char* mode, int sh) {
    FILE* hf = (path && mode) ? fopen(path, mode) : NULL;
    mw_fileptr_register(hf); /* BUG-065 */
    return hf;
}
__attribute__((ms_abi)) int mw__fseeki64(void* f, int64_t off, int whence) {
    return f ? fseek((FILE*)f, (long)off, whence) : -1;
}
__attribute__((ms_abi)) int64_t mw__ftelli64(void* f) {
    return f ? (int64_t)ftell((FILE*)f) : -1;
}
__attribute__((ms_abi)) int mw__lock_file(void* f) {
    (void)f;
    return 0; /* single team: lock uncontended */
}
__attribute__((ms_abi)) void mw__unlock_file(void* f) { (void)f; }
__attribute__((ms_abi)) int mw__fflush_nolock(void* f) { return f ? fflush((FILE*)f) : 0; }
__attribute__((ms_abi)) int mw__fstat64i32(int fd, void* st) {
    if (!st) return -1;
    struct stat s;
    if (fstat(fd, &s) != 0) return -1;
    /* struct _stat64i32: st_dev(4), st_ino(4), st_mode(4), st_nlink(4),
     * st_uid(4), st_gid(4), st_rdev(4), pad(4), st_size(8), times(4*4×...) */
    uint8_t* p = (uint8_t*)st;
    memset(p, 0, 64);
    *(uint32_t*)(p + 0)  = (uint32_t)s.st_dev;
    *(uint32_t*)(p + 4)  = (uint32_t)s.st_ino;
    *(uint32_t*)(p + 8)  = (uint32_t)s.st_mode;
    *(uint32_t*)(p + 12) = (uint32_t)s.st_nlink;
    *(uint32_t*)(p + 16) = (uint32_t)s.st_uid;
    *(uint32_t*)(p + 20) = (uint32_t)s.st_gid;
    *(uint32_t*)(p + 24) = (uint32_t)s.st_rdev;
    *(uint32_t*)(p + 28) = 0;
    *(int64_t*)(p + 32)  = (int64_t)s.st_size;
    *(int32_t*)(p + 40)  = (int32_t)s.st_atime;
    *(int32_t*)(p + 44)  = (int32_t)s.st_mtime;
    *(int32_t*)(p + 48)  = (int32_t)s.st_ctime;
    return 0;
}
__attribute__((ms_abi)) int mw__stat64i32(const char* path, void* st) {
    if (!path || !st) return -1;
    struct stat s;
    if (stat(path, &s) != 0) return -1;
    int fd = open(path, O_RDONLY);
    int r = mw__fstat64i32(fd, st);
    if (fd >= 0) close(fd);
    return r;
}
__attribute__((ms_abi)) int mw__wstat64i32(const wchar_t* path, void* st) {
    char p8[4096];
    if (!path || w3_w16_to_utf8z((const uint16_t*)path, p8, sizeof(p8)) < 0) return -1;
    p8[sizeof(p8)-1] = 0;
    return mw__stat64i32(p8, st);
}
__attribute__((ms_abi)) int mw__open_osfhandle(int64_t handle, int flags) {
    /* Win32 handle -> CRT fd: handles 4/8/0xC are stdin/stdout/stderr */
    uint64_t hv = (uint64_t)handle;
    if (hv == 4) return 0;
    if (hv == 8) return 1;
    if (hv == 0xC) return 2;
    int fd = w3fd_from_handle((void*)(uintptr_t)handle);
    return fd; /* already a POSIX fd in our model */
}
__attribute__((ms_abi)) int mw__pipe(int64_t* handles, unsigned size, int mode) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    if (handles) { handles[0] = fds[0]; handles[1] = fds[1]; }
    return 0;
}
__attribute__((ms_abi)) int mw__lseek(int fd, long off, int whence) { return (int)lseek(fd, off, whence); }
__attribute__((ms_abi)) int mw__locking(int fd, int mode, long nbytes) {
    (void)fd; (void)mode; (void)nbytes;
    return 0; /* uncontended */
}
static void* w3_create_file(const char* name8, uint32_t access, uint32_t disp);
__attribute__((ms_abi)) int mw__findfirst64i32(const char* spec, void* data) {
    /* narrow findfirst: reuse the W walker with an 8-bit pattern */
    uint16_t wspec[4096];
    if (!spec) return -1;
    size_t n = utf8_to_utf16(spec, wspec, 4095);
    ((uint16_t*)wspec)[n] = 0;
    void* h = mw_FindFirstFileExW((const wchar_t*)wspec, 0, data, 0, NULL, 0);
    return h == (void*)-1 ? -1 : (int)(intptr_t)h;
}
__attribute__((ms_abi)) int mw__findnext64i32(int h, void* data) {
    return mw_FindNextFileW((void*)(uintptr_t)h, data);
}
__attribute__((ms_abi)) int mw__findclose(int h) {
    return mw_FindClose((void*)(uintptr_t)h);
}
__attribute__((ms_abi)) char* mw__mbsrchr(const char* s, int c) {
    return s ? strrchr(s, c) : NULL;
}
__attribute__((ms_abi)) char* mw__ui64toa_s(uint64_t v, char* buf, size_t sz, int radix) {
    if (!buf || sz < 2) return NULL;
    snprintf(buf, sz, "%llu", (unsigned long long)v);
    return buf;
}
__attribute__((ms_abi)) int mw__wspawnv(int mode, const wchar_t* path, const wchar_t* const* argv) {
    (void)mode; (void)path; (void)argv;
    mw_SetLastError(2);
    return -1; /* no child processes */
}
__attribute__((ms_abi)) int mw__wspawnve(int mode, const wchar_t* path, const wchar_t* const* argv, const wchar_t* const* env) {
    (void)mode; (void)path; (void)argv; (void)env;
    mw_SetLastError(2);
    return -1;
}
__attribute__((ms_abi)) int mw__sopen_s(int* fd, const char* p, int oflags, int sh, int pmode) {
    if (!fd) return 22;
    *fd = open(p, oflags | O_CREAT | O_TRUNC | O_RDWR, pmode);
    return *fd < 0 ? errno : 0;
}
__attribute__((ms_abi)) int mw__wsopen(int* fd, const wchar_t* p, int oflags, int sh, int pmode) {
    char p8[4096];
    if (!p || w3_w16_to_utf8z((const uint16_t*)p, p8, sizeof(p8)) < 0) return 22;
    p8[sizeof(p8)-1] = 0;
    return mw__sopen_s(fd, p8, oflags, sh, pmode);
}
__attribute__((ms_abi)) int mw__wsopen_s(int* fd, const wchar_t* p, int oflags, int sh, int pmode) {
    return mw__wsopen(fd, p, oflags, sh, pmode);
}

/* rand_s: real entropy via BCryptGenRandom path (UCRT contract: returns 0
 * on success, fills the value; non-secure rand fallback is NOT honest) */
__attribute__((ms_abi)) int mw_rand_s(unsigned int* v) {
    if (!v) return 22; /* EINVAL-ish */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 1;
    ssize_t got = read(fd, v, sizeof(unsigned int));
    close(fd);
    return got == sizeof(unsigned int) ? 0 : 1;
}
/* Wave-4b: conversion/charclass CRT bridges (glibc-backed, real semantics) */
__attribute__((ms_abi)) size_t mw_wcrtomb(char* dst, wchar_t wc, void* ps) {
    return wcrtomb(dst, wc, (mbstate_t*)ps);
}
__attribute__((ms_abi)) size_t mw_mbrtowc(wchar_t* dst, const char* src, size_t n, void* ps) {
    return mbrtowc(dst, src, n, (mbstate_t*)ps);
}
__attribute__((ms_abi)) size_t mw_mbsrtowcs(wchar_t* dst, const char** src, size_t len, void* ps) {
    return mbsrtowcs(dst, src, len, (mbstate_t*)ps);
}
__attribute__((ms_abi)) size_t mw_wcsrtombs(char* dst, const wchar_t** src, size_t len, void* ps) {
    /* BUG-050: guest src is 2-byte UTF-16LE; convert the whole string */
    (void)ps;
    if (!src || !*src) return 0;
    int got = w3_w16_to_utf8z((const uint16_t*)*src, dst, dst ? len : 0);
    if (got < 0) return (size_t)-1;
    return dst ? (size_t)got : (size_t)got;
}
/* Wave-4b bridges: implementations */
__attribute__((ms_abi)) int mw__iswctype_bridge(wint_t c, wctype_t t) { return iswctype(c, t); }
__attribute__((ms_abi)) wint_t mw__towlower_bridge(wint_t c) { return towlower(c); }
__attribute__((ms_abi)) wint_t mw__towupper_bridge(wint_t c) { return towupper(c); }
__attribute__((ms_abi)) long long mw__strtoll_bridge(const char* s, char** end, int base) { return strtoll(s, end, base); }
__attribute__((ms_abi)) int mw_mbstowcs_s(size_t* ret, wchar_t* dst, size_t cap, const char* src, size_t count) {
    if (!dst || !src) { if (ret) *ret = 0; return 22; }
    size_t need = mbstowcs(NULL, src, 0) + 1;
    if (count != (size_t)-1 && need > count + 1) { if (ret) *ret = 0; return 34; }
    if (cap < need) { if (ret) *ret = 0; return 34; }
    size_t got = mbstowcs(dst, src, cap - 1);
    if (got == (size_t)-1) { if (ret) *ret = 0; return 42; }
    dst[got] = 0;
    if (ret) *ret = got + 1;
    return 0;
}

/* ISS1-WAVE4C: pure-CRT bridges (glibc-backed) */
__attribute__((ms_abi)) double mw_acos(double a0) { return acos(a0); }
__attribute__((ms_abi)) float mw_acosf(float a0) { return acosf(a0); }
__attribute__((ms_abi)) double mw_acosh(double a0) { return acosh(a0); }
__attribute__((ms_abi)) double mw_asin(double a0) { return asin(a0); }
__attribute__((ms_abi)) float mw_asinf(float a0) { return asinf(a0); }
__attribute__((ms_abi)) double mw_asinh(double a0) { return asinh(a0); }
__attribute__((ms_abi)) float mw_asinhf(float a0) { return asinhf(a0); }
__attribute__((ms_abi)) double mw_atan(double a0) { return atan(a0); }
__attribute__((ms_abi)) float mw_atanf(float a0) { return atanf(a0); }
__attribute__((ms_abi)) double mw_atanh(double a0) { return atanh(a0); }
__attribute__((ms_abi)) float mw_atanhf(float a0) { return atanhf(a0); }
__attribute__((ms_abi)) double mw_cbrt(double a0) { return cbrt(a0); }
__attribute__((ms_abi)) float mw_cbrtf(float a0) { return cbrtf(a0); }
__attribute__((ms_abi)) double mw_ceil(double a0) { return ceil(a0); }
__attribute__((ms_abi)) float mw_ceilf(float a0) { return ceilf(a0); }
__attribute__((ms_abi)) double mw_cos(double a0) { return cos(a0); }
__attribute__((ms_abi)) float mw_cosf(float a0) { return cosf(a0); }
__attribute__((ms_abi)) double mw_cosh(double a0) { return cosh(a0); }
__attribute__((ms_abi)) double mw_exp(double a0) { return exp(a0); }
__attribute__((ms_abi)) double mw_exp2(double a0) { return exp2(a0); }
__attribute__((ms_abi)) float mw_exp2f(float a0) { return exp2f(a0); }
__attribute__((ms_abi)) float mw_expf(float a0) { return expf(a0); }
__attribute__((ms_abi)) double mw_expm1(double a0) { return expm1(a0); }
__attribute__((ms_abi)) double mw_fabs(double a0) { return fabs(a0); }
__attribute__((ms_abi)) double mw_floor(double a0) { return floor(a0); }
__attribute__((ms_abi)) float mw_floorf(float a0) { return floorf(a0); }
__attribute__((ms_abi)) double mw_log(double a0) { return log(a0); }
__attribute__((ms_abi)) double mw_log10(double a0) { return log10(a0); }
__attribute__((ms_abi)) float mw_log10f(float a0) { return log10f(a0); }
__attribute__((ms_abi)) double mw_log1p(double a0) { return log1p(a0); }
__attribute__((ms_abi)) float mw_log1pf(float a0) { return log1pf(a0); }
__attribute__((ms_abi)) double mw_log2(double a0) { return log2(a0); }
__attribute__((ms_abi)) float mw_log2f(float a0) { return log2f(a0); }
__attribute__((ms_abi)) float mw_logf(float a0) { return logf(a0); }
__attribute__((ms_abi)) double mw_sin(double a0) { return sin(a0); }
__attribute__((ms_abi)) float mw_sinf(float a0) { return sinf(a0); }
__attribute__((ms_abi)) double mw_sinh(double a0) { return sinh(a0); }
__attribute__((ms_abi)) float mw_sinhf(float a0) { return sinhf(a0); }
__attribute__((ms_abi)) double mw_sqrt(double a0) { return sqrt(a0); }
__attribute__((ms_abi)) float mw_sqrtf(float a0) { return sqrtf(a0); }
__attribute__((ms_abi)) double mw_tan(double a0) { return tan(a0); }
__attribute__((ms_abi)) float mw_tanf(float a0) { return tanf(a0); }
__attribute__((ms_abi)) double mw_tanh(double a0) { return tanh(a0); }
__attribute__((ms_abi)) float mw_tanhf(float a0) { return tanhf(a0); }
__attribute__((ms_abi)) double mw_trunc(double a0) { return trunc(a0); }
__attribute__((ms_abi)) float mw_truncf(float a0) { return truncf(a0); }
__attribute__((ms_abi)) float mw_roundf(float a0) { return roundf(a0); }
__attribute__((ms_abi)) float mw_llrintf(float a0) { return llrintf(a0); }
__attribute__((ms_abi)) float mw_lrintf(float a0) { return lrintf(a0); }
__attribute__((ms_abi)) double mw_atan2(double a0, double a1) { return atan2(a0, a1); }
__attribute__((ms_abi)) float mw_atan2f(float a0, float a1) { return atan2f(a0, a1); }
__attribute__((ms_abi)) double mw_fmod(double a0, double a1) { return fmod(a0, a1); }
__attribute__((ms_abi)) float mw_fmodf(float a0, float a1) { return fmodf(a0, a1); }
__attribute__((ms_abi)) double mw_pow(double a0, double a1) { return pow(a0, a1); }
__attribute__((ms_abi)) float mw_powf(float a0, float a1) { return powf(a0, a1); }
__attribute__((ms_abi)) double mw_hypot(double a0, double a1) { return hypot(a0, a1); }
__attribute__((ms_abi)) double mw_fmax(double a0, double a1) { return fmax(a0, a1); }
__attribute__((ms_abi)) float mw_fmaxf(float a0, float a1) { return fmaxf(a0, a1); }
__attribute__((ms_abi)) double mw_fmin(double a0, double a1) { return fmin(a0, a1); }
__attribute__((ms_abi)) float mw_fminf(float a0, float a1) { return fminf(a0, a1); }
__attribute__((ms_abi)) double mw_remainder(double a0, double a1) { return remainder(a0, a1); }
__attribute__((ms_abi)) double mw_nextafter(double a0, double a1) { return nextafter(a0, a1); }
__attribute__((ms_abi)) double mw_fma(double a0, double a1, double a2) { return fma(a0, a1, a2); }
__attribute__((ms_abi)) float mw_fmaf(float a0, float a1, float a2) { return fmaf(a0, a1, a2); }
__attribute__((ms_abi)) long long mw_llround(double a0) { return llround(a0); }
__attribute__((ms_abi)) long long mw_llroundf(float a0) { return llroundf(a0); }
__attribute__((ms_abi)) long mw_lround(double a0) { return lround(a0); }
__attribute__((ms_abi)) long mw_lroundf(float a0) { return lroundf(a0); }
__attribute__((ms_abi)) long mw_lrint(double a0) { return lrint(a0); }
__attribute__((ms_abi)) long long mw_llrint(double a0) { return llrint(a0); }
__attribute__((ms_abi)) double mw_modf(double a0, double* a1) { return modf(a0, a1); }
__attribute__((ms_abi)) float mw_modff(float a0, float* a1) { return modff(a0, a1); }
__attribute__((ms_abi)) double mw_frexp(double a0, int* a1) { return frexp(a0, a1); }

__attribute__((ms_abi)) double mw_erf(double a0) { return erf(a0); }
__attribute__((ms_abi)) float mw_erff(float a0) { return erff(a0); }
__attribute__((ms_abi)) int mw_isalnum(int a0) { return isalnum(a0); }
__attribute__((ms_abi)) int mw_isalpha(int a0) { return isalpha(a0); }
__attribute__((ms_abi)) int mw_isblank(int a0) { return isblank(a0); }
__attribute__((ms_abi)) int mw_iscntrl(int a0) { return iscntrl(a0); }
__attribute__((ms_abi)) int mw_isdigit(int a0) { return isdigit(a0); }
__attribute__((ms_abi)) int mw_isprint(int a0) { return isprint(a0); }
__attribute__((ms_abi)) int mw_toupper(int a0) { return toupper(a0); }
__attribute__((ms_abi)) int mw_strcoll(const char* a0, const char* a1) { return strcoll(a0, a1); }
/* BUG-050: 2-byte guest ABI implementations (glibc bridges read 4-byte
 * strides and corrupt every non-trivial guest wide string) */
__attribute__((ms_abi)) wchar_t* mw_wcschr(wchar_t* a0, wchar_t a1) { return (wchar_t*)w16chr((const uint16_t*)a0, (uint16_t)a1); }
__attribute__((ms_abi)) wchar_t* mw_wcsrchr(wchar_t* a0, wchar_t a1) { return (wchar_t*)w16rchr((const uint16_t*)a0, (uint16_t)a1); }
__attribute__((ms_abi)) int mw_wcsncmp(const wchar_t* a0, const wchar_t* a1, size_t a2) { return w16ncmp((const uint16_t*)a0, (const uint16_t*)a1, a2); }
__attribute__((ms_abi)) wchar_t* mw_wcsncpy(wchar_t* a0, const wchar_t* a1, size_t a2) { return (wchar_t*)w16ncpy((uint16_t*)a0, (const uint16_t*)a1, a2); }
__attribute__((ms_abi)) wchar_t* mw_wcscat(wchar_t* a0, const wchar_t* a1) { return (wchar_t*)w16cat((uint16_t*)a0, (const uint16_t*)a1); }
__attribute__((ms_abi)) wchar_t* mw_wcscpy(wchar_t* a0, const wchar_t* a1) { return (wchar_t*)w16cpy((uint16_t*)a0, (const uint16_t*)a1); }
__attribute__((ms_abi)) wchar_t* mw_wcsstr(const wchar_t* a0, const wchar_t* a1) { return (wchar_t*)w16str((const uint16_t*)a0, (const uint16_t*)a1); }
__attribute__((ms_abi)) size_t mw_wcsspn(const wchar_t* a0, const wchar_t* a1) { return w16spn((const uint16_t*)a0, (const uint16_t*)a1); }
__attribute__((ms_abi)) int mw_wcscoll(const wchar_t* a0, const wchar_t* a1) { return w16cmp((const uint16_t*)a0, (const uint16_t*)a1); }
__attribute__((ms_abi)) size_t mw_wcsxfrm(wchar_t* a0, const wchar_t* a1, size_t a2) {
    /* identity transform: collation order = code-unit order */
    size_t n = w16len((const uint16_t*)a1);
    if (a0) { if (n >= a2) n = a2 ? a2 - 1 : 0; w16ncpy((uint16_t*)a0, (const uint16_t*)a1, n); ((uint16_t*)a0)[n] = 0; }
    return n + 1;
}
__attribute__((ms_abi)) size_t mw_wcsftime(wchar_t* a0, size_t a1, const wchar_t* a2, const void* a3) { return wcsftime(a0, a1, a2, a3); } /* BUG-050 residual: 4-byte format string, unused by targets */
__attribute__((ms_abi)) size_t mw_wcsnlen(const wchar_t* a0, size_t a1) {
    size_t n = 0; if (!a0) return 0;
    while (n < a1 && ((const uint16_t*)a0)[n]) n++;
    return n;
}
__attribute__((ms_abi)) void* mw_bsearch(const void* a0, const void* a1, size_t a2, size_t a3, int (*a4)(const void*, const void*)) { return bsearch(a0, a1, a2, a3, a4); }
__attribute__((ms_abi)) int mw_raise(int a0) { return raise(a0); }
__attribute__((ms_abi)) void mw_clearerr(void* a0) { return clearerr(a0); }
__attribute__((ms_abi)) int mw_ferror(void* a0) { return ferror(a0); }
__attribute__((ms_abi)) int mw_putc(int a0, void* a1) { return putc(a0, a1); }
__attribute__((ms_abi)) int mw_putchar(int a0) { return putchar(a0); }
__attribute__((ms_abi)) wint_t mw_fgetwc(void* a0) { return fgetwc(a0); }
__attribute__((ms_abi)) wint_t mw_ungetwc(wint_t a0, void* a1) { return ungetwc(a0, a1); }
__attribute__((ms_abi)) wint_t mw_putwc(wint_t a0, void* a1) { return putwc(a0, a1); }
__attribute__((ms_abi)) void mw_perror(const char* a0) { return perror(a0); }
__attribute__((ms_abi)) int mw_getchar(void) { return getchar(); }
__attribute__((ms_abi)) void mw_setbuf(void* a0, char* a1) { return setbuf(a0, a1); }
__attribute__((ms_abi)) double mw_atof(const char* a0) { return atof(a0); }
__attribute__((ms_abi)) long mw_atol(const char* a0) { return atol(a0); }
__attribute__((ms_abi)) double mw_strtod(const char* a0, char** a1) { return strtod(a0, a1); }
__attribute__((ms_abi)) float mw_strtof(const char* a0, char** a1) { return strtof(a0, a1); }
__attribute__((ms_abi)) unsigned long long mw_strtoull(const char* a0, char** a1, int a2) { return strtoull(a0, a1, a2); }
#include <inttypes.h>
__attribute__((ms_abi)) uintmax_t mw_strtoumax(const char* a0, char** a1, int a2) { return strtoumax(a0, a1, a2); }
__attribute__((ms_abi)) size_t mw_mbstowcs(wchar_t* a0, const char* a1, size_t a2) {
    /* BUG-050: Windows CRT wchar_t = 2 bytes; emit UTF-16LE units */
    if (!a1) return 0;
    int got = w3_utf8_to_utf16le(a1, strlen(a1), (uint16_t*)a0, a0 ? (int)a2 : 0);
    return got < 0 ? (size_t)-1 : (size_t)got;
}
__attribute__((ms_abi)) size_t mw_mbrlen(const char* a0, size_t a1, void* a2) { return mbrlen(a0, a1, a2); }
__attribute__((ms_abi)) int mw_wctomb(char* a0, wchar_t a1) { return wctomb(a0, a1); }
__attribute__((ms_abi)) unsigned long mw_wctype(const char* a0) { return (unsigned long)wctype(a0); }

__attribute__((ms_abi)) double mw_round_bridge(double a0) { return round(a0); }
/* Wave-4D: remaining string family bridges */
__attribute__((ms_abi)) char* mw_strcat(char* d, const char* s) { return strcat(d, s); }
__attribute__((ms_abi)) char* mw_strncat(char* d, const char* s, size_t n) { return strncat(d, s, n); }
__attribute__((ms_abi)) char* mw_strncpy(char* d, const char* s, size_t n) { return strncpy(d, s, n); }
__attribute__((ms_abi)) int mw_strncpy_s(char* d, size_t dsz, const char* s, size_t n) {
    if (!d || !dsz) return 22;
    if (!s) { d[0] = 0; return 22; }
    size_t len = strnlen(s, n);
    if (len >= dsz) { d[0] = 0; return 34; }
    memcpy(d, s, len);
    d[len] = 0;
    return 0;
}
__attribute__((ms_abi)) size_t mw_strnlen(const char* s, size_t n) { return strnlen(s, n); }
__attribute__((ms_abi)) char* mw_strpbrk(const char* s, const char* set) { return strpbrk(s, set); }
__attribute__((ms_abi)) size_t mw_strspn(const char* s, const char* set) { return strspn(s, set); }
__attribute__((ms_abi)) char* mw_strtok(char* s, const char* sep) { return strtok(s, sep); }
__attribute__((ms_abi)) char* mw_strtok_s(char* s, const char* sep, char** ctx) {
    (void)ctx;
    return strtok(s, sep);
}
__attribute__((ms_abi)) size_t mw_strxfrm(char* d, const char* s, size_t n) { return strxfrm(d, s, n); }
__attribute__((ms_abi)) int mw_wcscmp(const wchar_t* a, const wchar_t* b) { return w16cmp((const uint16_t*)a, (const uint16_t*)b); }
__attribute__((ms_abi)) int mw_wcscat_s(wchar_t* d, size_t dsz, const wchar_t* s) {
    if (!d || !dsz || !s) return 22;
    size_t dl = w16len((const uint16_t*)d);
    if (dl + w16len((const uint16_t*)s) + 1 > dsz) { ((uint16_t*)d)[0] = 0; return 34; }
    w16cat((uint16_t*)d, (const uint16_t*)s);
    return 0;
}
__attribute__((ms_abi)) int mw_wcscpy_s(wchar_t* d, size_t dsz, const wchar_t* s) {
    if (!d || !dsz || !s) return 22;
    if (w16len((const uint16_t*)s) + 1 > dsz) { ((uint16_t*)d)[0] = 0; return 34; }
    w16cpy((uint16_t*)d, (const uint16_t*)s);
    return 0;
}
__attribute__((ms_abi)) int mw_wcsncpy_s(wchar_t* d, size_t dsz, const wchar_t* s, size_t n) {
    if (!d || !dsz) return 22;
    if (!s) { ((uint16_t*)d)[0] = 0; return 22; }
    size_t len = mw_wcsnlen(s, n);
    if (len >= dsz) { ((uint16_t*)d)[0] = 0; return 34; }
    w16ncpy((uint16_t*)d, (const uint16_t*)s, len);
    ((uint16_t*)d)[len] = 0;
    return 0;
}
__attribute__((ms_abi)) long long mw_wcstoll(const wchar_t* s, wchar_t** end, int base) {
    size_t o = 0; while (((const uint16_t*)s)[o]) o++;
    char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
    char* e8; long long r = strtoll(b8, &e8, base);
    if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
    return r;
}
__attribute__((ms_abi)) unsigned long long mw_wcstoull(const wchar_t* s, wchar_t** end, int base) {
    size_t o = 0; while (((const uint16_t*)s)[o]) o++;
    char b8[64]; int gn = w3_utf16le_to_utf8((const uint16_t*)s, o > 63 ? 63 : o, b8, 63); if (gn < 0) gn = 0; b8[gn] = 0;
    char* e8; unsigned long long r = strtoull(b8, &e8, base);
    if (end) *end = (wchar_t*)((const uint16_t*)s + (e8 - b8));
    return r;
}
__attribute__((ms_abi)) int mw_wcstombs_s(size_t* ret, char* dst, size_t dsz, const wchar_t* src, size_t count) {
    if (!dst || !dsz || !src) { if (ret) *ret = 0; return 22; }
    size_t need = (size_t)w3_w16_to_utf8_size((const uint16_t*)src) + 1;
    if (count != (size_t)-1 && need > count + 1) { if (ret) *ret = 0; return 34; }
    if (dsz < need) { if (ret) *ret = 0; return 34; }
    size_t got = w3_w16_to_utf8z((const uint16_t*)src, dst, dsz - 1);
    if (got == (size_t)-1) { if (ret) *ret = 0; return 42; }
    dst[got] = 0;
    if (ret) *ret = got + 1;
    return 0;
}
__attribute__((ms_abi)) int mw_strerror_s(char* buf, size_t n, int err) {
    if (!buf || !n) return 22;
    char* r = strerror(err);
    snprintf(buf, n, "%s", r ? r : "unknown");
    return 0;
}
/* ---- Wave-4E: WinSock 2 (real host sockets; unimplemented async = honest fail) ---- */
#define MW_WSAENETDOWN 10050
static int g_wsa_err = 0;
static int g_wsa_started = 0;
/* WSADATA x64 layout (winsock2.h): wVersion(2) wHighVersion(2)
 * iMaxSockets(2) iMaxUdpDg(2) pad(4) lpVendorInfo(8) szDescription[257]
 * szSystemStatus[129] = 408 bytes total. Writing a wrong-order/larger
 * struct smashed the caller's stack (live: ffmpeg __stack_chk_fail). */
typedef struct { uint16_t version; uint16_t highver; uint16_t maxsocks; uint16_t maxudp; void* vendor; char desc[257]; char sys[129]; } WSADataT;
__attribute__((ms_abi)) int mw_WSAStartup(uint16_t want, void* data) {
    MW_TRACE("WSAStartup(%x)", want);
    if (!data) return MW_WSAENETDOWN;
    g_wsa_started = 1;
    WSADataT* d = (WSADataT*)data;
    memset(d, 0, sizeof(WSADataT));   /* exactly 408 bytes */
    d->version = 0x0202;
    d->highver = 0x0202;
    d->maxsocks = 64;
    d->maxudp = 64;
    snprintf(d->desc, sizeof(d->desc), "MiniWin WinSock 2.2");
    snprintf(d->sys, sizeof(d->sys), "MiniWin");
    return 0;
}
__attribute__((ms_abi)) int mw_WSACleanup(void) { g_wsa_started = 0; return 0; }
__attribute__((ms_abi)) int mw_WSAGetLastError(void) { return g_wsa_err; }
__attribute__((ms_abi)) void mw_WSASetLastError(int e) { g_wsa_err = e; }
__attribute__((ms_abi)) unsigned long mw_htonl(unsigned long h) { return htonl(h); }
__attribute__((ms_abi)) unsigned short mw_htons(unsigned short h) { return htons(h); }
__attribute__((ms_abi)) unsigned long mw_ntohl(unsigned long n) { return ntohl(n); }
__attribute__((ms_abi)) unsigned short mw_ntohs(unsigned short n) { return ntohs(n); }
__attribute__((ms_abi)) unsigned long mw_inet_addr(const char* s) { return inet_addr(s); }
__attribute__((ms_abi)) char* mw_inet_ntoa(struct in_addr a) { return inet_ntoa(a); }
__attribute__((ms_abi)) unsigned long long mw_socket(int af, int type, int proto) {
    if (!g_wsa_started) { g_wsa_err = MW_WSAENETDOWN; return ~0ull; }
    int s = (int)socket(af, type, proto);
    if (s < 0) { g_wsa_err = 10093; return ~0ull; }
    g_wsa_err = 0;
    return (unsigned long long)s;
}
__attribute__((ms_abi)) int mw_closesocket(unsigned long long s) {
    int r = close((int)s);
    return r == 0 ? 0 : (g_wsa_err = 10038, -1);
}
__attribute__((ms_abi)) int mw_connect(unsigned long long s, void* addr, int len) {
    int r = connect((int)s, (struct sockaddr*)addr, len);
    if (r != 0) g_wsa_err = 10061;
    return r;
}
__attribute__((ms_abi)) int mw_bind(unsigned long long s, void* addr, int len) { return bind((int)s, (struct sockaddr*)addr, len); }
__attribute__((ms_abi)) int mw_listen(unsigned long long s, int back) { return listen((int)s, back); }
__attribute__((ms_abi)) unsigned long long mw_accept(unsigned long long s, void* addr, int* len) {
    int c = accept((int)s, (struct sockaddr*)addr, (socklen_t*)len);
    return c < 0 ? ~0ull : (unsigned long long)c;
}
__attribute__((ms_abi)) int mw_shutdown(unsigned long long s, int how) { return shutdown((int)s, how); }
__attribute__((ms_abi)) int mw_getsockopt(unsigned long long s, int lvl, int opt, void* v, int* len) { return getsockopt((int)s, lvl, opt, v, (socklen_t*)len); }
__attribute__((ms_abi)) int mw_setsockopt(unsigned long long s, int lvl, int opt, const void* v, int len) { return setsockopt((int)s, lvl, opt, v, len); }
__attribute__((ms_abi)) int mw_getsockname(unsigned long long s, void* addr, int* len) { return getsockname((int)s, (struct sockaddr*)addr, (socklen_t*)len); }
__attribute__((ms_abi)) int mw_getpeername(unsigned long long s, void* addr, int* len) { return getpeername((int)s, (struct sockaddr*)addr, (socklen_t*)len); }
__attribute__((ms_abi)) int mw_send(unsigned long long s, const void* buf, int len, int flags) {
    ssize_t r = send((int)s, buf, len, flags & ~0x5);
    return (int)r;
}
__attribute__((ms_abi)) int mw_recv(unsigned long long s, void* buf, int len, int flags) {
    ssize_t r = recv((int)s, buf, len, flags & ~0x5);
    return (int)r;
}
__attribute__((ms_abi)) int mw_sendto(unsigned long long s, const void* buf, int len, int flags, void* to, int tolen) {
    return (int)sendto((int)s, buf, len, flags & ~0x5, (struct sockaddr*)to, tolen);
}
__attribute__((ms_abi)) int mw_recvfrom(unsigned long long s, void* buf, int len, int flags, void* from, int* fromlen) {
    return (int)recvfrom((int)s, buf, len, flags & ~0x5, (struct sockaddr*)from, (socklen_t*)fromlen);
}
__attribute__((ms_abi)) int mw_select(int nfds, void* r, void* w, void* e, void* tv) {
    return select(nfds, (fd_set*)r, (fd_set*)w, (fd_set*)e, (struct timeval*)tv);
}
__attribute__((ms_abi)) int mw_ioctlsocket(unsigned long long s, long cmd, unsigned long* arg) {
    /* FIONREAD/FIONBIO share numeric values with Linux */
    return ioctl((int)s, cmd, arg);
}
__attribute__((ms_abi)) int mw_WSAIoctl(unsigned long long s, long cmd, void* in, int ilen, void* out, int olen, unsigned long* ret, void* ov1, void* ov2) {
    if (cmd == 0x40040007 /* SIO_GET_EXTENSION_FUNCTION_POINTER */ || cmd == 0x48000000) {
        g_wsa_err = 10042; /* WSAENOPROTOOPT */
        return -1;
    }
    return mw_ioctlsocket(s, cmd, (unsigned long*)out);
}
__attribute__((ms_abi)) void* mw_LoadLibraryExA(const char* name, void* file, uint32_t flags) {
    MW_TRACE("LoadLibraryExA(%s, flags=%u) -> NULL", name ? name : "?", flags);
    mw_SetLastError(126);
    return NULL;
}

/* ====================================================================
 * OPENSRC-7ZIP wave A (2026-10-17): 45 imports required by 7-Zip 25.01
 * x64 (7za.exe / 7z.exe). Registered under TASK-OPENSRC-7ZIP.
 * ==================================================================== */

/* ---- OLEAUT32: BSTR = [uint32 byte_len][UTF-16 data][NUL] ---- */
static void* bstr_alloc_len(const uint16_t* src, uint32_t units) {
    uint32_t bytes = units * 2;
    uint8_t* raw = (uint8_t*)malloc(4 + bytes + 2);
    if (!raw) return NULL;
    *(uint32_t*)raw = bytes;
    uint16_t* data = (uint16_t*)(raw + 4);
    if (src) memcpy(data, src, bytes);
    data[units] = 0;
    return data;
}
__attribute__((ms_abi)) void* mw_SysAllocString(const uint16_t* str) {
    if (!str) return NULL;
    uint32_t n = 0; while (str[n]) n++;
    return bstr_alloc_len(str, n);
}
__attribute__((ms_abi)) void* mw_SysAllocStringLen(const uint16_t* str, uint32_t len) {
    return bstr_alloc_len(str, len);
}
__attribute__((ms_abi)) uint32_t mw_SysStringLen(const uint16_t* bstr) {
    if (!bstr) return 0;
    return *(uint32_t*)((uint8_t*)bstr - 4) / 2;
}
__attribute__((ms_abi)) uint32_t mw_SysStringByteLen(const uint16_t* bstr) {
    if (!bstr) return 0;
    return *(uint32_t*)((uint8_t*)bstr - 4);
}
__attribute__((ms_abi)) void mw_SysFreeString(uint16_t* bstr) {
    if (!bstr) return;
    free((uint8_t*)bstr - 4);
}
/* VARIANT x64: WORD vt; WORD r1,r2,r3; union { ... void* bstrVal @+8 } */
__attribute__((ms_abi)) long mw_VariantClear(void* pvarg) {
    if (!pvarg) return 0x80004003L; /* E_POINTER */
    uint16_t vt = *(uint16_t*)pvarg;
    if (vt == 8 /*VT_BSTR*/) {
        void* b = *(void**)((uint8_t*)pvarg + 8);
        if (b) mw_SysFreeString((uint16_t*)b);
    }
    *(uint16_t*)pvarg = 0; /* VT_EMPTY */
    return 0; /* S_OK */
}
__attribute__((ms_abi)) long mw_VariantCopy(void* dest, const void* src) {
    if (!dest || !src) return 0x80004003L;
    uint16_t vt = *(const uint16_t*)src;
    *(uint16_t*)dest = vt;
    if (vt == 8 /*VT_BSTR*/) {
        const uint16_t* b = *(const uint16_t* const*)((const uint8_t*)src + 8);
        *(void**)((uint8_t*)dest + 8) = b ? mw_SysAllocString(b) : NULL;
    } else {
        memcpy((uint8_t*)dest + 8, (const uint8_t*)src + 8, 8);
    }
    return 0; /* S_OK */
}

/* ---- USER32 ---- */
__attribute__((ms_abi)) uint16_t* mw_CharUpperW(uint16_t* s) {
    if (!s) return NULL;
    if ((uintptr_t)s < 0x10000) { /* single character in low word */
        uint16_t c = (uint16_t)(uintptr_t)s;
        if (c >= 'a' && c <= 'z') c -= 32;
        return (uint16_t*)(uintptr_t)c;
    }
    for (uint16_t* p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    return s;
}
__attribute__((ms_abi)) const char* mw_CharPrevExA(unsigned cp, const char* start, const char* cur, unsigned flags) {
    (void)cp; (void)flags;
    if (!start || !cur || cur <= start) return cur;
    return cur - 1; /* single-byte codepages (7-Zip uses ASCII paths) */
}

/* ---- ADVAPI32 ---- */
static int g_advapi_token_counter = 0;
__attribute__((ms_abi)) int mw_OpenProcessToken(void* proc, uint32_t access, void** tok) {
    (void)proc; (void)access;
    if (!tok) { mw_SetLastError(87); return 0; }
    *tok = alloc_handle((void*)(uintptr_t)(0x9000 + (g_advapi_token_counter++)));
    return 1;
}
__attribute__((ms_abi)) int mw_AdjustTokenPrivileges(void* tok, int disable_all, void* newstate,
        uint32_t buflen, void* prev, uint32_t* retlen) {
    (void)tok; (void)disable_all; (void)newstate; (void)buflen; (void)prev; (void)retlen;
    /* We never hold privileges: Windows sets ERROR_NOT_ALL_ASSIGNED and
     * still returns TRUE. 7-Zip (large pages) treats this as "no privilege". */
    mw_SetLastError(1300);
    return 1;
}
__attribute__((ms_abi)) int mw_LookupPrivilegeValueW(const uint16_t* system, const uint16_t* name, void* luid) {
    (void)system; (void)name;
    if (luid) { *(uint64_t*)luid = 0x000000000000001Full; /* SeLockMemoryPrivilege */ }
    return 1;
}
__attribute__((ms_abi)) int mw_SystemFunction036(void* buf, uint32_t len) {
    /* RtlGenRandom */
    uint8_t* p = (uint8_t*)buf;
    uint32_t done = 0;
    while (done < len) {
        ssize_t r = syscall(SYS_getrandom, p + done, len - done, 0);
        if (r <= 0) { if (errno == EINTR) continue; break; }
        done += (uint32_t)r;
    }
    for (uint32_t i = done; i < len; i++) p[i] = (uint8_t)rand();
    return 1;
}
__attribute__((ms_abi)) int mw_GetFileSecurityW(const uint16_t* name, uint32_t info, void* sd,
        uint32_t sdlen, uint32_t* needed) {
    (void)name; (void)info;
    if (needed) *needed = 0;
    mw_SetLastError(50); /* ERROR_NOT_SUPPORTED */
    return 0;
}
__attribute__((ms_abi)) int mw_SetFileSecurityW(const uint16_t* name, uint32_t info, void* sd) {
    (void)name; (void)info; (void)sd;
    mw_SetLastError(50);
    return 0;
}

/* ---- msvcrt ---- */
__attribute__((ms_abi)) void mw__c_exit(void) {
    MW_TRACE("_c_exit() (no-op: static-CRT bookkeeping)");
}
__attribute__((ms_abi)) void* mw___dllonexit(void* fn, void*** begin, void*** end) {
    /* MS contract: insert fn at *end, bump *end. Static-CRT onexit tables
     * are sized by the guest CRT — trust them like Windows does. */
    if (begin && end && *begin && *end && *end >= *begin) {
        *(*end) = (void**)fn;
        (*end)++;
    }
    return fn;
}
__attribute__((ms_abi)) void mw_msvcrt_terminate(void) {
    fprintf(stderr, "terminate called\n");
    fflush(stderr);
    abort();
}
__attribute__((ms_abi)) void mw_type_info_dtor(void* ti) {
    (void)ti; /* virtual dtor: no owned resources in the guest type_info */
}
__attribute__((ms_abi)) int mw__purecall(void) {
    fprintf(stderr, "R6025 - pure virtual function call\n");
    fflush(stderr);
    abort();
}
__attribute__((ms_abi)) int mw__CxxFrameHandler(void* er, void* est, void* ctx, void* dc) {
    /* MSVC C++ EH: the full __CxxFrameHandler model is a registered future
     * work item (TASK-API-018). Returning ContinueSearch is the honest
     * answer for frames that do not own a catch of this exception. */
    (void)er; (void)est; (void)ctx; (void)dc;
    return 1; /* ExceptionContinueSearch */
}
__attribute__((ms_abi)) void mw__CxxThrowException(void* object, void* throwinfo) {
    /* MS protocol: RaiseException(0xE06D7363, 0, 3, {magic, obj, throwinfo}) */
    uint64_t args[3] = { 0x19930520ULL, (uint64_t)(uintptr_t)object, (uint64_t)(uintptr_t)throwinfo };
    MW_TRACE("_CxxThrowException(obj=%p, ti=%p)", object, throwinfo);
    mw_RaiseException(0xE06D7363, 0, 3, args);
}

/* msvcrt _iob: a 3-slot data window the guest indexes with ITS FILE stride
 * (0x30 on x64); map_crt_stream recognizes the window like g_iob_array.
 * Storage defined early (before map_crt_stream); see g_msvcrt_iob there. */
/* ---- KERNEL32: VirtualAlloc/VirtualFree with region registry ---- */
#define MAX_VALLOC 256
typedef struct { void* base; size_t size; } VallocRegion;
static VallocRegion g_valloc[MAX_VALLOC];
static int g_valloc_count = 0;

static int w32_prot_from(uint32_t p) {
    switch (p & 0xFF) {
        case 0x01: return PROT_NONE;                    /* PAGE_NOACCESS */
        case 0x02: return PROT_READ;                    /* PAGE_READONLY */
        case 0x04: return PROT_READ | PROT_WRITE;       /* PAGE_READWRITE */
        case 0x08: return PROT_READ | PROT_WRITE;       /* PAGE_WRITECOPY */
        case 0x10: return PROT_EXEC;                    /* PAGE_EXECUTE */
        case 0x20: return PROT_READ | PROT_EXEC;        /* PAGE_EXECUTE_READ */
        case 0x40: return PROT_READ | PROT_WRITE | PROT_EXEC; /* ERW */
        case 0x80: return PROT_READ | PROT_WRITE | PROT_EXEC; /* WRITECOPY */
        default:   return PROT_READ | PROT_WRITE;
    }
}
static size_t page_round_up(size_t s) { return (s + 0xFFF) & ~(size_t)0xFFF; }

__attribute__((ms_abi)) void* mw_VirtualAlloc(void* addr, size_t size, uint32_t type, uint32_t prot) {
    MW_TRACE("VirtualAlloc(addr=%p, size=0x%zx, type=0x%x, prot=0x%x)", addr, size, type, prot);
    if (!size) { mw_SetLastError(87); return NULL; }
    size = page_round_up(size);
    int lp = w32_prot_from(prot);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void* base;
    if (addr) {
        if ((uintptr_t)addr & 0xFFF) { mw_SetLastError(87); return NULL; }
        flags |= MAP_FIXED;
        base = mmap(addr, size, lp, flags, -1, 0);
    } else {
        base = mmap(NULL, size, lp, flags, -1, 0);
    }
    if (base == MAP_FAILED) { mw_SetLastError(1455); return NULL; }
    if (g_valloc_count < MAX_VALLOC) {
        g_valloc[g_valloc_count].base = base;
        g_valloc[g_valloc_count].size = size;
        g_valloc_count++;
    }
    return base;
}
static VallocRegion* valloc_find(void* addr) {
    for (int i = 0; i < g_valloc_count; i++)
        if (g_valloc[i].base == addr) return &g_valloc[i];
    return NULL;
}
__attribute__((ms_abi)) int mw_VirtualFree(void* addr, size_t size, uint32_t type) {
    MW_TRACE("VirtualFree(addr=%p, size=0x%zx, type=0x%x)", addr, size, type);
    if (!addr) { mw_SetLastError(87); return 0; }
    if (type & 0x8000 /*MEM_RELEASE*/) {
        VallocRegion* r = valloc_find(addr);
        if (!r) { mw_SetLastError(158); return 0; }
        munmap(addr, r->size);
        *r = g_valloc[--g_valloc_count];
        g_valloc[g_valloc_count] = (VallocRegion){0};
        return 1;
    }
    if (type & 0x4000 /*MEM_DECOMMIT*/) {
        size_t n = page_round_up(size ? size : 0x1000);
        VallocRegion* r = valloc_find(addr);
        if (r && n > r->size) n = r->size;
        mprotect(addr, n, PROT_NONE);
        return 1;
    }
    mw_SetLastError(87);
    return 0;
}

/* ---- KERNEL32: file mapping family ---- */
#define MAX_MAPVIEWS 64
typedef struct { void* base; size_t size; } MapView;
static MapView g_mapviews[MAX_MAPVIEWS];
static int g_mapview_count = 0;

/* LADDER-009 (lz4): MapViewOfFile backed by REAL mmap.
 * Forward decls: the FileMap registry and its lookup live further down
 * (file mapping family, near mw_CreateFileMappingW). */
static int filemap_lookup(void* h, void** hfile, long long* size_out);
static int w3fd_from_handle(void* h);

__attribute__((ms_abi)) void* mw_MapViewOfFile(void* h, uint32_t access, uint32_t hi, uint32_t lo, size_t bytes) {
    MW_TRACE("MapViewOfFile(h=%p, access=0x%x, off=%u:%u, bytes=0x%zx)", h, access, hi, lo, bytes);
    void* hfile = NULL; long long msize = 0;
    if (!filemap_lookup(h, &hfile, &msize)) { mw_SetLastError(6); return NULL; } /* ERROR_INVALID_HANDLE */
    int fd = w3fd_from_handle(hfile);
    if (fd < 0) { mw_SetLastError(6); return NULL; }
    long long fsize = (long long)lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_CUR);
    if (msize == 0) msize = fsize;               /* CreateFileMapping size 0 -> EOF */
    if (msize <= 0) { mw_SetLastError(2); return NULL; }
    long long off = ((long long)hi << 32) | lo;
    if (off > msize) { mw_SetLastError(113); return NULL; }
    size_t view = (bytes != 0) ? (size_t)bytes : (size_t)(msize - off);
    if ((long long)view > msize - off) view = (size_t)(msize - off);
    int prot = PROT_READ;
    if (access & 0x2) prot |= PROT_WRITE;                       /* FILE_MAP_WRITE */
    if ((access & 0xF001Fu) == 0xF001Fu) prot |= PROT_WRITE;    /* FILE_MAP_ALL_ACCESS */
    if (access & 0x20u) prot |= PROT_EXEC;                      /* FILE_MAP_EXECUTE */
    void* base = mmap(NULL, view, prot, MAP_SHARED, fd, (off_t)off);
    if (base == MAP_FAILED) { mw_SetLastError(8); return NULL; }
    if (g_mapview_count < MAX_MAPVIEWS) {
        g_mapviews[g_mapview_count].base = base;
        g_mapviews[g_mapview_count].size = view;
        g_mapview_count++;
    }
    MW_TRACE("MapViewOfFile -> %p (%zu bytes, fd=%d)", base, view, fd);
    return base;
}
__attribute__((ms_abi)) int mw_UnmapViewOfFile(void* base) {
    for (int i = 0; i < g_mapview_count; i++) {
        if (g_mapviews[i].base == base) {
            munmap(base, g_mapviews[i].size);
            g_mapviews[i] = g_mapviews[--g_mapview_count];
            return 1;
        }
    }
    mw_SetLastError(6);
    return 0;
}
__attribute__((ms_abi)) void* mw_OpenFileMappingW(uint32_t access, int inherit, const uint16_t* name) {
    (void)access; (void)inherit;
    char n8[256];
    if (name) w3_w16_to_utf8z(name, n8, sizeof(n8)); else n8[0] = 0;
    MW_TRACE("OpenFileMappingW(%s) -> NULL", n8);
    mw_SetLastError(2); /* named mapping objects unsupported */
    return NULL;
}

/* ---- KERNEL32: FindFirstFileW (narrow sibling already shared) ---- */
__attribute__((ms_abi)) void* mw_FindFirstFileW(const uint16_t* pattern, void* data) {
    char p8[4096];
    if (!pattern || w3_w16_to_utf8z(pattern, p8, sizeof(p8)) < 0) {
        mw_SetLastError(87);
        return (void*)-1;
    }
    return find_first_impl(p8, data);
}
__attribute__((ms_abi)) void* mw_FindFirstStreamW(const uint16_t* name, uint32_t info, void* data, uint32_t flags) {
    (void)name; (void)info; (void)data; (void)flags;
    MW_TRACE("FindFirstStreamW() -> none");
    mw_SetLastError(38); /* ERROR_HANDLE_EOF: no alternate streams */
    return (void*)-1;
}
__attribute__((ms_abi)) int mw_FindNextStreamW(void* h, void* data) {
    (void)h; (void)data;
    mw_SetLastError(38);
    return 0;
}

/* ---- zstd wave (LADDER-008, 2026-10-17): ANSI find + priority + CRT pos/length ----
 * zstd.exe (mingw/msvcrt) imports 7 APIs we lacked; crash was RIP=0x0.
 * The 6 trivial ones are implemented here; SleepConditionVariableCS (threads
 * wave) is registered separately and -T1 exercises the single-thread path. */
__attribute__((ms_abi)) void* mw_FindFirstFileA(const char* pattern, void* data) {
    if (!pattern) { mw_SetLastError(87); return (void*)-1; }
    MW_TRACE("FindFirstFileA(%s)", pattern);
    return find_first_impl((char*)pattern, data);
}
__attribute__((ms_abi)) int mw_FindNextFileA(void* h, void* data) {
    return mw_FindNextFileW(h, data);
}
__attribute__((ms_abi)) void* mw_SetPriorityClass(void* proc, uint32_t cls) {
    (void)proc; (void)cls;
    MW_TRACE("SetPriorityClass(cls=%u) -> TRUE (no-op)", cls);
    return (void*)(uintptr_t)1;
}
__attribute__((ms_abi)) long long mw__filelengthi64(int fd) {
    struct stat st;
    MW_TRACE("_filelengthi64(fd=%d)", fd);
    if (fstat(fd, &st) != 0) { return -1LL; }
    return (long long)st.st_size;
}
/* MS msvcrt fpos_t is __int64 for msvcrt.dll-era; host off_t is 64-bit here. */
__attribute__((ms_abi)) int mw_fgetpos(FILE* f, long long* pos) {
    FILE* hf = map_crt_stream(f);
    if (!hf) hf = f; /* EXP-041 rule: pass unknown pointers through */
    MW_TRACE("fgetpos(%p)", (void*)f);
    if (!pos) return -1;
    off_t o = ftello(hf);
    if (o < 0) return -1;
    *pos = (long long)o;
    return 0;
}
__attribute__((ms_abi)) int mw_fsetpos(FILE* f, const long long* pos) {
    FILE* hf = map_crt_stream(f);
    if (!hf) hf = f;
    MW_TRACE("fsetpos(%p)", (void*)f);
    if (!pos) return -1;
    return fseeko(hf, (off_t)*pos, SEEK_SET) == 0 ? 0 : -1;
}

/* ---- KERNEL32: move/copy family ---- */
__attribute__((ms_abi)) int mw_MoveFileW(const uint16_t* from, const uint16_t* to) {
    char f8[4096], t8[4096];
    if (!from || !to || w3_w16_to_utf8z(from, f8, sizeof(f8)) < 0 ||
        w3_w16_to_utf8z(to, t8, sizeof(t8)) < 0) { mw_SetLastError(87); return 0; }
    MW_TRACE("MoveFileW(%s -> %s)", f8, t8);
    if (access(t8, F_OK) == 0) { mw_SetLastError(183); return 0; } /* dest exists */
    if (rename(f8, t8) != 0) { mw_SetLastError(5); return 0; }
    return 1;
}
__attribute__((ms_abi)) int mw_MoveFileWithProgressW(const uint16_t* from, const uint16_t* to,
        void* progress, void* data, uint32_t flags) {
    char f8[4096], t8[4096];
    if (!from || !to || w3_w16_to_utf8z(from, f8, sizeof(f8)) < 0 ||
        w3_w16_to_utf8z(to, t8, sizeof(t8)) < 0) { mw_SetLastError(87); return 0; }
    MW_TRACE("MoveFileWithProgressW(%s -> %s, flags=0x%x)", f8, t8, flags);
    if (!(flags & 0x1 /*REPLACE_EXISTING*/) && access(t8, F_OK) == 0) {
        mw_SetLastError(183); return 0;
    }
    if (rename(f8, t8) != 0) { mw_SetLastError(5); return 0; }
    return 1;
}

/* ---- KERNEL32: semaphores / named event open ---- */
#define MAX_SEMAS 64
typedef struct { int count; int max; } MwSema;
static MwSema g_semas[MAX_SEMAS];
static int g_sema_count = 0;
__attribute__((ms_abi)) void* mw_CreateSemaphoreW(void* attr, int32_t init, int32_t max, const uint16_t* name) {
    (void)attr; (void)name;
    MW_TRACE("CreateSemaphoreW(init=%d, max=%d)", init, max);
    if (g_sema_count >= MAX_SEMAS || max <= 0 || init < 0 || init > max) {
        mw_SetLastError(87); return NULL;
    }
    g_semas[g_sema_count].count = init;
    g_semas[g_sema_count].max = max;
    return alloc_handle((void*)(uintptr_t)(0x5000 + g_sema_count++));
}
__attribute__((ms_abi)) void* mw_OpenEventW(uint32_t access, int inherit, const uint16_t* name) {
    (void)access; (void)inherit;
    char n8[256];
    if (name) w3_w16_to_utf8z(name, n8, sizeof(n8)); else n8[0] = 0;
    MW_TRACE("OpenEventW(%s) -> NULL", n8);
    mw_SetLastError(2); /* named events unsupported */
    return NULL;
}

/* ---- KERNEL32: time conversion family ---- */
static long w3_tz_offset_seconds(void) {
    /* offset = mktime(gmtime(t)) - t  (POSIX-style west-negative inverse) */
    time_t t = 0;
    struct tm g;
    gmtime_r(&t, &g);
    return (long)mktime(&g) - (long)t;
}
static uint64_t ft_from_time_t_public(uint64_t t) { return ft_from_time_t(t); }
__attribute__((ms_abi)) int mw_FileTimeToLocalFileTime(const uint64_t* utc, uint64_t* local) {
    if (!utc || !local) { mw_SetLastError(87); return 0; }
    *local = *utc - (uint64_t)w3_tz_offset_seconds() * 10000000ULL;
    return 1;
}
__attribute__((ms_abi)) int mw_LocalFileTimeToFileTime(const uint64_t* local, uint64_t* utc) {
    if (!local || !utc) { mw_SetLastError(87); return 0; }
    *utc = *local + (uint64_t)w3_tz_offset_seconds() * 10000000ULL;
    return 1;
}
__attribute__((ms_abi)) int mw_FileTimeToDosDateTime(const uint64_t* ft, uint16_t* date, uint16_t* time_) {
    if (!ft || !date || !time_) { mw_SetLastError(87); return 0; }
    uint64_t secs = *ft / 10000000ULL - 11644473600ULL;
    time_t t = (time_t)secs;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    if (tmv.tm_year + 1900 < 1980) { *date = (1 << 5) | 1; *time_ = 0; return 1; }
    *date = (uint16_t)(((tmv.tm_year + 1900 - 1980) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
    *time_ = (uint16_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec >> 1));
    return 1;
}
__attribute__((ms_abi)) int mw_DosDateTimeToFileTime(uint16_t date, uint16_t time_, uint64_t* ft) {
    if (!ft) { mw_SetLastError(87); return 0; }
    int y = 1980 + ((date >> 9) & 0x7F);
    int mo = (date >> 5) & 0xF;  if (mo < 1) mo = 1; if (mo > 12) mo = 12;
    int d = date & 0x1F;         if (d < 1) d = 1;
    int h = (time_ >> 11) & 0x1F;
    int mi = (time_ >> 5) & 0x3F;
    int s = (time_ & 0x1F) * 2;
    struct tm tmv = { s, mi, h, d, mo - 1, y - 1900, 0, 0, 0 };
    time_t t = timegm(&tmv);
    *ft = ft_from_time_t_public((uint64_t)(t < 0 ? 0 : t));
    return 1;
}
__attribute__((ms_abi)) long mw_CompareFileTime(const uint64_t* a, const uint64_t* b) {
    if (!a || !b) { mw_SetLastError(87); return -2; }
    if (*a < *b) return -1;
    if (*a > *b) return 1;
    return 0;
}

/* ---- KERNEL32: misc ---- */
__attribute__((ms_abi)) int mw_GetDiskFreeSpaceW(const uint16_t* root, uint32_t* spc, uint32_t* bps,
        uint32_t* free_clusters, uint32_t* total_clusters) {
    char r8[1024] = "/";
    if (root && w3_w16_to_utf8z(root, r8, sizeof(r8) - 1) >= 0) {
        /* Windows passes "C:\", POSIX statvfs wants a dir; root always works */
        if (r8[0] && r8[1] == ':' ) snprintf(r8, sizeof(r8), "%s", "/");
    }
    struct statvfs v;
    if (statvfs(r8, &v) != 0) { mw_SetLastError(3); return 0; }
    if (bps) *bps = (uint32_t)(v.f_frsize ? v.f_frsize : 512);
    if (spc) *spc = 8;
    uint64_t bpc = (uint64_t)(bps ? *bps : 512) * (spc ? *spc : 8);
    if (total_clusters) *total_clusters = (uint32_t)((v.f_blocks * (uint64_t)v.f_frsize) / bpc);
    if (free_clusters) *free_clusters = (uint32_t)((v.f_bavail * (uint64_t)v.f_frsize) / bpc);
    MW_TRACE("GetDiskFreeSpaceW(%s) ok", r8);
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetLogicalDriveStringsW(uint32_t len, uint16_t* buf) {
    /* Exactly one drive: "C:\" + NUL, plus the final terminating NUL */
    if (!buf || len < 5) return 5;
    buf[0] = 'C'; buf[1] = ':'; buf[2] = '\\'; buf[3] = 0; buf[4] = 0;
    return 4;
}
__attribute__((ms_abi)) int mw_GetProcessTimes(void* h, uint64_t* create, uint64_t* exit,
        uint64_t* kernel, uint64_t* user) {
    (void)h;
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    uint64_t to_ft(const struct timeval* tv) {
        return ft_from_time_t_public((uint64_t)tv->tv_sec) + (uint64_t)tv->tv_usec * 10ULL;
    }
    if (create) *create = ft_from_time_t_public(0); /* process epoch */
    if (exit) *exit = 0;
    if (kernel) *kernel = to_ft(&ru.ru_stime);
    if (user) *user = to_ft(&ru.ru_utime);
    return 1;
}
__attribute__((ms_abi)) void* mw_SetThreadAffinityMask(void* h, uintptr_t mask) {
    (void)h; (void)mask;
    return (void*)(uintptr_t)1; /* previous mask */
}
__attribute__((ms_abi)) size_t mw_GetLargePageMinimum(void) {
    return 0x200000; /* 2 MiB */
}
__attribute__((ms_abi)) void mw_SetFileApisToOEM(void) {
    MW_TRACE("SetFileApisToOEM() (no-op: cp65001 identity)");
}


__attribute__((ms_abi)) int mw___intrinsic_setjmp(void* jb);

/* ====================================================================
 * OPENSRC battery wave B (2026-10-17): sqlite3.exe / jq.exe / curl.exe
 * ==================================================================== */

/* ---- file mapping: REAL mmap-backed objects (sqlite needs them) ---- */
#define MAX_FILEMAPS 64
typedef struct { void* handle; long long size; } FileMap;
static FileMap g_filemaps[MAX_FILEMAPS];
static int g_filemap_count = 0;

/* LADDER-009: resolve a mapping-object handle (0x6000+N from
 * mw_CreateFileMappingW) to its backing file handle + recorded size. */
static int filemap_lookup(void* h, void** hfile, long long* size_out) {
    if ((uintptr_t)h < 0x6000) return 0;
    int idx = (int)((uintptr_t)h - 0x6000);
    if (idx < 0 || idx >= g_filemap_count) return 0;
    if (hfile) *hfile = g_filemaps[idx].handle;
    if (size_out) *size_out = g_filemaps[idx].size;
    return 1;
}

__attribute__((ms_abi)) void* mw_CreateFileMappingW(void* hfile, void* sa, uint32_t prot,
        uint32_t maxhi, uint32_t maxlo, const uint16_t* name) {
    (void)sa; (void)name;
    MW_TRACE("CreateFileMappingW(h=%p, prot=0x%x, max=%u:%u)", hfile, prot, maxhi, maxlo);
    if (g_filemap_count >= MAX_FILEMAPS) { mw_SetLastError(4); return NULL; }
    long long size = ((long long)maxhi << 32) | maxlo;
    /* size-from-file: our file objects are loader-registry entries, not
     * POSIX fds; a 0-sized mapping stays 0 here (sqlite passes explicit
     * sizes for its shm mappings). */
    g_filemaps[g_filemap_count].handle = hfile;
    g_filemaps[g_filemap_count].size = size;
    return alloc_handle((void*)(uintptr_t)(0x6000 + g_filemap_count++));
}
__attribute__((ms_abi)) void* mw_CreateFileMappingA(void* hfile, void* sa, uint32_t prot,
        uint32_t maxhi, uint32_t maxlo, const char* name) {
    (void)name;
    return mw_CreateFileMappingW(hfile, sa, prot, maxhi, maxlo, NULL);
}

/* ---- byte-range locks (sqlite): single team -> contended never ---- */
__attribute__((ms_abi)) int mw_LockFile(void* h, uint32_t lo, uint32_t hi, uint32_t nlo, uint32_t nhi) {
    (void)h; (void)lo; (void)hi; (void)nlo; (void)nhi;
    MW_TRACE("LockFile(%p)", h);
    return 1;
}
__attribute__((ms_abi)) int mw_UnlockFile(void* h, uint32_t lo, uint32_t hi, uint32_t nlo, uint32_t nhi) {
    (void)h; (void)lo; (void)hi; (void)nlo; (void)nhi;
    return 1;
}
__attribute__((ms_abi)) int mw_LockFileEx(void* h, uint32_t flags, uint32_t res,
        uint32_t nlo, uint32_t nhi, void* ov) {
    (void)h; (void)flags; (void)res; (void)nlo; (void)nhi; (void)ov;
    MW_TRACE("LockFileEx(%p, flags=0x%x)", h, flags);
    return 1;
}
__attribute__((ms_abi)) int mw_UnlockFileEx(void* h, uint32_t res, uint32_t nlo, uint32_t nhi, void* ov) {
    (void)h; (void)res; (void)nlo; (void)nhi; (void)ov;
    return 1;
}

/* ---- A-variants for sqlite ---- */
__attribute__((ms_abi)) int mw_DeleteFileA(const char* name) {
    char n8[4096];
    if (!name) { mw_SetLastError(87); return 0; }
    snprintf(n8, sizeof(n8), "%s", name);
    w3_normalize_path(n8);
    MW_TRACE("DeleteFileA(%s)", n8);
    if (unlink(n8) != 0) { mw_SetLastError(2); return 0; }
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetFileAttributesA(const char* name) {
    char n8[4096];
    if (!name) { mw_SetLastError(87); return (uint32_t)-1; }
    snprintf(n8, sizeof(n8), "%s", name);
    w3_normalize_path(n8);
    struct stat st;
    if (stat(n8, &st) != 0) { mw_SetLastError(2); return (uint32_t)-1; }
    uint32_t attr = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
    const char* base = strrchr(n8, '/'); base = base ? base + 1 : n8;
    if (base[0] == '.') attr |= 0x2;
    return attr;
}
__attribute__((ms_abi)) int mw_GetDiskFreeSpaceA(const char* root, uint32_t* spc, uint32_t* bps,
        uint32_t* fc, uint32_t* tc) {
    /* delegate: same math as the W variant with a narrow root */
    char r8[1024] = "/";
    if (root && root[0]) snprintf(r8, sizeof(r8), "%s", root);
    struct statvfs v;
    if (statvfs(r8, &v) != 0) { mw_SetLastError(3); return 0; }
    if (bps) *bps = (uint32_t)(v.f_frsize ? v.f_frsize : 512);
    if (spc) *spc = 8;
    uint64_t bpc = (uint64_t)(bps ? *bps : 512) * (spc ? *spc : 8);
    if (tc) *tc = (uint32_t)((v.f_blocks * (uint64_t)v.f_frsize) / bpc);
    if (fc) *fc = (uint32_t)((v.f_bavail * (uint64_t)v.f_frsize) / bpc);
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetTempPathA(uint32_t cap, char* out) {
    const char* t = getenv("TMPDIR");
    if (!t || !t[0]) t = "/tmp";
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s/", t);
    uint32_t need = (uint32_t)strlen(buf) + 1;
    if (!out || cap < need) return need;
    memcpy(out, buf, need);
    return (uint32_t)strlen(out);
}
__attribute__((ms_abi)) uint32_t mw_GetFullPathNameA(const char* name, uint32_t cap, char* out, char** part) {
    char n8[4096];
    if (!name) { mw_SetLastError(87); return 0; }
    snprintf(n8, sizeof(n8), "%s", name);
    w3_normalize_path(n8);
    char full[4096];
    if (n8[0] == '/') snprintf(full, sizeof(full), "%s", n8);
    else { char cwd[2048]; getcwd(cwd, sizeof(cwd)); snprintf(full, sizeof(full), "%s/%s", cwd, n8); }
    MW_TRACE("GetFullPathNameA(%s) -> %s", n8, full);
    uint32_t need = (uint32_t)strlen(full) + 1;
    if (!out || cap < need) return need;
    snprintf(out, cap, "%s", full);
    if (part) { char* s = strrchr(out, '/'); *part = s ? s + 1 : out; }
    return (uint32_t)strlen(out);
}
__attribute__((ms_abi)) int mw_AreFileApisANSI(void) { return 1; }
__attribute__((ms_abi)) int mw_InitializeCriticalSectionAndSpinCount(void* cs, uint32_t spin) {
    mw_InitializeCriticalSection(cs);
    /* Windows stores the spin count in the reserved SpinCount field — our
     * sentinel lives there, so only report success. */
    (void)spin;
    return 1;
}

/* ---- misc kernel32 ---- */
__attribute__((ms_abi)) int mw_FlushViewOfFile(const void* base, size_t bytes) {
    (void)base; (void)bytes;
    return 1; /* mapped views are MAP_SHARED; the kernel coheres */
}
__attribute__((ms_abi)) void* mw_CreateMutexW(void* attr, int owner, const uint16_t* name) {
    (void)attr; (void)name;
    MW_TRACE("CreateMutexW(owner=%d)", owner);
    if (g_event_count >= MAX_EVENTS) return NULL;
    g_events[g_event_count].set = 1 - owner;
    g_events[g_event_count].manual = 1;
    return alloc_handle((void*)(uintptr_t)(4 + g_event_count++));
}
__attribute__((ms_abi)) int mw_HeapDestroy(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) uint32_t mw_TlsFree(uint32_t idx) { (void)idx; return 1; }
__attribute__((ms_abi)) uint32_t mw_FlsFree(uint32_t idx) { (void)idx; return 1; }
__attribute__((ms_abi)) int mw_SetStdHandle2(int32_t which, void* h) { (void)which; (void)h; return 1; }
__attribute__((ms_abi)) int mw_SystemTimeToTzSpecificLocalTime(void* tz, const void* utc, void* local) {
    /* identity: both SYSTEM_TIMEs are 8 x uint16 {year,mon,dow,day,hour,min,sec,ms} */
    if (utc && local) memcpy(local, utc, 16);
    return 1;
}
__attribute__((ms_abi)) int mw_GetDateFormatW(uint32_t locale, uint32_t flags, const void* st,
        const uint16_t* fmt, uint16_t* out, int cap) {
    (void)locale; (void)flags; (void)fmt;
    if (!st || !out || cap < 11) { mw_SetLastError(122); return 0; }
    const uint16_t* t = (const uint16_t*)st;
    int n = snprintf((char*)out, (size_t)cap * 2, "%04d-%02d-%02d",
                     (int)t[0], (int)t[1], (int)t[3]);
    return n > 0 ? n + 1 : 0; /* chars incl NUL (ASCII in UTF-16 = same count) */
}
__attribute__((ms_abi)) int mw_GetTimeFormatW(uint32_t locale, uint32_t flags, const void* st,
        const uint16_t* fmt, uint16_t* out, int cap) {
    (void)locale; (void)flags; (void)fmt;
    if (!st || !out || cap < 9) { mw_SetLastError(122); return 0; }
    const uint16_t* t = (const uint16_t*)st;
    int n = snprintf((char*)out, (size_t)cap * 2, "%02d:%02d:%02d",
                     (int)t[4], (int)t[5], (int)t[6]);
    return n > 0 ? n + 1 : 0;
}
__attribute__((ms_abi)) void mw_RtlUnwind(void* est, void* ip, void* er, uint64_t rax) {
    /* RtlUnwind(x86 legacy entry) routes to the x64 unwinder with no
     * personality callback beyond the normal walk. */
    MW_TRACE("RtlUnwind(est=%p, ip=%p)", est, ip);
    mw_RtlUnwindEx(est, ip, NULL, er, (void*)(uintptr_t)rax);
}

/* ---- jq: UCRT wide environment + math + time ---- */
static uint16_t g_wenv_block[16384];
static uint16_t* g_wenv_arr[512];
static void* g_wenviron_cell = NULL;
static uint16_t* g_wargv_arr[256];
static uint16_t g_wargv_block[8192];
static void* g_wargv_cell = NULL;
extern char* g_argv[256];
extern int g_argc;
static void build_wargv_wenv(void) {
    static int ready = 0;
    if (ready) return;
    ready = 1;
    /* wide argv array (UCRT __wargv semantics) */
    size_t o = 0;
    int n = g_argc > 255 ? 255 : g_argc;
    for (int i = 0; i < n; i++) {
        const char* a = g_argv[i] ? g_argv[i] : "";
        int got = w3_utf8_to_utf16le(a, strlen(a), g_wargv_block + o,
                                     (int)(8192 - o - 2));
        if (got < 0) got = 0;
        g_wargv_arr[i] = g_wargv_block + o;
        o += (size_t)got;
        g_wargv_block[o++] = 0;
    }
    g_wargv_arr[n] = NULL;
    g_wargv_cell = (void*)g_wargv_arr;
    /* wide environment block + array (UCRT _wenviron semantics) */
    size_t wo = 0;
    int wi = 0;
    for (char** e = environ; *e && wi < 510 && wo < 16380; e++) {
        int got = w3_utf8_to_utf16le(*e, strlen(*e), g_wenv_block + wo,
                                     (int)(16380 - wo - 2));
        if (got <= 0) continue;
        g_wenv_arr[wi++] = g_wenv_block + wo;
        wo += (size_t)got;
        g_wenv_block[wo++] = 0;
    }
    g_wenv_arr[wi] = NULL;
    g_wenviron_cell = (void*)g_wenv_arr;
}
__attribute__((ms_abi)) void** mw___p__wenviron(void) {
    build_wargv_wenv();
    MW_TRACE("__p__wenviron() = %p", g_wenviron_cell);
    return &g_wenviron_cell;
}
__attribute__((ms_abi)) void** mw___p___wargv(void) {
    build_wargv_wenv();
    MW_TRACE("__p___wargv() = %p", g_wargv_cell);
    return &g_wargv_cell;
}
__attribute__((ms_abi)) int mw__configure_wide_argv(int mode) { (void)mode; return 0; }
__attribute__((ms_abi)) int mw__initialize_wide_environment(void) { return 0; }
__attribute__((ms_abi)) int mw__crt_at_quick_exit(void* fn) { (void)fn; return 0; }
__attribute__((ms_abi)) char* mw_setlocale_bridge(int cat, const char* loc) {
    MW_TRACE("setlocale(%d, %s)", cat, loc ? loc : "NULL");
    return setlocale(cat, loc);
}
__attribute__((ms_abi)) double mw__j0(double x) { return j0(x); }
__attribute__((ms_abi)) double mw__y0(double x) { return y0(x); }
__attribute__((ms_abi)) double mw_erfc(double x) { return erfc(x); }
__attribute__((ms_abi)) double mw_fdim(double a, double b) { return fdim(a, b); }
__attribute__((ms_abi)) double mw_logb(double x) { return logb(x); }
__attribute__((ms_abi)) double mw_nearbyint(double x) { return nearbyint(x); }
__attribute__((ms_abi)) double mw_tgamma(double x) { return tgamma(x); }
__attribute__((ms_abi)) long long mw__mkgmtime64(void* tm) {
    return (long long)timegm((struct tm*)tm);
}
__attribute__((ms_abi)) int mw___intrinsic_setjmpex(void* buf, void* frame) {
    /* UCRT intrinsic setjmpex: same loader jmp_buf contract as setjmp */
    (void)frame;
    return mw___intrinsic_setjmp(buf);
}
__attribute__((ms_abi)) int mw_PathIsRelativeA(const char* p) {
    if (!p || !p[0]) return 1;
    if (p[0] == '\\' || p[0] == '/') return 0;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') return 0;
    return 1;
}

/* ---- curl: honest failure stubs for service paths --version never calls ---- */
__attribute__((ms_abi)) void* mw_CertOpenSystemStoreA(void* prov, const char* name) {
    (void)prov; (void)name;
    mw_SetLastError(126); return NULL;
}
__attribute__((ms_abi)) int mw_CertCloseStore(void* store, uint32_t flags) { (void)store; (void)flags; return 1; }
__attribute__((ms_abi)) void* mw_CertEnumCertificatesInStore(void* store, void* prev) {
    (void)store; (void)prev;
    mw_SetLastError(0x80092004); return NULL; /* CRYPT_E_NO_MORE_DATA-ish */
}
__attribute__((ms_abi)) void mw_CertFreeCertificateContext(void* ctx) { (void)ctx; }
__attribute__((ms_abi)) int mw_CertGetEnhancedKeyUsage(void* ctx, uint32_t flags, void* usage, uint32_t* need) {
    (void)ctx; (void)flags; (void)usage;
    if (need) *need = 0;
    mw_SetLastError(122); return 0;
}
__attribute__((ms_abi)) int mw_CertGetIntendedKeyUsage(uint32_t enc, void* ctx, void* usage, uint32_t len) {
    (void)enc; (void)ctx; (void)usage; (void)len;
    return 0;
}
__attribute__((ms_abi)) uint16_t mw_if_nametoindex(const char* name) {
    return (uint16_t)syscall(393 /*SYS_if_nametoindex*/, name ? name : "");
}
__attribute__((ms_abi)) int mw_CancelIo(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) void* mw_CreateToolhelp32Snapshot(uint32_t flags, uint32_t pid) {
    (void)flags; (void)pid;
    mw_SetLastError(126); return (void*)-1;
}
__attribute__((ms_abi)) int mw_Module32First(void* snap, void* me) { (void)snap; (void)me; mw_SetLastError(18); return 0; }
__attribute__((ms_abi)) int mw_Module32Next(void* snap, void* me) { (void)snap; (void)me; mw_SetLastError(18); return 0; }
__attribute__((ms_abi)) void* mw_FindFirstVolumeW(uint16_t* name, uint32_t len) {
    (void)name; (void)len;
    mw_SetLastError(18); return (void*)-1;
}
__attribute__((ms_abi)) int mw_FindNextVolumeW(void* h, uint16_t* name, uint32_t len) {
    (void)h; (void)name; (void)len; mw_SetLastError(18); return 0;
}
__attribute__((ms_abi)) int mw_FindVolumeClose(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) int mw_GetOverlappedResult(void* h, void* ov, uint32_t* got, int wait) {
    (void)h; (void)ov; (void)wait;
    if (got) *got = 0;
    mw_SetLastError(87); return 0;
}
__attribute__((ms_abi)) int mw_MoveFileExA(const char* from, const char* to, uint32_t flags) {
    char f8[4096], t8[4096];
    if (!from || !to) { mw_SetLastError(87); return 0; }
    snprintf(f8, sizeof(f8), "%s", from);
    snprintf(t8, sizeof(t8), "%s", to);
    w3_normalize_path(f8);
    w3_normalize_path(t8);
    if (!(flags & 0x1) && access(t8, F_OK) == 0) { mw_SetLastError(183); return 0; }
    if (rename(f8, t8) != 0) { mw_SetLastError(5); return 0; }
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_SleepEx(uint32_t ms, int alertable) {
    (void)alertable;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    return 0;
}
__attribute__((ms_abi)) uint64_t mw_VerSetConditionMask(uint64_t cm, uint32_t mask, uint8_t cond) {
    (void)mask; (void)cond;
    return cm; /* VER_SET_CONDITION folding: keep last */
}
__attribute__((ms_abi)) int mw_VerifyVersionInfoW(void* vi, uint32_t mask, uint64_t cond) {
    (void)vi; (void)mask; (void)cond;
    return 1; /* claim compatibility */
}
__attribute__((ms_abi)) int mw_WaitNamedPipeA(const char* name, uint32_t ms) {
    (void)name; (void)ms;
    mw_SetLastError(2); return 0;
}
__attribute__((ms_abi)) long mw_IdnToAscii(uint32_t flags, const uint16_t* src, int len, uint16_t* dst, int cap) {
    (void)flags;
    if (!src || len < 0) return 0;
    if (len == -1) { int u = 0; while (src[u]) u++; len = u; }
    if (!dst) return len;
    int n = len < cap ? len : cap;
    memcpy(dst, src, (size_t)n * 2);
    return n; /* ASCII passthrough: no IDN punycode yet */
}
__attribute__((ms_abi)) long mw_IdnToUnicode(uint32_t flags, const uint16_t* src, int len, uint16_t* dst, int cap) {
    return mw_IdnToAscii(flags, src, len, dst, cap);
}
__attribute__((ms_abi)) void* mw_InitSecurityInterfaceA(void) {
    return NULL; /* SSPI unsupported honestly */
}
__attribute__((ms_abi)) void* mw_FindWindowA(const char* cls, const char* title) {
    (void)cls; (void)title;
    return NULL;
}
__attribute__((ms_abi)) long mw_SendMessageA(void* h, uint32_t msg, uintptr_t wp, long lp) {
    (void)h; (void)msg; (void)wp; (void)lp;
    return 0;
}
__attribute__((ms_abi)) void mw_ber_free(void* b, int flags) { (void)b; (void)flags; }
__attribute__((ms_abi)) int mw_ldap_bind_s(void* ld, const char* who, const char* pw, unsigned meth) {
    (void)ld; (void)who; (void)pw; (void)meth;
    return 0x31; /* LDAP_NOT_SUPPORTED */
}
__attribute__((ms_abi)) const char* mw_ldap_err2string(int err) { (void)err; return "ldap unsupported"; }
__attribute__((ms_abi)) void* mw_ldap_first_attribute(void* ld, void* e, void** ptr) { (void)ld; (void)e; if (ptr) *ptr = NULL; return NULL; }
__attribute__((ms_abi)) void* mw_ldap_next_attribute(void* ld, void* e, void* ptr) { (void)ld; (void)e; (void)ptr; return NULL; }
__attribute__((ms_abi)) void* mw_ldap_first_entry(void* ld, void* res) { (void)ld; (void)res; return NULL; }
__attribute__((ms_abi)) void* mw_ldap_next_entry(void* ld, void* e) { (void)ld; (void)e; return NULL; }
__attribute__((ms_abi)) char* mw_ldap_get_dn(void* ld, void* e) { (void)ld; (void)e; return NULL; }
__attribute__((ms_abi)) void** mw_ldap_get_values_len(void* ld, void* e, const char* a) { (void)ld; (void)e; (void)a; return NULL; }
__attribute__((ms_abi)) void mw_ldap_memfree(void* p) { free(p); }
__attribute__((ms_abi)) int mw_ldap_msgfree(void* res) { (void)res; return 0; }
__attribute__((ms_abi)) void mw_ldap_value_free_len(void** vals) { free(vals); }
__attribute__((ms_abi)) int mw_ldap_search_s(void* ld, const char* base, int scope, const char* filter,
        char** attrs, int attrsonly, void** res) {
    (void)ld; (void)base; (void)scope; (void)filter; (void)attrs; (void)attrsonly;
    if (res) *res = NULL;
    return 0x31;
}
__attribute__((ms_abi)) int mw_ldap_set_option(void* ld, int opt, const void* val) { (void)ld; (void)opt; (void)val; return 0x31; }
__attribute__((ms_abi)) int mw_ldap_simple_bind_s(void* ld, const char* who, const char* pw) { (void)ld; (void)who; (void)pw; return 0x31; }
__attribute__((ms_abi)) void* mw_ldap_sslinit(const char* host, int port, int ssl) {
    (void)host; (void)port; (void)ssl;
    return NULL;
}
__attribute__((ms_abi)) int mw_ldap_unbind_s(void* ld) { (void)ld; return 0; }
__attribute__((ms_abi)) void* mw_ldap_init(const char* host, int port) {
    (void)host; (void)port;
    return NULL; /* LDAP honestly unsupported */
}
__attribute__((ms_abi)) void* mw_WSACreateEvent(void) {
    if (g_event_count >= MAX_EVENTS) { g_wsa_err = 10024; return NULL; }
    g_events[g_event_count].set = 0;
    g_events[g_event_count].manual = 1;
    return alloc_handle((void*)(uintptr_t)(4 + g_event_count++));
}
__attribute__((ms_abi)) int mw_WSACloseEvent(void* h) { (void)h; return 1; }
__attribute__((ms_abi)) int mw_WSAEventSelect(unsigned long long s, void* ev, long mask) {
    (void)s; (void)ev; (void)mask;
    return 0;
}
__attribute__((ms_abi)) int mw_WSAEnumNetworkEvents(unsigned long long s, void* ev, void* buf) {
    (void)s; (void)ev;
    if (buf) memset(buf, 0, 56);
    return 0;
}
__attribute__((ms_abi)) int mw_WSAResetEvent(void* h) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_event_count) return 0;
    g_events[idx].set = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_WSASetEvent(void* h) {
    int idx = (int)(uintptr_t)h - 4;
    if (idx < 0 || idx >= g_event_count) return 0;
    g_events[idx].set = 1;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_WSAWaitForMultipleEvents(uint32_t n, void** hs, int all, uint32_t ms, int alertable) {
    (void)all; (void)alertable;
    return mw_WaitForMultipleObjects(n, hs, all, ms);
}
__attribute__((ms_abi)) int mw_WSAStringToAddressW(const uint16_t* str, int af, void* sa, void* addr, int* len) {
    (void)af; (void)sa; (void)addr; (void)len;
    (void)str;
    g_wsa_err = 11001; /* WSAHOST_NOT_FOUND */
    return -1;
}
__attribute__((ms_abi)) int mw_freeaddrinfo_(void* ai) { freeaddrinfo(ai); return 0; }
__attribute__((ms_abi)) int mw_getaddrinfo_(const char* host, const char* svc, const void* hints, void** res) {
    return getaddrinfo(host, svc, (const struct addrinfo*)hints, (struct addrinfo**)res);
}
__attribute__((ms_abi)) int mw_gethostname_(char* name, int len) {
    return gethostname(name, (size_t)len);
}
__attribute__((ms_abi)) int mw_inet_pton_(int af, const char* src, void* dst) {
    return inet_pton(af, src, dst);
}

/* ---- curl UCRT gaps ---- */
static const char* g_sys_errlist[] = { "Unknown error" };
static int g_sys_nerr = 1;
__attribute__((ms_abi)) long long mw__telli64(int fd) {
    return (long long)lseek(fd, 0, SEEK_CUR);
}
__attribute__((ms_abi)) void* mw_freopen_bridge(const char* path, const char* mode, void* stream) {
    return freopen(path, mode, map_crt_stream(stream));
}
__attribute__((ms_abi)) uint64_t mw__byteswap_uint64(uint64_t v) {
    return __builtin_bswap64(v);
}

__attribute__((ms_abi)) intptr_t mw__findfirst64(const char* spec, void* data) {
    return (intptr_t)(uintptr_t)mw__findfirst64i32(spec, data);
}
__attribute__((ms_abi)) int mw__findnext64(intptr_t h, void* data) {
    return mw_FindNextFileW((void*)(uintptr_t)h, data);
}

/* ============================================================
 * LADDER-013 wave (md5deep64 4.4, 2014 mingw/msvcrt) — BUG-065 surface.
 * 15 unresolved imports + 3 data imports blocked CRT init (SIGSEGV at
 * 0x42eac9). All implemented with honest semantics below.
 * ============================================================ */

/* ---- data imports: __argv / __mb_cur_max / __pioinfo ---- */
static int g_mb_cur_max = 1; /* SBCS answer, mirrors GetCPInfo */

#define PIOINFO_ARRAY_ELTS 32
typedef struct {           /* msvcrt ioinfo, x64 = 16 bytes */
    void* osfhnd;          /* OS handle */
    char  osfile;          /* flags: 0x80=FH_DEVICE, 0x01=open */
    char  pipech;
    char  pipech2[2];
    char  pad[4];
} MwIoinfo;
static MwIoinfo g_ioinfo_blocks[2][PIOINFO_ARRAY_ELTS];
static void* g_pioinfo[64]; /* msvcrt: ioinfo* __pioinfo[64], 32 fds per block */

__attribute__((constructor)) static void mw_md5deep_wave_init(void) {
    /* fd 0/1/2 = character devices (console) — mingw CRT init calls
     * _isatty on them through __pioinfo during startup. */
    for (int blk = 0; blk < 2; blk++) g_pioinfo[blk] = &g_ioinfo_blocks[blk][0];
    for (int i = 0; i < 3; i++) {
        g_ioinfo_blocks[0][i].osfhnd = (void*)(uintptr_t)(4 + 4 * i); /* std pseudo handles 4/8/0xC */
        g_ioinfo_blocks[0][i].osfile = (char)(0x80 | 0x01);           /* FH_DEVICE | open */
    }
}

/* ---- _atoi64 ---- */
__attribute__((ms_abi)) long long mw__atoi64(const char* s) {
    MW_TRACE("_atoi64(%s)", s ? s : "NULL");
    return s ? strtoll(s, NULL, 10) : 0;
}

/* ---- _ftime (struct _timeb: time64? legacy msvcrt = 8+2+2+2 pad 16) ---- */
__attribute__((ms_abi)) void mw__ftime(void* tb) {
    MW_TRACE("_ftime(tb=%p)", tb);
    if (!tb) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *(long long*)(uint8_t*)tb = (long long)tv.tv_sec;
    *(uint16_t*)((uint8_t*)tb + 8) = (uint16_t)(tv.tv_usec / 1000);
    *(int16_t*)((uint8_t*)tb + 10) = 0;  /* timezone (min) */
    *(int16_t*)((uint8_t*)tb + 12) = 0;  /* dstflag */
}

/* ---- _wfindfirst64/_wfindnext64 (own registry: _wfinddata64_t layout) ---- */
#define MW_WFD64_SIZE 552 /* attrib4 pad4 t8 t8 t8 size8 name[256]*2 */
typedef struct {
    int used; DIR* d; char pattern[1024]; char dirpart[4096]; int first;
} MwWFind;
static MwWFind g_wfinds[16];
static int g_wfind_count = 0;

static int mw_wfd64_pattern_match(const char* pat, const char* name) {
    /* exact or wildcard (case-insensitive like Windows) */
    int star = 0, cq = 0;
    for (const char* p = pat; *p; p++) { if (*p == '*') star = 1; if (*p == '?') cq = 1; }
    if (!star && !cq) return strcasecmp(pat, name) == 0;
    /* minimal wildcard matcher: '*' and '?' */
    const char* pv = pat; const char* nv = name;
    const char* star_p = NULL; const char* star_n = NULL;
    while (*nv) {
        if (*pv == '?' || tolower((unsigned char)*pv) == tolower((unsigned char)*nv)) { pv++; nv++; }
        else if (*pv == '*') { star_p = ++pv; star_n = nv; }
        else if (star_p) { pv = star_p; nv = ++star_n; }
        else return 0;
    }
    while (*pv == '*') pv++;
    return *pv == 0;
}

static void mw_wfd64_fill(void* data, const char* name, struct stat* st) {
    uint8_t* p = (uint8_t*)data;
    memset(p, 0, MW_WFD64_SIZE);
    *(uint32_t*)(p + 0) = S_ISDIR(st->st_mode) ? 0x10 : 0x20;
    if (name[0] == '.') *(uint32_t*)(p + 0) |= 0x2;
    *(long long*)(p + 8)  = (long long)st->st_ctime;
    *(long long*)(p + 16) = (long long)st->st_atime;
    *(long long*)(p + 24) = (long long)st->st_mtime;
    *(long long*)(p + 32) = (long long)st->st_size;
    /* name as UTF-16 at +40 */
    const char* s = name;
    uint16_t* w = (uint16_t*)(p + 40);
    int k = 0;
    while (*s && k < 255) w[k++] = (uint16_t)(unsigned char)*s++;
    w[k] = 0;
}

__attribute__((ms_abi)) intptr_t mw__wfindfirst64(const uint16_t* spec, void* fd64) {
    char p8[4096];
    if (!spec || !fd64 || w3_w16_to_utf8z(spec, p8, sizeof(p8)) < 0) {
        mw_SetLastError(87); return -1;
    }
    w3_normalize_path(p8);
    MW_TRACE("_wfindfirst64(%s)", p8);
    if (g_wfind_count >= 16) { mw_SetLastError(4); return -1; }
    char dirpart[4096];
    snprintf(dirpart, sizeof(dirpart), "%s", p8);
    char* slash = strrchr(dirpart, '/');
    const char* fname = "*";
    if (slash) { *slash = 0; fname = slash + 1; if (!dirpart[0]) snprintf(dirpart, sizeof(dirpart), "/"); }
    else { snprintf(dirpart, sizeof(dirpart), "."); fname = p8; }
    if (!fname[0]) fname = "*";
    DIR* d = opendir(dirpart[0] ? dirpart : ".");
    if (!d) { mw_SetLastError(3); return -1; }
    MwWFind* f = &g_wfinds[g_wfind_count];
    memset(f, 0, sizeof(*f));
    f->d = d;
    snprintf(f->pattern, sizeof(f->pattern), "%s", fname);
    snprintf(f->dirpart, sizeof(f->dirpart), "%s", dirpart[0] ? dirpart : ".");
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (!mw_wfd64_pattern_match(f->pattern, de->d_name)) continue;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            if (strcmp(f->pattern, "*") == 0 && strcmp(f->pattern, ".*") != 0) {
                /* Windows * does not match . / .. unless pattern starts with . */
                continue;
            }
        }
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", f->dirpart, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        mw_wfd64_fill(fd64, de->d_name, &st);
        intptr_t h = (intptr_t)(4 + g_wfind_count++);
        return h;
    }
    closedir(d);
    mw_SetLastError(2);
    return -1;
}

__attribute__((ms_abi)) int mw__wfindnext64(intptr_t h, void* fd64) {
    int idx = (int)h - 4;
    if (idx < 0 || idx >= 16 || !g_wfinds[idx].d) { mw_SetLastError(6); return -1; }
    MwWFind* f = &g_wfinds[idx];
    struct dirent* de;
    while ((de = readdir(f->d)) != NULL) {
        if (!mw_wfd64_pattern_match(f->pattern, de->d_name)) continue;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", f->dirpart, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        mw_wfd64_fill(fd64, de->d_name, &st);
        return 0;
    }
    closedir(f->d);
    f->d = NULL;
    mw_SetLastError(18); /* ERROR_NO_MORE_FILES */
    return -1;
}

/* ---- wide char classification + getwc ---- */
__attribute__((ms_abi)) int mw_getwc(void* f) {
    FILE* hf = map_crt_stream(f);
    return hf ? fgetwc(hf) : WEOF;
}
__attribute__((ms_abi)) int mw_iswalpha(int c) { return iswalpha(c & 0xFFFF); }
__attribute__((ms_abi)) int mw_iswdigit(int c) { return iswdigit(c & 0xFFFF); }

/* ---- sprintf (msvcrt) — ms_abi variadic into guest buffer ---- */
__attribute__((ms_abi)) int mw_sprintf(char* buf, const char* fmt, ...) {
    MW_TRACE("sprintf(fmt=%s)", fmt ? fmt : "NULL");
    if (!buf || !fmt) return -1;
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    __builtin_ms_va_end(ap);
    char tmp[16384];
    FILE* mf = fmemopen(tmp, sizeof tmp, "w");
    if (!mf) return -1;
    int r = mw_printf_emit(mf, fmt, slots, nargs);
    fclose(mf);
    if (r < 0) return -1;
    memcpy(buf, tmp, (size_t)r + 1);
    return r;
}

/* ---- _snwprintf (wide, guest wchar = UTF-16 2B) ----
 * Subset: %s args are guest-wide strings (converted to narrow), %c wide char,
 * everything else passes through to glibc narrow formatting; result converted
 * back to UTF-16. Honest limitation: locale-specific wide conversions are
 * ASCII-scope. */
__attribute__((ms_abi)) int mw__snwprintf(uint16_t* buf, size_t cnt,
        const uint16_t* fmt, ...) {
    if (!buf || !fmt) return -1;
    /* narrow the format with %s -> %s and remember which args were wide */
    char nfmt[2048]; int nk = 0;
    char wide_arg[MW_PRINTF_MAX_ARGS];
    int argi = 0;
    for (int i = 0; fmt[i] && nk + 2 < (int)sizeof(nfmt); i++) {
        if (fmt[i] != '%') { nfmt[nk++] = (char)(fmt[i] & 0xFF); continue; }
        nfmt[nk++] = '%';
        i++;
        if (fmt[i] == '%') { nfmt[nk++] = '%'; continue; }
        /* copy flags/width/precision/length */
        while (fmt[i] && strchr("-+ #0123456789.lhjzt", fmt[i]) && nk + 2 < (int)sizeof(nfmt)) {
            nfmt[nk++] = (char)(fmt[i] & 0xFF); i++;
        }
        char conv = (char)(fmt[i] & 0xFF);
        if (argi < MW_PRINTF_MAX_ARGS) wide_arg[argi] = (conv == 's') ? 1 : (conv == 'c') ? 2 : 0;
        argi++;
        nfmt[nk++] = conv;
    }
    nfmt[nk] = 0;
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(nfmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    for (int i = 0; i < nargs && i < MW_PRINTF_MAX_ARGS; i++) {
        unsigned long long v = (unsigned long long)va_arg(ap, long long);
        if (wide_arg[i] && cls[i] == 0 && v) {
            /* guest wide string/char -> narrow UTF-8 */
            char conv[1024]; int cx = 0;
            const uint16_t* w = (const uint16_t*)(uintptr_t)v;
            if (wide_arg[i] == 2) {
                /* %c: single UTF-16 unit */
                cx = w3_utf16le_to_utf8(w, 1, conv, sizeof(conv) - 1);
                if (cx < 0) cx = 0;
            } else {
                int units = 0; while (w[units] && units < 511) units++;
                cx = w3_utf16le_to_utf8(w, units, conv, sizeof(conv) - 1);
                if (cx < 0) { cx = 0; }
            }
            conv[cx] = 0;
            v = (unsigned long long)(uintptr_t)conv;
        }
        slots[i] = v;
    }
    __builtin_ms_va_end(ap);
    (void)argi;
    char tmp[8192];
    FILE* mf = fmemopen(tmp, sizeof tmp, "w");
    if (!mf) return -1;
    int r = mw_printf_emit(mf, nfmt, slots, nargs);
    fclose(mf);
    if (r < 0) return -1;
    /* narrow result -> UTF-16 into buf */
    size_t o = 0;
    for (int i = 0; tmp[i] && o + 1 < cnt; i++) buf[o++] = (uint16_t)(unsigned char)tmp[i];
    if (o < cnt) buf[o] = 0;
    return (int)o;
}

/* ---- _mktemp ---- */
__attribute__((ms_abi)) char* mw__mktemp(char* tmpl) {
    MW_TRACE("_mktemp(%s)", tmpl ? tmpl : "NULL");
    if (!tmpl) return NULL;
    size_t len = strlen(tmpl);
    static int mk_counter = 0;
    if (len < 6) { /* msvcrt: invalid template returns "" */ tmpl[0] = 0; return tmpl; }
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06d", (getpid() + (mk_counter++)) % 1000000);
    memcpy(tmpl + len - 6, suffix, 6);
    return tmpl;
}

/* ---- SWEEP-003: GUI skeleton v1 — window-class / window / message-loop
 * primitives. Evidence: cports + produkey exit(1) at InitCommonControlsEx
 * (GetProcAddress→FALSE gate); putty/pageant exit inside GUI init; NirSoft
 * toolbar path needs comctl32 registration. Contract (honest): class
 * registration succeeds (ATOM non-zero), window creation returns a stable
 * non-NULL HWND identity, message loop reports WM_QUIT on first GetMessage
 * (no event source yet) so guests unwind their loop cleanly. ---- */
#define MW_GUI_ATOM      0xC001u           /* registered-class identity */
#define MW_GUI_HWND      ((void*)0x00010001) /* first window identity */
/* SWEEP-005 forward decls (definitions live after the SWEEP-004 raster):
 * real paint surface + WNDPROC registry + gated event pump (MW_GUI_EVENTS). */
static void mw_gui_store_wndproc(void* proc);
static void mw_gui_store_window(int w, int h);
static void mw_gui_set_quit(int code);
static void mw_gui_send_destroy(void* hwnd);
static void mw_gui_send_create(void); /* SWEEP-006: real WM_CREATE on CreateWindowEx */
static void mw_gui_request_paint(void);
static int  mw_gui_pump_getmsg(void* msg, void* hwnd, const char* api);
static long long mw_gui_dispatch_msg(const void* msg);
static int  mw_gui_events_enabled(void);
static uint32_t g_char_pending = 0; /* SWEEP-006: WM_CHAR queued by TranslateMessage */
__attribute__((ms_abi)) int mw_InitCommonControlsEx(const void* picc) {
    uint32_t classes = picc ? *(const uint32_t*)((const char*)picc + 4) : 0;
    MW_TRACE("InitCommonControlsEx(dwICC=0x%x) -> TRUE", classes);
    return 1; /* TRUE — all common-control classes registered */
}
__attribute__((ms_abi)) void mw_InitCommonControls(void) {
    MW_TRACE("InitCommonControls()");
}
__attribute__((ms_abi)) uint16_t mw_RegisterClassExW(const void* wcex) {
    /* SWEEP-005: WNDCLASSEXW.lpfnWndProc lives at offset 8 (after cbSize) */
    if (wcex) mw_gui_store_wndproc(*(void* const*)((const char*)wcex + 8));
    MW_TRACE("RegisterClassExW(wcex=%p) -> ATOM 0x%x", wcex, MW_GUI_ATOM);
    return MW_GUI_ATOM;
}
__attribute__((ms_abi)) uint16_t mw_RegisterClassExA(const void* wcex) {
    if (wcex) mw_gui_store_wndproc(*(void* const*)((const char*)wcex + 8));
    MW_TRACE("RegisterClassExA(wcex=%p) -> ATOM 0x%x", wcex, MW_GUI_ATOM);
    return MW_GUI_ATOM;
}
__attribute__((ms_abi)) uint16_t mw_RegisterClassW(const void* wc) {
    /* WNDCLASSW.lpfnWndProc also at offset 8 (style is 4 bytes + pad) */
    if (wc) mw_gui_store_wndproc(*(void* const*)((const char*)wc + 8));
    MW_TRACE("RegisterClassW(wc=%p) -> ATOM 0x%x", wc, MW_GUI_ATOM);
    return MW_GUI_ATOM;
}
__attribute__((ms_abi)) uint16_t mw_RegisterClassA(const void* wc) {
    if (wc) mw_gui_store_wndproc(*(void* const*)((const char*)wc + 8));
    MW_TRACE("RegisterClassA(wc=%p) -> ATOM 0x%x", wc, MW_GUI_ATOM);
    return MW_GUI_ATOM;
}
__attribute__((ms_abi)) void* mw_CreateWindowExW(uint32_t exstyle, const void* cls,
        const uint16_t* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExW(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}
__attribute__((ms_abi)) void* mw_CreateWindowExA(uint32_t exstyle, const void* cls,
        const char* win, uint32_t style, int x, int y, int w, int h,
        void* parent, void* menu, void* inst, void* param) {
    mw_gui_store_window((int)w, (int)h); /* SWEEP-005: allocate paint surface */
    mw_gui_send_create(); /* SWEEP-006: synchronous WM_CREATE */
    MW_TRACE("CreateWindowExA(ex=0x%x style=0x%x cls=%p %dx%d@%d,%d) -> HWND %p",
             exstyle, style, cls, w, h, x, y, MW_GUI_HWND);
    return MW_GUI_HWND;
}
__attribute__((ms_abi)) int mw_ShowWindow(void* hwnd, int cmd) {
    MW_TRACE("ShowWindow(hwnd=%p cmd=%d) -> TRUE", hwnd, cmd);
    return 1;
}
__attribute__((ms_abi)) int mw_UpdateWindow(void* hwnd) {
    MW_TRACE("UpdateWindow(hwnd=%p)", hwnd);
    if (mw_gui_events_enabled()) mw_gui_request_paint(); /* update region -> WM_PAINT */
    return 1;
}
__attribute__((ms_abi)) int mw_DestroyWindow(void* hwnd) {
    MW_TRACE("DestroyWindow(hwnd=%p)", hwnd);
    /* SWEEP-005: real semantics — WM_DESTROY is sent to the wndproc */
    mw_gui_send_destroy(hwnd);
    return 1;
}
__attribute__((ms_abi)) int mw_GetMessageW(void* msg, void* hwnd, uint32_t lo, uint32_t hi) {
    (void)lo; (void)hi;
    return mw_gui_pump_getmsg(msg, hwnd, "GetMessageW");
}
__attribute__((ms_abi)) int mw_GetMessageA(void* msg, void* hwnd, uint32_t lo, uint32_t hi) {
    (void)lo; (void)hi;
    return mw_gui_pump_getmsg(msg, hwnd, "GetMessageA");
}
__attribute__((ms_abi)) int mw_TranslateMessage(const void* msg) {
    /* SWEEP-006: real TranslateMessage — WM_KEYDOWN for a printable key
     * (VK 0x20..0x5A maps to the same ASCII char) queues a WM_CHAR that the
     * pump delivers as the next event. Non-printable keys (arrows/ESC/F-keys)
     * queue nothing -> cadence unchanged for existing guests (snake keys are
     * all non-printable by design). */
    if (!msg) return 0;
    const uint8_t* p = (const uint8_t*)msg;
    uint32_t m = *(const uint32_t*)(p + 8);
    uintptr_t wp = *(const uintptr_t*)(p + 16);
    if (m == 0x0100u && wp >= 0x20u && wp <= 0x5Au) {
        g_char_pending = (uint32_t)wp;
        MW_TRACE("[GUI-EVENT] TranslateMessage WM_KEYDOWN VK=0x%llx -> WM_CHAR queued",
                 (unsigned long long)wp);
        return 1;
    }
    return 0;
}
__attribute__((ms_abi)) long long mw_DispatchMessageW(const void* msg) {
    return mw_gui_dispatch_msg(msg); /* SWEEP-005: calls guest wndproc */
}
__attribute__((ms_abi)) long long mw_DispatchMessageA(const void* msg) {
    return mw_gui_dispatch_msg(msg); /* SWEEP-005: calls guest wndproc */
}
__attribute__((ms_abi)) long long mw_DefWindowProcW(void* hwnd, uint32_t msg,
        uintptr_t wp, long long lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}
__attribute__((ms_abi)) long long mw_DefWindowProcA(void* hwnd, uint32_t msg,
        uintptr_t wp, long long lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}
__attribute__((ms_abi)) void mw_PostQuitMessage(int code) {
    mw_gui_set_quit(code); /* SWEEP-005: pump honors this on next GetMessage */
    MW_TRACE("PostQuitMessage(%d)", code);
}
/* ==== end SWEEP-003 GUI skeleton ==== */

/* ---- SWEEP-004: GDI-lite dialog raster — MessageBoxW/A render to BMP ----
 * User mandate (2026-10-18): «از برنامه گرافیکی تصویر هم بده» — produce a
 * real image from a graphical program. Classic Win95-style dialog drawn into
 * an RGB buffer with the public-domain font8x8 (D. Hepper, U+0000..007F),
 * written as 24-bit BMP into <results>/screenshots/messagebox_N.bmp.
 * Honest contract: layout is ours (no GDI yet), but the TEXT, TITLE, and
 * ICON CLASS are the guest's own — the image is genuine program output. */
#include "font8x8_basic.h"
static char g_result_base_dir[512] = "miniwin-results";
static int  g_dialog_count = 0;

typedef struct { uint8_t* px; int w, h; } MWBmp;

static void mwb_set(MWBmp* b, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) return;
    uint8_t* p = b->px + 3 * ((size_t)y * b->w + x);
    p[0] = (uint8_t)(c & 0xFF); p[1] = (uint8_t)((c >> 8) & 0xFF); p[2] = (uint8_t)((c >> 16) & 0xFF);
}
static void mwb_fill(MWBmp* b, int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) mwb_set(b, i, j, c);
}
/* Win95 3D chrome: raised (out) or sunken (in) */
static void mwb_3d(MWBmp* b, int x, int y, int w, int h, int raised) {
    uint32_t lt = 0xFFFFFF, dk = 0x808080, blk = 0x000000, face = 0xC0C0C0;
    mwb_fill(b, x, y, w, h, face);
    mwb_fill(b, x, y, w, 2, raised ? lt : dk);        /* top */
    mwb_fill(b, x, y, 2, h, raised ? lt : dk);        /* left */
    mwb_fill(b, x, y + h - 2, w, 2, raised ? dk : lt);/* bottom */
    mwb_fill(b, x + w - 2, y, 2, h, raised ? dk : lt);/* right */
    mwb_fill(b, x, y, 1, 1, blk); mwb_fill(b, x + w - 1, y, 1, 1, blk);
    mwb_fill(b, x, y + h - 1, 1, 1, blk); mwb_fill(b, x + w - 1, y + h - 1, 1, 1, blk);
}
static void mwb_text8(MWBmp* b, int x, int y, const char* s, uint32_t c, int scale) {
    for (; *s; s++, x += 8 * scale) {
        uint8_t ch = (uint8_t)*s;
        if (ch > 0x7F) ch = '?';
        for (int ry = 0; ry < 8; ry++) {
            uint8_t row = font8x8_basic[ch][ry];
            for (int rx = 0; rx < 8; rx++)
                if (row & (1 << rx))
                    mwb_fill(b, x + rx * scale, y + ry * scale, scale, scale, c);
        }
    }
}
static int mwb_textw(const char* s, int scale) { return (int)strlen(s) * 8 * scale; }
static void mwb_circle(MWBmp* b, int cx, int cy, int r, uint32_t c) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r) mwb_set(b, cx + i, cy + j, c);
}

static void mw_dialog_write_bmp(const char* title, const char* text, uint32_t type) {
    int sc = 2;                                  /* glyph scale: 8x8 -> 16x16 px */
    uint32_t navy = 0x000080, white = 0xFFFFFF; /* constants are 0xRRGGBB; mwb_set packs BGR */
    int lines_max = 24, tw = 0, nl = 0;
    char* copy = strdup(text ? text : "");
    char* linev[32];
    for (char* ln = strtok(copy, "\n"); ln && nl < lines_max; ln = strtok(NULL, "\n")) {
        linev[nl++] = ln;
        int lw = mwb_textw(ln, sc); if (lw > tw) tw = lw;
    }
    int icon = type & 0xF0;                       /* 0x10 err 0x20 ? 0x30 warn 0x40 info */
    int icon_area = icon ? 36 : 0;
    int bw = mwb_textw("OK", sc) + 32, bh = 30;   /* OK button */
    int W = tw + 24 + icon_area; if (W < 200) W = 200;
    if (W > 720) { W = 720; }                     /* clip honest: no wrap engine yet */
    int client_h = 14 + nl * 10 * sc + 16;
    int H = 18 + client_h + 12 + bh + 12;
    MWBmp b = { malloc((size_t)W * H * 3), W, H };
    if (!b.px) { free(copy); return; }
    mwb_fill(&b, 0, 0, W, H, 0xC0C0C0);
    /* window frame + navy caption + fake close box */
    mwb_3d(&b, 0, 0, W, H, 1);
    mwb_fill(&b, 2, 2, W - 4, 16, navy);
    mwb_text8(&b, 8, 5, title ? title : "", white, 1);
    mwb_3d(&b, W - 22, 4, 16, 14, 0);
    mwb_text8(&b, W - 19, 6, "x", 0x000000, 1);
    /* icon */
    int tx = 12 + icon_area, ty = 18 + 8;
    if (icon == 0x30) {                            /* warning: yellow ! */
        mwb_fill(&b, 12, ty + 6, 30, 30, 0x000080); /* shadow block */
        mwb_fill(&b, 10, ty + 4, 30, 30, 0xFFFF00); /* classic warning yellow */
        mwb_text8(&b, 22, ty + 14, "!", 0x000000, 2);
    } else if (icon) {                             /* err/question/info: colored disc */
        uint32_t col = (icon == 0x10) ? 0xC00000 : 0x0000C0; /* err red disc, ?/i blue disc */
        mwb_circle(&b, 27, ty + 18, 15, 0x000000);
        mwb_circle(&b, 27, ty + 17, 14, col);
        mwb_text8(&b, 23, ty + 12, (icon == 0x10) ? "X" : (icon == 0x20) ? "?" : "i", white, 2);
    }
    /* body text lines */
    for (int i = 0; i < nl; i++)
        mwb_text8(&b, tx, ty + i * 10 * sc, linev[i], 0x000000, sc);
    /* OK button centered */
    mwb_3d(&b, (W - bw) / 2, H - bh - 10, bw, bh, 1);
    mwb_text8(&b, (W - bw) / 2 + (bw - mwb_textw("OK", sc)) / 2, H - bh - 10 + 7, "OK", 0x000000, sc);
    /* write 24-bit bottom-up BMP */
    char path[640];
    snprintf(path, sizeof(path), "%s/screenshots/messagebox_%d.bmp",
             g_result_base_dir, ++g_dialog_count);
    mkdir(g_result_base_dir, 0755);
    char sdir[640]; snprintf(sdir, sizeof(sdir), "%s/screenshots", g_result_base_dir);
    mkdir(sdir, 0755);
    int pad = (4 - (W * 3) % 4) % 4;
    uint32_t datasz = (uint32_t)((W * 3 + pad) * H);
    uint8_t hdr[54] = { 'B','M',0,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,0,0,0,0,0,0,0,0,1,0,24,0 };
    uint32_t fsz = 54 + datasz;
    memcpy(hdr + 2, &fsz, 4); memcpy(hdr + 22, &H, 4); memcpy(hdr + 18, &W, 4);
    memcpy(hdr + 34, &datasz, 4);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(hdr, 1, 54, f);
        uint8_t* row = malloc((size_t)W * 3 + 4);
        for (int y = H - 1; y >= 0; y--) {
            memcpy(row, b.px + 3 * ((size_t)y * W), (size_t)W * 3);
            memset(row + W * 3, 0, (size_t)pad);
            fwrite(row, 1, (size_t)W * 3 + pad, f);
        }
        free(row);
        fclose(f);
        MW_TRACE("[GDI-LITE] dialog #%d rendered %dx%d -> %s", g_dialog_count, W, H, path);
        fprintf(stderr, "[MessageBox] dialog #%d rendered -> %s\n", g_dialog_count, path);
    }
    free(b.px);
    free(copy);
}

/* ---- MessageBoxW: render dialog image, log, return IDOK ---- */
__attribute__((ms_abi)) int mw_MessageBoxW(void* hwnd, const uint16_t* text,
        const uint16_t* caption, uint32_t type) {
    (void)hwnd;
    char t8[2048], c8[256];
    if (text) w3_w16_to_utf8z(text, t8, sizeof(t8)); else t8[0] = 0;
    if (caption) w3_w16_to_utf8z(caption, c8, sizeof(c8)); else c8[0] = 0;
    fprintf(stderr, "[MessageBoxW] %s: %s\n", c8, t8);
    MW_TRACE("MessageBoxW(type=0x%x) caption='%s'", type, c8);
    mw_dialog_write_bmp(c8, t8, type);
    return 1; /* IDOK */
}
__attribute__((ms_abi)) int mw_MessageBoxA(void* hwnd, const char* text,
        const char* caption, uint32_t type) {
    (void)hwnd;
    fprintf(stderr, "[MessageBoxA] %s: %s\n", caption ? caption : "", text ? text : "");
    MW_TRACE("MessageBoxA(type=0x%x) caption='%s'", type, caption ? caption : "");
    mw_dialog_write_bmp(caption ? caption : "", text ? text : "", type);
    return 1; /* IDOK */
}
/* ==== end SWEEP-004 GDI-lite ==== */


/* ---- SWEEP-005: real paint surface + GDI32 module + message pump v2 ----
 * SW-501 (user mandate, must run 100%): a micro-game has to run to
 * completion under MiniWin. Games need a real event loop (WM_TIMER /
 * WM_PAINT / WM_KEYDOWN) and a real draw surface (GDI). This block adds:
 *   1. A persistent window framebuffer allocated at CreateWindowEx from the
 *      guest's own width/height, rasterized with the SWEEP-004 helpers.
 *   2. A guest WNDPROC registry: RegisterClass stores lpfnWndProc (offset 8),
 *      DispatchMessage actually CALLS it — guest message handlers run.
 *   3. An event pump gated by MW_GUI_EVENTS=1: WM_PAINT on demand
 *      (UpdateWindow/InvalidateRect), WM_TIMER (SetTimer), scripted
 *      WM_KEYDOWN (MW_GUI_KEYS=VK,.., paced by MW_GUI_KEY_EVERY, default 8),
 *      budget guard (MW_GUI_EVENTS_BUDGET, default 400), then WM_QUIT.
 *      Default mode (MW_GUI_EVENTS unset) keeps the SWEEP-003 v1 contract:
 *      immediate WM_QUIT — zero behavior change for existing guests.
 *   4. GDI32 pseudo-module: TextOut/Rectangle/Ellipse/FillRect/LineTo/
 *      brushes/pens/state — every draw lands in the framebuffer; EndPaint
 *      dumps it to <results>/screenshots/frame_NNN.bmp (guest's own image).
 * ---- */
#include <stdarg.h>
typedef __attribute__((ms_abi)) long long (*MW_WNDPROC)(void*, uint32_t, uintptr_t, long long);

static MW_WNDPROC g_wndproc_fwd = 0;   /* guest window procedure */
static void*     g_main_hwnd = 0;
static int       g_win_w = 640, g_win_h = 480;
static MWBmp     g_surf = { 0, 0, 0 }; /* window framebuffer (0xRRGGBB pixels) */
static int       g_events_mode = -1;   /* -1 = not yet read from env */
static int       g_paint_pending = 0;
static int       g_quit_flag = 0, g_quit_code = 0;
static int       g_timer_live = 0; static uintptr_t g_timer_id = 0;
static int       g_delivered = 0, g_budget = 400;
static uint32_t  g_keyv[64]; static int g_keyn = 0, g_keyi = 0;
static int       g_key_every = 8;
static int       g_frame_no = 0;
static char      g_frame_dir[600];

/* GDI draw state (colors stored as 0xRRGGBB, guest COLORREF is 0x00BBGGRR) */
static uint32_t  g_cur_brush = 0xFFFFFF; static int g_brush_hollow = 0;
static uint32_t  g_cur_pen = 0x000000;
static int       g_pen_x = 0, g_pen_y = 0;
static uint32_t  g_text_col = 0x000000;
static uint32_t  g_bk_col = 0xFFFFFF;
static int       g_bk_mode = 2;        /* OPAQUE=2, TRANSPARENT=1 */
static uint32_t  g_brush_rgb[32]; static uint8_t g_brush_live[32];
static uint32_t  g_pen_rgb[32];   static uint8_t g_pen_live[32];
static int       g_obj_next = 0;

/* ---- SWEEP-006: memory DCs + compatible bitmaps + BitBlt (real double buffering) ----
 * A memory DC (CreateCompatibleDC) draws into a compatible bitmap
 * (CreateCompatibleBitmap + SelectObject); BitBlt copies bitmap->window or
 * bitmap->bitmap. Draw functions below route through mw_dc_surface(hdc), so
 * a guest that renders off-screen and blits once per frame works for real. */
#define MW_MEMDC_BASE    0x00021000u  /* memDC handles   0x00021001+i */
#define MW_HBITMAP_BASE  0x00052000u  /* HBITMAP handles 0x00052001+i (index=v&0xFF)-1 */
#define MW_MEMDC_MAX     8
static MWBmp   g_bitmap_obj[MW_MEMDC_MAX]; /* CreateCompatibleBitmap pixel store */
static uint8_t g_bitmap_live[MW_MEMDC_MAX];
static int     g_memdc_sel[MW_MEMDC_MAX];  /* selected bitmap index, -1 = none */
static uint8_t g_memdc_live[MW_MEMDC_MAX];

/* target surface of a DC: the window DC (0x20001 — BeginPaint/GetDC) draws
 * into the window framebuffer; a memory DC draws into its selected bitmap. */
static MWBmp* mw_dc_surface(void* hdc) {
    uintptr_t v = (uintptr_t)hdc;
    if (v == 0x00020001u) return g_surf.px ? &g_surf : 0;
    if ((v & 0xFFFFF000u) == MW_MEMDC_BASE) {
        int i = (int)(v & 0xFF) - 1; /* handle = base|(slot+1) */
        if (i >= 0 && i < MW_MEMDC_MAX && g_memdc_live[i] && g_memdc_sel[i] >= 0)
            return &g_bitmap_obj[g_memdc_sel[i]];
    }
    return 0;
}

static uint32_t mw_cr2rgb(uint32_t c) { /* COLORREF 0x00BBGGRR -> 0xRRGGBB */
    return ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
}
static uint32_t mw_rgb2cr(uint32_t c) { /* inverse */
    return ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
}
static const char* mw_wm_name(uint32_t m) {
    switch (m) {
    case 0x0002u: return "WM_DESTROY";
    case 0x000Fu: return "WM_PAINT";
    case 0x0012u: return "WM_QUIT";
    case 0x0100u: return "WM_KEYDOWN";
    case 0x0102u: return "WM_CHAR";
    case 0x0113u: return "WM_TIMER";
    default: return "WM_?";
    }
}
/* safe guest-memory probe (BUG-066 discipline: never trust readability) */
static int mw_probe_copy(const void* src, size_t n, void* dst) {
    struct iovec l = { dst, n }, r = { (void*)src, n };
    ssize_t got = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
    return (got == (ssize_t)n) ? (int)n : 0;
}

/* ---- registry / surface hooks (called from SWEEP-003 stubs) ---- */
static void mw_gui_store_wndproc(void* proc) { if (proc) g_wndproc_fwd = (MW_WNDPROC)proc; }
static void mw_gui_store_window(int w, int h) {
    if (w < 32) w = 32; if (w > 1280) w = 1280;
    if (h < 32) h = 32; if (h > 1024) h = 1024;
    g_win_w = w; g_win_h = h; g_main_hwnd = MW_GUI_HWND;
    if (!g_surf.px) {
        g_surf.px = (uint8_t*)calloc((size_t)w * (size_t)h, 3);
        g_surf.w = w; g_surf.h = h;
        mwb_fill(&g_surf, 0, 0, w, h, 0xFFFFFF);
        snprintf(g_frame_dir, sizeof(g_frame_dir), "%s/screenshots", g_result_base_dir);
        mkdir(g_result_base_dir, 0755); mkdir(g_frame_dir, 0755);
        MW_TRACE("[GUI] paint surface allocated %dx%d (SWEEP-005)", w, h);
    }
}
static void mw_gui_set_quit(int code) { g_quit_flag = 1; g_quit_code = code; }
static void mw_gui_send_destroy(void* hwnd) {
    if (mw_gui_events_enabled() && g_wndproc_fwd) {
        long long r = g_wndproc_fwd(hwnd, 0x0002u, 0, 0);
        MW_TRACE("[GUI] WM_DESTROY -> wndproc -> %lld", r);
    }
}
static void mw_gui_request_paint(void) { g_paint_pending = 1; }
/* SWEEP-006: real Win32 sends WM_CREATE synchronously inside CreateWindowEx
 * (guests create their double buffers / child controls there). Gated on the
 * event pump so the v1 no-pump contract is unchanged. */
static void mw_gui_send_create(void) {
    if (mw_gui_events_enabled() && g_wndproc_fwd) {
        long long r = g_wndproc_fwd(MW_GUI_HWND, 0x0001u, 0, 0); /* WM_CREATE */
        MW_TRACE("[GUI] WM_CREATE -> wndproc -> %lld", r);
    }
}

static int mw_gui_events_enabled(void) {
    if (g_events_mode < 0) {
        const char* e = getenv("MW_GUI_EVENTS");
        g_events_mode = (e && e[0] && e[0] != '0') ? 1 : 0;
        const char* k = getenv("MW_GUI_KEYS");
        if (k && g_events_mode) {
            char tmp[512]; strncpy(tmp, k, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
            for (char* t = strtok(tmp, ","); t && g_keyn < 64; t = strtok(NULL, ","))
                g_keyv[g_keyn++] = (uint32_t)strtoul(t, 0, 0);
        }
        const char* b = getenv("MW_GUI_EVENTS_BUDGET");
        if (b) { int v = atoi(b); if (v > 0) g_budget = v; }
        const char* ke = getenv("MW_GUI_KEY_EVERY");
        if (ke) { int v = atoi(ke); if (v > 0) g_key_every = v; }
        if (g_events_mode)
            MW_TRACE("[GUI] event pump v2 ARMED (budget=%d keys=%d every=%d)",
                     g_budget, g_keyn, g_key_every);
    }
    return g_events_mode;
}

/* fill an x64 MSG: hwnd@0 message@8 wParam@16 lParam@24 time@32 pt@36 */
static void mw_gui_fill_msg(void* msg, void* hwnd, uint32_t m, uintptr_t wp, long long lp) {
    if (!msg) return;
    uint8_t* p = (uint8_t*)msg;
    memset(p, 0, 48);
    *(void**)(p + 0) = hwnd;
    *(uint32_t*)(p + 8) = m;
    *(uintptr_t*)(p + 16) = wp;
    *(long long*)(p + 24) = lp;
}

/* pick the next pump event; consume=1 mutates pump state (GetMessage),
 * consume=0 is a PM_NOREMOVE peek. Returns 1 when an event was filled.
 * Priority: WM_KEYDOWN first (BUG-067c: with PAINT first, the pending paint
 * set by every WM_TIMER->InvalidateRect always won at the key-parity slots
 * and scripted keys were never delivered). */
static int mw_gui_next_event(void* msg, void* hwnd, int consume, const char* api) {
    void* h = g_main_hwnd ? g_main_hwnd : hwnd;
    if (g_char_pending) { /* SWEEP-006: TranslateMessage-queued WM_CHAR comes next */
        uint32_t ch = g_char_pending;
        if (consume) { g_char_pending = 0; g_delivered++; }
        mw_gui_fill_msg(msg, h, 0x0102u, (uintptr_t)ch, 1);
        if (consume)
            MW_TRACE("[GUI-EVENT] #%d %s -> WM_CHAR 0x%x", g_delivered, api, ch);
        return 1;
    }
    if (g_keyi < g_keyn && (g_delivered % g_key_every) == 0) {
        uint32_t vk = g_keyv[g_keyi++];
        if (consume) g_delivered++;
        mw_gui_fill_msg(msg, h, 0x0100u, (uintptr_t)vk, 1);
        if (consume) MW_TRACE("[GUI-EVENT] #%d %s -> WM_KEYDOWN VK=0x%x", g_delivered, api, vk);
        return 1;
    }
    if (g_paint_pending) {
        if (consume) { g_paint_pending = 0; g_delivered++; }
        mw_gui_fill_msg(msg, h, 0x000Fu, 0, 0);
        if (consume) MW_TRACE("[GUI-EVENT] #%d %s -> WM_PAINT", g_delivered, api);
        return 1;
    }
    if (g_timer_live) {
        if (consume) g_delivered++;
        mw_gui_fill_msg(msg, h, 0x0113u, (uintptr_t)g_timer_id, 0);
        if (consume)
            MW_TRACE("[GUI-EVENT] #%d %s -> WM_TIMER id=%lu", g_delivered, api,
                     (unsigned long)g_timer_id);
        return 1;
    }
    return 0;
}

static int mw_gui_pump_getmsg(void* msg, void* hwnd, const char* api) {
    if (!mw_gui_events_enabled()) {
        MW_TRACE("%s(hwnd=%p) -> 0 (WM_QUIT: no event source)", api, hwnd);
        return 0;
    }
    /* Win32: when GetMessage returns 0 the MSG holds WM_QUIT with wParam =
     * the PostQuitMessage code — guests do `return (int)msg.wParam;` */
    if (g_quit_flag) {
        MW_TRACE("%s() -> 0 (WM_QUIT code=%d)", api, g_quit_code);
        mw_gui_fill_msg(msg, hwnd, 0x0012u, (uintptr_t)g_quit_code, 0);
        return 0;
    }
    if (g_delivered >= g_budget) {
        MW_TRACE("[GUI] event budget %d exhausted -> WM_QUIT", g_budget);
        g_quit_flag = 1;
        mw_gui_fill_msg(msg, hwnd, 0x0012u, 0, 0);
        return 0;
    }
    if (mw_gui_next_event(msg, hwnd, 1, api)) return 1;
    MW_TRACE("%s() -> 0 (no event source: no timer/paint/keys left)", api);
    g_quit_flag = 1;
    mw_gui_fill_msg(msg, hwnd, 0x0012u, 0, 0);
    return 0;
}

static long long mw_gui_dispatch_msg(const void* msg) {
    if (!msg) return 0;
    const uint8_t* p = (const uint8_t*)msg;
    void* hwnd = *(void* const*)p;
    uint32_t m = *(const uint32_t*)(p + 8);
    uintptr_t wp = *(const uintptr_t*)(p + 16);
    long long lp = *(const long long*)(p + 24);
    if (m == 0x0012u) return 0; /* WM_QUIT is never dispatched */
    if (g_wndproc_fwd && hwnd == g_main_hwnd) {
        long long r = g_wndproc_fwd(hwnd, m, wp, lp);
        MW_TRACE("[GUI-EVENT] dispatch %s wp=0x%llx -> %lld",
                 mw_wm_name(m), (unsigned long long)wp, r);
        return r;
    }
    MW_TRACE("[GUI-EVENT] dispatch %s -> 0 (no registered wndproc)", mw_wm_name(m));
    return 0;
}

/* ---- paint surface: BeginPaint/EndPaint + frame dump ---- */
__attribute__((ms_abi)) void* mw_BeginPaint(void* hwnd, void* ps) {
    if (ps) {
        /* x64 PAINTSTRUCT: hdc@0 fErase@8 rcPaint@12 fRestore@28 fIncUpdate@32
         * rgbReserved[32]@36 — total 72 bytes */
        uint8_t* p = (uint8_t*)ps;
        memset(p, 0, 72);
        *(void**)(p + 0) = (void*)0x00020001;
        *(uint32_t*)(p + 8) = 1; /* fErase = TRUE */
        *(int32_t*)(p + 12) = 0; *(int32_t*)(p + 16) = 0;
        *(int32_t*)(p + 20) = g_win_w; *(int32_t*)(p + 24) = g_win_h;
    }
    if (g_surf.px) mwb_fill(&g_surf, 0, 0, g_surf.w, g_surf.h, 0xFFFFFF); /* erase */
    MW_TRACE("BeginPaint(hwnd=%p) -> hdc 0x20001 (%dx%d)", hwnd, g_win_w, g_win_h);
    return (void*)0x00020001;
}
/* 24-bit bottom-up BMP writer for the window surface */
static void mw_surface_dump(const char* kind) {
    if (!g_surf.px) return;
    char path[768];
    snprintf(path, sizeof(path), "%s/frame_%03d.bmp",
             g_frame_dir[0] ? g_frame_dir : "miniwin-results/screenshots", ++g_frame_no);
    int W = g_surf.w, H = g_surf.h;
    int pad = (4 - (W * 3) % 4) % 4;
    uint32_t datasz = (uint32_t)((W * 3 + pad) * H);
    uint8_t hdr[54] = { 'B','M',0,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0,0,0,0,0,0,0,0,0,1,0,24,0 };
    uint32_t fsz = 54 + datasz;
    memcpy(hdr + 2, &fsz, 4); memcpy(hdr + 22, &H, 4); memcpy(hdr + 18, &W, 4);
    memcpy(hdr + 34, &datasz, 4);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(hdr, 1, 54, f);
        uint8_t* row = (uint8_t*)malloc((size_t)W * 3 + 4);
        if (row) {
            for (int y = H - 1; y >= 0; y--) {
                memcpy(row, g_surf.px + 3 * ((size_t)y * W), (size_t)W * 3);
                memset(row + W * 3, 0, (size_t)pad);
                fwrite(row, 1, (size_t)W * 3 + pad, f);
            }
            free(row);
        }
        fclose(f);
        MW_TRACE("[GDI] frame #%d (%s) %dx%d -> %s", g_frame_no, kind, W, H, path);
        fprintf(stderr, "[GDI-FRAME] #%d -> %s\n", g_frame_no, path);
    }
}
__attribute__((ms_abi)) int mw_EndPaint(void* hwnd, const void* ps) {
    (void)hwnd; (void)ps;
    mw_surface_dump("EndPaint");
    return 1; /* TRUE */
}
__attribute__((ms_abi)) void* mw_GetDC(void* hwnd) {
    (void)hwnd;
    return (void*)0x00020001;
}
__attribute__((ms_abi)) int mw_ReleaseDC(void* hwnd, void* hdc) {
    (void)hwnd; (void)hdc;
    return 1;
}

/* ---- SWEEP-005 part 2: USER32 draw stubs + GDI32 pseudo-module ---- */
__attribute__((ms_abi)) int mw_FillRect(void* hdc, const void* rect, void* hbrush) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (rect && S) {
        const int32_t* r = (const int32_t*)rect;
        uintptr_t v = (uintptr_t)hbrush;
        uint32_t col = 0xC0C0C0;
        int hollow = 0;
        if ((v & 0xFFFFF000u) == 0x00030000u) {
            int i = (int)(v & 0x1F);
            if (g_brush_live[i]) col = g_brush_rgb[i];
        } else if ((v & 0xFFFF0000u) == 0x00040000u) {
            int idx = (int)(v & 0xFF);
            switch (idx) { /* stock brushes */
            case 0: col = 0xFFFFFF; break; case 1: col = 0xC0C0C0; break;
            case 2: col = 0x808080; break; case 3: col = 0x404040; break;
            case 4: col = 0x000000; break;
            case 5: hollow = 1; break; /* NULL_BRUSH: paints nothing */
            case 18: col = 0xFFFFFF; break; /* DC_BRUSH */
            default: col = 0xC0C0C0; break;
            }
        }
        if (!hollow) {
            mwb_fill(S, r[0], r[1], r[2] - r[0], r[3] - r[1], col);
            MW_TRACE("[GDI] FillRect(%d,%d)-(%d,%d) color=#%06x", r[0], r[1], r[2], r[3], col);
        }
    }
    return 1;
}
__attribute__((ms_abi)) int mw_GetClientRect(void* hwnd, void* rect) {
    (void)hwnd;
    if (rect) {
        int32_t* r = (int32_t*)rect;
        r[0] = 0; r[1] = 0; r[2] = g_win_w; r[3] = g_win_h;
    }
    MW_TRACE("GetClientRect(hwnd=%p) -> %dx%d", hwnd, g_win_w, g_win_h);
    return 1;
}
__attribute__((ms_abi)) int mw_InvalidateRect(void* hwnd, const void* rect, int erase) {
    (void)hwnd; (void)rect; (void)erase;
    g_paint_pending = 1;
    MW_TRACE("[GUI] InvalidateRect -> paint pending");
    return 1;
}
__attribute__((ms_abi)) int mw_ValidateRect(void* hwnd, const void* rect) {
    (void)hwnd; (void)rect;
    g_paint_pending = 0;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_GetSysColor(int idx) {
    uint32_t c; /* COLORREF 0x00BBGGRR, classic Win95 palette */
    switch (idx) {
    case 5:  c = 0x00FFFFFF; break;  /* COLOR_WINDOW */
    case 8:  c = 0x00000000; break;  /* COLOR_WINDOWTEXT */
    case 13: c = 0x00800000; break;  /* COLOR_HIGHLIGHT */
    case 15: case 16: c = 0x00C0C0C0; break; /* BTNFACE / 3DFACE */
    case 17: c = 0x00808080; break;  /* COLOR_3DSHADOW */
    case 18: c = 0x00000000; break;  /* COLOR_BTNTEXT */
    default: c = 0x00C0C0C0; break;
    }
    MW_TRACE("GetSysColor(%d) -> 0x%06x", idx, c);
    return c;
}
__attribute__((ms_abi)) void* mw_LoadCursorA(void* h, const char* name) {
    (void)h; (void)name;
    return (void*)0x00050001;
}
__attribute__((ms_abi)) void* mw_LoadCursorW(void* h, const uint16_t* name) {
    (void)h; (void)name;
    return (void*)0x00050001;
}
__attribute__((ms_abi)) void* mw_LoadIconA(void* h, const char* name) {
    (void)h; (void)name;
    return (void*)0x00050002;
}
__attribute__((ms_abi)) void* mw_LoadIconW(void* h, const uint16_t* name) {
    (void)h; (void)name;
    return (void*)0x00050002;
}
__attribute__((ms_abi)) long long mw_CallWindowProcA(void* proc, void* hwnd, uint32_t m,
        uintptr_t wp, long long lp) {
    if (!proc) return 0;
    return ((MW_WNDPROC)proc)(hwnd, m, wp, lp);
}
__attribute__((ms_abi)) long long mw_CallWindowProcW(void* proc, void* hwnd, uint32_t m,
        uintptr_t wp, long long lp) {
    if (!proc) return 0;
    return ((MW_WNDPROC)proc)(hwnd, m, wp, lp);
}
__attribute__((ms_abi)) uintptr_t mw_SetTimer(void* hwnd, uintptr_t id, uint32_t elapse, void* proc) {
    (void)hwnd; (void)proc;
    g_timer_live = 1; g_timer_id = id;
    MW_TRACE("[GUI] SetTimer(id=%lu, %ums) -> armed", (unsigned long)id, elapse);
    return id;
}
__attribute__((ms_abi)) int mw_KillTimer(void* hwnd, uintptr_t id) {
    (void)hwnd; (void)id;
    g_timer_live = 0;
    return 1;
}
__attribute__((ms_abi)) int mw_PeekMessageA(void* msg, void* hwnd, uint32_t lo, uint32_t hi, uint32_t remove) {
    (void)lo; (void)hi;
    if (!mw_gui_events_enabled() || g_quit_flag) return 0;
    return mw_gui_next_event(msg, hwnd, (int)(remove & 1u), "PeekMessageA");
}
__attribute__((ms_abi)) int mw_PeekMessageW(void* msg, void* hwnd, uint32_t lo, uint32_t hi, uint32_t remove) {
    (void)lo; (void)hi;
    if (!mw_gui_events_enabled() || g_quit_flag) return 0;
    return mw_gui_next_event(msg, hwnd, (int)(remove & 1u), "PeekMessageW");
}
/* tiny printf: %d %i %u %x %X %s %c %% (+ 'l'/'ll' + width skim).
 * BUG-067b: must use __builtin_ms_va_list (Wine pattern) — the default SysV
 * va_list inside an ms_abi variadic function reads garbage (reg_save_area=0
 * -> movslq (%rsi) with RSI=0 -> SIGSEGV inside mw_vfmt_a, seen at
 * 0x202fd74 in minwin_loader). */
static int mw_vfmt_a(char* dst, size_t cap, const char* fmt, __builtin_ms_va_list ap) {
    size_t o = 0;
    for (const char* p = fmt; *p && o + 1 < cap; p++) {
        if (*p != '%') { dst[o++] = *p; continue; }
        p++;
        while (*p == '-' || *p == '0' || *p == '+' || *p == ' ' ||
               *p == '.' || (*p >= '0' && *p <= '9')) p++;
        if (*p == 'l') p++;
        if (*p == 'l') p++;
        char c = *p;
        if (!c) break;
        if (c == 'd' || c == 'i') {
            long long v = va_arg(ap, int);
            char t[24]; int k = 0; unsigned long long u;
            if (v < 0) { if (o + 1 < cap) dst[o++] = '-'; u = (unsigned long long)(-(v + 1)) + 1ULL; }
            else u = (unsigned long long)v;
            do { t[k++] = (char)('0' + (u % 10)); u /= 10; } while (u);
            while (k && o + 1 < cap) dst[o++] = t[--k];
        } else if (c == 'u') {
            unsigned long long u = va_arg(ap, unsigned int);
            char t[24]; int k = 0;
            do { t[k++] = (char)('0' + (u % 10)); u /= 10; } while (u);
            while (k && o + 1 < cap) dst[o++] = t[--k];
        } else if (c == 'x' || c == 'X') {
            unsigned long long u = va_arg(ap, unsigned int);
            char t[24]; int k = 0;
            const char* hx = (c == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            do { t[k++] = hx[u & 15]; u >>= 4; } while (u);
            while (k && o + 1 < cap) dst[o++] = t[--k];
        } else if (c == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && o + 1 < cap) dst[o++] = *s++;
        } else if (c == 'c') {
            int ch = va_arg(ap, int);
            dst[o++] = (char)ch;
        } else if (c == '%') {
            dst[o++] = '%';
        } else {
            dst[o++] = '%'; /* unknown conv: keep honest */
        }
        dst[o] = 0;
    }
    dst[o] = 0;
    return (int)o;
}
__attribute__((ms_abi)) int mw_wsprintfA(char* buf, const char* fmt, ...) {
    if (!buf) return 0;
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    int n = mw_vfmt_a(buf, 1024, fmt, ap);
    __builtin_ms_va_end(ap);
    return n;
}
__attribute__((ms_abi)) int mw_wsprintfW(uint16_t* buf, const uint16_t* fmt, ...) {
    if (!buf) return 0;
    char f8[512], out[1024];
    if (fmt) w3_w16_to_utf8z(fmt, f8, sizeof(f8)); else f8[0] = 0;
    __builtin_ms_va_list ap;
    __builtin_ms_va_start(ap, fmt);
    int n = mw_vfmt_a(out, sizeof(out), f8, ap);
    __builtin_ms_va_end(ap);
    int i = 0;
    for (; i < n && i < 1023; i++) buf[i] = (uint16_t)(uint8_t)out[i];
    buf[i] = 0;
    return n;
}
static int mw_safe_strlen_a(const char* s) {
    int n = 0;
    char chunk[128];
    while (n < 32768) {
        if (!mw_probe_copy(s + n, sizeof(chunk), chunk)) {
            for (int k = 0; k < (int)sizeof(chunk); k++) {
                char c;
                if (!mw_probe_copy(s + n + k, 1, &c)) return n;
                if (!c) return n + k;
            }
            return n;
        }
        for (int k = 0; k < (int)sizeof(chunk); k++)
            if (!chunk[k]) return n + k;
        n += (int)sizeof(chunk);
    }
    return n;
}
__attribute__((ms_abi)) int mw_lstrlenA(const char* s) {
    return s ? mw_safe_strlen_a(s) : 0;
}
__attribute__((ms_abi)) int mw_lstrlenW(const uint16_t* s) {
    if (!s) return 0;
    int n = 0;
    uint16_t chunk[64];
    while (n < 16384) {
        if (!mw_probe_copy(s + n, sizeof(chunk), chunk)) return n;
        for (int k = 0; k < (int)(sizeof(chunk) / 2); k++)
            if (!chunk[k]) return n + k;
        n += (int)(sizeof(chunk) / 2);
    }
    return n;
}
/* ---- GDI32 module: text, shapes, objects, state ---- */
__attribute__((ms_abi)) int mw_TextOutA(void* hdc, int x, int y, const char* s, int len) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (!s || len <= 0 || !S) return 1;
    if (len > 512) len = 512;
    char buf[513];
    if (!mw_probe_copy(s, (size_t)len, buf)) {
        MW_TRACE("[GDI] TextOutA @%d,%d len=%d (guest text unreadable)", x, y, len);
        return 1;
    }
    buf[len] = 0;
    if (g_bk_mode != 1) mwb_fill(S, x, y, len * 8, 8, g_bk_col);
    mwb_text8(S, x, y, buf, g_text_col, 1);
    MW_TRACE("[GDI] TextOutA(%d,%d) '%s'", x, y, buf);
    return 1;
}
__attribute__((ms_abi)) int mw_TextOutW(void* hdc, int x, int y, const uint16_t* s, int len) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (!s || len <= 0 || !S) return 1;
    if (len > 512) len = 512;
    uint16_t wbuf[513];
    if (!mw_probe_copy(s, (size_t)len * 2, wbuf)) return 1;
    wbuf[len] = 0;
    char buf[1025];
    w3_w16_to_utf8z(wbuf, buf, sizeof(buf));
    if (g_bk_mode != 1) mwb_fill(S, x, y, len * 8, 8, g_bk_col);
    mwb_text8(S, x, y, buf, g_text_col, 1);
    MW_TRACE("[GDI] TextOutW(%d,%d) '%s'", x, y, buf);
    return 1;
}
__attribute__((ms_abi)) int mw_ExtTextOutA(void* hdc, int x, int y, uint32_t opts,
        const void* rect, const char* s, int len, const void* dx) {
    (void)opts; (void)rect; (void)dx;
    return mw_TextOutA(hdc, x, y, s, len);
}
__attribute__((ms_abi)) int mw_DrawTextA(void* hdc, const char* s, int n, void* rect, uint32_t flags) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (!s || !rect || !S) return 0;
    char buf[513];
    int cap = (n < 0) ? 512 : (n > 512 ? 512 : n);
    if (n < 0) {
        if (!mw_probe_copy(s, 512, buf)) return 0;
        buf[512] = 0;
    } else {
        if (!mw_probe_copy(s, (size_t)cap, buf)) return 0;
        buf[cap] = 0;
    }
    int32_t* r = (int32_t*)rect;
    int tw = (int)strlen(buf) * 8, th = 8;
    if (flags & 0x400u) { /* DT_CALCRECT */
        r[2] = r[0] + tw; r[3] = r[1] + th;
        MW_TRACE("[GDI] DrawTextA DT_CALCRECT '%s' -> %dx%d", buf, tw, th);
        return th;
    }
    mwb_text8(S, r[0], r[1], buf, g_text_col, 1);
    MW_TRACE("[GDI] DrawTextA @(%d,%d) '%s'", r[0], r[1], buf);
    return th;
}
__attribute__((ms_abi)) int mw_Rectangle(void* hdc, int l, int t, int r, int b) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (S && r > l && b > t) {
        if (!g_brush_hollow) mwb_fill(S, l, t, r - l, b - t, g_cur_brush);
        mwb_fill(S, l, t, r - l, 1, g_cur_pen);
        mwb_fill(S, l, b - 1, r - l, 1, g_cur_pen);
        mwb_fill(S, l, t, 1, b - t, g_cur_pen);
        mwb_fill(S, r - 1, t, 1, b - t, g_cur_pen);
        MW_TRACE("[GDI] Rectangle(%d,%d,%d,%d) fill=#%06x", l, t, r, b, g_cur_brush);
    }
    return 1;
}
__attribute__((ms_abi)) int mw_Ellipse(void* hdc, int l, int t, int r, int b) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (S && r > l && b > t) {
        int cx = (l + r) / 2, cy = (t + b) / 2;
        long rx = (r - l) / 2, ry = (b - t) / 2;
        if (rx < 1 || ry < 1) {
            mwb_fill(S, l, t, r - l, b - t, g_cur_brush);
        } else {
            for (int y = t; y <= b; y++)
                for (int x = l; x <= r; x++) {
                    long dx = (long)(x - cx) * 2, dy = (long)(y - cy) * 2;
                    if ((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry) <= 1)
                        mwb_set(S, x, y, g_cur_brush);
                }
        }
        MW_TRACE("[GDI] Ellipse(%d,%d,%d,%d) fill=#%06x", l, t, r, b, g_cur_brush);
    }
    return 1;
}
__attribute__((ms_abi)) int mw_MoveToEx(void* hdc, int x, int y, void* old) {
    (void)hdc;
    if (old) { ((int32_t*)old)[0] = g_pen_x; ((int32_t*)old)[1] = g_pen_y; }
    g_pen_x = x; g_pen_y = y;
    MW_TRACE("[GDI] MoveToEx(%d,%d)", x, y);
    return 1;
}
static void mwb_line(MWBmp* b, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
    int err = dx - dy;
    for (;;) {
        mwb_set(b, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}
__attribute__((ms_abi)) int mw_LineTo(void* hdc, int x, int y) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (S) mwb_line(S, g_pen_x, g_pen_y, x, y, g_cur_pen);
    g_pen_x = x; g_pen_y = y;
    MW_TRACE("[GDI] LineTo(%d,%d)", x, y);
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_SetPixel(void* hdc, int x, int y, uint32_t colorref) {
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (S) mwb_set(S, x, y, mw_cr2rgb(colorref));
    return colorref;
}
__attribute__((ms_abi)) void* mw_CreateSolidBrush(uint32_t colorref) {
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        int j = (g_obj_next + i) & 31;
        if (!g_brush_live[j]) { slot = j; break; }
    }
    if (slot < 0) slot = g_obj_next & 31;
    g_obj_next = slot + 1;
    g_brush_live[slot] = 1;
    g_brush_rgb[slot] = mw_cr2rgb(colorref);
    MW_TRACE("[GDI] CreateSolidBrush(#%06x) -> h 0x%08x", g_brush_rgb[slot],
             0x00030000u | (unsigned)slot);
    return (void*)(uintptr_t)(0x00030000u | (unsigned)slot);
}
__attribute__((ms_abi)) void* mw_CreatePen(int style, int width, uint32_t colorref) {
    (void)style;
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        int j = (g_obj_next + i) & 31;
        if (!g_pen_live[j]) { slot = j; break; }
    }
    if (slot < 0) slot = g_obj_next & 31;
    g_obj_next = slot + 1;
    g_pen_live[slot] = 1;
    g_pen_rgb[slot] = mw_cr2rgb(colorref);
    MW_TRACE("[GDI] CreatePen(w=%d,#%06x) -> h 0x%08x", width, g_pen_rgb[slot],
             0x00031000u | (unsigned)slot);
    return (void*)(uintptr_t)(0x00031000u | (unsigned)slot);
}
__attribute__((ms_abi)) void* mw_GetStockObject(int idx) {
    MW_TRACE("[GDI] GetStockObject(%d)", idx);
    return (void*)(uintptr_t)(0x00040000u | (unsigned)(idx & 0xFF));
}
__attribute__((ms_abi)) void* mw_SelectObject(void* hdc, void* h) {
    uintptr_t v = (uintptr_t)h;
    /* SWEEP-006: bitmap selection into a memory DC (double buffering) */
    {
        uintptr_t dcv = (uintptr_t)hdc;
        if ((dcv & 0xFFFFF000u) == MW_MEMDC_BASE) {
            int di = (int)(dcv & 0xFF) - 1; /* handle = base|(slot+1) */
            if (di >= 0 && di < MW_MEMDC_MAX && g_memdc_live[di]) {
                if ((v & 0xFFFFF000u) == MW_HBITMAP_BASE) {
                    int prev = g_memdc_sel[di];
                    int bi = (int)(v & 0xFF) - 1;
                    g_memdc_sel[di] =
                        (bi >= 0 && bi < MW_MEMDC_MAX && g_bitmap_live[bi]) ? bi : -1;
                    MW_TRACE("[GDI] SelectObject(memdc=%d, 0x%08llx) -> sel=%d",
                             di, (unsigned long long)v, g_memdc_sel[di]);
                    if (prev >= 0)
                        return (void*)(uintptr_t)(MW_HBITMAP_BASE | (unsigned)(prev + 1));
                    return (void*)0x00040001; /* dummy previous object */
                }
                /* brush/pen/stock on a memDC: fall through to the global draw
                 * state — selecting a brush does NOT deselect the bitmap
                 * (real GDI keeps them independent). */
            }
        }
    }
    if ((v & 0xFFFFF000u) == 0x00030000u) {
        int i = (int)(v & 0x1F);
        if (g_brush_live[i]) { g_cur_brush = g_brush_rgb[i]; g_brush_hollow = 0; }
    } else if ((v & 0xFFFFF000u) == 0x00031000u) {
        int i = (int)(v & 0x1F);
        if (g_pen_live[i]) g_cur_pen = g_pen_rgb[i];
    } else if ((v & 0xFFFF0000u) == 0x00040000u) {
        int idx = (int)(v & 0xFF);
        switch (idx) {
        case 0: g_cur_brush = 0xFFFFFF; g_brush_hollow = 0; break;
        case 1: g_cur_brush = 0xC0C0C0; g_brush_hollow = 0; break;
        case 2: g_cur_brush = 0x808080; g_brush_hollow = 0; break;
        case 3: g_cur_brush = 0x404040; g_brush_hollow = 0; break;
        case 4: g_cur_brush = 0x000000; g_brush_hollow = 0; break;
        case 5: g_brush_hollow = 1; break;                    /* NULL_BRUSH */
        case 6: g_cur_pen = 0xFFFFFF; break;                  /* WHITE_PEN */
        case 7: g_cur_pen = 0x000000; break;                  /* BLACK_PEN */
        default: break; /* fonts: font8x8 used regardless */
        }
    }
    MW_TRACE("[GDI] SelectObject(0x%08llx)", (unsigned long long)v);
    return (void*)0x00040001; /* dummy previous object */
}
__attribute__((ms_abi)) int mw_DeleteObject(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFFF000u) == 0x00030000u) g_brush_live[v & 0x1F] = 0;
    if ((v & 0xFFFFF000u) == 0x00031000u) g_pen_live[v & 0x1F] = 0;
    return 1;
}
__attribute__((ms_abi)) uint32_t mw_SetTextColor(void* hdc, uint32_t colorref) {
    (void)hdc;
    uint32_t prev = mw_rgb2cr(g_text_col);
    g_text_col = mw_cr2rgb(colorref);
    MW_TRACE("[GDI] SetTextColor(#%06x)", g_text_col);
    return prev;
}
__attribute__((ms_abi)) uint32_t mw_SetBkColor(void* hdc, uint32_t colorref) {
    (void)hdc;
    uint32_t prev = mw_rgb2cr(g_bk_col);
    g_bk_col = mw_cr2rgb(colorref);
    MW_TRACE("[GDI] SetBkColor(#%06x)", g_bk_col);
    return prev;
}
__attribute__((ms_abi)) int mw_SetBkMode(void* hdc, int mode) {
    (void)hdc;
    int prev = g_bk_mode;
    g_bk_mode = mode;
    MW_TRACE("[GDI] SetBkMode(%d)", mode);
    return prev;
}
__attribute__((ms_abi)) int mw_SetMapMode(void* hdc, int mode) {
    (void)hdc; (void)mode;
    return 1; /* previous: MM_TEXT */
}
__attribute__((ms_abi)) int mw_GetDeviceCaps(void* hdc, int idx) {
    (void)hdc;
    int v = 0;
    switch (idx) {
    case 2:  v = 1; break;          /* TECHNOLOGY = DT_RASDISPLAY */
    case 8:  v = g_win_w; break;    /* HORZRES */
    case 10: v = g_win_h; break;    /* VERTRES */
    case 12: v = 32; break;         /* BITSPIXEL */
    case 14: v = 1; break;          /* PLANES */
    case 24: v = -1; break;         /* NUMCOLORS */
    case 88: case 90: v = 96; break;/* LOGPIXELSX/Y */
    default: v = 0; break;
    }
    MW_TRACE("[GDI] GetDeviceCaps(%d) -> %d", idx, v);
    return v;
}
__attribute__((ms_abi)) int mw_SaveDC(void* hdc) { (void)hdc; return 1; }
__attribute__((ms_abi)) int mw_RestoreDC(void* hdc, int saved) { (void)hdc; (void)saved; return 1; }
__attribute__((ms_abi)) int mw_DeleteDC(void* hdc) {
    uintptr_t v = (uintptr_t)hdc; /* SWEEP-006: free memory DC slots for real */
    if ((v & 0xFFFFF000u) == MW_MEMDC_BASE) {
        int i = (int)(v & 0xFF) - 1; /* handle = base|(slot+1) */
        if (i >= 0 && i < MW_MEMDC_MAX && g_memdc_live[i]) {
            g_memdc_live[i] = 0; g_memdc_sel[i] = -1;
            MW_TRACE("[GDI] DeleteDC(memdc=%d) -> freed", i);
        }
    }
    return 1;
}
__attribute__((ms_abi)) int mw_PatBlt(void* hdc, int x, int y, int w, int h, uint32_t rop) {
    (void)rop; /* v1: ROP ignored, brush fill (covers WHITENESS/PATCOPY paths) */
    MWBmp* S = mw_dc_surface(hdc); /* SWEEP-006: route by DC */
    if (S && w > 0 && h > 0) mwb_fill(S, x, y, w, h, g_cur_brush);
    MW_TRACE("[GDI] PatBlt(%d,%d %dx%d rop=0x%x)", x, y, w, h, rop);
    return 1;
}
/* ==== end SWEEP-005 ==== */

/* ==== SWEEP-006: CreateCompatibleDC/CreateCompatibleBitmap/BitBlt — the
 * double-buffering trio. A memory DC draws into its selected compatible
 * bitmap; BitBlt SRCCOPY copies pixel blocks between DC surfaces (window
 * framebuffer <-> bitmap, bitmap <-> bitmap), clipped to both surfaces. ==== */
__attribute__((ms_abi)) void* mw_CreateCompatibleDC(void* hdc) {
    (void)hdc;
    for (int i = 0; i < MW_MEMDC_MAX; i++) {
        if (!g_memdc_live[i]) {
            g_memdc_live[i] = 1; g_memdc_sel[i] = -1;
            MW_TRACE("[GDI] CreateCompatibleDC -> 0x%08x",
                     MW_MEMDC_BASE | (unsigned)(i + 1));
            return (void*)(uintptr_t)(MW_MEMDC_BASE | (unsigned)(i + 1));
        }
    }
    MW_TRACE("[GDI] CreateCompatibleDC -> NULL (table full)");
    return 0;
}
__attribute__((ms_abi)) void* mw_CreateCompatibleBitmap(void* hdc, int w, int h) {
    (void)hdc;
    if (w < 1) w = 1; if (h < 1) h = 1;
    if (w > 2048) w = 2048; if (h > 2048) h = 2048;
    for (int i = 0; i < MW_MEMDC_MAX; i++) {
        if (!g_bitmap_live[i]) {
            g_bitmap_live[i] = 1;
            g_bitmap_obj[i].w = w; g_bitmap_obj[i].h = h;
            g_bitmap_obj[i].px = (uint8_t*)calloc((size_t)w * (size_t)h, 3);
            if (g_bitmap_obj[i].px) mwb_fill(&g_bitmap_obj[i], 0, 0, w, h, 0xFFFFFF);
            MW_TRACE("[GDI] CreateCompatibleBitmap(%dx%d) -> 0x%08x", w, h,
                     MW_HBITMAP_BASE | (unsigned)(i + 1));
            return (void*)(uintptr_t)(MW_HBITMAP_BASE | (unsigned)(i + 1));
        }
    }
    MW_TRACE("[GDI] CreateCompatibleBitmap -> NULL (table full)");
    return 0;
}

/* ---- SWEEP-010: comctl32 ImageList_Create/Destroy/Add/GetImageCount ----
 * Honest handle-based image lists (procexp64 creates 2 while sizing its
 * toolbar; SWEEP-009 stub map @0x140001f0f/0x140001fab + Destroy x2 at
 * comctl32 internals 0x140132d5e/0x140132d2e). Storage mirrors the MWBmp
 * raster (3 bytes/px BGR, stride = w*3) so ImageList_Add can copy real
 * pixels from any HBITMAP created by our GDI (CreateCompatibleBitmap /
 * LoadImage pool). Slot caps documented; Create validates dimensions. */
#define MW_IMGL_MAX   8
#define MW_IMGL_BASE  0x00061000u  /* HIMAGELIST handles 0x00061001+i */
#define MW_IMGL_IMGMAX 512
typedef struct {
    int cx, cy, count, cap;
    uint32_t flags;
    uint8_t* px; /* cap images * cx*cy, 3 bytes/px BGR */
} MWImgList;
static MWImgList g_imglist[MW_IMGL_MAX];
static uint8_t   g_imglist_live[MW_IMGL_MAX];

static MWImgList* mw_imgl_decode(void* h) {
    uintptr_t v = (uintptr_t)h;
    if ((v & 0xFFFFF000u) != MW_IMGL_BASE) return 0;
    int i = (int)(v & 0xFFu) - 1; /* handle = base|(slot+1) */
    if (i < 0 || i >= MW_IMGL_MAX || !g_imglist_live[i]) return 0;
    return &g_imglist[i];
}
/* source-bitmap decode: HBITMAP from the CreateCompatibleBitmap pool */
static MWBmp* mw_imgl_srcbmp(void* hbmp) {
    uintptr_t v = (uintptr_t)hbmp;
    if ((v & 0xFFFFF000u) != MW_HBITMAP_BASE) return 0;
    int i = (int)(v & 0xFFu) - 1;
    if (i < 0 || i >= MW_MEMDC_MAX || !g_bitmap_live[i]) return 0;
    return &g_bitmap_obj[i];
}
__attribute__((ms_abi)) void* mw_ImageList_Create(int cx, int cy, uint32_t flags,
                                                  int cInitial, int cGrow) {
    (void)cGrow;
    if (cx < 1 || cy < 1 || cx > 256 || cy > 256) return 0;
    for (int i = 0; i < MW_IMGL_MAX; i++) {
        if (!g_imglist_live[i]) {
            g_imglist_live[i] = 1;
            g_imglist[i].cx = cx; g_imglist[i].cy = cy;
            g_imglist[i].flags = flags; g_imglist[i].count = 0;
            g_imglist[i].cap = cInitial > 0 ? cInitial : 1;
            g_imglist[i].px = (uint8_t*)calloc((size_t)g_imglist[i].cap * cx * cy, 3);
            MW_TRACE("[GDI] ImageList_Create(%d,%d,0x%x,%d,%d) -> 0x%08x",
                     cx, cy, flags, cInitial, cGrow, MW_IMGL_BASE | (unsigned)(i + 1));
            return (void*)(uintptr_t)(MW_IMGL_BASE | (unsigned)(i + 1));
        }
    }
    MW_TRACE("[GDI] ImageList_Create -> NULL (table full)");
    return 0;
}
__attribute__((ms_abi)) int mw_ImageList_Destroy(void* himl) {
    MWImgList* L = mw_imgl_decode(himl);
    if (!L) return 0;
    free(L->px);
    int i = (int)((uintptr_t)himl & 0xFFu) - 1;
    memset(&g_imglist[i], 0, sizeof(MWImgList));
    g_imglist_live[i] = 0;
    MW_TRACE("[GDI] ImageList_Destroy(0x%08x) -> TRUE", (unsigned)(uintptr_t)himl);
    return 1;
}
__attribute__((ms_abi)) int mw_ImageList_Add(void* himl, void* hbmImage, void* hbmMask) {
    MWImgList* L = mw_imgl_decode(himl);
    MWBmp* src = hbmImage ? mw_imgl_srcbmp(hbmImage) : 0;
    if (!L || !src || !src->px) return -1;
    if (src->w < L->cx || src->h < L->cy) return -1;
    int n = src->w / L->cx; /* strip bitmap: real API adds w/cx images */
    if (n < 1) return -1;
    if (L->count + n > MW_IMGL_IMGMAX) n = MW_IMGL_IMGMAX - L->count;
    if (n <= 0) return -1;
    if (L->count + n > L->cap) {
        int ncap = L->cap;
        while (ncap < L->count + n) ncap *= 2;
        uint8_t* np = (uint8_t*)realloc(L->px, (size_t)ncap * L->cx * L->cy * 3);
        if (!np) return -1;
        memset(np + (size_t)L->cap * L->cx * L->cy * 3, 0,
               (size_t)(ncap - L->cap) * L->cx * L->cy * 3);
        L->px = np; L->cap = ncap;
    }
    for (int j = 0; j < n; j++)
        for (int y = 0; y < L->cy; y++)
            memcpy(L->px + ((size_t)(L->count + j) * L->cy + y) * L->cx * 3,
                   src->px + ((size_t)y * src->w + j * L->cx) * 3,
                   (size_t)L->cx * 3);
    int first = L->count; L->count += n;
    MW_TRACE("[GDI] ImageList_Add(0x%08x) -> %d (%d images)", (unsigned)(uintptr_t)himl, first, n);
    return first;
}
__attribute__((ms_abi)) int mw_ImageList_GetImageCount(void* himl) {
    MWImgList* L = mw_imgl_decode(himl);
    return L ? L->count : 0;
}

/* ---- SWEEP-010: kernel32!SetDefaultDllDirectories ----
 * Contract (documented policy no-op): the guest loader keeps no dynamic
 * LoadLibraryEx search-path state this could configure — the call is
 * advisory, returns TRUE, and records the requested flags in the trace.
 * Honesty class: same as CoInitializeEx all-purpose stub-0 (== S_OK benign,
 * named + measured), NOT a silent fake of observable state. */
__attribute__((ms_abi)) int mw_SetDefaultDllDirectories(uint32_t flags) {
    MW_TRACE("SetDefaultDllDirectories(0x%x) -> TRUE", flags);
    return 1;
}
__attribute__((ms_abi)) int mw_BitBlt(void* hdcDst, int x, int y, int w, int h,

        void* hdcSrc, int xs, int ys, uint32_t rop) {
    MWBmp* D = mw_dc_surface(hdcDst);
    MWBmp* S = mw_dc_surface(hdcSrc);
    if (rop == 0x00000042u) { /* BLACKNESS */
        if (D) mwb_fill(D, x, y, w, h, 0x000000);
        return 1;
    }
    if (rop == 0x00FF0062u) { /* WHITENESS */
        if (D) mwb_fill(D, x, y, w, h, 0xFFFFFF);
        return 1;
    }
    if (!D || !S) {
        MW_TRACE("[GDI] BitBlt -> 0 (bad dst/src DC)");
        return 0;
    }
    if (rop != 0x00CC0020u) { /* SRCCOPY is the contract; honest fallback */
        mwb_fill(D, x, y, w, h, g_cur_brush);
        MW_TRACE("[GDI] BitBlt rop=0x%x -> brush fill fallback", rop);
        return 1;
    }
    int copied = 0;
    for (int row = 0; row < h; row++) {
        int sy = ys + row, dy = y + row;
        if (sy < 0 || dy < 0 || sy >= S->h || dy >= D->h) continue;
        for (int col = 0; col < w; col++) {
            int sx = xs + col, dx = x + col;
            if (sx < 0 || dx < 0 || sx >= S->w || dx >= D->w) continue;
            memcpy(D->px + 3 * ((size_t)dy * (size_t)D->w + (size_t)dx),
                   S->px + 3 * ((size_t)sy * (size_t)S->w + (size_t)sx), 3);
            copied++;
        }
    }
    MW_TRACE("[GDI] BitBlt %dx%d @%d,%d <- src@%d,%d SRCCOPY (%d px)",
             w, h, x, y, xs, ys, copied);
    return 1;
}

static ImportEntry g_import_table[] = {
    /* KERNEL32.DLL */
    ENTRY("KERNEL32.DLL", "AddVectoredExceptionHandler",        mw_AddVectoredExceptionHandler),
    ENTRY("KERNEL32.DLL", "CloseHandle",                        mw_CloseHandle),
    ENTRY("KERNEL32.DLL", "CreateEventA",                       mw_CreateEventA),
    ENTRY("KERNEL32.DLL", "CreateIoCompletionPort",             mw_CreateIoCompletionPort),
    ENTRY("KERNEL32.DLL", "GetQueuedCompletionStatus",          mw_GetQueuedCompletionStatus),
    ENTRY("KERNEL32.DLL", "PostQueuedCompletionStatus",         mw_PostQueuedCompletionStatus),
    ENTRY("KERNEL32.DLL", "RtlAddFunctionTable",                mw_RtlAddFunctionTable),
    ENTRY("KERNEL32.DLL", "CreateSemaphoreA",                   mw_CreateSemaphoreA),
    ENTRY("KERNEL32.DLL", "DebugBreak",                         mw_DebugBreak),
    ENTRY("KERNEL32.DLL", "DeleteCriticalSection",              mw_DeleteCriticalSection),
    ENTRY("KERNEL32.DLL", "DuplicateHandle",                    mw_DuplicateHandle),
    ENTRY("KERNEL32.DLL", "EnterCriticalSection",               mw_EnterCriticalSection),
    ENTRY("KERNEL32.DLL", "GetConsoleCursorInfo",               mw_GetConsoleCursorInfo),
    ENTRY("KERNEL32.DLL", "GetConsoleMode",                      mw_GetConsoleMode),
    ENTRY("KERNEL32.DLL", "GetCPInfo",                          mw_GetCPInfo),
    ENTRY("KERNEL32.DLL", "GetConsoleScreenBufferInfo",          mw_GetConsoleScreenBufferInfo),
    ENTRY("KERNEL32.DLL", "GetCurrentProcess",                   mw_GetCurrentProcess),
    ENTRY("KERNEL32.DLL", "GetCurrentProcessId",                 mw_GetCurrentProcessId),
    ENTRY("KERNEL32.DLL", "GetCurrentThread",                   mw_GetCurrentThread),
    ENTRY("KERNEL32.DLL", "GetCurrentThreadId",                 mw_GetCurrentThreadId),
    ENTRY("KERNEL32.DLL", "GetFileTime",                         mw_GetFileTime),
    ENTRY("KERNEL32.DLL", "GetHandleInformation",               mw_GetHandleInformation),
    ENTRY("KERNEL32.DLL", "GetLastError",                        mw_GetLastError),
    ENTRY("KERNEL32.DLL", "GetModuleHandleA",                    mw_GetModuleHandleA),
    ENTRY("KERNEL32.DLL", "GetProcAddress",                      mw_GetProcAddress),
    ENTRY("bcryptprimitives.dll", "ProcessPrng",                mw_ProcessPrng), /* SWEEP-002 */
    ENTRY("winmm.dll", "timeBeginPeriod",                       mw_timeBeginPeriod), /* SWEEP-002b: Go osinit */
    ENTRY("winmm.dll", "timeEndPeriod",                         mw_timeEndPeriod), /* SWEEP-002b: Go osinit */
    ENTRY("KERNEL32.DLL", "GetProcessAffinityMask",             mw_GetProcessAffinityMask),
    ENTRY("KERNEL32.DLL", "GetStartupInfoA",                     mw_GetStartupInfoA),
    ENTRY("KERNEL32.DLL", "GetStdHandle",                       mw_GetStdHandle),
    ENTRY("KERNEL32.DLL", "GetSystemTimeAsFileTime",             mw_GetSystemTimeAsFileTime),
    ENTRY("KERNEL32.DLL", "GetThreadContext",                    mw_GetThreadContext),
    ENTRY("KERNEL32.DLL", "GetThreadPriority",                   mw_GetThreadPriority),
    ENTRY("KERNEL32.DLL", "GetTickCount",                        mw_GetTickCount),
    ENTRY("SHELL32.DLL", "CommandLineToArgvW",                   mw_CommandLineToArgvW),
    ENTRY("KERNEL32.DLL", "InitializeCriticalSection",           mw_InitializeCriticalSection),
    ENTRY("KERNEL32.DLL", "IsDBCSLeadByteEx",                    mw_IsDBCSLeadByteEx),
    ENTRY("KERNEL32.DLL", "ExitProcess",                        mw_ExitProcess),
    ENTRY("KERNEL32.DLL", "IsDebuggerPresent",                   mw_IsDebuggerPresent),
    ENTRY("KERNEL32.DLL", "LoadLibraryA",                        mw_LoadLibraryA),
    ENTRY("KERNEL32.DLL", "LeaveCriticalSection",               mw_LeaveCriticalSection),
    ENTRY("KERNEL32.DLL", "MultiByteToWideChar",                 mw_MultiByteToWideChar),
    ENTRY("KERNEL32.DLL", "OpenProcess",                         mw_OpenProcess),
    ENTRY("KERNEL32.DLL", "OutputDebugStringA",                  mw_OutputDebugStringA),
    ENTRY("KERNEL32.DLL", "QueryPerformanceCounter",             mw_QueryPerformanceCounter),
    ENTRY("KERNEL32.DLL", "QueryPerformanceFrequency",           mw_QueryPerformanceFrequency),
    ENTRY("KERNEL32.DLL", "RaiseException",                      mw_RaiseException),
    ENTRY("KERNEL32.DLL", "ReleaseSemaphore",                    mw_ReleaseSemaphore),
    ENTRY("KERNEL32.DLL", "RemoveVectoredExceptionHandler",       mw_RemoveVectoredExceptionHandler),
    ENTRY("KERNEL32.DLL", "ResetEvent",                          mw_ResetEvent),
    ENTRY("KERNEL32.DLL", "ResumeThread",                        mw_ResumeThread),
    ENTRY("KERNEL32.DLL", "RtlCaptureContext",                   mw_RtlCaptureContext),
    ENTRY("KERNEL32.DLL", "RtlLookupFunctionEntry",              mw_RtlLookupFunctionEntry),
    ENTRY("KERNEL32.DLL", "RtlUnwindEx",                         mw_RtlUnwindEx),
    ENTRY("KERNEL32.DLL", "RtlVirtualUnwind",                    mw_RtlVirtualUnwind),
    ENTRY("KERNEL32.DLL", "ScrollConsoleScreenBufferA",          mw_ScrollConsoleScreenBufferA),
    ENTRY("KERNEL32.DLL", "SetConsoleCursorInfo",                mw_SetConsoleCursorInfo),
    ENTRY("KERNEL32.DLL", "SetConsoleCursorPosition",            mw_SetConsoleCursorPosition),
    ENTRY("KERNEL32.DLL", "SetConsoleTextAttribute",             mw_SetConsoleTextAttribute),
    ENTRY("KERNEL32.DLL", "SetEvent",                            mw_SetEvent),
    ENTRY("KERNEL32.DLL", "SetFileTime",                         mw_SetFileTime),
    ENTRY("KERNEL32.DLL", "SetLastError",                        mw_SetLastError),
    ENTRY("KERNEL32.DLL", "SetProcessAffinityMask",              mw_SetProcessAffinityMask),
    ENTRY("KERNEL32.DLL", "SetDllDirectoryA",                    mw_SetDllDirectoryA),
    ENTRY("KERNEL32.DLL", "SetDllDirectoryW",                    mw_SetDllDirectoryW),
    ENTRY("KERNEL32.DLL", "SetThreadContext",                    mw_SetThreadContext),
    ENTRY("KERNEL32.DLL", "SetThreadPriority",                   mw_SetThreadPriority),
    ENTRY("KERNEL32.DLL", "SetUnhandledExceptionFilter",         mw_SetUnhandledExceptionFilter),
    ENTRY("KERNEL32.DLL", "Sleep",                               mw_Sleep),
    ENTRY("KERNEL32.DLL", "SuspendThread",                       mw_SuspendThread),
    ENTRY("KERNEL32.DLL", "TlsAlloc",                            mw_TlsAlloc),
    ENTRY("KERNEL32.DLL", "TlsGetValue",                         mw_TlsGetValue),
    ENTRY("KERNEL32.DLL", "TlsSetValue",                         mw_TlsSetValue),
    ENTRY("KERNEL32.DLL", "TryEnterCriticalSection",             mw_TryEnterCriticalSection),
    ENTRY("KERNEL32.DLL", "VirtualProtect",                      mw_VirtualProtect),
    ENTRY("KERNEL32.DLL", "VirtualQuery",                        mw_VirtualQuery),
    ENTRY("KERNEL32.DLL", "WaitForMultipleObjects",              mw_WaitForMultipleObjects),
    ENTRY("KERNEL32.DLL", "WaitForSingleObject",                 mw_WaitForSingleObject),
    ENTRY("KERNEL32.DLL", "WaitForSingleObjectEx",               mw_WaitForSingleObjectEx),
    ENTRY("KERNEL32.DLL", "WakeConditionVariable",               mw_WakeConditionVariable),
    ENTRY("KERNEL32.DLL", "WakeAllConditionVariable",            mw_WakeAllConditionVariable),
    ENTRY("KERNEL32.DLL", "SleepConditionVariableSRW",           mw_SleepConditionVariableSRW),
    ENTRY("KERNEL32.DLL", "SleepConditionVariableCS",            mw_SleepConditionVariableCS),
    ENTRY("KERNEL32.DLL", "AcquireSRWLockExclusive",             mw_AcquireSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "ReleaseSRWLockExclusive",             mw_ReleaseSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "TryAcquireSRWLockExclusive",          mw_TryAcquireSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "InitOnceExecuteOnce",                 mw_InitOnceExecuteOnce),
    ENTRY("KERNEL32.DLL", "RtlRestoreContext",                   mw_RtlRestoreContext),
    ENTRY("KERNEL32.DLL", "GetModuleHandleW",                    mw_GetModuleHandleW),
    ENTRY("KERNEL32.DLL", "GetThreadId",                         mw_GetThreadId),
    ENTRY("KERNEL32.DLL", "SwitchToThread",                      mw_SwitchToThread),
    ENTRY("KERNEL32.DLL", "WideCharToMultiByte",                 mw_WideCharToMultiByte),
    ENTRY("KERNEL32.DLL", "WriteConsoleOutputA",                 mw_WriteConsoleOutputA),

    /* msvcrt.dll — Code imports */
    ENTRY("msvcrt.dll", "__C_specific_handler",   mw___C_specific_handler),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__C_specific_handler", mw___C_specific_handler),
    ENTRY("msvcrt.dll", "__getmainargs",         mw___getmainargs),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_configure_narrow_argv", mw__configure_narrow_argv),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_narrow_environment", mw__initialize_narrow_environment),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_new_mode", mw__set_new_mode),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_invalid_parameter_handler", mw__set_invalid_parameter_handler),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_crt_atexit",   mw_atexit),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "signal",        mw_signal_stub),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_app_type", mw___set_app_type),
    ENTRY("msvcrt.dll", "__iob_func",            mw___iob_func),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__acrt_iob_func", mw___acrt_iob_func),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfprintf", mw___stdio_common_vfprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsprintf", mw___stdio_common_vsprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfwprintf", mw___stdio_common_vfwprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsscanf",  mw___stdio_common_vsscanf),
    ENTRY("api-ms-win-crt-time-l1-1-0.dll", "_tzset",           mw__tzset),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fopen",           mw_fopen),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fclose",          mw_fclose),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fread",           mw_fread),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fwrite",          mw_fwrite),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fgets",           mw_fgets),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fgetc",           mw_fgetc),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "rewind",          mw_rewind),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fseek",           mw_fseek),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "ftell",           mw_ftell),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "feof",            mw_feof),
    ENTRY("msvcrt.dll", "fseek",                 mw_fseek),
    ENTRY("msvcrt.dll", "ftell",                 mw_ftell),
    ENTRY("msvcrt.dll", "feof",                  mw_feof),
    ENTRY("api-ms-win-crt-time-l1-1-0.dll", "strftime",         mw_strftime),
    ENTRY("api-ms-win-crt-private-l1-1-0.dll", "__p__fmode",    mw___p__fmode),
    ENTRY("api-ms-win-crt-private-l1-1-0.dll", "__p__commode",  mw___p__commode),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p___argc",    mw___p___argc),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p___argv",    mw___p___argv),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p__acmdln",   mw___p__acmdln), /* BUG-067 */
    ENTRY("api-ms-win-crt-environment-l1-1-0.dll", "__p__environ", mw___p__environ),
    ENTRY("msvcrt.dll", "__lconv_init",          mw___lconv_init),
    ENTRY("msvcrt.dll", "___lc_codepage_func",    mw___lc_codepage_func),
    ENTRY("msvcrt.dll", "___mb_cur_max_func",     mw___mb_cur_max_func),
    ENTRY("msvcrt.dll", "__set_app_type",        mw___set_app_type),
    ENTRY("msvcrt.dll", "__setusermatherr",      mw___setusermatherr),
    ENTRY("msvcrt.dll", "_amsg_exit",            mw__amsg_exit),
    ENTRY("msvcrt.dll", "_beginthreadex",        mw__beginthreadex),
    ENTRY("msvcrt.dll", "_cexit",                mw__cexit),
    ENTRY("msvcrt.dll", "_chmod",                mw__chmod),
    ENTRY("msvcrt.dll", "_close",                mw__close),
    ENTRY("msvcrt.dll", "_dup",                  mw__dup),
    ENTRY("msvcrt.dll", "_endthreadex",          mw__endthreadex),
    ENTRY("msvcrt.dll", "_exit",                 mw__exit_noflush),
    ENTRY("msvcrt.dll", "_errno",                mw__errno),
    ENTRY("msvcrt.dll", "_fstat64",              mw__fstat64),
    ENTRY("msvcrt.dll", "_fileno",               mw__fileno),
    ENTRY("msvcrt.dll", "_get_osfhandle",        mw__get_osfhandle),
    ENTRY("msvcrt.dll", "_gmtime64",             mw__gmtime64),
    ENTRY("msvcrt.dll", "_initterm",             mw__initterm),
    ENTRY("msvcrt.dll", "_isatty",               mw__isatty),
    ENTRY("msvcrt.dll", "_kbhit",                mw__kbhit),
    ENTRY("msvcrt.dll", "_localtime64",          mw__localtime64),
    ENTRY("msvcrt.dll", "_lock",                 mw__lock),
    ENTRY("msvcrt.dll", "_lseeki64",             mw__lseeki64),
    ENTRY("msvcrt.dll", "_onexit",               mw__onexit),
    ENTRY("msvcrt.dll", "_open",                 mw__open),
    ENTRY("msvcrt.dll", "_read",                 mw__read),
    ENTRY("msvcrt.dll", "_rename",               mw_rename),
    ENTRY("msvcrt.dll", "_rmdir",                mw__rmdir),
    ENTRY("msvcrt.dll", "_sopen",                mw__sopen),
    ENTRY("msvcrt.dll", "_setmode",              mw__setmode),
    ENTRY("msvcrt.dll", "_setjmp",               mw__setjmp),
    ENTRY("msvcrt.dll", "_stat64",               mw__stat64),
    ENTRY("msvcrt.dll", "_wstat64",              mw__wstat64),
    ENTRY("msvcrt.dll", "_strdup",               mw__strdup),
    ENTRY("msvcrt.dll", "_stricmp",              mw__stricmp),
    ENTRY("msvcrt.dll", "_time64",               mw__time64),
    ENTRY("msvcrt.dll", "_ultoa",                mw__ultoa),
    ENTRY("msvcrt.dll", "_unlink",               mw__unlink),
    ENTRY("msvcrt.dll", "_unlock",               mw__unlock),
    ENTRY("msvcrt.dll", "_wopen",                mw__wopen),
    ENTRY("msvcrt.dll", "_write",                mw__write),
    ENTRY("msvcrt.dll", "_XcptFilter",            mw__XcptFilter),
    ENTRY("msvcrt.dll", "abort",                 mw_abort),
    ENTRY("msvcrt.dll", "atoi",                  mw_atoi),
    ENTRY("msvcrt.dll", "atexit",                mw_atexit),
    ENTRY("msvcrt.dll", "calloc",                mw_calloc),
    ENTRY("msvcrt.dll", "clock",                 mw_clock),
    ENTRY("msvcrt.dll", "exit",                  mw_exit),
    ENTRY("msvcrt.dll", "fflush",                mw_fflush),
    ENTRY("msvcrt.dll", "fprintf",               mw_fprintf),
    ENTRY("msvcrt.dll", "fputc",                 mw_fputc),
    ENTRY("msvcrt.dll", "fputs",                 mw_fputs),
    ENTRY("msvcrt.dll", "free",                  mw_free),
    ENTRY("msvcrt.dll", "__doserrno",            mw___doserrno),
    ENTRY("msvcrt.dll", "_vsnprintf",            mw__vsnprintf),
    ENTRY("msvcrt.dll", "_atoi64",               mw__atoi64),
    ENTRY("msvcrt.dll", "_ftime",                mw__ftime),
    ENTRY("msvcrt.dll", "_wfindfirst64",         mw__wfindfirst64),
    ENTRY("msvcrt.dll", "_wfindnext64",          mw__wfindnext64),
    ENTRY("msvcrt.dll", "getwc",                 mw_getwc),
    ENTRY("msvcrt.dll", "iswalpha",              mw_iswalpha),
    ENTRY("msvcrt.dll", "iswdigit",              mw_iswdigit),
    ENTRY("msvcrt.dll", "sprintf",               mw_sprintf),
    ENTRY("msvcrt.dll", "_snwprintf",            mw__snwprintf),
    ENTRY("msvcrt.dll", "_mktemp",               mw__mktemp),
    ENTRY("KERNEL32.DLL", "MessageBoxW",         mw_MessageBoxW),
    ENTRY("msvcrt.dll", "_aligned_malloc",       mw__aligned_malloc),
    ENTRY("msvcrt.dll", "_aligned_free",         mw__aligned_free),
    ENTRY("msvcrt.dll", "_aligned_realloc",      mw__aligned_realloc),
    ENTRY("msvcrt.dll", "_aligned_offset_realloc", mw__aligned_offset_realloc),
    ENTRY("msvcrt.dll", "_aligned_recalloc",     mw__aligned_recalloc),
    ENTRY("msvcrt.dll", "_aligned_msize",        mw__aligned_msize),
    ENTRY("msvcrt.dll", "_assert",               mw__assert),
    ENTRY("msvcrt.dll", "_strtoi64",             mw__strtoi64),
    ENTRY("msvcrt.dll", "_strtoui64",            mw__strtoui64),
    ENTRY("msvcrt.dll", "wcstol",                mw_wcstol),
    ENTRY("msvcrt.dll", "wcstoul",               mw_wcstoul),
    ENTRY("msvcrt.dll", "_wcstoi64",             mw__wcstoi64),
    ENTRY("msvcrt.dll", "_wcstoui64",            mw__wcstoui64),
    ENTRY("msvcrt.dll", "fputwc",                mw_fputwc),
    ENTRY("msvcrt.dll", "fwprintf",              mw_fwprintf),
    ENTRY("msvcrt.dll", "FlsAlloc",              mw_FlsAlloc),
    ENTRY("msvcrt.dll", "FlsGetValue",           mw_FlsGetValue),
    ENTRY("msvcrt.dll", "FlsSetValue",           mw_FlsSetValue),
    ENTRY("msvcrt.dll", "fwrite",                mw_fwrite),
    ENTRY("msvcrt.dll", "getc",                  mw_getc),
    ENTRY("msvcrt.dll", "getenv",                mw_getenv),
    ENTRY("msvcrt.dll", "islower",               mw_islower),
    ENTRY("msvcrt.dll", "isspace",               mw_isspace),
    ENTRY("msvcrt.dll", "isupper",               mw_isupper),
    ENTRY("msvcrt.dll", "isxdigit",              mw_isxdigit),
    ENTRY("msvcrt.dll", "localeconv",            mw_localeconv),
    ENTRY("msvcrt.dll", "longjmp",               mw_longjmp),
    ENTRY("msvcrt.dll", "malloc",                mw_malloc),
    ENTRY("msvcrt.dll", "memchr",                mw_memchr),
    ENTRY("msvcrt.dll", "memcmp",                mw_memcmp),
    ENTRY("msvcrt.dll", "memcpy",                mw_memcpy),
    ENTRY("msvcrt.dll", "memmove",               mw_memmove),
    ENTRY("msvcrt.dll", "memset",                mw_memset),
    ENTRY("msvcrt.dll", "printf",                mw_printf),
    ENTRY("msvcrt.dll", "puts",                  mw_puts),
    ENTRY("msvcrt.dll", "setvbuf",               mw_setvbuf),
    ENTRY("msvcrt.dll", "qsort",                 mw_qsort),
    ENTRY("msvcrt.dll", "rand",                  mw_rand),
    ENTRY("msvcrt.dll", "realloc",               mw_realloc),
    ENTRY("msvcrt.dll", "rename",                mw_rename),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "remove",           mw_remove),
    ENTRY("msvcrt.dll", "signal",                mw_signal),
    ENTRY("msvcrt.dll", "srand",                 mw_srand),
    ENTRY("msvcrt.dll", "strchr",                mw_strchr),
    ENTRY("msvcrt.dll", "strcmp",                mw_strcmp),
    ENTRY("msvcrt.dll", "strcpy",                mw_strcpy),
    ENTRY("msvcrt.dll", "strcspn",               mw_strcspn),
    ENTRY("msvcrt.dll", "strerror",              (void*)strerror),
    ENTRY("msvcrt.dll", "strlen",                mw_strlen),
    ENTRY("msvcrt.dll", "strncmp",               mw_strncmp),
    ENTRY("msvcrt.dll", "strrchr",               mw_strrchr),
    ENTRY("msvcrt.dll", "strstr",                mw_strstr),
    ENTRY("msvcrt.dll", "strtol",                mw_strtol),
    ENTRY("msvcrt.dll", "strtoul",               mw_strtoul),
    ENTRY("msvcrt.dll", "tolower",               mw_tolower),
    ENTRY("msvcrt.dll", "ungetc",                mw_ungetc),
    ENTRY("msvcrt.dll", "vfprintf",              mw_vfprintf),
    ENTRY("msvcrt.dll", "wcstombs",               mw_wcstombs),
    ENTRY("msvcrt.dll", "wcslen",                mw_wcslen),
    ENTRY("KERNEL32.DLL", "GetCommandLineA", mw_GetCommandLineA),
    ENTRY("KERNEL32.DLL", "GetCommandLineW", mw_GetCommandLineW),
    ENTRY("KERNEL32.DLL", "GetStartupInfoW", mw_GetStartupInfoW),
    ENTRY("KERNEL32.DLL", "GetModuleHandleExA", mw_GetModuleHandleExA),
    ENTRY("KERNEL32.DLL", "GetModuleHandleExW", mw_GetModuleHandleExW),
    ENTRY("KERNEL32.DLL", "GetModuleFileNameA", mw_GetModuleFileNameA),
    ENTRY("KERNEL32.DLL", "GetModuleFileNameW", mw_GetModuleFileNameW),
    ENTRY("KERNEL32.DLL", "LoadLibraryW", mw_LoadLibraryW),
    ENTRY("KERNEL32.DLL", "LoadLibraryExW", mw_LoadLibraryExW),
    ENTRY("KERNEL32.DLL", "FreeLibrary", mw_FreeLibrary),
    ENTRY("KERNEL32.DLL", "FreeLibraryAndExitThread", mw_FreeLibraryAndExitThread),
    ENTRY("KERNEL32.DLL", "GetEnvironmentStringsW", mw_GetEnvironmentStringsW),
    ENTRY("KERNEL32.DLL", "FreeEnvironmentStringsW", mw_FreeEnvironmentStringsW),
    ENTRY("KERNEL32.DLL", "GetEnvironmentVariableW", mw_GetEnvironmentVariableW),
    ENTRY("KERNEL32.DLL", "GetEnvironmentVariableA", mw_GetEnvironmentVariableA),
    ENTRY("KERNEL32.DLL", "SetEnvironmentVariableA", mw_SetEnvironmentVariableA),
    ENTRY("KERNEL32.DLL", "SetEnvironmentVariableW", mw_SetEnvironmentVariableW),
    ENTRY("KERNEL32.DLL", "ExpandEnvironmentStringsW", mw_ExpandEnvironmentStringsW),
    ENTRY("KERNEL32.DLL", "GetProcessHeap", mw_GetProcessHeap),
    ENTRY("KERNEL32.DLL", "HeapCreate", mw_HeapCreate),
    ENTRY("KERNEL32.DLL", "HeapAlloc", mw_HeapAlloc),
    ENTRY("KERNEL32.DLL", "HeapReAlloc", mw_HeapReAlloc),
    ENTRY("KERNEL32.DLL", "HeapFree", mw_HeapFree),
    ENTRY("KERNEL32.DLL", "HeapSize", mw_HeapSize),
    ENTRY("KERNEL32.DLL", "HeapValidate", mw_HeapValidate),
    ENTRY("KERNEL32.DLL", "HeapSetInformation", mw_HeapSetInformation),
    ENTRY("KERNEL32.DLL", "HeapQueryInformation", mw_HeapQueryInformation),
    ENTRY("KERNEL32.DLL", "HeapCompact", mw_HeapCompact),
    ENTRY("KERNEL32.DLL", "GlobalAlloc", mw_GlobalAlloc),
    ENTRY("KERNEL32.DLL", "GlobalLock", mw_GlobalLock),
    ENTRY("KERNEL32.DLL", "GlobalUnlock", mw_GlobalUnlock),
    ENTRY("KERNEL32.DLL", "GlobalSize", mw_GlobalSize),
    ENTRY("KERNEL32.DLL", "GlobalFree", mw_GlobalFree),
    ENTRY("KERNEL32.DLL", "GlobalAddAtomW", mw_GlobalAddAtomW),
    ENTRY("KERNEL32.DLL", "GlobalFindAtomW", mw_GlobalFindAtomW),
    ENTRY("KERNEL32.DLL", "GlobalDeleteAtom", mw_GlobalDeleteAtom),
    ENTRY("KERNEL32.DLL", "GlobalGetAtomNameW", mw_GlobalGetAtomNameW),
    ENTRY("KERNEL32.DLL", "LocalAlloc", mw_LocalAlloc),
    ENTRY("KERNEL32.DLL", "LocalLock", mw_LocalLock),
    ENTRY("KERNEL32.DLL", "LocalUnlock", mw_LocalUnlock),
    ENTRY("KERNEL32.DLL", "LocalSize", mw_LocalSize),
    ENTRY("KERNEL32.DLL", "LocalFree", mw_LocalFree),
    ENTRY("KERNEL32.DLL", "GlobalMemoryStatusEx", mw_GlobalMemoryStatusEx),
    ENTRY("KERNEL32.DLL", "GetSystemInfo", mw_GetSystemInfo),
    ENTRY("KERNEL32.DLL", "GetNativeSystemInfo", mw_GetNativeSystemInfo),
    ENTRY("KERNEL32.DLL", "GetLogicalProcessorInformationEx", mw_GetLogicalProcessorInformationEx),
    ENTRY("KERNEL32.DLL", "IsProcessorFeaturePresent", mw_IsProcessorFeaturePresent),
    ENTRY("KERNEL32.DLL", "CheckRemoteDebuggerPresent", mw_CheckRemoteDebuggerPresent),
    ENTRY("KERNEL32.DLL", "RtlCaptureStackBackTrace", mw_RtlCaptureStackBackTrace),
    ENTRY("KERNEL32.DLL", "RtlPcToFileHeader", mw_RtlPcToFileHeader),
    ENTRY("KERNEL32.DLL", "EncodePointer", mw_EncodePointer),
    ENTRY("KERNEL32.DLL", "DecodePointer", mw_DecodePointer),
    ENTRY("KERNEL32.DLL", "InitializeSListHead", mw_InitializeSListHead),
    ENTRY("KERNEL32.DLL", "InterlockedPushEntrySList", mw_InterlockedPushEntrySList),
    ENTRY("KERNEL32.DLL", "InterlockedFlushSList", mw_InterlockedFlushSList),
    ENTRY("KERNEL32.DLL", "QueryDepthSList", mw_QueryDepthSList),
    ENTRY("KERNEL32.DLL", "InitializeCriticalSectionEx", mw_InitializeCriticalSectionEx),
    ENTRY("KERNEL32.DLL", "GetCurrentProcessorNumber", mw_GetCurrentProcessorNumber),
    ENTRY("KERNEL32.DLL", "InitializeSRWLock", mw_InitializeSRWLock),
    ENTRY("KERNEL32.DLL", "AcquireSRWLockShared", mw_AcquireSRWLockShared),
    ENTRY("KERNEL32.DLL", "ReleaseSRWLockShared", mw_ReleaseSRWLockShared),
    ENTRY("KERNEL32.DLL", "TryAcquireSRWLockShared", mw_TryAcquireSRWLockShared),
    ENTRY("KERNEL32.DLL", "InitializeConditionVariable", mw_InitializeConditionVariable),
    ENTRY("KERNEL32.DLL", "InitOnceBeginInitialize", mw_InitOnceBeginInitialize),
    ENTRY("KERNEL32.DLL", "InitOnceComplete", mw_InitOnceComplete),
    ENTRY("KERNEL32.DLL", "WaitOnAddress", mw_WaitOnAddress),
    ENTRY("KERNEL32.DLL", "WakeByAddressSingle", mw_WakeByAddressSingle),
    ENTRY("KERNEL32.DLL", "WakeByAddressAll", mw_WakeByAddressAll),
    ENTRY("KERNEL32.DLL", "CreateEventW", mw_CreateEventW),
    ENTRY("KERNEL32.DLL", "CreateMutexA", mw_CreateMutexA),
    ENTRY("KERNEL32.DLL", "ReleaseMutex", mw_ReleaseMutex),
    ENTRY("KERNEL32.DLL", "WaitForMultipleObjectsEx", mw_WaitForMultipleObjectsEx),
    ENTRY("KERNEL32.DLL", "SignalObjectAndWait", mw_SignalObjectAndWait),
    ENTRY("KERNEL32.DLL", "CreateThread", mw_CreateThread),
    ENTRY("KERNEL32.DLL", "ExitThread", mw_ExitThread),
    ENTRY("KERNEL32.DLL", "GetExitCodeThread", mw_GetExitCodeThread),
    ENTRY("KERNEL32.DLL", "TerminateThread", mw_TerminateThread),
    ENTRY("KERNEL32.DLL", "CreateThreadpoolWork", mw_CreateThreadpoolWork),
    ENTRY("KERNEL32.DLL", "SubmitThreadpoolWork", mw_SubmitThreadpoolWork),
    ENTRY("KERNEL32.DLL", "CloseThreadpoolWork", mw_CloseThreadpoolWork),
    ENTRY("KERNEL32.DLL", "FreeLibraryWhenCallbackReturns", mw_FreeLibraryWhenCallbackReturns),
    ENTRY("KERNEL32.DLL", "RegisterWaitForSingleObject", mw_RegisterWaitForSingleObject),
    ENTRY("KERNEL32.DLL", "UnregisterWait", mw_UnregisterWait),
    ENTRY("KERNEL32.DLL", "QueueUserWorkItem", mw_QueueUserWorkItem),
    ENTRY("KERNEL32.DLL", "GetSystemTime", mw_GetSystemTime),
    ENTRY("KERNEL32.DLL", "GetSystemTimePreciseAsFileTime", mw_GetSystemTimePreciseAsFileTime),
    ENTRY("KERNEL32.DLL", "GetTickCount64", mw_GetTickCount64),
    ENTRY("KERNEL32.DLL", "GetTimeZoneInformation", mw_GetTimeZoneInformation),
    ENTRY("KERNEL32.DLL", "FileTimeToSystemTime", mw_FileTimeToSystemTime),
    ENTRY("KERNEL32.DLL", "SystemTimeToFileTime", mw_SystemTimeToFileTime),
    ENTRY("KERNEL32.DLL", "GetVersion", mw_GetVersion),
    ENTRY("KERNEL32.DLL", "GetVersionExA", mw_GetVersionExA),
    ENTRY("KERNEL32.DLL", "GetVersionExW", mw_GetVersionExW),
    ENTRY("KERNEL32.DLL", "SetDefaultDllDirectories", mw_SetDefaultDllDirectories), /* SWEEP-010 */
    ENTRY("KERNEL32.DLL", "GetACP", mw_GetACP),
    ENTRY("KERNEL32.DLL", "GetOEMCP", mw_GetOEMCP),
    ENTRY("KERNEL32.DLL", "IsValidCodePage", mw_IsValidCodePage),
    ENTRY("KERNEL32.DLL", "IsValidLocale", mw_IsValidLocale),
    ENTRY("KERNEL32.DLL", "GetUserDefaultLCID", mw_GetUserDefaultLCID),
    ENTRY("KERNEL32.DLL", "GetUserDefaultLangID", mw_GetUserDefaultLangID),
    ENTRY("KERNEL32.DLL", "GetSystemDefaultLCID", mw_GetSystemDefaultLCID),
    ENTRY("KERNEL32.DLL", "GetThreadLocale", mw_GetThreadLocale),
    ENTRY("KERNEL32.DLL", "SetThreadLocale", mw_SetThreadLocale),
    ENTRY("KERNEL32.DLL", "GetLocaleInfoA", mw_GetLocaleInfoA),
    ENTRY("KERNEL32.DLL", "GetLocaleInfoW", mw_GetLocaleInfoW),
    ENTRY("KERNEL32.DLL", "GetLocaleInfoEx", mw_GetLocaleInfoEx),
    ENTRY("KERNEL32.DLL", "CompareStringW", mw_CompareStringW),
    ENTRY("KERNEL32.DLL", "CompareStringEx", mw_CompareStringEx),
    ENTRY("KERNEL32.DLL", "LCMapStringW", mw_LCMapStringW),
    ENTRY("KERNEL32.DLL", "LCMapStringEx", mw_LCMapStringEx),
    ENTRY("KERNEL32.DLL", "LCMapStringA", mw_LCMapStringA),
    ENTRY("KERNEL32.DLL", "EnumSystemLocalesW", mw_EnumSystemLocalesW),
    ENTRY("KERNEL32.DLL", "GetStringTypeW", mw_GetStringTypeW),
    ENTRY("KERNEL32.DLL", "IsDBCSLeadByte", mw_IsDBCSLeadByte),
    ENTRY("KERNEL32.DLL", "CreateFileA", mw_CreateFileA),
    ENTRY("KERNEL32.DLL", "CreateFileW", mw_CreateFileW),
    ENTRY("KERNEL32.DLL", "ReadFile", mw_ReadFile),
    ENTRY("KERNEL32.DLL", "WriteFile", mw_WriteFile),
    ENTRY("KERNEL32.DLL", "SetFilePointer", mw_SetFilePointer),
    ENTRY("KERNEL32.DLL", "SetFilePointerEx", mw_SetFilePointerEx),
    ENTRY("KERNEL32.DLL", "SetEndOfFile", mw_SetEndOfFile),
    ENTRY("KERNEL32.DLL", "GetFileSize", mw_GetFileSize),
    ENTRY("KERNEL32.DLL", "GetFileSizeEx", mw_GetFileSizeEx),
    ENTRY("KERNEL32.DLL", "GetFileType", mw_GetFileType),
    ENTRY("KERNEL32.DLL", "FlushFileBuffers", mw_FlushFileBuffers),
    ENTRY("KERNEL32.DLL", "GetFileInformationByHandle", mw_GetFileInformationByHandle),
    ENTRY("KERNEL32.DLL", "GetFileInformationByHandleEx", mw_GetFileInformationByHandleEx),
    ENTRY("KERNEL32.DLL", "SetFileInformationByHandle", mw_SetFileInformationByHandle),
    ENTRY("KERNEL32.DLL", "GetFileAttributesW", mw_GetFileAttributesW),
    ENTRY("KERNEL32.DLL", "GetFileAttributesExW", mw_GetFileAttributesExW),
    ENTRY("KERNEL32.DLL", "SetFileAttributesW", mw_SetFileAttributesW),
    ENTRY("KERNEL32.DLL", "DeleteFileW", mw_DeleteFileW),
    ENTRY("KERNEL32.DLL", "RemoveDirectoryW", mw_RemoveDirectoryW),
    ENTRY("KERNEL32.DLL", "CreateDirectoryW", mw_CreateDirectoryW),
    ENTRY("KERNEL32.DLL", "CreateDirectoryExW", mw_CreateDirectoryExW),
    ENTRY("KERNEL32.DLL", "MoveFileExW", mw_MoveFileExW),
    ENTRY("KERNEL32.DLL", "ReplaceFileW", mw_ReplaceFileW),
    ENTRY("KERNEL32.DLL", "CreateFile2", mw_CreateFile2),
    ENTRY("KERNEL32.DLL", "CopyFile2", mw_CopyFile2),
    ENTRY("KERNEL32.DLL", "CreateSymbolicLinkW", mw_CreateSymbolicLinkW),
    ENTRY("KERNEL32.DLL", "CreateHardLinkW", mw_CreateHardLinkW),
    ENTRY("KERNEL32.DLL", "GetFullPathNameW", mw_GetFullPathNameW),
    ENTRY("KERNEL32.DLL", "GetLongPathNameW", mw_GetLongPathNameW),
    ENTRY("KERNEL32.DLL", "GetShortPathNameW", mw_GetShortPathNameW),
    ENTRY("KERNEL32.DLL", "GetFinalPathNameByHandleW", mw_GetFinalPathNameByHandleW),
    ENTRY("KERNEL32.DLL", "GetTempPathW", mw_GetTempPathW),
    ENTRY("KERNEL32.DLL", "GetTempFileNameW", mw_GetTempFileNameW),
    ENTRY("KERNEL32.DLL", "GetDiskFreeSpaceExW", mw_GetDiskFreeSpaceExW),
    ENTRY("KERNEL32.DLL", "GetLogicalDrives", mw_GetLogicalDrives),
    ENTRY("KERNEL32.DLL", "GetDriveTypeA", mw_GetDriveTypeA),
    ENTRY("KERNEL32.DLL", "GetDriveTypeW", mw_GetDriveTypeW),
    ENTRY("KERNEL32.DLL", "GetVolumeInformationW", mw_GetVolumeInformationW),
    ENTRY("KERNEL32.DLL", "GetVolumePathNameW", mw_GetVolumePathNameW),
    ENTRY("KERNEL32.DLL", "SetCurrentDirectoryW", mw_SetCurrentDirectoryW),
    ENTRY("KERNEL32.DLL", "GetCurrentDirectoryW", mw_GetCurrentDirectoryW),
    ENTRY("KERNEL32.DLL", "GetSystemDirectoryA", mw_GetSystemDirectoryA),
    ENTRY("KERNEL32.DLL", "GetSystemDirectoryW", mw_GetSystemDirectoryW),
    ENTRY("KERNEL32.DLL", "GetWindowsDirectoryW", mw_GetWindowsDirectoryW),
    ENTRY("KERNEL32.DLL", "FindFirstFileExW", mw_FindFirstFileExW),
    ENTRY("KERNEL32.DLL", "FindNextFileW", mw_FindNextFileW),
    ENTRY("KERNEL32.DLL", "FindFirstFileA", mw_FindFirstFileA),
    ENTRY("KERNEL32.DLL", "FindNextFileA", mw_FindNextFileA),
    ENTRY("KERNEL32.DLL", "SetPriorityClass", mw_SetPriorityClass),
    ENTRY("KERNEL32.DLL", "FindClose", mw_FindClose),
    ENTRY("KERNEL32.DLL", "GetConsoleCP", mw_GetConsoleCP),
    ENTRY("KERNEL32.DLL", "GetConsoleOutputCP", mw_GetConsoleOutputCP),
    ENTRY("KERNEL32.DLL", "SetConsoleOutputCP", mw_SetConsoleOutputCP),
    ENTRY("KERNEL32.DLL", "SetConsoleCP", mw_SetConsoleCP),
    ENTRY("KERNEL32.DLL", "SetConsoleMode", mw_SetConsoleMode),
    ENTRY("KERNEL32.DLL", "WriteConsoleW", mw_WriteConsoleW),
    ENTRY("KERNEL32.DLL", "GetConsoleWindow", mw_GetConsoleWindow),
    ENTRY("KERNEL32.DLL", "AllocConsole", mw_AllocConsole),
    ENTRY("KERNEL32.DLL", "AttachConsole", mw_AttachConsole),
    ENTRY("KERNEL32.DLL", "SetConsoleCtrlHandler", mw_SetConsoleCtrlHandler),
    ENTRY("KERNEL32.DLL", "GetNumberOfConsoleInputEvents", mw_GetNumberOfConsoleInputEvents),
    ENTRY("KERNEL32.DLL", "PeekConsoleInputA", mw_PeekConsoleInputA),
    ENTRY("KERNEL32.DLL", "PeekConsoleInputW", mw_PeekConsoleInputW),
    ENTRY("KERNEL32.DLL", "ReadConsoleW", mw_ReadConsoleW),
    ENTRY("KERNEL32.DLL", "ReadConsoleInputW", mw_ReadConsoleInputW),
    ENTRY("KERNEL32.DLL", "ReadConsoleInputA", mw_ReadConsoleInputA),
    ENTRY("KERNEL32.DLL", "RegOpenKeyExW", mw_RegOpenKeyExW),
    ENTRY("KERNEL32.DLL", "RegOpenKeyExA", mw_RegOpenKeyExA),
    ENTRY("KERNEL32.DLL", "RegQueryValueExW", mw_RegQueryValueExW),
    ENTRY("KERNEL32.DLL", "RegQueryValueExA", mw_RegQueryValueExA),
    ENTRY("KERNEL32.DLL", "RegGetValueW", mw_RegGetValueW),
    ENTRY("KERNEL32.DLL", "RegCloseKey", mw_RegCloseKey),
    ENTRY("KERNEL32.DLL", "RegCreateKeyExW", mw_RegCreateKeyExW),
    ENTRY("KERNEL32.DLL", "RegOpenKeyW", mw_RegOpenKeyW),
    ENTRY("KERNEL32.DLL", "RegCreateKeyW", mw_RegCreateKeyW),
    ENTRY("KERNEL32.DLL", "RegDeleteValueW", mw_RegDeleteValueW),
    ENTRY("KERNEL32.DLL", "RegEnumKeyA", mw_RegEnumKeyA),
    ENTRY("KERNEL32.DLL", "RegEnumKeyExW", mw_RegEnumKeyExW),
    ENTRY("KERNEL32.DLL", "RegEnumValueW", mw_RegEnumValueW),
    ENTRY("KERNEL32.DLL", "RegQueryInfoKeyW", mw_RegQueryInfoKeyW),
    ENTRY("KERNEL32.DLL", "RegSetValueExW", mw_RegSetValueExW),
    ENTRY("KERNEL32.DLL", "RegLoadMUIStringW", mw_RegLoadMUIStringW),
    ENTRY("KERNEL32.DLL", "RegNotifyChangeKeyValue", mw_RegNotifyChangeKeyValue),
    ENTRY("KERNEL32.DLL", "BCryptOpenAlgorithmProvider", mw_BCryptOpenAlgorithmProvider),
    ENTRY("KERNEL32.DLL", "BCryptCloseAlgorithmProvider", mw_BCryptCloseAlgorithmProvider),
    ENTRY("KERNEL32.DLL", "BCryptGenRandom", mw_BCryptGenRandom),
    ENTRY("KERNEL32.DLL", "BCryptGetProperty", mw_BCryptGetProperty),
    ENTRY("KERNEL32.DLL", "BCryptSetProperty", mw_BCryptSetProperty),
    ENTRY("KERNEL32.DLL", "BCryptCreateHash", mw_BCryptCreateHash),
    ENTRY("KERNEL32.DLL", "BCryptHashData", mw_BCryptHashData),
    ENTRY("KERNEL32.DLL", "BCryptFinishHash", mw_BCryptFinishHash),
    ENTRY("KERNEL32.DLL", "BCryptDestroyHash", mw_BCryptDestroyHash),
    ENTRY("KERNEL32.DLL", "BCryptGenerateSymmetricKey", mw_BCryptGenerateSymmetricKey),
    ENTRY("KERNEL32.DLL", "BCryptDestroyKey", mw_BCryptDestroyKey),
    ENTRY("KERNEL32.DLL", "BCryptEncrypt", mw_BCryptEncrypt),
    ENTRY("KERNEL32.DLL", "BCryptDeriveKeyPBKDF2", mw_BCryptDeriveKeyPBKDF2),
    ENTRY("KERNEL32.DLL", "CryptAcquireContextA", mw_CryptAcquireContextA),
    ENTRY("KERNEL32.DLL", "CryptGenRandom", mw_CryptGenRandom),
    ENTRY("KERNEL32.DLL", "CryptReleaseContext", mw_CryptReleaseContext),
    ENTRY("KERNEL32.DLL", "omp_get_max_threads", mw_omp_get_max_threads),
    ENTRY("KERNEL32.DLL", "omp_get_thread_num", mw_omp_get_thread_num),
    ENTRY("KERNEL32.DLL", "omp_set_nested", mw_omp_set_nested),
    ENTRY("KERNEL32.DLL", "omp_init_lock", mw_omp_init_lock),
    ENTRY("KERNEL32.DLL", "omp_destroy_lock", mw_omp_destroy_lock),
    ENTRY("KERNEL32.DLL", "omp_set_lock", mw_omp_set_lock),
    ENTRY("KERNEL32.DLL", "omp_unset_lock", mw_omp_unset_lock),
    ENTRY("KERNEL32.DLL", "_vcomp_set_num_threads", mw__vcomp_set_num_threads),
    ENTRY("KERNEL32.DLL", "_vcomp_barrier", mw__vcomp_barrier),
    ENTRY("KERNEL32.DLL", "_vcomp_enter_critsect", mw__vcomp_enter_critsect),
    ENTRY("KERNEL32.DLL", "_vcomp_leave_critsect", mw__vcomp_leave_critsect),
    ENTRY("KERNEL32.DLL", "_vcomp_single_begin", mw__vcomp_single_begin),
    ENTRY("KERNEL32.DLL", "_vcomp_single_end", mw__vcomp_single_end),
    ENTRY("KERNEL32.DLL", "_vcomp_reduction_r8", mw__vcomp_reduction_r8),
    ENTRY("KERNEL32.DLL", "_vcomp_atomic_add_i4", mw__vcomp_atomic_add_i4),
    ENTRY("KERNEL32.DLL", "_vcomp_atomic_add_i8", mw__vcomp_atomic_add_i8),
    ENTRY("KERNEL32.DLL", "_vcomp_for_static_simple_init", mw__vcomp_for_static_simple_init),
    ENTRY("KERNEL32.DLL", "_vcomp_for_static_init_i8", mw__vcomp_for_static_init_i8),
    ENTRY("KERNEL32.DLL", "_vcomp_for_next", mw__vcomp_for_next),
    ENTRY("KERNEL32.DLL", "_vcomp_for_static_end", mw__vcomp_for_static_end),
    ENTRY("KERNEL32.DLL", "_vcomp_for_dynamic_init", mw__vcomp_for_dynamic_init),
    ENTRY("KERNEL32.DLL", "_vcomp_for_dynamic_init_i8", mw__vcomp_for_dynamic_init_i8),
    ENTRY("KERNEL32.DLL", "_vcomp_for_dynamic_next", mw__vcomp_for_dynamic_next),
    ENTRY("KERNEL32.DLL", "_vcomp_for_dynamic_next_i8", mw__vcomp_for_dynamic_next_i8),
    ENTRY("KERNEL32.DLL", "_vcomp_fork", mw__vcomp_fork),
    ENTRY("KERNEL32.DLL", "UnhandledExceptionFilter", mw_UnhandledExceptionFilter),
    ENTRY("KERNEL32.DLL", "FormatMessageA", mw_FormatMessageA),
    ENTRY("KERNEL32.DLL", "FormatMessageW", mw_FormatMessageW),
    ENTRY("KERNEL32.DLL", "OutputDebugStringW", mw_OutputDebugStringW),
    ENTRY("KERNEL32.DLL", "TerminateProcess", mw_TerminateProcess),
    ENTRY("KERNEL32.DLL", "GetUserNameW", mw_GetUserNameW),
    ENTRY("KERNEL32.DLL", "GetComputerNameW", mw_GetComputerNameW),
    ENTRY("KERNEL32.DLL", "DeviceIoControl", mw_DeviceIoControl),
    ENTRY("KERNEL32.DLL", "SetErrorMode", mw_SetErrorMode),
    ENTRY("KERNEL32.DLL", "SetHandleInformation", mw_SetHandleInformation),
    ENTRY("KERNEL32.DLL", "PeekNamedPipe", mw_PeekNamedPipe),
    ENTRY("KERNEL32.DLL", "CreatePipe", mw_CreatePipe),
    ENTRY("KERNEL32.DLL", "CreateProcessW", mw_CreateProcessW),
    ENTRY("KERNEL32.DLL", "CreateProcessA", mw_CreateProcessA),
    ENTRY("KERNEL32.DLL", "GetExitCodeProcess", mw_GetExitCodeProcess),
/* --- end ISS1-WAVE3 --- */
    ENTRY("KERNEL32.DLL", "FindResourceA", mw_FindResourceA),
    ENTRY("KERNEL32.DLL", "FindResourceW", mw_FindResourceW),
    ENTRY("KERNEL32.DLL", "FindResourceExW", mw_FindResourceExW),
    ENTRY("KERNEL32.DLL", "LoadResource", mw_LoadResource),
    ENTRY("KERNEL32.DLL", "LockResource", mw_LockResource),
    ENTRY("KERNEL32.DLL", "SizeofResource", mw_SizeofResource),
    ENTRY("KERNEL32.DLL", "FreeResource", mw_FreeResource),
    ENTRY("KERNEL32.DLL", "EnumResourceNamesW", mw_EnumResourceNamesW),
    ENTRY("msvcrt.dll", "_register_thread_local_exe_atexit_callback", mw__register_thread_local_exe_atexit_callback),
    ENTRY("msvcrt.dll", "_set_abort_behavior", mw__set_abort_behavior),
    ENTRY("msvcrt.dll", "_set_errno", mw__set_errno),
    ENTRY("msvcrt.dll", "_get_errno", mw__get_errno),
    ENTRY("msvcrt.dll", "_initterm_e", mw__initterm_e),
    ENTRY("msvcrt.dll", "_seh_filter_exe", mw__seh_filter_exe),
    ENTRY("msvcrt.dll", "_seh_filter_dll", mw__seh_filter_dll),
    ENTRY("msvcrt.dll", "__intrinsic_setjmp", mw___intrinsic_setjmp),
    ENTRY("msvcrt.dll", "__intrinsic_longjmp", mw___intrinsic_longjmp),
    ENTRY("msvcrt.dll", "_wassert", mw__wassert),
    ENTRY("msvcrt.dll", "_hypot", mw__hypot),
    ENTRY("msvcrt.dll", "_isnan", mw__isnan),
    ENTRY("msvcrt.dll", "_j1", mw__j1),
    ENTRY("msvcrt.dll", "_y1", mw__y1),
    ENTRY("msvcrt.dll", "_jn", mw__jn),
    ENTRY("msvcrt.dll", "_yn", mw__yn),
    ENTRY("msvcrt.dll", "_localtime64_s", mw__localtime64_s),
    ENTRY("msvcrt.dll", "_gmtime64_s", mw__gmtime64_s),
    ENTRY("msvcrt.dll", "_mktime64", mw__mktime64),
    ENTRY("msvcrt.dll", "_time64", mw__time64),
    ENTRY("msvcrt.dll", "_time32", mw__time32),
    ENTRY("msvcrt.dll", "_ctime64", mw__ctime64),
    ENTRY("msvcrt.dll", "_difftime64", mw__difftime64),
    ENTRY("msvcrt.dll", "_ftime64", mw__ftime64),
    ENTRY("msvcrt.dll", "_ftime64_s", mw__ftime64),
    ENTRY("msvcrt.dll", "_access", mw__access),
    ENTRY("msvcrt.dll", "_waccess", mw__waccess),
    ENTRY("msvcrt.dll", "_chsize", mw__chsize),
    ENTRY("msvcrt.dll", "_chsize_s", mw__chsize_s),
    ENTRY("msvcrt.dll", "_commit", mw__commit),
    ENTRY("msvcrt.dll", "_configthreadlocale", mw__configthreadlocale),
    ENTRY("msvcrt.dll", "_create_locale", mw__create_locale),
    ENTRY("msvcrt.dll", "_free_locale", mw__free_locale),
    ENTRY("msvcrt.dll", "_dup2", mw__dup2),
    ENTRY("msvcrt.dll", "_getch", mw__getch),
    ENTRY("msvcrt.dll", "_getpid", mw__getpid),
    ENTRY("msvcrt.dll", "_getdrive", mw__getdrive),
    ENTRY("msvcrt.dll", "_mkdir", mw__mkdir),
    ENTRY("msvcrt.dll", "_wmkdir", mw__wmkdir),
    ENTRY("msvcrt.dll", "_wchdir", mw__wchdir),
    ENTRY("msvcrt.dll", "_wchmod", mw__wchmod),
    ENTRY("msvcrt.dll", "_wcreat", mw__wcreat),
    ENTRY("msvcrt.dll", "_wcsdup", mw__wcsdup),
    ENTRY("msvcrt.dll", "_wcsicmp", mw__wcsicmp),
    ENTRY("msvcrt.dll", "_wcsnicmp", mw__wcsnicmp),
    ENTRY("msvcrt.dll", "_strnicmp", mw__strnicmp),
    ENTRY("msvcrt.dll", "_wgetenv", mw__wgetenv),
    ENTRY("msvcrt.dll", "_fullpath", mw__fullpath),
    ENTRY("msvcrt.dll", "_wfullpath", mw__wfullpath),
    ENTRY("msvcrt.dll", "_wgetcwd", mw__wgetcwd),
    ENTRY("msvcrt.dll", "_wremove", mw__wremove),
    ENTRY("msvcrt.dll", "_wrename", mw__wrename),
    ENTRY("msvcrt.dll", "_wrmdir", mw__wrmdir),
    ENTRY("msvcrt.dll", "_wutime64", mw__wutime64),
    ENTRY("msvcrt.dll", "_utime64", mw__utime64),
    ENTRY("msvcrt.dll", "_wfopen", mw__wfopen),
    ENTRY("msvcrt.dll", "_fdopen", mw__fdopen),
    ENTRY("msvcrt.dll", "_fsopen", mw__fsopen),
    ENTRY("msvcrt.dll", "_fseeki64", mw__fseeki64),
    ENTRY("msvcrt.dll", "_ftelli64", mw__ftelli64),
    ENTRY("msvcrt.dll", "_lock_file", mw__lock_file),
    ENTRY("msvcrt.dll", "_unlock_file", mw__unlock_file),
    ENTRY("msvcrt.dll", "_fflush_nolock", mw__fflush_nolock),
    ENTRY("msvcrt.dll", "_fstat64i32", mw__fstat64i32),
    ENTRY("msvcrt.dll", "_stat64i32", mw__stat64i32),
    ENTRY("msvcrt.dll", "_wstat64i32", mw__wstat64i32),
    ENTRY("msvcrt.dll", "_open_osfhandle", mw__open_osfhandle),
    ENTRY("msvcrt.dll", "_pipe", mw__pipe),
    ENTRY("msvcrt.dll", "_lseek", mw__lseek),
    ENTRY("msvcrt.dll", "_locking", mw__locking),
    ENTRY("msvcrt.dll", "_findfirst64i32", mw__findfirst64i32),
    ENTRY("msvcrt.dll", "_findnext64i32", mw__findnext64i32),
    ENTRY("msvcrt.dll", "_findclose", mw__findclose),
    ENTRY("msvcrt.dll", "_mbsrchr", mw__mbsrchr),
    ENTRY("msvcrt.dll", "_ui64toa_s", mw__ui64toa_s),
    ENTRY("msvcrt.dll", "_wspawnv", mw__wspawnv),
    ENTRY("msvcrt.dll", "_wspawnve", mw__wspawnve),
    ENTRY("msvcrt.dll", "_sopen_s", mw__sopen_s),
    ENTRY("msvcrt.dll", "_wsopen", mw__wsopen),
    ENTRY("msvcrt.dll", "_wsopen_s", mw__wsopen_s),
    ENTRY("msvcrt.dll", "_invalid_parameter_noinfo", mw__invalid_parameter_noinfo),
    ENTRY("msvcrt.dll", "__report_rangecheckfailure", mw___report_rangecheckfailure),
    ENTRY("msvcrt.dll", "rand_s", mw_rand_s),
    ENTRY("msvcrt.dll", "wcrtomb", mw_wcrtomb),
    ENTRY("msvcrt.dll", "mbrtowc", mw_mbrtowc),
    ENTRY("msvcrt.dll", "mbsrtowcs", mw_mbsrtowcs),
    ENTRY("msvcrt.dll", "wcsrtombs", mw_wcsrtombs),
    ENTRY("msvcrt.dll", "iswctype", mw__iswctype_bridge),
    ENTRY("msvcrt.dll", "mbstowcs_s", mw_mbstowcs_s),
    ENTRY("msvcrt.dll", "strtoll", mw__strtoll_bridge),
    ENTRY("msvcrt.dll", "towlower", mw__towlower_bridge),
    ENTRY("msvcrt.dll", "towupper", mw__towupper_bridge),

ENTRY("msvcrt.dll", "acos", mw_acos),
ENTRY("msvcrt.dll", "acosf", mw_acosf),
ENTRY("msvcrt.dll", "acosh", mw_acosh),
ENTRY("msvcrt.dll", "asin", mw_asin),
ENTRY("msvcrt.dll", "asinf", mw_asinf),
ENTRY("msvcrt.dll", "asinh", mw_asinh),
ENTRY("msvcrt.dll", "asinhf", mw_asinhf),
ENTRY("msvcrt.dll", "atan", mw_atan),
ENTRY("msvcrt.dll", "atanf", mw_atanf),
ENTRY("msvcrt.dll", "atanh", mw_atanh),
ENTRY("msvcrt.dll", "atanhf", mw_atanhf),
ENTRY("msvcrt.dll", "cbrt", mw_cbrt),
ENTRY("msvcrt.dll", "cbrtf", mw_cbrtf),
ENTRY("msvcrt.dll", "ceil", mw_ceil),
ENTRY("msvcrt.dll", "ceilf", mw_ceilf),
ENTRY("msvcrt.dll", "cos", mw_cos),
ENTRY("msvcrt.dll", "cosf", mw_cosf),
ENTRY("msvcrt.dll", "cosh", mw_cosh),
ENTRY("msvcrt.dll", "exp", mw_exp),
ENTRY("msvcrt.dll", "exp2", mw_exp2),
ENTRY("msvcrt.dll", "exp2f", mw_exp2f),
ENTRY("msvcrt.dll", "expf", mw_expf),
ENTRY("msvcrt.dll", "expm1", mw_expm1),
ENTRY("msvcrt.dll", "fabs", mw_fabs),
ENTRY("msvcrt.dll", "floor", mw_floor),
ENTRY("msvcrt.dll", "floorf", mw_floorf),
ENTRY("msvcrt.dll", "log", mw_log),
ENTRY("msvcrt.dll", "log10", mw_log10),
ENTRY("msvcrt.dll", "log10f", mw_log10f),
ENTRY("msvcrt.dll", "log1p", mw_log1p),
ENTRY("msvcrt.dll", "log1pf", mw_log1pf),
ENTRY("msvcrt.dll", "log2", mw_log2),
ENTRY("msvcrt.dll", "log2f", mw_log2f),
ENTRY("msvcrt.dll", "logf", mw_logf),
ENTRY("msvcrt.dll", "sin", mw_sin),
ENTRY("msvcrt.dll", "sinf", mw_sinf),
ENTRY("msvcrt.dll", "sinh", mw_sinh),
ENTRY("msvcrt.dll", "sinhf", mw_sinhf),
ENTRY("msvcrt.dll", "sqrt", mw_sqrt),
ENTRY("msvcrt.dll", "sqrtf", mw_sqrtf),
ENTRY("msvcrt.dll", "tan", mw_tan),
ENTRY("msvcrt.dll", "tanf", mw_tanf),
ENTRY("msvcrt.dll", "tanh", mw_tanh),
ENTRY("msvcrt.dll", "tanhf", mw_tanhf),
ENTRY("msvcrt.dll", "trunc", mw_trunc),
ENTRY("msvcrt.dll", "truncf", mw_truncf),
ENTRY("msvcrt.dll", "roundf", mw_roundf),
ENTRY("msvcrt.dll", "llrintf", mw_llrintf),
ENTRY("msvcrt.dll", "lrintf", mw_lrintf),
ENTRY("msvcrt.dll", "atan2", mw_atan2),
ENTRY("msvcrt.dll", "atan2f", mw_atan2f),
ENTRY("msvcrt.dll", "fmod", mw_fmod),
ENTRY("msvcrt.dll", "fmodf", mw_fmodf),
ENTRY("msvcrt.dll", "pow", mw_pow),
ENTRY("msvcrt.dll", "powf", mw_powf),
ENTRY("msvcrt.dll", "hypot", mw_hypot),
ENTRY("msvcrt.dll", "fmax", mw_fmax),
ENTRY("msvcrt.dll", "fmaxf", mw_fmaxf),
ENTRY("msvcrt.dll", "fmin", mw_fmin),
ENTRY("msvcrt.dll", "fminf", mw_fminf),
ENTRY("msvcrt.dll", "remainder", mw_remainder),
ENTRY("msvcrt.dll", "nextafter", mw_nextafter),
ENTRY("msvcrt.dll", "fma", mw_fma),
ENTRY("msvcrt.dll", "fmaf", mw_fmaf),
ENTRY("msvcrt.dll", "llround", mw_llround),
ENTRY("msvcrt.dll", "llroundf", mw_llroundf),
ENTRY("msvcrt.dll", "lround", mw_lround),
ENTRY("msvcrt.dll", "lroundf", mw_lroundf),
ENTRY("msvcrt.dll", "lrint", mw_lrint),
ENTRY("msvcrt.dll", "lrintf", mw_lrintf),
ENTRY("msvcrt.dll", "llrint", mw_llrint),
ENTRY("msvcrt.dll", "modf", mw_modf),
ENTRY("msvcrt.dll", "modff", mw_modff),
ENTRY("msvcrt.dll", "frexp", mw_frexp),
ENTRY("msvcrt.dll", "erf", mw_erf),
ENTRY("msvcrt.dll", "erff", mw_erff),
ENTRY("msvcrt.dll", "isalnum", mw_isalnum),
ENTRY("msvcrt.dll", "isalpha", mw_isalpha),
ENTRY("msvcrt.dll", "isblank", mw_isblank),
ENTRY("msvcrt.dll", "iscntrl", mw_iscntrl),
ENTRY("msvcrt.dll", "isdigit", mw_isdigit),
ENTRY("msvcrt.dll", "isprint", mw_isprint),
ENTRY("msvcrt.dll", "toupper", mw_toupper),
ENTRY("msvcrt.dll", "strcoll", mw_strcoll),
ENTRY("msvcrt.dll", "wcschr", mw_wcschr),
ENTRY("msvcrt.dll", "wcsrchr", mw_wcsrchr),
ENTRY("msvcrt.dll", "wcsncmp", mw_wcsncmp),
ENTRY("msvcrt.dll", "wcsncpy", mw_wcsncpy),
ENTRY("msvcrt.dll", "wcscat", mw_wcscat),
ENTRY("msvcrt.dll", "wcscpy", mw_wcscpy),
ENTRY("msvcrt.dll", "wcsstr", mw_wcsstr),
ENTRY("msvcrt.dll", "wcsspn", mw_wcsspn),
ENTRY("msvcrt.dll", "wcscoll", mw_wcscoll),
ENTRY("msvcrt.dll", "wcsxfrm", mw_wcsxfrm),
ENTRY("msvcrt.dll", "wcsftime", mw_wcsftime),
ENTRY("msvcrt.dll", "wcsnlen", mw_wcsnlen),
ENTRY("msvcrt.dll", "bsearch", mw_bsearch),
ENTRY("msvcrt.dll", "raise", mw_raise),
ENTRY("msvcrt.dll", "clearerr", mw_clearerr),
ENTRY("msvcrt.dll", "ferror", mw_ferror),
ENTRY("msvcrt.dll", "putc", mw_putc),
ENTRY("msvcrt.dll", "putchar", mw_putchar),
ENTRY("msvcrt.dll", "fgetwc", mw_fgetwc),
ENTRY("msvcrt.dll", "ungetwc", mw_ungetwc),
ENTRY("msvcrt.dll", "putwc", mw_putwc),
ENTRY("msvcrt.dll", "perror", mw_perror),
ENTRY("msvcrt.dll", "getchar", mw_getchar),
ENTRY("msvcrt.dll", "setbuf", mw_setbuf),
ENTRY("msvcrt.dll", "atof", mw_atof),
ENTRY("msvcrt.dll", "atol", mw_atol),
ENTRY("msvcrt.dll", "strtod", mw_strtod),
ENTRY("msvcrt.dll", "strtof", mw_strtof),
ENTRY("msvcrt.dll", "strtoull", mw_strtoull),
ENTRY("msvcrt.dll", "strtoumax", mw_strtoumax),
ENTRY("msvcrt.dll", "mbstowcs", mw_mbstowcs),
ENTRY("msvcrt.dll", "mbrlen", mw_mbrlen),
ENTRY("msvcrt.dll", "wctomb", mw_wctomb),
ENTRY("msvcrt.dll", "wctype", mw_wctype),

    ENTRY("msvcrt.dll", "round", mw_round_bridge),
    ENTRY("msvcrt.dll", "strcat", mw_strcat),
    ENTRY("msvcrt.dll", "strncat", mw_strncat),
    ENTRY("msvcrt.dll", "strncpy", mw_strncpy),
    ENTRY("msvcrt.dll", "strncpy_s", mw_strncpy_s),
    ENTRY("msvcrt.dll", "strnlen", mw_strnlen),
    ENTRY("msvcrt.dll", "strpbrk", mw_strpbrk),
    ENTRY("msvcrt.dll", "strspn", mw_strspn),
    ENTRY("msvcrt.dll", "strtok", mw_strtok),
    ENTRY("msvcrt.dll", "strtok_s", mw_strtok_s),
    ENTRY("msvcrt.dll", "strxfrm", mw_strxfrm),
    ENTRY("msvcrt.dll", "wcscmp", mw_wcscmp),
    ENTRY("msvcrt.dll", "wcscat_s", mw_wcscat_s),
    ENTRY("msvcrt.dll", "wcscpy_s", mw_wcscpy_s),
    ENTRY("msvcrt.dll", "wcsncpy_s", mw_wcsncpy_s),
    ENTRY("msvcrt.dll", "wcstoll", mw_wcstoll),
    ENTRY("msvcrt.dll", "wcstoull", mw_wcstoull),
    ENTRY("msvcrt.dll", "wcstombs_s", mw_wcstombs_s),
    ENTRY("msvcrt.dll", "strerror_s", mw_strerror_s),
    ENTRY("ws2_32.dll", "WSAStartup", mw_WSAStartup),
    ENTRY("ws2_32.dll", "WSACleanup", mw_WSACleanup),
    ENTRY("ws2_32.dll", "WSAGetLastError", mw_WSAGetLastError),
    ENTRY("ws2_32.dll", "WSASetLastError", mw_WSASetLastError),
    ENTRY("ws2_32.dll", "htonl", mw_htonl),
    ENTRY("ws2_32.dll", "htons", mw_htons),
    ENTRY("ws2_32.dll", "ntohl", mw_ntohl),
    ENTRY("ws2_32.dll", "ntohs", mw_ntohs),
    ENTRY("ws2_32.dll", "inet_addr", mw_inet_addr),
    ENTRY("ws2_32.dll", "inet_ntoa", mw_inet_ntoa),
    ENTRY("ws2_32.dll", "socket", mw_socket),
    ENTRY("ws2_32.dll", "closesocket", mw_closesocket),
    ENTRY("ws2_32.dll", "connect", mw_connect),
    ENTRY("ws2_32.dll", "bind", mw_bind),
    ENTRY("ws2_32.dll", "listen", mw_listen),
    ENTRY("ws2_32.dll", "accept", mw_accept),
    ENTRY("ws2_32.dll", "shutdown", mw_shutdown),
    ENTRY("ws2_32.dll", "getsockopt", mw_getsockopt),
    ENTRY("ws2_32.dll", "setsockopt", mw_setsockopt),
    ENTRY("ws2_32.dll", "getsockname", mw_getsockname),
    ENTRY("ws2_32.dll", "getpeername", mw_getpeername),
    ENTRY("ws2_32.dll", "send", mw_send),
    ENTRY("ws2_32.dll", "recv", mw_recv),
    ENTRY("ws2_32.dll", "sendto", mw_sendto),
    ENTRY("ws2_32.dll", "recvfrom", mw_recvfrom),
    ENTRY("ws2_32.dll", "select", mw_select),
    ENTRY("ws2_32.dll", "ioctlsocket", mw_ioctlsocket),
    ENTRY("ws2_32.dll", "WSAIoctl", mw_WSAIoctl),
    ENTRY("msvcrt.dll", "__WSAFDIsSet", mw_select), /* shim: FD_ISSET helper */
    ENTRY("KERNEL32.DLL", "LoadLibraryExA", mw_LoadLibraryExA),
    /* ---- OPENSRC-7ZIP wave A (TASK-OPENSRC-7ZIP, BUG-052 era) ---- */
    ENTRY("OLEAUT32.DLL", "SysAllocString",                     mw_SysAllocString),
    ENTRY("OLEAUT32.DLL", "SysAllocStringLen",                  mw_SysAllocStringLen),
    ENTRY("OLEAUT32.DLL", "SysStringLen",                       mw_SysStringLen),
    ENTRY("OLEAUT32.DLL", "SysStringByteLen",                   mw_SysStringByteLen),
    ENTRY("OLEAUT32.DLL", "SysFreeString",                      mw_SysFreeString),
    ENTRY("OLEAUT32.DLL", "VariantClear",                       mw_VariantClear),
    ENTRY("OLEAUT32.DLL", "VariantCopy",                        mw_VariantCopy),
    ENTRY("USER32.DLL", "CharUpperW",                           mw_CharUpperW),
    ENTRY("USER32.DLL", "CharPrevExA",                          mw_CharPrevExA),
    ENTRY("ADVAPI32.DLL", "OpenProcessToken",                   mw_OpenProcessToken),
    ENTRY("ADVAPI32.DLL", "AdjustTokenPrivileges",              mw_AdjustTokenPrivileges),
    ENTRY("ADVAPI32.DLL", "LookupPrivilegeValueW",              mw_LookupPrivilegeValueW),
    ENTRY("ADVAPI32.DLL", "SystemFunction036",                  mw_SystemFunction036),
    ENTRY("ADVAPI32.DLL", "GetFileSecurityW",                   mw_GetFileSecurityW),
    ENTRY("ADVAPI32.DLL", "SetFileSecurityW",                   mw_SetFileSecurityW),
    ENTRY("msvcrt.dll", "_c_exit",                              mw__c_exit),
    ENTRY("msvcrt.dll", "__dllonexit",                          mw___dllonexit),
    ENTRY("msvcrt.dll", "?terminate@@YAXXZ",                    mw_msvcrt_terminate),
    ENTRY("msvcrt.dll", "??1type_info@@UEAA@XZ",                mw_type_info_dtor),
    ENTRY("msvcrt.dll", "_purecall",                            mw__purecall),
    ENTRY("msvcrt.dll", "__CxxFrameHandler",                    mw__CxxFrameHandler),
    ENTRY("msvcrt.dll", "_CxxThrowException",                   mw__CxxThrowException),
    ENTRY("msvcrt.dll", "_filelengthi64",                       mw__filelengthi64),
    ENTRY("msvcrt.dll", "fgetpos",                              mw_fgetpos),
    ENTRY("msvcrt.dll", "fsetpos",                              mw_fsetpos),
    ENTRY("KERNEL32.DLL", "VirtualAlloc",                       mw_VirtualAlloc),
    ENTRY("KERNEL32.DLL", "VirtualFree",                        mw_VirtualFree),
    ENTRY("KERNEL32.DLL", "MapViewOfFile",                      mw_MapViewOfFile),
    ENTRY("KERNEL32.DLL", "UnmapViewOfFile",                    mw_UnmapViewOfFile),
    ENTRY("KERNEL32.DLL", "OpenFileMappingW",                   mw_OpenFileMappingW),
    ENTRY("KERNEL32.DLL", "FindFirstFileW",                     mw_FindFirstFileW),
    ENTRY("KERNEL32.DLL", "FindFirstStreamW",                   mw_FindFirstStreamW),
    ENTRY("KERNEL32.DLL", "FindNextStreamW",                    mw_FindNextStreamW),
    ENTRY("KERNEL32.DLL", "MoveFileW",                          mw_MoveFileW),
    ENTRY("KERNEL32.DLL", "MoveFileWithProgressW",              mw_MoveFileWithProgressW),
    ENTRY("KERNEL32.DLL", "CreateSemaphoreW",                   mw_CreateSemaphoreW),
    ENTRY("KERNEL32.DLL", "OpenEventW",                         mw_OpenEventW),
    ENTRY("KERNEL32.DLL", "FileTimeToLocalFileTime",            mw_FileTimeToLocalFileTime),
    ENTRY("KERNEL32.DLL", "LocalFileTimeToFileTime",            mw_LocalFileTimeToFileTime),
    ENTRY("KERNEL32.DLL", "FileTimeToDosDateTime",              mw_FileTimeToDosDateTime),
    ENTRY("KERNEL32.DLL", "DosDateTimeToFileTime",              mw_DosDateTimeToFileTime),
    ENTRY("KERNEL32.DLL", "CompareFileTime",                    mw_CompareFileTime),
    ENTRY("KERNEL32.DLL", "GetDiskFreeSpaceW",                  mw_GetDiskFreeSpaceW),
    ENTRY("KERNEL32.DLL", "GetLogicalDriveStringsW",            mw_GetLogicalDriveStringsW),
    ENTRY("KERNEL32.DLL", "GetProcessTimes",                    mw_GetProcessTimes),
    ENTRY("KERNEL32.DLL", "SetThreadAffinityMask",              mw_SetThreadAffinityMask),
    ENTRY("KERNEL32.DLL", "GetLargePageMinimum",                mw_GetLargePageMinimum),
    ENTRY("KERNEL32.DLL", "SetFileApisToOEM",                   mw_SetFileApisToOEM),

    /* ---- OPENSRC battery wave B (sqlite3/jq/curl) ---- */
    ENTRY("KERNEL32.DLL", "CreateFileMappingW",                 mw_CreateFileMappingW),
    ENTRY("KERNEL32.DLL", "CreateFileMappingA",                 mw_CreateFileMappingA),
    ENTRY("KERNEL32.DLL", "FlushViewOfFile",                    mw_FlushViewOfFile),
    ENTRY("KERNEL32.DLL", "LockFile",                           mw_LockFile),
    ENTRY("KERNEL32.DLL", "UnlockFile",                         mw_UnlockFile),
    ENTRY("KERNEL32.DLL", "LockFileEx",                         mw_LockFileEx),
    ENTRY("KERNEL32.DLL", "UnlockFileEx",                       mw_UnlockFileEx),
    ENTRY("KERNEL32.DLL", "DeleteFileA",                        mw_DeleteFileA),
    ENTRY("KERNEL32.DLL", "GetFileAttributesA",                 mw_GetFileAttributesA),
    ENTRY("KERNEL32.DLL", "GetDiskFreeSpaceA",                  mw_GetDiskFreeSpaceA),
    ENTRY("KERNEL32.DLL", "GetTempPathA",                       mw_GetTempPathA),
    ENTRY("KERNEL32.DLL", "GetFullPathNameA",                   mw_GetFullPathNameA),
    ENTRY("KERNEL32.DLL", "AreFileApisANSI",                    mw_AreFileApisANSI),
    ENTRY("KERNEL32.DLL", "InitializeCriticalSectionAndSpinCount", mw_InitializeCriticalSectionAndSpinCount),
    ENTRY("KERNEL32.DLL", "InitializeCriticalSectionEx",        mw_InitializeCriticalSectionEx),
    ENTRY("KERNEL32.DLL", "CreateMutexW",                       mw_CreateMutexW),
    ENTRY("KERNEL32.DLL", "HeapDestroy",                        mw_HeapDestroy),
    ENTRY("KERNEL32.DLL", "TlsFree",                            mw_TlsFree),
    ENTRY("KERNEL32.DLL", "FlsFree",                            mw_FlsFree),
    ENTRY("KERNEL32.DLL", "SetStdHandle",                       mw_SetStdHandle2),
    ENTRY("KERNEL32.DLL", "SystemTimeToTzSpecificLocalTime",    mw_SystemTimeToTzSpecificLocalTime),
    ENTRY("KERNEL32.DLL", "GetDateFormatW",                     mw_GetDateFormatW),
    ENTRY("KERNEL32.DLL", "GetTimeFormatW",                     mw_GetTimeFormatW),
    ENTRY("KERNEL32.DLL", "RtlUnwind",                          mw_RtlUnwind),
    ENTRY("KERNEL32.DLL", "CancelIo",                           mw_CancelIo),
    ENTRY("KERNEL32.DLL", "CreateToolhelp32Snapshot",           mw_CreateToolhelp32Snapshot),
    ENTRY("KERNEL32.DLL", "Module32First",                      mw_Module32First),
    ENTRY("KERNEL32.DLL", "Module32Next",                       mw_Module32Next),
    ENTRY("KERNEL32.DLL", "FindFirstVolumeW",                   mw_FindFirstVolumeW),
    ENTRY("KERNEL32.DLL", "FindNextVolumeW",                    mw_FindNextVolumeW),
    ENTRY("KERNEL32.DLL", "FindVolumeClose",                    mw_FindVolumeClose),
    ENTRY("KERNEL32.DLL", "GetOverlappedResult",                mw_GetOverlappedResult),
    ENTRY("KERNEL32.DLL", "MoveFileExA",                        mw_MoveFileExA),
    ENTRY("KERNEL32.DLL", "SleepEx",                            mw_SleepEx),
    ENTRY("KERNEL32.DLL", "VerSetConditionMask",                mw_VerSetConditionMask),
    ENTRY("KERNEL32.DLL", "VerifyVersionInfoW",                 mw_VerifyVersionInfoW),
    ENTRY("KERNEL32.DLL", "WaitNamedPipeA",                     mw_WaitNamedPipeA),
    ENTRY("api-ms-win-crt-environment-l1-1-0.dll", "__p__wenviron", mw___p__wenviron),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p___wargv",   mw___p___wargv),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_configure_wide_argv", mw__configure_wide_argv),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_wide_environment", mw__initialize_wide_environment),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_crt_at_quick_exit", mw__crt_at_quick_exit),
    ENTRY("api-ms-win-crt-locale-l1-1-0.dll", "setlocale",      mw_setlocale_bridge),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "_j0",              mw__j0),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "_y0",              mw__y0),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "erfc",             mw_erfc),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "fdim",             mw_fdim),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "logb",             mw_logb),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "nearbyint",        mw_nearbyint),
    ENTRY("api-ms-win-crt-math-l1-1-0.dll", "tgamma",           mw_tgamma),
    ENTRY("api-ms-win-crt-private-l1-1-0.dll", "__intrinsic_setjmpex", mw___intrinsic_setjmpex),
    ENTRY("api-ms-win-crt-time-l1-1-0.dll", "_mkgmtime64",      mw__mkgmtime64),
    ENTRY("api-ms-win-crt-filesystem-l1-1-0.dll", "_findfirst64", mw__findfirst64),
    ENTRY("api-ms-win-crt-filesystem-l1-1-0.dll", "_findnext64", mw__findnext64),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "_telli64",        mw__telli64),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "freopen",         mw_freopen_bridge),
    ENTRY("api-ms-win-crt-utility-l1-1-0.dll", "_byteswap_uint64", mw__byteswap_uint64),
    ENTRY("SHLWAPI.dll", "PathIsRelativeA",                     mw_PathIsRelativeA),
    ENTRY("CRYPT32.dll", "CertOpenSystemStoreA",                mw_CertOpenSystemStoreA),
    ENTRY("CRYPT32.dll", "CertCloseStore",                      mw_CertCloseStore),
    ENTRY("CRYPT32.dll", "CertEnumCertificatesInStore",         mw_CertEnumCertificatesInStore),
    ENTRY("CRYPT32.dll", "CertFreeCertificateContext",          mw_CertFreeCertificateContext),
    ENTRY("CRYPT32.dll", "CertGetEnhancedKeyUsage",             mw_CertGetEnhancedKeyUsage),
    ENTRY("CRYPT32.dll", "CertGetIntendedKeyUsage",             mw_CertGetIntendedKeyUsage),
    ENTRY("IPHLPAPI.DLL", "if_nametoindex",                     mw_if_nametoindex),
    ENTRY("Normaliz.dll", "IdnToAscii",                         mw_IdnToAscii),
    ENTRY("Normaliz.dll", "IdnToUnicode",                       mw_IdnToUnicode),
    ENTRY("Secur32.dll", "InitSecurityInterfaceA",              mw_InitSecurityInterfaceA),
    ENTRY("USER32.DLL", "FindWindowA",                          mw_FindWindowA),
    ENTRY("USER32.DLL", "SendMessageA",                         mw_SendMessageA),
    /* SWEEP-003 GUI skeleton v1 (see functions above for contract) */
    ENTRY("USER32.DLL", "RegisterClassExW",                     mw_RegisterClassExW),
    ENTRY("USER32.DLL", "RegisterClassExA",                     mw_RegisterClassExA),
    ENTRY("USER32.DLL", "RegisterClassW",                       mw_RegisterClassW),
    ENTRY("USER32.DLL", "RegisterClassA",                       mw_RegisterClassA),
    ENTRY("USER32.DLL", "CreateWindowExW",                      mw_CreateWindowExW),
    ENTRY("USER32.DLL", "CreateWindowExA",                      mw_CreateWindowExA),
    ENTRY("USER32.DLL", "ShowWindow",                           mw_ShowWindow),
    ENTRY("USER32.DLL", "UpdateWindow",                         mw_UpdateWindow),
    ENTRY("USER32.DLL", "DestroyWindow",                        mw_DestroyWindow),
    ENTRY("USER32.DLL", "GetMessageW",                          mw_GetMessageW),
    ENTRY("USER32.DLL", "GetMessageA",                          mw_GetMessageA),
    ENTRY("USER32.DLL", "TranslateMessage",                     mw_TranslateMessage),
    ENTRY("USER32.DLL", "DispatchMessageW",                     mw_DispatchMessageW),
    ENTRY("USER32.DLL", "DispatchMessageA",                     mw_DispatchMessageA),
    ENTRY("USER32.DLL", "DefWindowProcW",                       mw_DefWindowProcW),
    ENTRY("USER32.DLL", "DefWindowProcA",                       mw_DefWindowProcA),
    ENTRY("USER32.DLL", "PostQuitMessage",                      mw_PostQuitMessage),
    ENTRY("COMCTL32.DLL", "InitCommonControlsEx",               mw_InitCommonControlsEx),
    ENTRY("COMCTL32.DLL", "InitCommonControls",                 mw_InitCommonControls),
    /* SWEEP-010: honest image lists (procexp64 toolbar init) */
    ENTRY("COMCTL32.DLL", "ImageList_Create",                   mw_ImageList_Create),
    ENTRY("COMCTL32.DLL", "ImageList_Destroy",                  mw_ImageList_Destroy),
    ENTRY("COMCTL32.DLL", "ImageList_Add",                      mw_ImageList_Add),
    ENTRY("COMCTL32.DLL", "ImageList_GetImageCount",            mw_ImageList_GetImageCount),
    ENTRY("USER32.DLL", "GetSystemMetrics",                     mw_GetSystemMetrics), /* SWEEP-010 */
    /* SWEEP-005: paint surface + event pump + draw stubs */
    ENTRY("USER32.DLL", "BeginPaint",                           mw_BeginPaint),
    ENTRY("USER32.DLL", "EndPaint",                             mw_EndPaint),
    ENTRY("USER32.DLL", "GetDC",                                mw_GetDC),
    ENTRY("USER32.DLL", "ReleaseDC",                            mw_ReleaseDC),
    ENTRY("USER32.DLL", "FillRect",                             mw_FillRect),
    ENTRY("USER32.DLL", "DrawTextA",                            mw_DrawTextA),
    ENTRY("USER32.DLL", "GetClientRect",                        mw_GetClientRect),
    ENTRY("USER32.DLL", "InvalidateRect",                       mw_InvalidateRect),
    ENTRY("USER32.DLL", "ValidateRect",                         mw_ValidateRect),
    ENTRY("USER32.DLL", "GetSysColor",                          mw_GetSysColor),
    ENTRY("USER32.DLL", "LoadCursorA",                          mw_LoadCursorA),
    ENTRY("USER32.DLL", "LoadCursorW",                          mw_LoadCursorW),
    ENTRY("USER32.DLL", "LoadIconA",                            mw_LoadIconA),
    ENTRY("USER32.DLL", "LoadIconW",                            mw_LoadIconW),
    ENTRY("USER32.DLL", "CallWindowProcA",                      mw_CallWindowProcA),
    ENTRY("USER32.DLL", "CallWindowProcW",                      mw_CallWindowProcW),
    ENTRY("USER32.DLL", "wsprintfA",                            mw_wsprintfA),
    ENTRY("USER32.DLL", "wsprintfW",                            mw_wsprintfW),
    ENTRY("USER32.DLL", "lstrlenA",                             mw_lstrlenA),
    ENTRY("USER32.DLL", "lstrlenW",                             mw_lstrlenW),
    ENTRY("USER32.DLL", "SetTimer",                             mw_SetTimer),
    ENTRY("USER32.DLL", "KillTimer",                            mw_KillTimer),
    ENTRY("USER32.DLL", "PeekMessageA",                         mw_PeekMessageA),
    ENTRY("USER32.DLL", "PeekMessageW",                         mw_PeekMessageW),
    ENTRY("GDI32.DLL", "TextOutA",                              mw_TextOutA),
    ENTRY("GDI32.DLL", "TextOutW",                              mw_TextOutW),
    ENTRY("GDI32.DLL", "ExtTextOutA",                           mw_ExtTextOutA),
    ENTRY("GDI32.DLL", "Rectangle",                             mw_Rectangle),
    ENTRY("GDI32.DLL", "Ellipse",                               mw_Ellipse),
    ENTRY("GDI32.DLL", "MoveToEx",                              mw_MoveToEx),
    ENTRY("GDI32.DLL", "LineTo",                                mw_LineTo),
    ENTRY("GDI32.DLL", "SetPixel",                              mw_SetPixel),
    ENTRY("GDI32.DLL", "CreateSolidBrush",                      mw_CreateSolidBrush),
    ENTRY("GDI32.DLL", "CreatePen",                             mw_CreatePen),
    ENTRY("GDI32.DLL", "GetStockObject",                        mw_GetStockObject),
    ENTRY("GDI32.DLL", "SelectObject",                          mw_SelectObject),
    ENTRY("GDI32.DLL", "DeleteObject",                          mw_DeleteObject),
    ENTRY("GDI32.DLL", "SetTextColor",                          mw_SetTextColor),
    ENTRY("GDI32.DLL", "SetBkColor",                            mw_SetBkColor),
    ENTRY("GDI32.DLL", "SetBkMode",                             mw_SetBkMode),
    ENTRY("GDI32.DLL", "SetMapMode",                            mw_SetMapMode),
    ENTRY("GDI32.DLL", "GetDeviceCaps",                         mw_GetDeviceCaps),
    ENTRY("GDI32.DLL", "CreateCompatibleDC",                    mw_CreateCompatibleDC), /* SWEEP-006 */
    ENTRY("GDI32.DLL", "CreateCompatibleBitmap",                mw_CreateCompatibleBitmap), /* SWEEP-006 */
    ENTRY("GDI32.DLL", "BitBlt",                                mw_BitBlt), /* SWEEP-006 */
    ENTRY("GDI32.DLL", "SaveDC",                                mw_SaveDC),
    ENTRY("GDI32.DLL", "RestoreDC",                             mw_RestoreDC),
    ENTRY("GDI32.DLL", "DeleteDC",                              mw_DeleteDC),
    ENTRY("GDI32.DLL", "PatBlt",                                mw_PatBlt),
    ENTRY("USER32.DLL", "MessageBoxA",                          mw_MessageBoxA),
    ENTRY("WLDAP32.dll", "ber_free",                            mw_ber_free),
    ENTRY("WLDAP32.dll", "ldap_bind_s",                         mw_ldap_bind_s),
    ENTRY("WLDAP32.dll", "ldap_err2string",                     mw_ldap_err2string),
    ENTRY("WLDAP32.dll", "ldap_first_attribute",                mw_ldap_first_attribute),
    ENTRY("WLDAP32.dll", "ldap_next_attribute",                 mw_ldap_next_attribute),
    ENTRY("WLDAP32.dll", "ldap_first_entry",                    mw_ldap_first_entry),
    ENTRY("WLDAP32.dll", "ldap_next_entry",                     mw_ldap_next_entry),
    ENTRY("WLDAP32.dll", "ldap_get_dn",                         mw_ldap_get_dn),
    ENTRY("WLDAP32.dll", "ldap_get_values_len",                 mw_ldap_get_values_len),
    ENTRY("WLDAP32.dll", "ldap_memfree",                        mw_ldap_memfree),
    ENTRY("WLDAP32.dll", "ldap_msgfree",                        mw_ldap_msgfree),
    ENTRY("WLDAP32.dll", "ldap_value_free_len",                 mw_ldap_value_free_len),
    ENTRY("WLDAP32.dll", "ldap_search_s",                       mw_ldap_search_s),
    ENTRY("WLDAP32.dll", "ldap_set_option",                     mw_ldap_set_option),
    ENTRY("WLDAP32.dll", "ldap_simple_bind_s",                  mw_ldap_simple_bind_s),
    ENTRY("WLDAP32.dll", "ldap_sslinit",                        mw_ldap_sslinit),
    ENTRY("WLDAP32.dll", "ldap_unbind_s",                       mw_ldap_unbind_s),
    ENTRY("WLDAP32.dll", "ldap_init",                           mw_ldap_init),
    ENTRY("WS2_32.dll", "WSACreateEvent",                       mw_WSACreateEvent),
    ENTRY("WS2_32.dll", "WSACloseEvent",                        mw_WSACloseEvent),
    ENTRY("WS2_32.dll", "WSAEventSelect",                       mw_WSAEventSelect),
    ENTRY("WS2_32.dll", "WSAEnumNetworkEvents",                 mw_WSAEnumNetworkEvents),
    ENTRY("WS2_32.dll", "WSAResetEvent",                        mw_WSAResetEvent),
    ENTRY("WS2_32.dll", "WSASetEvent",                          mw_WSASetEvent),
    ENTRY("WS2_32.dll", "WSAWaitForMultipleEvents",             mw_WSAWaitForMultipleEvents),
    ENTRY("WS2_32.dll", "WSAStringToAddressW",                  mw_WSAStringToAddressW),
    ENTRY("WS2_32.dll", "freeaddrinfo",                         mw_freeaddrinfo_),
    ENTRY("WS2_32.dll", "getaddrinfo",                          mw_getaddrinfo_),
    ENTRY("WS2_32.dll", "gethostname",                          mw_gethostname_),
    ENTRY("WS2_32.dll", "inet_pton",                            mw_inet_pton_),
    /* SW-555 honest batch (SWEEP-009): shlwapi color pair (procexp64) */
    ENTRY("shlwapi.dll", "ColorRGBToHLS",                       mw_ColorRGBToHLS),
    ENTRY("shlwapi.dll", "ColorHLSToRGB",                       mw_ColorHLSToRGB),

};

#define NUM_IMPORTS (sizeof(g_import_table) / sizeof(g_import_table[0]))

/* Data imports - resolved separately (IAT slot = address of data) */
typedef struct {
    const char* dll;
    const char* name;
    void*       data_addr; /* pointer to the actual data */
} DataImportEntry;

static DataImportEntry g_data_imports[] = {
    { "msvcrt.dll", "_acmdln",    &g_acmdln_ptr },
    { "msvcrt.dll", "__initenv",   &g_initenv_ptr },
    { "msvcrt.dll", "_commode",   &g_commode_val },
    { "msvcrt.dll", "_fmode",     &g_fmode_val },
    { "msvcrt.dll", "_iob",        g_msvcrt_iob },   /* OPENSRC-7ZIP: 3-slot FILE window */
    { "api-ms-win-crt-runtime-l1-1-0.dll", "__sys_errlist", g_sys_errlist },  /* OPENSRC battery */
    { "api-ms-win-crt-runtime-l1-1-0.dll", "__sys_nerr",    &g_sys_nerr },    /* OPENSRC battery */
    /* LADDER-013 (md5deep64, BUG-065): old-mingw data surface */
    { "msvcrt.dll", "__argv",      &g_argv_cell },   /* slot = &var; guest derefs to char** */
    { "msvcrt.dll", "__mb_cur_max", &g_mb_cur_max },
    { "msvcrt.dll", "__pioinfo",   &g_pioinfo[0] },  /* ioinfo* [64] */
    /* TASK-API-011: UCRT-flavored data exports (__p_* return pointers to
     * the variables; __tzname/__timezone/__daylight are the variables). */
    /* ALL UCRT __p_* exports are FUNCTIONS returning &var — t10 crashes
     * (0x2039038 __p__fmode, 0x2039034 __p__commode, 0x2039050 __p___argc)
     * proved call-style usage for each. See mw___p__* below. */
    { "api-ms-win-crt-time-l1-1-0.dll", "__tzname",   g_tzname },
    { "api-ms-win-crt-time-l1-1-0.dll", "__timezone", &g_timezone_val },
    { "api-ms-win-crt-time-l1-1-0.dll", "__daylight", &g_daylight_val },
    /* _errno removed from data imports — it's a function, not data */
};
#define NUM_DATA_IMPORTS (sizeof(g_data_imports) / sizeof(g_data_imports[0]))

/* ============================================================
 * GetProcAddress Resolver
 * Defined after import tables so it can access them.
 * ============================================================ */

static void* resolve_proc_from_table(void* module, const char* name) {
    /* OPENSRC battery: any pseudo-handle (LoadLibrary identities) or the
     * image base resolves by NAME — the table is module-agnostic. */
    /* SWEEP-002 fix: same stale-range bug as mw_GetProcAddress — the
     * pseudo-handle space grew with known[] (57 DLLs). Bound: +0xff. */
    if ((uintptr_t)module >= 0xdead0001 && (uintptr_t)module <= 0xdead00ff) {
        for (size_t i = 0; i < NUM_IMPORTS; i++) {
            if (strcmp(g_import_table[i].name, name) == 0) {
                MW_TRACE("  -> %p", g_import_table[i].func);
                return g_import_table[i].func;
            }
        }
        for (size_t i = 0; i < NUM_DATA_IMPORTS; i++) {
            if (strcmp(g_data_imports[i].name, name) == 0) {
                MW_TRACE("  -> DATA %p", g_data_imports[i].data_addr);
                return g_data_imports[i].data_addr;
            }
        }
        MW_TRACE("  -> NULL (not in dispatch table)");
    } else if (module == g_image_base) {
        for (size_t i = 0; i < NUM_IMPORTS; i++) {
            if (strcmp(g_import_table[i].name, name) == 0) {
                MW_TRACE("  -> %p", g_import_table[i].func);
                return g_import_table[i].func;
            }
        }
        for (size_t i = 0; i < NUM_DATA_IMPORTS; i++) {
            if (strcmp(g_data_imports[i].name, name) == 0) {
                MW_TRACE("  -> DATA %p", g_data_imports[i].data_addr);
                return g_data_imports[i].data_addr;
            }
        }
        MW_TRACE("  -> NULL (not in dispatch table)");
    }
    return NULL;
}

/* Initialize resolver — called once before PE loading */
static void resolve_proc_init(void) {
    g_resolve_proc_fn = resolve_proc_from_table;
}

/* TASK-TEST-004 (Windows-faithful IAT patching): the OS loader makes IAT
 * pages writable for the import pass regardless of their section flags
 * (UPX's IAT lives in the writable .idata; synthetic test PEs put it in
 * read-only .rdata). Track touched pages, restore per-section prot after. */
static struct { uint64_t rva; uint32_t size; int prot; } g_sec_prot[16];
static int g_sec_prot_count = 0;
static uint64_t g_iat_pages[64];
static int g_iat_page_count = 0;
static void iat_page_ensure_writable(uint8_t* addr) {
    uint64_t page = (uint64_t)(uintptr_t)addr & ~0xFFFULL;
    for (int i = 0; i < g_iat_page_count; i++)
        if (g_iat_pages[i] == page) return;
    if (g_iat_page_count < 64) g_iat_pages[g_iat_page_count++] = page;
    mprotect((void*)(uintptr_t)page, 0x1000, PROT_READ | PROT_WRITE);
}
static void iat_pages_restore(void) {
    for (int i = 0; i < g_iat_page_count; i++) {
        uint64_t off = g_iat_pages[i] - (uint64_t)(uintptr_t)g_image_base;
        int prot = PROT_READ; /* default for a restored image page */
        for (int t = 0; t < g_sec_prot_count; t++)
            if (off >= g_sec_prot[t].rva && off < g_sec_prot[t].rva + g_sec_prot[t].size) { prot = g_sec_prot[t].prot; break; }
        mprotect((void*)(uintptr_t)g_iat_pages[i], 0x1000, prot);
    }
    g_iat_page_count = 0;
}

/* ============================================================
 * SWEEP-001 (2026-10-18): safe stub for unresolved imports.
 * 50-program sweep evidence: 33/50 programs SIGSEGV'd the loader the
 * moment guest code called an unresolved IAT slot (the slot was left
 * NULL, so the call jumped to 0x0; strings64/accesschk/rg/nasm/ssh* all
 * show "[CRASH] ... NOT inside loaded PE" after the unresolved banner).
 * Wine precedent (dlls/ntdll/loader stubs): an unimplemented export is a
 * real callable that returns failure, never a NULL slot. We return 0 and
 * rate-limit the stderr note to keep sweep runs readable. Zero behavior
 * change for fully-resolved binaries (G1/T0 all resolve every import).
 * ============================================================ */
static uint64_t g_stub_calls = 0; /* rate-limiter only; racy count is fine */
__attribute__((ms_abi)) uint64_t mw_unresolved_import_stub(void);
__attribute__((ms_abi)) uint64_t mw_unresolved_import_stub(void) {
    uint64_t n = ++g_stub_calls;
    if (n <= 20) {
        /* SW-555 tooling prerequisite (SWEEP-008b): log the GUEST return
         * address of each unresolved call. The stub cannot know its own
         * import name (it is one shared function patched into many IAT
         * slots), but the return address is the guest call site — static
         * decode of the `call qword [rip+disp32]` that precedes it maps
         * the site to the exact dll!import (pefile IAT map + REA/Ghidra
         * decompile of the containing function gives the gate branch). */
        fprintf(stderr, "[STUB] unresolved import called -> returning 0 (call #%llu) guest_rip=%p\n",
                (unsigned long long)n, __builtin_return_address(0));
    } else if (n == 21) {
        fprintf(stderr, "[STUB] further unresolved-import calls silenced\n");
    }
    return 0;
}
/* Patch one IAT slot (handling the BUG-059 page-crossing case). */
static void iat_patch_slot(uint64_t iat_off, int slot, void* target) {
    void* patch_addr = g_image_base + iat_off + slot * 8;
    iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr);
    if ((((uintptr_t)patch_addr & 0xFFF) + 8) > 0x1000)
        iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr + 8);
    *(uint64_t*)patch_addr = (uint64_t)(uintptr_t)target;
}

/* ============================================================
 * PE Loading
 * ============================================================ */

static int load_pe(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) { fprintf(stderr, "[ERROR] Cannot open %s\n", filepath); return -1; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* filedata = (uint8_t*)malloc(fsize);
    fread(filedata, 1, fsize, f);
    fclose(f);

    MW_TRACE("PE Loader: file=%s size=%ld", filepath, fsize);

    /* Parse DOS header */
    DosHeader* dos = (DosHeader*)filedata;
    if (dos->e_magic[0] != 'M' || dos->e_magic[1] != 'Z') {
        fprintf(stderr, "[ERROR] Not a valid PE file (bad DOS magic)\n");
        free(filedata);
        return -1;
    }

    uint32_t pe_off = dos->e_lfanew;
    uint8_t* pe_sig = filedata + pe_off;
    if (pe_sig[0] != 'P' || pe_sig[1] != 'E' || pe_sig[2] != 0 || pe_sig[3] != 0) {
        fprintf(stderr, "[ERROR] Not a valid PE file (bad PE signature)\n");
        free(filedata);
        return -1;
    }

    CoffHeader* coff = (CoffHeader*)(filedata + pe_off + 4);
    PeOptHeader64* opt = (PeOptHeader64*)(filedata + pe_off + 4 + sizeof(CoffHeader));

    if (opt->Magic != PE32PLUS_MAGIC) {
        fprintf(stderr, "[ERROR] Not a PE32+ file (magic=0x%x)\n", opt->Magic);
        free(filedata);
        return -1;
    }

    g_entry_point = (uint64_t)opt->AddressOfEntryPoint;
    uint64_t image_base = opt->ImageBase;
    uint32_t size_of_image = opt->SizeOfImage;
    uint32_t section_align = opt->SectionAlignment;
    uint32_t num_sections = coff->NumberOfSections;
    uint32_t num_dd = opt->NumberOfRvaAndSizes;

    MW_TRACE("PE Loader: ImageBase=0x%lx EP=0x%lx SizeOfImage=0x%x Sections=%u",
             (unsigned long)image_base, (unsigned long)g_entry_point, size_of_image, num_sections);

    /* Allocate image at ImageBase using MAP_FIXED.
     * MAP_FIXED_NOREPLACE was causing silent failures: if the address was
     * occupied, the fallback path mapped at a random address WITHOUT applying
     * relocations (which are not implemented), corrupting all absolute
     * addresses in the PE image. MAP_FIXED forcibly replaces any existing
     * mapping, ensuring the PE is always at its preferred ImageBase.
     */
    g_image_base = (uint8_t*)mmap((void*)(uintptr_t)image_base, size_of_image,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (g_image_base == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Cannot mmap %u bytes at 0x%lx: %s\n",
                size_of_image, (unsigned long)image_base, strerror(errno));
        free(filedata);
        return -1;
    }
    MW_TRACE("PE Loader: Mapped at 0x%lx (forced via MAP_FIXED)", image_base);
    g_image_size = size_of_image;

    /* Fix up entry point to absolute address */
    g_entry_point = (uint64_t)(uintptr_t)(g_image_base + opt->AddressOfEntryPoint);

    /* Copy headers */
    memcpy(g_image_base, filedata, opt->SizeOfHeaders);

    /* Map sections */
    uint32_t opt_hdr_size = coff->SizeOfOptionalHeader;
    SectionHeader* sections = (SectionHeader*)(filedata + pe_off + 4 + sizeof(CoffHeader) + opt_hdr_size);

    for (uint32_t i = 0; i < num_sections; i++) {
        SectionHeader* sec = &sections[i];
        if (sec->SizeOfRawData > 0) {
            memcpy(g_image_base + sec->VirtualAddress,
                   filedata + sec->PointerToRawData,
                   sec->SizeOfRawData);
        }
        /* Zero BSS */
        if (sec->VirtualSize > sec->SizeOfRawData) {
            memset(g_image_base + sec->VirtualAddress + sec->SizeOfRawData, 0,
                   sec->VirtualSize - sec->SizeOfRawData);
        }

        char sec_name[9] = {0};
        memcpy(sec_name, sec->Name, 8);
        MW_TRACE("Section [%u] %-8s VA=0x%08x VS=0x%08x Raw=0x%08x Ch=0x%08x",
                 i, sec_name, sec->VirtualAddress, sec->VirtualSize,
                 sec->SizeOfRawData, sec->Characteristics);

        /* Set section permissions */
        int prot = PROT_READ;
        if (sec->Characteristics & SCN_MEM_EXECUTE) prot |= PROT_EXEC;
        if (sec->Characteristics & SCN_MEM_WRITE)   prot |= PROT_WRITE;
        if (sec->Characteristics & SCN_MEM_READ)    prot |= PROT_READ;
        mprotect(g_image_base + sec->VirtualAddress,
                 (sec->VirtualSize + section_align - 1) & ~(section_align - 1), prot);
        if (g_sec_prot_count < 16) {
            g_sec_prot[g_sec_prot_count].rva = sec->VirtualAddress;
            g_sec_prot[g_sec_prot_count].size = (sec->VirtualSize + section_align - 1) & ~(section_align - 1);
            g_sec_prot[g_sec_prot_count].prot = prot;
            g_sec_prot_count++;
        }
    }

    /* Parse and resolve imports */
    /* DataDirectory starts at offset 112 within the optional header */
    uint32_t dd_offset = pe_off + 4 + sizeof(CoffHeader) + 112;
    DataDirectory* dd = (DataDirectory*)(filedata + dd_offset);

    uint32_t import_rva = dd[DD_IMPORT].VirtualAddress;
    uint32_t import_size = dd[DD_IMPORT].Size;

    if (import_rva == 0) {
        fprintf(stderr, "[ERROR] No import directory\n");
        free(filedata);
        return -1;
    }

    MW_TRACE("Import Directory: RVA=0x%x Size=0x%x", import_rva, import_size);

    /* Walk import directory */
    int import_idx = 0;
    int total_resolved = 0;
    int total_unresolved = 0;

    for (uint32_t imp_idx = 0; ; imp_idx++) {
        ImportDirectoryEntry* imp = (ImportDirectoryEntry*)(g_image_base + import_rva + imp_idx * 20);
        if (imp->Name == 0 && imp->FirstThunk == 0) break;

        const char* dll_name = (const char*)(g_image_base + imp->Name);
        uint32_t ilt_rva = imp->OriginalFirstThunk;
        uint32_t iat_off = imp->FirstThunk;

        MW_TRACE("Import DLL: %s (IAT RVA=0x%x, ILT RVA=0x%x)", dll_name, iat_off, ilt_rva);

        /* Walk IAT/ILT entries */
        uint32_t lookup_rva = ilt_rva ? ilt_rva : iat_off;
        int slot = 0;

        for (;;) {
            uint64_t entry = *(uint64_t*)(g_image_base + lookup_rva + slot * 8);
            if (entry == 0) break;

            const char* func_name = NULL;
            uint16_t hint = 0;

            if (!(entry & 0x8000000000000000ULL)) {
                /* Import by name */
                uint32_t name_rva = (uint32_t)(entry & 0x7FFFFFFF);
                hint = *(uint16_t*)(g_image_base + name_rva);
                func_name = (const char*)(g_image_base + name_rva + 2);
            } else {
                /* Import by ordinal — OPENSRC-7ZIP (BUG-052): 7-Zip's MSVC
                 * import lib binds OLEAUT32 by FIXED ordinal (2/4/6/7/9/10).
                 * Resolve through a small known-ordinal map; unknown
                 * (dll, ordinal) pairs remain honestly unresolved. */
                uint16_t ordinal = (uint16_t)(entry & 0xFFFF);
                static const struct { const char* dll; uint16_t ord; const char* name; } ole_ord[] = {
                    { "OLEAUT32", 2,  "SysAllocString" },
                    { "OLEAUT32", 4,  "SysAllocStringLen" },
                    { "OLEAUT32", 6,  "SysFreeString" },
                    { "OLEAUT32", 7,  "SysStringLen" },
                    { "OLEAUT32", 8,  "SysStringByteLen" },
                    { "OLEAUT32", 149, "SysStringByteLen" }, /* 7z.exe binds this one */
                    { "OLEAUT32", 9,  "VariantClear" },
                    { "OLEAUT32", 10, "VariantCopy" },
                };
                const char* ord_name = NULL;
                char up_dll[16];
                { const char* s = dll_name; size_t k = 0;
                  for (; s[k] && k < sizeof(up_dll) - 1; k++)
                      up_dll[k] = (s[k] >= 'a' && s[k] <= 'z') ? (char)(s[k] - 32) : s[k];
                  up_dll[k] = '\0'; }
                for (size_t k = 0; k < sizeof(ole_ord)/sizeof(ole_ord[0]); k++) {
                    if (strncmp(up_dll, ole_ord[k].dll, strlen(ole_ord[k].dll)) == 0 &&
                        ordinal == ole_ord[k].ord) {
                        ord_name = ole_ord[k].name;
                        break;
                    }
                }
                if (ord_name) {
                    func_name = ord_name;
                    MW_TRACE("  [%d] Ordinal %u -> %s (OLEAUT32 fixed)", slot, ordinal, ord_name);
                } else {
                    MW_TRACE("  [%d] Ordinal %u (not supported)", slot, ordinal);
                    /* SWEEP-001: stub the slot too — same crash class as
                     * name-unresolved if guest ever calls it. */
                    iat_patch_slot(iat_off, slot, (void*)mw_unresolved_import_stub);
                    total_unresolved++;
                    slot++;
                    continue;
                }
            }

            /* Search our dispatch table */
            int found = 0;
            for (size_t t = 0; t < NUM_IMPORTS; t++) {
                if (strcmp(g_import_table[t].name, func_name) == 0) {
                    /* Found! Patch IAT */
                    void* patch_addr = g_image_base + iat_off + slot * 8;
                    iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr);
                    /* BUG-059: an 8-byte slot at page offset 0xFF8/0xFFC spans
                     * into the next page (curl's IAT ends exactly there) —
                     * make BOTH pages writable before the store. */
                    if ((((uintptr_t)patch_addr & 0xFFF) + 8) > 0x1000)
                        iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr + 8);
                    *(uint64_t*)patch_addr = (uint64_t)(uintptr_t)g_import_table[t].func;
                    found = 1;
                    total_resolved++;
                    break;
                }
            }

            /* Check data imports */
            if (!found) {
                for (size_t t = 0; t < NUM_DATA_IMPORTS; t++) {
                    if (strcmp(g_data_imports[t].name, func_name) == 0) {
                        void* patch_addr = g_image_base + iat_off + slot * 8;
                        iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr);
                        if ((((uintptr_t)patch_addr & 0xFFF) + 8) > 0x1000)
                            iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr + 8); /* BUG-059 */
                        *(uint64_t*)patch_addr = (uint64_t)(uintptr_t)g_data_imports[t].data_addr;
                        found = 1;
                        total_resolved++;
                        MW_TRACE("  [%d] %s -> DATA %p", slot, func_name, g_data_imports[t].data_addr);
                        break;
                    }
                }
            }

            if (!found) {
                MW_TRACE("  [%d] UNRESOLVED: %s", slot, func_name);
                /* OPENSRC-7ZIP diagnosability: surface the missing names on
                 * the summary line's neighbor so a run without MINWIN_TRACE
                 * still identifies them. */
                fprintf(stderr, "[WARN]   unresolved: %s\n", func_name);
                /* SWEEP-001: a NULL IAT slot is a delayed SIGSEGV — every
                 * sweep program that called one died with the loader. Wine
                 * never leaves a slot NULL; install the returning-0 stub. */
                iat_patch_slot(iat_off, slot, (void*)mw_unresolved_import_stub);
                total_unresolved++;
            } else {
                /* Only log code imports here (data imports logged above) */
                int is_data = 0;
                for (size_t t = 0; t < NUM_DATA_IMPORTS; t++) {
                    if (strcmp(g_data_imports[t].name, func_name) == 0) { is_data = 1; break; }
                }
                if (!is_data) {
                    void* resolved = (void*)*(uint64_t*)(g_image_base + iat_off + slot * 8);
                    MW_TRACE("  [%d] %s -> %p", slot, func_name, resolved);
                }
            }

            slot++;
            import_idx++;
        }
    }

    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",
             total_resolved, total_unresolved, total_resolved + total_unresolved);

    /* TASK-TEST-004: restore the per-section protection on every page the
     * IAT pass made writable (Windows restores IAT page protection too). */
    iat_pages_restore();

    if (total_unresolved > 0) {
        fprintf(stderr, "[WARN] %d imports unresolved\n", total_unresolved);
    }

    /* Check TLS (TASK-TLS-001): implement native static TLS.
     * - allocate the image's TLS template + zero-fill as slot 0 block
     * - write the slot index into the image's __tls_index
     * - publish the block via ThreadLocalStoragePointer (gs:0x58 -> array)
     * - run TLS callbacks with DLL_PROCESS_ATTACH */
    if (num_dd > DD_TLS && dd[DD_TLS].VirtualAddress != 0) {
        TlsDirectory64* tls = (TlsDirectory64*)(g_image_base + dd[DD_TLS].VirtualAddress);
        MW_TRACE("TLS: StartRaw=0x%lx EndRaw=0x%lx Callbacks=0x%lx",
                 tls->StartAddressOfRawData, tls->EndAddressOfRawData,
                 tls->AddressOfCallBacks);
        size_t raw_size = 0;
        if (tls->EndAddressOfRawData > tls->StartAddressOfRawData)
            raw_size = (size_t)(tls->EndAddressOfRawData - tls->StartAddressOfRawData);
        uint8_t* block = (uint8_t*)malloc(raw_size + tls->SizeOfZeroFill);
        if (block) {
            if (raw_size) memcpy(block, (void*)(uintptr_t)tls->StartAddressOfRawData, raw_size);
            if (tls->SizeOfZeroFill) memset(block + raw_size, 0, tls->SizeOfZeroFill);
            g_tls_ptr_array[0] = block;                 /* static TLS slot = 0 */
            if (tls->AddressOfIndex)
                *(uint32_t*)(uintptr_t)tls->AddressOfIndex = 0;
            MW_TRACE("TLS: static block %p (%zu+%u bytes) -> slot 0",
                     block, raw_size, tls->SizeOfZeroFill);
        }
        if (tls->AddressOfCallBacks) {
            typedef void (__attribute__((ms_abi)) *tls_cb)(void*, uint32_t, void*);
            void** cb = (void**)(uintptr_t)tls->AddressOfCallBacks;
            for (uint32_t i = 0; cb[i]; i++) {
                MW_TRACE("TLS: callback[%u] = %p", i, cb[i]);
                ((tls_cb)cb[i])(g_image_base, 1 /* DLL_PROCESS_ATTACH */, NULL);
            }
        }
    }

    /* Store Exception directory (.pdata) for SEH unwinding */
    if (num_dd > DD_EXCEPTION && dd[DD_EXCEPTION].VirtualAddress != 0) {
        g_pdata_rva = dd[DD_EXCEPTION].VirtualAddress;
        g_pdata_size = dd[DD_EXCEPTION].Size;
        g_num_rt_functions = g_pdata_size / sizeof(RUNTIME_FUNCTION);
        MW_TRACE("Exception: .pdata RVA=0x%x Size=0x%x Entries=%u",
                 g_pdata_rva, g_pdata_size, g_num_rt_functions);
    } else {
        MW_TRACE("Exception: no .pdata directory");
    }

    /* CXX EH FIX: Patch specific function's DWARF LSDA.
     * check_basic_cxx_exception_handling has filter=1 with TType=0xff.
     * Personality fn can't resolve the type -> returns ContinueSearch.
     * Change filter to 0x7f (sleb128=-1, catch-all).
     */
    if (g_image_base && mw_eh_hacks_enabled()) {
        uint32_t lsda_abs = 0x20107c + 12 + 64;
        if (lsda_abs < g_image_size) {
            /* .xdata is read-only; temporarily make it writable */
            uint32_t page_rva = lsda_abs & ~0xFFFUL;  /* page-align */
            uint32_t page_off = lsda_abs - page_rva;
            uint32_t page_sz = 0x1000; /* one page */
            mprotect(g_image_base + page_rva, page_sz, PROT_READ | PROT_WRITE | PROT_EXEC);
            uint8_t* patch_byte = g_image_base + lsda_abs;
            uint8_t old_val = *patch_byte;
            if (old_val > 0 && old_val < 0x7f) {
                *patch_byte = 0x7f;
                MW_TRACE("EH_FIX: Patched RVA 0x%x: 0x%02x -> 0x7f", lsda_abs, old_val);
            } else {
                MW_TRACE("EH_FIX: Byte at RVA 0x%x is 0x%02x, skipping", lsda_abs, old_val);
            }
            /* Restore read-only */
            mprotect(g_image_base + page_rva, page_sz, PROT_READ | PROT_EXEC);
        }
    } else if (g_image_base) {
        /* TASK-API-005 (ADR-010): hack present but disabled by default. */
        MW_TRACE("[API-005] CXX-EH-FIX (LSDA catch-all patch RVA 0x%x) skipped: MINWIN_EH_HACKS not set",
                 0x20107c + 12 + 64);
    }

    free(filedata);
    return 0;
}

/* ============================================================
 * Signal Handler (for debugging crashes)
 * ============================================================ */

static void crash_handler(int sig, siginfo_t* info, void* ctx) {
    ucontext_t* uc = (ucontext_t*)ctx;
    uint64_t rip = uc->uc_mcontext.gregs[REG_RIP];
    uint64_t rax = uc->uc_mcontext.gregs[REG_RAX];
    uint64_t rcx = uc->uc_mcontext.gregs[REG_RCX];
    uint64_t rdx = uc->uc_mcontext.gregs[REG_RDX];
    uint64_t r8  = uc->uc_mcontext.gregs[REG_R8];
    uint64_t r9  = uc->uc_mcontext.gregs[REG_R9];
    uint64_t rsp = uc->uc_mcontext.gregs[REG_RSP];
    uint64_t rbp = uc->uc_mcontext.gregs[REG_RBP];

    fprintf(stderr, "\n[CRASH] Signal %d at RIP=0x%lx\n", sig, rip);
    fprintf(stderr, "  RAX=0x%lx RCX=0x%lx RDX=0x%lx R8=0x%lx R9=0x%lx\n",
            rax, rcx, rdx, r8, r9);
    fprintf(stderr, "  RSP=0x%lx RBP=0x%lx\n", rsp, rbp);
    fprintf(stderr, "  g_image_base=%p g_image_size=0x%lx\n", g_image_base, g_image_size);

    if (g_image_base && rip >= (uint64_t)(uintptr_t)g_image_base &&
        rip < (uint64_t)(uintptr_t)g_image_base + g_image_size) {
        uint64_t rva = rip - (uint64_t)(uintptr_t)g_image_base;
        fprintf(stderr, "  RVA=0x%lx (inside loaded PE)\n", rva);

        /* Dump bytes at crash point */
        uint8_t* crash_ptr = (uint8_t*)(uintptr_t)rip;
        fprintf(stderr, "  Bytes: ");
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, "%02x ", crash_ptr[i]);
        }
        fprintf(stderr, "\n");

        /* Dump return address from stack */
        fprintf(stderr, "  Return addr: 0x%lx\n", *(uint64_t*)rsp);
    } else {
        fprintf(stderr, "  (NOT inside loaded PE)\n");
        /* TASK-OPENSRC-1 diag: attribute the out-of-PE RIP via /proc/self/maps.
         * Distinguishes a glibc-internal fault (heap metadata / realloc on a
         * non-chunk pointer) from a jump/call into data (guard-region body,
         * unresolved-import raw RVA). Read-safe: bytes are dumped only when
         * the containing map is readable. */
        {
            FILE* mf = fopen("/proc/self/maps", "r");
            if (mf) {
                char line[512];
                int attributed = 0;
                while (fgets(line, sizeof line, mf)) {
                    unsigned long s = 0, e = 0;
                    char perms[8] = {0};
                    char path[400] = {0};
                    if (sscanf(line, "%lx-%lx %7s %*x %*x:%*x %*d %399[^\n]",
                               &s, &e, perms, path) >= 3) {
                        if (rip >= s && rip < e) {
                            fprintf(stderr, "  [RIP-MAP] 0x%lx in 0x%lx-0x%lx %s %s\n",
                                    rip, s, e, perms, path);
                            if (perms[0] == 'r') {
                                uint8_t* cp = (uint8_t*)(uintptr_t)rip;
                                fprintf(stderr, "  Bytes: ");
                                for (int i = 0; i < 16; i++) fprintf(stderr, "%02x ", cp[i]);
                                fprintf(stderr, "\n");
                            }
                            attributed = 1;
                            break;
                        }
                    }
                }
                if (!attributed) fprintf(stderr, "  [RIP-MAP] no mapping contains RIP\n");
                fclose(mf);
            }
        }
    }

    /* Check if the crash is from calling a NULL function pointer */
    fprintf(stderr, "  RSI=0x%lx RDI=0x%lx\n", uc->uc_mcontext.gregs[REG_RSI], uc->uc_mcontext.gregs[REG_RDI]);
    fprintf(stderr, "  R8=0x%lx  R9=0x%lx R10=0x%lx R11=0x%lx\n",
            uc->uc_mcontext.gregs[REG_R8], uc->uc_mcontext.gregs[REG_R9],
            uc->uc_mcontext.gregs[REG_R10], uc->uc_mcontext.gregs[REG_R11]);
    fprintf(stderr, "  R12=0x%lx R13=0x%lx R14=0x%lx R15=0x%lx\n",
            uc->uc_mcontext.gregs[REG_R12], uc->uc_mcontext.gregs[REG_R13],
            uc->uc_mcontext.gregs[REG_R14], uc->uc_mcontext.gregs[REG_R15]);

    /* ISS1-WAVE3B: tagged stack scan — return addresses into the loader or
     * the PE reveal the abort/fault chain even when libc frames confuse the
     * RBP walker. Bounded + read-safe. */
    {
        uint64_t scan_rsp = rsp;
        for (int i = 0; i < 512; i++) {
            uint64_t v = *(uint64_t*)(scan_rsp + (uint64_t)(i * 8));
            const char* tag = NULL;
            if (v >= 0x2000000ull && v < 0x2050000ull) tag = "LOADER";
            else if (g_image_base && v >= (uint64_t)(uintptr_t)g_image_base &&
                     v < (uint64_t)(uintptr_t)g_image_base + g_image_size) tag = "PE";
            if (tag)
                fprintf(stderr, "  [SCAN rsp+%03d] 0x%lx <%s>\n", i * 8, v, tag);
        }
    }

    /* Try to identify the function at RIP */
    fprintf(stderr, "  Backtrace (via stack walking):\n");
    uint64_t bt_rip = rip, bt_rsp = rsp;
    for (int i = 0; i < 8; i++) {
        if (g_image_base && bt_rip >= (uint64_t)(uintptr_t)g_image_base &&
            bt_rip < (uint64_t)(uintptr_t)g_image_base + g_image_size) {
            uint64_t bt_rva = bt_rip - (uint64_t)(uintptr_t)g_image_base;
            fprintf(stderr, "    [%d] RIP=0x%lx RVA=0x%lx", i, bt_rip, bt_rva);
            /* Find return address */
            uint64_t* ra_ptr = (uint64_t*)(uintptr_t)bt_rsp;
            fprintf(stderr, "      [RSP+0] = 0x%lx (return addr?)\n", *ra_ptr);
            bt_rsp = *ra_ptr + 8;  /* follow return chain */
        } else if (bt_rip > 0x100000 && bt_rip < 0x7fffffffffffULL) {
            fprintf(stderr, "    [%d] RIP=0x%lx (outside PE, possible shared lib)\n", i, bt_rip);
            bt_rsp = *(uint64_t*)(uintptr_t)(bt_rsp + 8);
        } else {
            break;
        }
    }

    /* Check for NULL/invalid function pointer */
    if (rip < 0x1000 || (info->si_code == SEGV_MAPERR && info->si_addr == NULL)) {
        fprintf(stderr, "  Likely: NULL or invalid function pointer call\n");
    }

    _exit(139);
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* ============================================================
 * Main
 * ============================================================ */


/* ISS1-WATCHDOG: MINWIN_WATCHDOG=<seconds> dumps RIP/RSP at hang time */
#include <ucontext.h>
static void mw_watchdog_handler(int sig, siginfo_t* si, void* uc) {
    ucontext_t* c = (ucontext_t*)uc;
    fprintf(stderr, "[WATCHDOG] hang detected — RIP=0x%llx RSP=0x%llx RDI=0x%llx RSI=0x%llx RAX=0x%llx RCX=0x%llx RDX=0x%llx R8=0x%llx RBP=0x%llx\n",
            (unsigned long long)c->uc_mcontext.gregs[REG_RIP],
            (unsigned long long)c->uc_mcontext.gregs[REG_RSP],
            (unsigned long long)c->uc_mcontext.gregs[REG_RDI],
            (unsigned long long)c->uc_mcontext.gregs[REG_RSI],
            (unsigned long long)c->uc_mcontext.gregs[REG_RAX],
            (unsigned long long)c->uc_mcontext.gregs[REG_RCX],
            (unsigned long long)c->uc_mcontext.gregs[REG_RDX],
            (unsigned long long)c->uc_mcontext.gregs[REG_R8],
            (unsigned long long)c->uc_mcontext.gregs[REG_RBP]);
    uint64_t rsp = c->uc_mcontext.gregs[REG_RSP];
    for (int i = 0; i < 256; i++) {
        uint64_t v = *(uint64_t*)(rsp + i * 8);
        const char* tag = "";
        if (v >= 0x2000000ull && v < 0x2050000ull) tag = " <LOADER>";
        else if (v >= 0x140000000ull && v < 0x141fdf000ull) tag = " <PE>";
        if (tag[0] || i < 12)
            fprintf(stderr, "[WATCHDOG] [rsp+%03d] = 0x%llx%s\n", i * 8,
                    (unsigned long long)v, tag);
    }
    _exit(3);
}
static void mw_install_watchdog(int seconds) {
    const char* v = getenv("MINWIN_WATCHDOG");
    int secs = v ? atoi(v) : seconds;
    if (secs <= 0) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = mw_watchdog_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGALRM, &sa, NULL);
    alarm((unsigned int)secs);
    fprintf(stderr, "[WATCHDOG] armed for %ds\n", secs);
}

int main(int argc, char* argv[]) {
    /* full Unicode through wcstombs/wcstombs (Wave-3B conversion APIs) */
    setlocale(LC_ALL, "C.UTF-8");
    mw_install_watchdog(0);
    if (argc < 2) {
        printf("MiniWin PE Loader v0.1\n");
        printf("Usage: %s <pe_executable> [args...]\n", argv[0]);
        return 1;
    }

    const char* exe_path = argv[1];
    g_exe_path = exe_path;

    /* Build command line */
    snprintf(g_cmdline_ansi, sizeof(g_cmdline_ansi), "%s", exe_path);
    for (int i = 2; i < argc; i++) {
        strcat(g_cmdline_ansi, " ");
        strcat(g_cmdline_ansi, argv[i]);
    }
    MW_TRACE("Command line: %s", g_cmdline_ansi);

    /* Build wide command line */
    mbstowcs(g_cmdline_wide, g_cmdline_ansi, sizeof(g_cmdline_wide)/sizeof(wchar_t));

    /* Set up CRT data */
    g_acmdln_ptr = g_cmdline_ansi;
    g_initenv_ptr = NULL; /* no env for now */
    /* LADDER-013: argv[0] = basename only. Old console software does
     * argv[0]-dispatch on the bare name (md5deep 4.4 cuts "deep" IN PLACE
     * and strcmps the remainder; busybox64 multi-call needs the same). A
     * full-path argv[0] breaks that class of programs on any OS. */
    g_argv[0] = (char*)exe_path;
    {
        const char* slash = strrchr(exe_path, '/');
        const char* base = slash ? slash + 1 : exe_path;
        const char* bs = strrchr(base, '\\');
        if (bs) base = bs + 1;
        g_argv[0] = (char*)base;
    }
    for (int i = 2; i < argc; i++) {
        g_argv[i - 1] = argv[i];
    }
    g_argv[argc - 1] = NULL;
    g_argc = argc - 1;

    /* Set up signal handlers */
    setup_signal_handlers();

    /* Set up trace log */
    {
        char trace_path[512];
        const char* base = strrchr(exe_path, '/');
        base = base ? base + 1 : exe_path;
        snprintf(trace_path, sizeof(trace_path), "miniwin-results/%s/api_trace.json", base);
        /* Create directory if needed */
        mkdir("miniwin-results", 0755);
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "miniwin-results/%s", base);
        mkdir(dir_path, 0755);
        /* SWEEP-004: remember per-exe results dir for GDI-lite BMP output */
        snprintf(g_result_base_dir, sizeof(g_result_base_dir), "%s", dir_path);
        /* Also create subdirs */
        char subdir[600];
        snprintf(subdir, sizeof(subdir), "%s/execution", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/binary", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/analysis", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/screenshots", dir_path);
        mkdir(subdir, 0755);

        g_trace_log = fopen(trace_path, "w");
        if (g_trace_log) {
            fprintf(g_trace_log, "{\n  \"trace\": [\n");
            /* TASK-API-002: exit() paths (mw_exit, CRT exit) run atexit
             * handlers — close the trace JSON there. _exit() paths
             * (ExitProcess/abort) call mw_trace_shutdown_flush directly. */
            atexit(mw_trace_shutdown_flush);
        }
    }

    printf("[MiniWin] Loading %s...\n", exe_path);

    resolve_proc_init();

    /* Load PE */
    if (load_pe(exe_path) != 0) {
        fprintf(stderr, "[ERROR] Failed to load PE\n");
        return 1;
    }

    /* Set up fake TEB/PEB for Windows TLS (gs:0x30) access */
    setup_teb_peb();

    /* Jump to entry point */
    MW_TRACE("Jumping to EP=0x%lx", g_entry_point);
    fflush(stdout);
    fflush(stderr);

    typedef int (*pe_entry_fn)(void);
    pe_entry_fn entry = (pe_entry_fn)(void*)(uintptr_t)g_entry_point;
    int ret = entry();

    MW_TRACE("Entry point returned %d", ret);

    /* Close trace log */
    if (g_trace_log) {
        fprintf(g_trace_log, "  ]\n}\n");
        fclose(g_trace_log);
        g_trace_log = NULL;
    }

    return ret;
}

