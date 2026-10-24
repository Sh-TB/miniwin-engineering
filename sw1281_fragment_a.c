/* ============================================================
 * SW-1281 (SWEEP-060 Train 1) «چند ده قطار»: PE32 (x86 / 0x10b)
 * subsystem. Evidence (artifacts/sweep059/guest_inventory.tsv):
 * 57/108 battery guests blocked EXACTLY at the 0x10b rejection
 * (loader.c:36302 + LoadLibrary:1230) — winamp295, 3D Pinball,
 * every NirSoft x86 build, Sysinternals x86 twins.
 *
 * Architecture (honest v1, measured):
 *   1. LOADER PLANE — load_pe32(): PE32 opt header (ImageBase u32
 *      @oh+28, DataDir @oh+96, NumberOfRvaAndSizes @oh+92), section
 *      map, base relocations (HIGHLOW type-1 only; others counted),
 *      import binding against the SAME name-only dispatch table but
 *      IAT slots = synthesized x86 API THUNKS (low-4GB code page,
 *      `mov eax,slot; jmp gate`), export dir recorded for
 *      GetProcAddress.
 *   2. CPU CORE — i386 interpreter (x86 code cannot execute natively
 *      in this x64 process): thread-local MwCpu32, flat memory
 *      (image mapped in-process at ImageBase), full integer core +
 *      x87 stack subset + SSE2 scalar subset; unsupported opcodes
 *      FAIL LOUD with decode context (repo discipline: honest).
 *   3. API GATE — EIP == gate address: pop stdcall ret addr, widen
 *      up to 16 dword args, call the host ms_abi mw_* through a
 *      16-arg function-pointer cast (extra args ABI-ignored by the
 *      callee), EAX = return, `esp += arity_bytes` (stdcall callee
 *      pops; annotation table for the hot set, DEFAULT 32 bytes +
 *      one-time log; guest ebp-frame prologues resync esp, so a
 *      wrong default self-heals at the next function boundary).
 *   4. LOW-4GB WORLD — x86 pointers must stay < 4GB: arena32 mmap
 *      MAP_FIXED 0x10000000..0x3FFFFFFF serves TEB32/PEB32/stack/
 *      VirtualAlloc/HeapAlloc in x86 mode (host non-PIE data at
 *      ~0x2000000 is already 32-bit-reachable).
 *   5. CALLBACKS — guest wndprocs/dialogprocs/timerprocs/threadprocs
 *      are x86 EIPs: re-entered via cpu32_call() (state save /
 *      restore, sentinel return address). mw_dispatch_proc() returns
 *      a host MW_WNDPROC shim when the x86 CPU is active.
 *   6. EXIT — ExitProcess/TerminateProcess intercepted at the gate
 *      (host mw_ExitProcess _exit()s the whole engine); flag-unwind
 *      through nested cpu32_run levels instead.
 *
 * v1 limits (documented): no 16-bit addressing sub-decoder edge
 * cases beyond the common forms, no guest SEH dispatch (chain
 * installs are data writes; a fault is a loud abort), FPU beyond
 * the logged subset aborts, no per-thread TEB cloning beyond
 * thread-local cpu + stack.
 * ============================================================ */

/* ---------------- arena32 + x86 world ---------------- */
#define MW32_ARENA_BASE   0x10000000u
#define MW32_ARENA_SIZE   0x30000000u   /* 768MB: 0x10000000..0x3FFFFFFF */
#define MW32_THUNK_BASE   0x00A00000u   /* API thunk code page (16 pages) */
#define MW32_THUNK_BYTES  (16u * 4096u)
#define MW32_GATE_VA      MW32_THUNK_BASE  /* all thunks jmp here */
#define MW32_TEB32_VA     (MW32_ARENA_BASE + 0x0000)
#define MW32_PEB32_VA     (MW32_ARENA_BASE + 0x1000)
#define MW32_TLSARR_VA    (MW32_ARENA_BASE + 0x2000)
#define MW32_STRA_VA      (MW32_ARENA_BASE + 0x3000)  /* cmdline/str scratch */
#define MW32_HEAP_BASE    (MW32_ARENA_BASE + 0x01000000)
#define MW32_HEAP_END     (MW32_ARENA_BASE + 0x20000000)
#define MW32_STACK_TOP    0x3FF00000u
#define MW32_STACK_BYTES  0x00800000u   /* 8MB main stack */

static uint8_t* g_arena32 = NULL;
static uint32_t g_heap32_next = MW32_HEAP_BASE;
static uint32_t g_tls32_next  = MW32_TLSARR_VA + 4096; /* bump for _tls_array slots */
static int      g_world32_up  = 0;
static int      g_ran_pe32    = 0;
static int      g_pe32_exit   = 0;

static void mw32_world_init(void) {
    if (g_world32_up) return;
    void* p = mmap((void*)(uintptr_t)MW32_ARENA_BASE, MW32_ARENA_SIZE,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "[PE32] arena mmap failed at 0x%x: %s\n",
                MW32_ARENA_BASE, strerror(errno));
        exit(3);
    }
    g_arena32 = (uint8_t*)p;
    void* t = mmap((void*)(uintptr_t)MW32_THUNK_BASE, MW32_THUNK_BYTES,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (t == MAP_FAILED) {
        fprintf(stderr, "[PE32] thunk page mmap failed at 0x%x: %s\n",
                MW32_THUNK_BASE, strerror(errno));
        exit(3);
    }
    g_world32_up = 1;
    MW_TRACE("[PE32] world up: arena 0x%x..0x%x, thunks 0x%x..0x%x",
             MW32_ARENA_BASE, MW32_ARENA_BASE + MW32_ARENA_SIZE - 1,
             MW32_THUNK_BASE, MW32_THUNK_BASE + MW32_THUNK_BYTES - 1);
}

/* low-4GB bump allocator (VirtualAlloc/HeapAlloc/malloc-family for x86) */
static uint32_t mw32_heap_alloc(uint32_t size, int zero) {
    uint32_t sz = (size + 0xFFF) & ~0xFFFu;
    if (g_heap32_next + sz > MW32_HEAP_END) {
        fprintf(stderr, "[PE32] arena32 heap exhausted (need 0x%x)\n", sz);
        return 0;
    }
    uint32_t va = g_heap32_next;
    g_heap32_next += sz;
    if (zero) memset(g_arena32 + (va - MW32_ARENA_BASE), 0, sz);
    return va;
}
static void mw32_free_all(void) { /* bump arena: free is a no-op (logged once by caller) */ }

/* ---------------- API thunk page + slots ---------------- */
typedef struct {
    const char* name;     /* dispatch-table name (owned: strdup'd) */
    void*       fn;       /* host ms_abi mw_* (NULL = unresolved stub) */
    uint32_t    arity;    /* stdcall pop bytes EXCLUDING ret addr; 0xFFFFFFFF = unresolved */
    uint64_t    calls;
} MwApi32Slot;
static MwApi32Slot* g_api32_slots = NULL;
static uint32_t     g_api32_count = 0, g_api32_cap = 0;
static uint32_t     g_thunk_next  = MW32_THUNK_BASE + 16; /* 0..15 reserved as gate landing pad */
static uint32_t     g_api32_gate_calls = 0;

static void mw32_thunk_emit(uint32_t slot_index) {
    uint8_t* c = g_arena32; /* unused; thunks live in their own mapping */
    (void)c;
    uint8_t* page = (uint8_t*)(uintptr_t)MW32_THUNK_BASE;
    uint8_t* at = page + (g_thunk_next - MW32_THUNK_BASE);
    /* mov eax, imm32 */
    at[0] = 0xB8;
    memcpy(at + 1, &slot_index, 4);
    /* jmp rel32 -> gate */
    at[5] = 0xE9;
    int32_t rel = (int32_t)(MW32_GATE_VA - (g_thunk_next + 10));
    memcpy(at + 6, &rel, 4);
    g_thunk_next += 10;
}
static uint32_t mw32_api_thunk(const char* dll, const char* name) {
    /* dedupe by name (dispatch table is module-agnostic: name-only rule) */
    for (uint32_t i = 0; i < g_api32_count; i++)
        if (strcmp(g_api32_slots[i].name, name) == 0)
            return MW32_THUNK_BASE + 16 + i * 10;
    if (g_api32_count == g_api32_cap) {
        g_api32_cap = g_api32_cap ? g_api32_cap * 2 : 256;
        g_api32_slots = (MwApi32Slot*)realloc(g_api32_slots,
                        g_api32_cap * sizeof(MwApi32Slot));
        if (!g_api32_slots) { fprintf(stderr, "[PE32] OOM api slots\n"); exit(3); }
    }
    MwApi32Slot* s = &g_api32_slots[g_api32_count];
    s->name = strdup(name);
    s->fn = g_resolve_proc_fn ? g_resolve_proc_fn((void*)0xdead0001, name) : NULL;
    s->arity = mw32_stdcall_arity(name);
    s->calls = 0;
    if (!s->fn) {
        s->arity = 0xFFFFFFFFu; /* unresolved-stub semantics */
        MW_TRACE("[PE32] api '%s' (%s) UNRESOLVED -> stub thunk", name, dll);
    }
    mw32_thunk_emit(g_api32_count);
    return MW32_THUNK_BASE + 16 + g_api32_count++ * 10;
}
/* reverse map for GetProcAddress-returns-host-ptr */
static uint32_t mw32_thunk_for_host_fn(void* fn) {
    for (uint32_t i = 0; i < g_api32_count; i++)
        if (g_api32_slots[i].fn == fn) return MW32_THUNK_BASE + 16 + i * 10;
    /* anonymous thunk wrapping a raw host pointer (default arity) */
    if (g_api32_count == g_api32_cap) {
        g_api32_cap = g_api32_cap ? g_api32_cap * 2 : 256;
        g_api32_slots = (MwApi32Slot*)realloc(g_api32_slots,
                        g_api32_cap * sizeof(MwApi32Slot));
    }
    MwApi32Slot* s = &g_api32_slots[g_api32_count];
    char nm[64]; snprintf(nm, sizeof(nm), "@host:%p", fn);
    s->name = strdup(nm); s->fn = fn; s->arity = 32; s->calls = 0;
    mw32_thunk_emit(g_api32_count);
    return MW32_THUNK_BASE + 16 + g_api32_count++ * 10;
}

/* stdcall pop-bytes for the hot set; measured against real exports.
 * DEFAULT = 32 (8 args); guest ebp prologues resync esp so a wrong
 * default is transient (logged once per API for refinement). */
static const struct { const char* n; uint32_t b; } g_api32_arity[] = {
    { "ExitProcess", 4 }, { "TerminateProcess", 8 },
    { "GetCommandLineA", 0 }, { "GetCommandLineW", 0 },
    { "GetStartupInfoA", 4 }, { "GetStartupInfoW", 4 },
    { "GetStdHandle", 4 }, { "CloseHandle", 4 },
    { "GetModuleHandleA", 4 }, { "GetModuleHandleW", 4 },
    { "GetModuleFileNameA", 12 }, { "GetModuleFileNameW", 12 },
    { "LoadLibraryA", 4 }, { "LoadLibraryW", 4 },
    { "GetProcAddress", 8 }, { "FreeLibrary", 4 },
    { "Sleep", 4 }, { "GetTickCount", 0 }, { "GetTickCount64", 0 },
    { "QueryPerformanceCounter", 4 }, { "QueryPerformanceFrequency", 4 },
    { "VirtualAlloc", 16 }, { "VirtualFree", 12 }, { "VirtualProtect", 16 },
    { "GetProcessHeap", 0 }, { "HeapAlloc", 12 }, { "HeapFree", 12 },
    { "HeapCreate", 12 }, { "HeapReAlloc", 16 }, { "HeapSize", 12 },
    { "GetVersion", 0 }, { "GetVersionExA", 4 }, { "GetVersionExW", 4 },
    { "SetErrorMode", 4 }, { "GetLastError", 0 }, { "SetLastError", 4 },
    { "GetCurrentProcess", 0 }, { "GetCurrentThread", 0 },
    { "GetCurrentProcessId", 0 }, { "GetCurrentThreadId", 0 },
    { "GetSystemDirectoryA", 8 }, { "GetSystemDirectoryW", 8 },
    { "GetWindowsDirectoryA", 8 }, { "GetWindowsDirectoryW", 8 },
    { "GetTempPathA", 8 }, { "GetTempFileNameA", 16 },
    { "CreateFileA", 28 }, { "CreateFileW", 28 },
    { "ReadFile", 20 }, { "WriteFile", 20 },
    { "SetFilePointer", 16 }, { "GetFileSize", 8 },
    { "FindFirstFileA", 8 }, { "FindFirstFileW", 8 },
    { "FindNextFileA", 8 }, { "FindNextFileW", 8 }, { "FindClose", 4 },
    { "CreateEventA", 16 }, { "CreateEventW", 16 },
    { "SetEvent", 4 }, { "ResetEvent", 4 },
    { "WaitForSingleObject", 8 }, { "WaitForMultipleObjects", 16 },
    { "CreateMutexA", 12 }, { "CreateMutexW", 12 },
    { "InitializeCriticalSection", 4 }, { "DeleteCriticalSection", 4 },
    { "EnterCriticalSection", 4 }, { "LeaveCriticalSection", 4 },
    { "CreateThread", 24 }, { "ExitThread", 4 },
    { "TlsAlloc", 0 }, { "TlsFree", 4 }, { "TlsGetValue", 4 }, { "TlsSetValue", 8 },
    { "RegisterClassA", 4 }, { "RegisterClassW", 4 },
    { "RegisterClassExA", 4 }, { "RegisterClassExW", 4 },
    { "UnregisterClassA", 8 }, { "UnregisterClassW", 8 },
    { "CreateWindowExA", 48 }, { "CreateWindowExW", 48 },
    { "DestroyWindow", 4 }, { "ShowWindow", 8 }, { "UpdateWindow", 4 },
    { "GetMessageA", 16 }, { "GetMessageW", 16 },
    { "PeekMessageA", 20 }, { "PeekMessageW", 20 },
    { "TranslateMessage", 4 }, { "DispatchMessageA", 4 }, { "DispatchMessageW", 4 },
    { "PostMessageA", 16 }, { "PostMessageW", 16 },
    { "SendMessageA", 16 }, { "SendMessageW", 16 },
    { "PostQuitMessage", 4 }, { "DefWindowProcA", 16 }, { "DefWindowProcW", 16 },
    { "SetTimer", 16 }, { "KillTimer", 8 },
    { "LoadIconA", 8 }, { "LoadIconW", 8 }, { "LoadCursorA", 8 }, { "LoadCursorW", 8 },
    { "LoadBitmapA", 8 }, { "LoadBitmapW", 8 }, { "LoadImageA", 24 }, { "LoadImageW", 24 },
    { "GetDC", 4 }, { "ReleaseDC", 8 }, { "BeginPaint", 8 }, { "EndPaint", 8 },
    { "InvalidateRect", 12 }, { "ValidateRect", 8 }, { "GetClientRect", 8 },
    { "GetWindowRect", 8 }, { "MoveWindow", 24 }, { "SetWindowPos", 28 },
    { "AdjustWindowRect", 12 }, { "AdjustWindowRectEx", 16 },
    { "ClientToScreen", 8 }, { "ScreenToClient", 8 },
    { "SetWindowTextA", 8 }, { "SetWindowTextW", 8 },
    { "GetWindowTextA", 12 }, { "GetWindowTextW", 12 },
    { "MessageBoxA", 16 }, { "MessageBoxW", 16 },
    { "DialogBoxParamA", 20 }, { "DialogBoxParamW", 20 },
    { "EndDialog", 8 }, { "GetDlgItem", 8 }, { "SetDlgItemTextA", 12 },
    { "SendDlgItemMessageA", 20 }, { "SendDlgItemMessageW", 20 },
    { "EnumWindows", 8 }, { "EnumChildWindows", 12 },
    { "wsprintfA", 0 }, /* WINAPIV cdecl — caller pops; gate pops nothing */
    { "wsprintfW", 0 },
    { "wvsprintfA", 0 }, { "wvsprintfW", 0 },
    { "lstrlenA", 4 }, { "lstrlenW", 4 },
    { "lstrcpyA", 8 }, { "lstrcpyW", 8 }, { "lstrcatA", 8 }, { "lstrcatW", 8 },
    { "lstrcmpA", 8 }, { "lstrcmpW", 8 }, { "lstrcmpiA", 8 }, { "lstrcmpiW", 8 },
    { "GlobalAlloc", 12 }, { "GlobalFree", 8 }, { "GlobalLock", 4 }, { "GlobalUnlock", 4 },
    { "LocalAlloc", 8 }, { "LocalFree", 4 },
    { "GetSystemMetrics", 4 }, { "GetDesktopWindow", 0 },
    { "SetCursorPos", 8 }, { "GetCursorPos", 4 }, { "ShowCursor", 4 },
    { "TextOutA", 20 }, { "TextOutW", 20 }, { "ExtTextOutA", 32 }, { "ExtTextOutW", 32 },
    { "DrawTextA", 20 }, { "DrawTextW", 20 }, { "TabbedTextOutA", 32 },
    { "GetTextExtentPoint32A", 16 }, { "GetTextExtentPoint32W", 16 },
    { "SetTextColor", 8 }, { "SetBkColor", 8 }, { "SetBkMode", 8 },
    { "GetSysColor", 4 }, { "GetSysColorBrush", 4 },
    { "CreateSolidBrush", 4 }, { "CreatePen", 12 },
    { "SelectObject", 8 }, { "DeleteObject", 8 }, { "GetStockObject", 4 },
    { "Rectangle", 20 }, { "Ellipse", 20 }, { "FillRect", 12 },
    { "MoveToEx", 16 }, { "LineTo", 12 }, { "Polyline", 12 }, { "Polygon", 12 },
    { "BitBlt", 36 }, { "StretchBlt", 44 }, { "PatBlt", 24 },
    { "CreateCompatibleDC", 4 }, { "CreateCompatibleBitmap", 12 },
    { "DeleteDC", 4 }, { "SaveDC", 4 }, { "RestoreDC", 8 },
    { "SetPixel", 16 }, { "GetPixel", 12 },
    { "CreateFontA", 56 }, { "CreateFontW", 56 }, { "CreateFontIndirectA", 4 },
    { "GdiFlush", 0 }, { "SetDIBitsToDevice", 48 }, { "StretchDIBits", 52 },
    { "CreateDIBSection", 24 }, { "GetObjectA", 12 }, { "GetObjectW", 12 },
    { "GetDeviceCaps", 8 }, { "SetMapMode", 8 }, { "SetStretchBltMode", 8 },
    { "SetROP2", 8 }, { "SetBrushOrgEx", 16 },
    { "InitCommonControls", 0 }, { "InitCommonControlsEx", 4 },
    { "RegOpenKeyExA", 20 }, { "RegOpenKeyExW", 20 },
    { "RegQueryValueExA", 24 }, { "RegQueryValueExW", 24 },
    { "RegSetValueExA", 24 }, { "RegSetValueExW", 24 },
    { "RegCloseKey", 4 }, { "RegCreateKeyExA", 36 }, { "RegCreateKeyExW", 36 },
    { "ShellExecuteA", 24 }, { "ShellExecuteW", 24 },
    { "GetFileAttributesA", 4 }, { "GetFileAttributesW", 4 },
    { "SetFileAttributesA", 8 }, { "SetFileAttributesW", 8 },
    { "DeleteFileA", 4 }, { "DeleteFileW", 4 },
    { "MoveFileA", 8 }, { "MoveFileW", 8 }, { "CopyFileA", 12 }, { "CopyFileW", 12 },
    { "CreateDirectoryA", 8 }, { "CreateDirectoryW", 8 },
    { "RemoveDirectoryA", 4 }, { "RemoveDirectoryW", 4 },
    { "SetCurrentDirectoryA", 4 }, { "GetCurrentDirectoryA", 8 },
    { "GetDiskFreeSpaceA", 20 }, { "GetDriveTypeA", 4 }, { "GetLogicalDrives", 0 },
    { "GetVolumeInformationA", 32 },
    { "MultiByteToWideChar", 24 }, { "WideCharToMultiByte", 32 },
    { "lstrcpynA", 12 }, { "lstrcpynW", 12 },
    { "IsWindow", 4 }, { "IsWindowVisible", 4 }, { "IsIconic", 4 },
    { "GetParent", 4 }, { "SetFocus", 4 }, { "GetFocus", 0 },
    { "EnableWindow", 8 }, { "GetWindowLongA", 8 }, { "GetWindowLongW", 8 },
    { "SetWindowLongA", 12 }, { "SetWindowLongW", 12 },
    { "CallWindowProcA", 20 }, { "CallWindowProcW", 20 },
    { "GetClassInfoA", 8 }, { "GetClassInfoW", 8 },
    { "GetKeyState", 4 }, { "GetAsyncKeyState", 4 },
    { "CharLowerA", 4 }, { "CharUpperA", 4 }, { "CharLowerW", 4 }, { "CharUpperW", 4 },
    { "GetProfileStringA", 24 }, { "WriteProfileStringA", 12 },
    { "OutputDebugStringA", 4 }, { "OutputDebugStringW", 4 },
    { "RtlUnwind", 16 },
};
#define MW32_ARITY_DEFAULT 32u
#define MW32_ARITY_CDECL   0u      /* caller pops; gate pops nothing */
static uint32_t mw32_stdcall_arity(const char* name) {
    for (size_t i = 0; i < sizeof(g_api32_arity)/sizeof(g_api32_arity[0]); i++)
        if (strcmp(g_api32_arity[i].n, name) == 0) return g_api32_arity[i].b;
    static char warned[256]; uint32_t h = 0;
    for (const char* p = name; *p; p++) h = h * 131 + (uint8_t)*p;
    if (!(warned[h / 8] & (1 << (h % 8)))) {
        warned[h / 8] |= (char)(1 << (h % 8));
        MW_TRACE("[PE32] arity('%s') unknown -> default 32B (ebp frames resync)", name);
    }
    return MW32_ARITY_DEFAULT;
}
