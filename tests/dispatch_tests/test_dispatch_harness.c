/*
 * BUG-024: RtlDispatchException Synthetic Test Harness
 *
 * Loads each test PE, resolves IAT (RaiseException -> our stub),
 * executes entry point, and checks dispatcher trace output.
 *
 * Build: gcc -o test_dispatch_harness test_dispatch_harness.c -O2 -g -no-pie
 * Run:   ./test_dispatch_harness test_a.exe
 *         ./test_dispatch_harness test_b.exe
 *         ./test_dispatch_harness test_c.exe
 *         ./test_dispatch_harness test_d.exe
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "../../include/pe.h"

/* ============================================================
 * Global State
 * ============================================================ */
static uint8_t* g_image_base = NULL;
static uint64_t g_image_size = 0;
static uint32_t g_entry_point = 0;
static uint32_t g_pdata_rva = 0;
static uint32_t g_pdata_size = 0;
static uint32_t g_num_rt_functions = 0;

/* Captured state from naked stub (non-static for naked asm access) */
uint64_t g_cap_rip = 0;
uint64_t g_cap_rsp_entry = 0;
uint64_t g_cap_rsp = 0;
uint32_t g_cap_code = 0;
uint32_t g_cap_flags = 0;
uint32_t g_cap_nargs = 0;
uint64_t g_cap_args = 0;

/* Dispatcher trace (written by seh_dispatch_exception) */
#define MAX_TRACE 256
static const char* g_trace[MAX_TRACE];
static int g_trace_count = 0;
static int g_trace_fail = 0;  /* set if exception not handled */

/* Handler call tracking */
static int g_handler_called = 0;
static uint64_t g_handler_rva = 0;

/* ============================================================
 * Trace logging (replaces MW_TRACE for standalone harness)
 * ============================================================ */
/* We override MW_TRACE — but since this is a standalone binary,
 * we just use fprintf(stderr, ...) directly. */
#define MW_TRACE(fmt, ...) do { \
    fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* ============================================================
 * PE Loader (minimal)
 * ============================================================ */

static void die(const char* msg) {
    fprintf(stderr, "FATAL: %s\n", msg);
    exit(1);
}

static uint8_t* load_pe(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("cannot open PE");
    struct stat st;
    fstat(fd, &st);
    uint8_t* file_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_data == MAP_FAILED) die("mmap failed");

    if (file_data[0] != 'M' || file_data[1] != 'Z') die("not MZ");
    uint32_t lfanew = *(uint32_t*)(file_data + 0x3C);
    if (*(uint32_t*)(file_data + lfanew) != 0x00004550) die("not PE");

    uint8_t* coff = file_data + lfanew + 4;
    if (*(uint16_t*)(coff) != 0x8664) die("not AMD64");
    uint16_t num_sections = *(uint16_t*)(coff + 2);
    uint16_t opt_size = *(uint16_t*)(coff + 16);
    uint8_t* opt = coff + 20;
    if (*(uint16_t*)(opt) != 0x020B) die("not PE32+");

    uint64_t image_base = *(uint64_t*)(opt + 24);
    uint32_t size_of_image = *(uint32_t*)(opt + 56);
    uint32_t size_of_headers = *(uint32_t*)(opt + 60);
    uint32_t num_dd = *(uint32_t*)(opt + 108);

    g_entry_point = *(uint32_t*)(opt + 16);

    uint8_t* dd = opt + 112;
    uint32_t exc_va = 0, exc_size = 0;
    if (num_dd > 3 && *(uint32_t*)(dd + 3*8) != 0) {
        exc_va = *(uint32_t*)(dd + 3*8);
        exc_size = *(uint32_t*)(dd + 3*8 + 4);
    }

    printf("[LOAD] ImageBase=0x%lx EP=0x%x Size=0x%x\n",
           image_base, g_entry_point, size_of_image);

    g_image_base = mmap(NULL, size_of_image, PROT_READ|PROT_WRITE|PROT_EXEC,
                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (g_image_base == MAP_FAILED) die("image mmap failed");
    g_image_size = size_of_image;

    memcpy(g_image_base, file_data, size_of_headers);

    uint8_t* sec_hdr = opt + opt_size;
    for (int i = 0; i < num_sections; i++) {
        uint8_t* s = sec_hdr + i * 40;
        uint32_t vsize = *(uint32_t*)(s + 8);
        uint32_t vaddr = *(uint32_t*)(s + 12);
        uint32_t rawsize = *(uint32_t*)(s + 16);
        uint32_t rawptr = *(uint32_t*)(s + 20);
        if (vaddr + vsize > size_of_image) vsize = size_of_image - vaddr;
        if (rawptr + rawsize > (uint32_t)st.st_size) rawsize = st.st_size - rawptr;
        memset(g_image_base + vaddr, 0, vsize);
        if (rawsize > 0) memcpy(g_image_base + vaddr, file_data + rawptr, rawsize);
    }

    g_pdata_rva = exc_va;
    g_pdata_size = exc_size;
    g_num_rt_functions = (exc_va != 0) ? exc_size / sizeof(RUNTIME_FUNCTION) : 0;
    /* Trim trailing null entries */
    if (g_num_rt_functions > 0) {
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
        while (g_num_rt_functions > 0 &&
               rf[g_num_rt_functions - 1].BeginAddress == 0)
            g_num_rt_functions--;
    }

    munmap(file_data, st.st_size);
    return g_image_base;
}

static void resolve_imports(void) {
    uint8_t* pe_hdr = g_image_base + *(uint32_t*)(g_image_base + 0x3C);
    uint8_t* opt = pe_hdr + 4 + 20;
    uint32_t num_dd = *(uint32_t*)(opt + 108);
    uint8_t* dd = opt + 112;
    if (num_dd < 2 || *(uint32_t*)(dd + 1*8) == 0) return;

    uint32_t import_rva = *(uint32_t*)(dd + 1*8);
    ImportDirectoryEntry* idt = (ImportDirectoryEntry*)(g_image_base + import_rva);
    for (int i = 0; ; i++) {
        ImportDirectoryEntry* entry = &idt[i];
        if (entry->OriginalFirstThunk == 0 && entry->Name == 0) break;
        uint32_t* ilt = (uint32_t*)(g_image_base + entry->OriginalFirstThunk);
        uint64_t* iat = (uint64_t*)(g_image_base + entry->FirstThunk);
        for (int j = 0; ilt[j] != 0; j++) {
            if (ilt[j] & 0x80000000) continue;
            char* name = (char*)(g_image_base + ilt[j] + 2);
            if (strcmp(name, "RaiseException") == 0) {
                /* Patch IAT to point to our stub */
                extern void raise_stub_entry(void);
                iat[j] = (uint64_t)(uintptr_t)raise_stub_entry;
                printf("[IMPORT] RaiseException -> raise_stub_entry (0x%lx)\n",
                       (uint64_t)(uintptr_t)raise_stub_entry);
            }
        }
    }
}

/* ============================================================
 * RaiseException Naked Stub
 * ============================================================ */

__attribute__((naked))
void raise_stub_entry(void) {
    __asm__ volatile (
        "movq %%rsp, g_cap_rsp_entry(%%rip)\n\t"
        "movq (%%rsp), %%rax\n\t"
        "movq %%rax, g_cap_rip(%%rip)\n\t"
        "leaq 8(%%rsp), %%rax\n\t"
        "movq %%rax, g_cap_rsp(%%rip)\n\t"
        "movl %%ecx, g_cap_code(%%rip)\n\t"
        "call seh_dispatch_test\n\t"
        "ret\n\t"
        ::: "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
    );
}

/* ============================================================
 * Internal Lookup + Unwind (from exp_next2_harness, adapted)
 * ============================================================ */

static RUNTIME_FUNCTION* internal_lookup(uint64_t rva) {
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

static void* internal_virtual_unwind(
    uint64_t control_pc, RUNTIME_FUNCTION* rf,
    uint8_t* ctx, uint64_t* establisher_frame,
    uint32_t* out_handler_rva, uint8_t* out_flags)
{
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;
    uint8_t flags       = (ui_base[0] >> 3) & 0x03;
    uint8_t count_codes = ui_base[2];
    uint8_t frame_reg   = ui_base[3] >> 4;
    uint8_t frame_off   = ui_base[3] & 0x0f;

    if (out_flags) *out_flags = flags;

    uint64_t rsp = *(uint64_t*)(ctx + CTX_Rsp);
    uint64_t rip = *(uint64_t*)(ctx + CTX_Rip);
    uint32_t rip_rva = (uint32_t)(rip - (uint64_t)(uintptr_t)g_image_base);
    uint32_t func_offset = rip_rva - rf->BeginAddress;

    uint64_t new_rsp = rsp;
    int fp_set = 0;
    uint64_t fp_reg_val = 0;

    uint8_t* codes = ui_base + 4;
    int slot = 0;
    for (int i = 0; i < count_codes; i++) {
        if (slot * 2 + 1 >= (int)(count_codes * 2 + 16)) break;
        uint16_t cw = *(uint16_t*)(codes + slot * 2);
        uint8_t op = (cw >> 12) & 0x0f;
        uint8_t info = (cw >> 8) & 0x0f;
        uint8_t code_offset = cw & 0xff;

        if (func_offset > 0 && code_offset >= func_offset) {
            slot++; continue;
        }

        switch (op) {
        case UWOP_PUSH_NONVOL: {
            new_rsp += 8;
            uint32_t off = reg_to_ctx_offset(info);
            if (off && new_rsp <= rsp + 4096)
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            break;
        }
        case UWOP_ALLOC_LARGE: {
            slot++;
            if (info == 0) new_rsp += *(uint16_t*)(codes + slot * 2);
            else { slot++; new_rsp += *(uint32_t*)(codes + slot * 2 - 2) << 16; }
            break;
        }
        case UWOP_ALLOC_SMALL: {
            new_rsp += (uint64_t)(info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
            fp_set = 1;
            break;
        }
        case UWOP_SAVE_NONVOL: { slot++; break; }
        case UWOP_SAVE_NONVOL_FAR: { slot++; slot++; break; }
        default: if (op >= 9) slot++; break;
        }
        slot++;
    }

    if (fp_set) {
        uint32_t fp_off = reg_to_ctx_offset(frame_reg);
        if (fp_off) *(uint64_t*)(ctx + fp_off) = fp_reg_val;
    }

    *(uint64_t*)(ctx + CTX_Rsp) = new_rsp;
    uint64_t establisher = fp_set ? fp_reg_val : new_rsp;
    if (establisher_frame) *establisher_frame = establisher;

    uint32_t handler_rva = 0;
    if (flags & UNW_FLAG_EHANDLER) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
    }
    if (out_handler_rva) *out_handler_rva = handler_rva;

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* ============================================================
 * Test Dispatcher (simulates seh_dispatch_exception logic)
 * ============================================================ */

static void seh_dispatch_test(void) __attribute__((used));

static void seh_dispatch_test(void) {
    printf("\n=== DISPATCH TEST ===\n");
    printf("RIP=0x%lx RSP=0x%lx Code=0x%x\n",
           g_cap_rip, g_cap_rsp, g_cap_code);

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    uint8_t ctx[CONTEXT_SIZE];
    memset(ctx, 0, CONTEXT_SIZE);
    *(uint64_t*)(ctx + CTX_Rip) = g_cap_rip;
    *(uint64_t*)(ctx + CTX_Rsp) = g_cap_rsp;

    int pass = 1;

    for (int frame = 0; frame < 32; frame++) {
        uint64_t cur_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t cur_rva = cur_rip - img_base;

        printf("\nFrame[%d]: RIP=0x%lx RVA=0x%lx\n", frame, cur_rip, cur_rva);

        if (cur_rip < img_base || cur_rip >= img_end) {
            printf("  -> outside PE, walk ends\n");
            g_trace_fail = 1;
            break;
        }

        RUNTIME_FUNCTION* rf = internal_lookup(cur_rva);
        if (!rf) {
            printf("  -> no RUNTIME_FUNCTION, walk ends\n");
            g_trace_fail = 1;
            break;
        }

        printf("  RF: begin=0x%x end=0x%x ui=0x%x\n",
               rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

        uint64_t est = 0;
        uint32_t handler_rva = 0;
        uint8_t ui_flags = 0;
        void* handler = internal_virtual_unwind(cur_rip, rf, ctx, &est, &handler_rva, &ui_flags);

        printf("  Unwind: est=0x%lx handler_rva=0x%x flags=0x%x\n",
               est, handler_rva, ui_flags);

        if (!handler) {
            printf("  -> no EHANDLER\n");
            uint64_t parent = *(uint64_t*)(uintptr_t)est;
            printf("  -> parent RIP=0x%lx RVA=0x%lx\n", parent, parent - img_base);
            *(uint64_t*)(ctx + CTX_Rip) = parent;
            *(uint64_t*)(ctx + CTX_Rsp) = est + 8;
            continue;
        }

        /* EHANDLER found! */
        printf("  *** EHANDLER FOUND at RVA 0x%x ***\n", handler_rva);
        g_handler_called = 1;
        g_handler_rva = handler_rva;

        /* Build DISPATCHER_CONTEXT */
        DISPATCHER_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        dc.ControlPc = cur_rip;
        dc.ImageBase = img_base;
        dc.FunctionEntry = rf;
        dc.EstablisherFrame = est;
        dc.ContextRecord = ctx;
        dc.LanguageHandler = handler;

        EXCEPTION_RECORD er;
        memset(&er, 0, sizeof(er));
        er.ExceptionCode = g_cap_code;
        er.ExceptionAddress = g_cap_rip;

        /* Call handler via ms_abi inline asm */
        int32_t disposition = 1;  /* default ContinueSearch */
        __asm__ volatile (
            "movq %2, %%rcx\n\t"
            "movq %3, %%rdx\n\t"
            "movq %4, %%r8\n\t"
            "movq %5, %%r9\n\t"
            "subq $0x28, %%rsp\n\t"
            "call *%1\n\t"
            "addq $0x28, %%rsp\n\t"
            : "=a"(disposition)
            : "r"(handler), "r"((uint64_t)(uintptr_t)&er),
              "r"(est), "r"((uint64_t)(uintptr_t)ctx),
              "r"((uint64_t)(uintptr_t)&dc)
            : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
        );

        printf("  Handler returned disposition=%d", disposition);
        if (disposition == 0) printf(" (ContinueExecution)\n");
        else if (disposition == 1) printf(" (ContinueSearch)\n");
        else printf(" (other=%d)\n", disposition);

        if (disposition == 0) {
            printf("  -> handler handled exception\n");
            g_trace_fail = 0;
            return;
        }

        /* ContinueSearch — continue walking */
        uint64_t parent = *(uint64_t*)(uintptr_t)est;
        printf("  -> ContinueSearch, walking to parent RIP=0x%lx\n", parent);
        *(uint64_t*)(ctx + CTX_Rip) = parent;
        *(uint64_t*)(ctx + CTX_Rsp) = est + 8;
    }

    g_trace_fail = 1;
    printf("\nNo handler handled the exception!\n");
}

/* ============================================================
 * Main
 * ============================================================ */
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test_pe.exe> [expected_handler_frame]\n", argv[0]);
        return 1;
    }

    const char* test_name = strrchr(argv[1], '/');
    test_name = test_name ? test_name + 1 : argv[1];

    printf("\n========================================\n");
    printf("DISPATCH TEST: %s\n", test_name);
    printf("========================================\n");

    load_pe(argv[1]);
    resolve_imports();

    /* Dump .pdata */
    printf("\n[PDATA] %u entries:\n", g_num_rt_functions);
    RUNTIME_FUNCTION* rfb = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
    for (uint32_t i = 0; i < g_num_rt_functions; i++) {
        uint8_t* ui = g_image_base + rfb[i].UnwindInfo;
        uint8_t flags = (ui[0] >> 3) & 0x03;
        printf("  [%u] begin=0x%x end=0x%x ui=0x%x flags=0x%x\n",
               i, rfb[i].BeginAddress, rfb[i].EndAddress, rfb[i].UnwindInfo, flags);
    }

    /* Execute */
    printf("\n[EXEC] Calling PE entry at RVA 0x%x\n", g_entry_point);
    fflush(stdout);
    g_handler_called = 0;
    g_trace_fail = 0;

    printf("[EXEC] About to call entry()...\n");
    fflush(stdout);
    void (*entry)(void) = (void (*)(void))(g_image_base + g_entry_point);
    printf("[EXEC] entry func ptr = %p\n", (void*)entry);
    fflush(stdout);
    entry();
    printf("[EXEC] Returned from PE entry point.\n");
    fflush(stdout);

    /* Results */
    printf("\n========================================\n");
    printf("RESULT: %s\n", test_name);
    printf("  Handler called: %s\n", g_handler_called ? "YES" : "NO");
    if (g_handler_called)
        printf("  Handler RVA:   0x%lx\n", g_handler_rva);
    printf("  Exception handled: %s\n", g_trace_fail ? "NO (FAIL)" : "YES (PASS)");
    printf("========================================\n");

    return g_trace_fail ? 1 : 0;
}
