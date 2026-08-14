/*
 * EXP-NEXT-2: Controlled Exception Dispatch Validation
 *
 * Standalone harness that:
 * 1. Loads synthetic_test.exe into memory
 * 2. Resolves IAT (RaiseException -> our stub)
 * 3. Executes PE entry point (func_A -> func_B -> func_C -> RaiseException)
 * 4. Stub captures CONTEXT at RaiseException entry
 * 5. Runs RtlLookupFunctionEntry + RtlVirtualUnwind frame-by-frame
 * 6. Prints frame table and handler discovery proof
 *
 * Build: gcc -o exp_next2_harness exp_next2_harness.c -O2 -no-pie -g
 * Run:   ./exp_next2_harness synthetic_test.exe
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

/* Captured state from naked stub */
uint64_t g_cap_rip = 0;
uint64_t g_cap_rsp_entry = 0;
uint64_t g_cap_rsp = 0;       /* caller's pre-call RSP */
uint32_t g_cap_code = 0;

/* Frame table */
#define MAX_FRAMES 32
struct frame_entry {
    int         index;
    uint64_t    rip;
    uint64_t    rsp;
    uint32_t    rva;
    int         rf_index;
    uint32_t    rf_begin;
    uint32_t    rf_end;
    uint32_t    ui_rva;
    uint8_t     ui_flags;
    uint8_t     ui_codes;
    uint32_t    handler_rva;
    uint64_t    establisher;
    const char* status;  /* "EHANDLER_FOUND", "no_handler", "no_rf", "outside_pe" */
};
static struct frame_entry g_frames[MAX_FRAMES];
static int g_num_frames = 0;

/* ============================================================
 * PE Loader (minimal, extracted from loader.c)
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

    /* Parse DOS header */
    if (file_data[0] != 'M' || file_data[1] != 'Z') die("not MZ");
    uint32_t lfanew = *(uint32_t*)(file_data + 0x3C);
    if (*(uint32_t*)(file_data + lfanew) != 0x00004550) die("not PE");

    uint8_t* coff = file_data + lfanew + 4;
    uint16_t machine = *(uint16_t*)(coff);
    if (machine != 0x8664) die("not AMD64");
    uint16_t num_sections = *(uint16_t*)(coff + 2);
    uint16_t opt_size = *(uint16_t*)(coff + 16);

    uint8_t* opt = coff + 20;
    uint16_t magic = *(uint16_t*)(opt);
    if (magic != 0x020B) die("not PE32+");

    uint64_t image_base = *(uint64_t*)(opt + 24);
    uint32_t section_align = *(uint32_t*)(opt + 32);
    uint32_t file_align = *(uint32_t*)(opt + 36);
    uint32_t size_of_image = *(uint32_t*)(opt + 56);
    uint32_t size_of_headers = *(uint32_t*)(opt + 60);
    uint32_t num_dd = *(uint32_t*)(opt + 108);

    g_entry_point = *(uint32_t*)(opt + 16);

    /* Data directories — just save raw values, trim later after image is loaded */
    uint8_t* dd = opt + 112;
    uint32_t raw_pdata_rva = 0, raw_pdata_size = 0;
    if (num_dd > 3) {
        uint32_t exc_va = *(uint32_t*)(dd + 3*8);
        if (exc_va != 0) {
            raw_pdata_rva = exc_va;
            raw_pdata_size = *(uint32_t*)(dd + 3*8 + 4);
        }
    }

    printf("[LOAD] ImageBase=0x%lx SizeOfImage=0x%x EntryPoint=0x%x\n",
           image_base, size_of_image, g_entry_point);
    printf("[LOAD] .pdata: RVA=0x%x size=0x%x entries=%u\n",
           g_pdata_rva, g_pdata_size, g_num_rt_functions);

    /* Allocate image */
    g_image_base = mmap(NULL, size_of_image, PROT_READ|PROT_WRITE|PROT_EXEC,
                        MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (g_image_base == MAP_FAILED) die("image mmap failed");
    g_image_size = size_of_image;

    /* Copy headers */
    memcpy(g_image_base, file_data, size_of_headers);

    /* Map sections */
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

        printf("[LOAD] Section %.8s: VA=0x%x VSize=0x%x Raw=0x%x\n",
               s, vaddr, vsize, rawsize);
    }

    /* Now set up pdata globals and trim trailing zero entries */
    g_pdata_rva = raw_pdata_rva;
    g_pdata_size = raw_pdata_size;
    g_num_rt_functions = g_pdata_size / sizeof(RUNTIME_FUNCTION);
    if (g_num_rt_functions > 0 && g_pdata_rva != 0) {
        RUNTIME_FUNCTION* rf_base_tmp = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
        while (g_num_rt_functions > 0 &&
               rf_base_tmp[g_num_rt_functions - 1].BeginAddress == 0 &&
               rf_base_tmp[g_num_rt_functions - 1].EndAddress == 0) {
            g_num_rt_functions--;
        }
    }

    munmap(file_data, st.st_size);
    return g_image_base;
}

/* ============================================================
 * Import Resolution (minimal - just RaiseException)
 * ============================================================ */

/* Forward declaration of naked stub */
void raise_stub_entry(void);

static void resolve_imports(void) {
    /* Read import directory from PE optional header */
    uint8_t* pe_hdr = g_image_base + *(uint32_t*)(g_image_base + 0x3C);
    uint8_t* opt = pe_hdr + 4 + 20;
    uint32_t num_dd = *(uint32_t*)(opt + 108);
    uint8_t* dd = opt + 112;

    if (num_dd < 2 || *(uint32_t*)(dd + 1*8) == 0) {
        printf("[IMPORT] No import directory\n");
        return;
    }

    uint32_t import_rva = *(uint32_t*)(dd + 1*8);
    uint32_t import_size = *(uint32_t*)(dd + 1*8 + 4);

    ImportDirectoryEntry* idt = (ImportDirectoryEntry*)(g_image_base + import_rva);
    for (int i = 0; ; i++) {
        ImportDirectoryEntry* entry = &idt[i];
        if (entry->OriginalFirstThunk == 0 && entry->Name == 0) break;

        char* dll_name = (char*)(g_image_base + entry->Name);
        printf("[IMPORT] DLL: %s\n", dll_name);

        /* Walk ILT */
        uint32_t* ilt = (uint32_t*)(g_image_base + entry->OriginalFirstThunk);
        uint64_t* iat = (uint64_t*)(g_image_base + entry->FirstThunk);

        for (int j = 0; ilt[j] != 0; j++) {
            if (ilt[j] & 0x80000000) {
                /* Import by ordinal */
                printf("[IMPORT]   ordinal %u -> (skipped)\n", ilt[j] & 0xFFFF);
            } else {
                /* Import by name */
                uint8_t* hint_name = g_image_base + ilt[j];
                char* name = (char*)(hint_name + 2);
                printf("[IMPORT]   %s -> ", name);

                if (strcmp(name, "RaiseException") == 0) {
                    iat[j] = (uint64_t)(uintptr_t)raise_stub_entry;
                    printf("raise_stub_entry (0x%lx)\n", (uint64_t)(uintptr_t)raise_stub_entry);
                } else {
                    iat[j] = 0; /* null stub */
                    printf("(null)\n");
                }
            }
        }
    }
}

/* ============================================================
 * RaiseException Naked Stub (captures state, calls walker)
 * ============================================================ */

/* This is the SAME pattern as loader.c's mw_RaiseException stub.
 * It captures registers at RaiseException entry, stores them in globals,
 * then calls the C unwind walker. */
__attribute__((naked))
void raise_stub_entry(void) {
    /* At entry: [rsp] = return address (in func_C), rcx=exception code */
    __asm__ volatile (
        /* Save rsp at entry (before any pushes) */
        "movq %%rsp, %c[cap_rsp_entry](%%rip)\n\t"
        /* Save return address = caller's next instruction */
        "movq (%%rsp), %%rax\n\t"
        "movq %%rax, %c[cap_rip](%%rip)\n\t"
        /* caller's RSP = rsp_entry + 8 (skip return address) */
        "leaq 8(%%rsp), %%rax\n\t"
        "movq %%rax, %c[cap_rsp](%%rip)\n\t"
        /* Save exception code (rcx) */
        "movl %%ecx, %c[cap_code](%%rip)\n\t"
        /* Call the C unwind walker */
        "call unwind_walk\n\t"
        /* Return to caller (func_C) */
        "ret\n\t"
        : 
        : [cap_rsp_entry] "i" (&g_cap_rsp_entry),
          [cap_rip] "i" (&g_cap_rip),
          [cap_rsp] "i" (&g_cap_rsp),
          [cap_code] "i" (&g_cap_code)
    );
}

/* ============================================================
 * Internal Lookup (SysV, no ABI issues)
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

/* ============================================================
 * RtlVirtualUnwind (SysV, operates on a CONTEXT buffer)
 * ============================================================ */

/* Build a minimal CONTEXT from captured state */
static void build_context(uint8_t* ctx, uint64_t rip, uint64_t rsp) {
    memset(ctx, 0, CONTEXT_SIZE);
    /* Set initial register values */
    *(uint64_t*)(ctx + CTX_Rip) = rip;
    *(uint64_t*)(ctx + CTX_Rsp) = rsp;
    /* For a real capture, we'd set all registers.
     * For this test, RBP, RBX etc. are whatever they happen to be,
     * which is fine since we're testing the UNWIND mechanism. */
    /* Read actual register values at unwind time via inline asm */
    uint64_t rbp, rbx, rsi, rdi, r12, r13, r14, r15;
    __asm__ volatile ("" : "=b"(rbx));
    __asm__ volatile ("" : "=D"(rdi));
    __asm__ volatile ("" : "=S"(rsi));
    __asm__ volatile ("" : "=a"(rbp)); /* rbp via constrain hack - actually need rbp */
    /* Better approach: just read from current state */
    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("movq %%r12, %0" : "=r"(r12));
    __asm__ volatile ("movq %%r13, %0" : "=r"(r13));
    __asm__ volatile ("movq %%r14, %0" : "=r"(r14));
    __asm__ volatile ("movq %%r15, %0" : "=r"(r15));

    *(uint64_t*)(ctx + CTX_Rbx) = rbx;
    *(uint64_t*)(ctx + CTX_Rbp) = rbp;
    *(uint64_t*)(ctx + CTX_Rsi) = rsi;
    *(uint64_t*)(ctx + CTX_Rdi) = rdi;
    *(uint64_t*)(ctx + CTX_R12) = r12;
    *(uint64_t*)(ctx + CTX_R13) = r13;
    *(uint64_t*)(ctx + CTX_R14) = r14;
    *(uint64_t*)(ctx + CTX_R15) = r15;
}

/*
 * RtlVirtualUnwind — internal SysV implementation.
 * Returns: handler address (or NULL if no handler).
 * Sets: *establisher_frame, updates CONTEXT in place.
 */
static void* internal_virtual_unwind(
    uint64_t control_pc,
    RUNTIME_FUNCTION* rf,
    uint8_t* ctx,
    uint64_t* establisher_frame,
    uint32_t* out_handler_rva,
    uint8_t* out_flags)
{
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;

    uint8_t version     = ui_base[0] & 0x07;
    uint8_t flags       = (ui_base[0] >> 3) & 0x03;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    uint8_t frame_reg   = ui_base[3] >> 4;
    uint8_t frame_off   = ui_base[3] & 0x0f;

    (void)version;

    if (out_flags) *out_flags = flags;

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
        if (slot * 2 + 1 >= (int)(count_codes * 2 + 16)) break; /* safety */
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 12) & 0x0f;
        uint8_t op_info = (code_word >> 8) & 0x0f;
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot++;
            continue;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            new_rsp += 8;
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off && new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            break;
        }
        case UWOP_ALLOC_LARGE: {
            slot++;
            if (op_info == 0) {
                uint16_t alloc = *(uint16_t*)(codes + slot * 2);
                new_rsp += alloc;
            } else {
                slot++;
                uint32_t alloc = *(uint32_t*)(codes + slot * 2 - 2);
                new_rsp += (uint64_t)alloc << 16;
            }
            break;
        }
        case UWOP_ALLOC_SMALL: {
            new_rsp += (uint64_t)(op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
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
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            if (new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
                new_rsp = *(uint64_t*)(new_rsp + 8);
            }
            break;
        }
        default: {
            printf("  [UNWIND] unknown opcode %d\n", op_code);
            if (op_code >= 9) slot++;
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
    if (flags & UNW_FLAG_EHANDLER) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
    }

    if (out_handler_rva) *out_handler_rva = handler_rva;

    printf("  [UNWIND] pc=0x%lx begin=0x%x end=0x%x handler=0x%x est=0x%lx rsp=0x%lx->0x%lx\n",
           rip, rf->BeginAddress, rf->EndAddress, handler_rva,
           establisher, rsp, new_rsp);

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* Forward declarations */
static void record_frame(int index, uint64_t rip, uint64_t rsp, uint64_t rva,
    int rf_idx, uint32_t rf_begin, uint32_t rf_end, uint32_t ui_rva,
    uint8_t ui_flags, uint32_t handler_rva, uint64_t establisher,
    const char* status);
static void print_frame_table(void);
static void print_summary(void);

/* ============================================================
 * Unwind Walker (called from naked stub)
 * ============================================================ */

static void unwind_walk(void) __attribute__((used));

static void unwind_walk(void) {
    printf("\n");
    printf("=================================================================\n");
    printf("EXP-NEXT-2: Controlled Exception Dispatch Validation\n");
    printf("=================================================================\n");

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    printf("\n[STATE] Captured RIP=0x%lx (RVA=0x%lx)\n", g_cap_rip, g_cap_rip - img_base);
    printf("[STATE] Captured RSP_entry=0x%lx\n", g_cap_rsp_entry);
    printf("[STATE] Caller RSP=0x%lx\n", g_cap_rsp);
    printf("[STATE] Exception code=0x%x\n", g_cap_code);
    printf("[STATE] *[RSP_entry]=0x%lx (expect RIP=0x%lx) %s\n",
           *(uint64_t*)(uintptr_t)g_cap_rsp_entry, g_cap_rip,
           (*(uint64_t*)(uintptr_t)g_cap_rsp_entry == g_cap_rip) ? "MATCH" : "MISMATCH");

    /* Build initial CONTEXT from captured state.
     * The captured RIP is the return address in func_C (right after the call [IAT]).
     * The captured RSP is the caller's pre-call RSP. */
    uint8_t ctx[CONTEXT_SIZE];
    build_context(ctx, g_cap_rip, g_cap_rsp);

    g_num_frames = 0;

    /* FRAME 0: The function containing the return address (func_C).
     * This is the caller of RaiseException. */
    uint64_t current_rip = g_cap_rip;
    uint64_t current_rva = current_rip - img_base;

    printf("\n--- FRAME 0 (RaiseException caller = func_C) ---\n");
    RUNTIME_FUNCTION* rf = internal_lookup(current_rva);
    if (!rf) {
        printf("[F0] FAIL: No RUNTIME_FUNCTION for RVA 0x%lx\n", current_rva);
        record_frame(0, current_rip, g_cap_rsp, current_rva, -1, 0, 0, 0, 0, 0, 0, "no_rf");
        print_frame_table();
        return;
    }

    int rf_idx = (int)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
    printf("[F0] RF[%d] begin=0x%x end=0x%x unwind=0x%x\n",
           rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

    /* Dump UNWIND_INFO */
    uint8_t* ui = g_image_base + rf->UnwindInfo;
    uint8_t ui_flags = (ui[0] >> 3) & 0x03;
    uint8_t count_codes = ui[2];
    printf("[F0] UI: version=%d flags=0x%x prolog=%d codes=%d frame_reg=%d\n",
           ui[0] & 7, ui_flags, ui[1], count_codes, ui[3] >> 4);

    /* Dump unwind codes */
    for (int i = 0; i < count_codes; i++) {
        uint16_t cw = *(uint16_t*)(ui + 4 + i * 2);
        printf("[F0]   code[%d]: op=%d info=%d offset=%d (0x%04x)\n",
               i, (cw>>12)&0xf, (cw>>8)&0xf, cw&0xff, cw);
    }

    /* Run RtlVirtualUnwind on Frame 0 */
    uint64_t est_frame = 0;
    uint32_t handler_rva = 0;
    void* handler = internal_virtual_unwind(current_rip, rf, ctx, &est_frame, &handler_rva, &ui_flags);

    record_frame(0, current_rip, g_cap_rsp, current_rva, rf_idx,
                 rf->BeginAddress, rf->EndAddress, rf->UnwindInfo,
                 ui_flags, handler_rva, est_frame,
                 handler ? "EHANDLER_FOUND" : "no_handler");

    if (handler) {
        printf("[F0] *** EHANDLER at RVA 0x%x (VA 0x%lx) ***\n",
               handler_rva, (uint64_t)(uintptr_t)handler);
        /* For our test, func_C should NOT have a handler */
        printf("[F0] UNEXPECTED: func_C should not have EHANDLER!\n");
    }

    /* Read parent return address from [establisher_frame].
     * RtlVirtualUnwind updates RSP but NOT RIP in the CONTEXT.
     * The parent return address lives at [establisher_frame] on the stack. */
    uint64_t next_rip = *(uint64_t*)(uintptr_t)est_frame;
    uint64_t next_rsp = est_frame + 8;
    printf("[F0] Parent: [0x%lx]=0x%lx (RVA 0x%lx)\n",
           est_frame, next_rip, next_rip - img_base);

    /* FRAME 1+: Walk up the stack */
    for (int frame = 1; frame < MAX_FRAMES; frame++) {
        uint64_t parent_rip = next_rip;
        uint64_t parent_rsp = next_rsp;
        uint64_t parent_rva = parent_rip - img_base;

        printf("\n--- FRAME %d ---\n", frame);
        printf("[F%d] RIP=0x%lx RVA=0x%lx RSP=0x%lx\n", frame, parent_rip, parent_rva, parent_rsp);

        if (parent_rip < img_base || parent_rip >= img_end) {
            printf("[F%d] Outside PE image — walk ends\n", frame);
            record_frame(frame, parent_rip, parent_rsp, parent_rva, -1, 0, 0, 0, 0, 0, 0, "outside_pe");
            break;
        }

        rf = internal_lookup(parent_rva);
        if (!rf) {
            printf("[F%d] No RUNTIME_FUNCTION for RVA 0x%lx — left PE code\n", frame, parent_rva);
            record_frame(frame, parent_rip, parent_rsp, parent_rva, -1, 0, 0, 0, 0, 0, 0, "no_rf");
            break;
        }

        rf_idx = (int)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
        ui = g_image_base + rf->UnwindInfo;
        ui_flags = (ui[0] >> 3) & 0x03;
        count_codes = ui[2];

        printf("[F%d] RF[%d] begin=0x%x end=0x%x unwind=0x%x\n",
               frame, rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);
        printf("[F%d] UI: flags=0x%x codes=%d\n", frame, ui_flags, count_codes);

        /* Update CONTEXT before VirtualUnwind so it uses correct RIP for offset calc */
        *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
        *(uint64_t*)(ctx + CTX_Rsp) = parent_rsp;
        handler = internal_virtual_unwind(parent_rip, rf, ctx, &est_frame, &handler_rva, &ui_flags);

        const char* status = "no_handler";
        if (handler) {
            status = "EHANDLER_FOUND";
            printf("[F%d] *** EHANDLER FOUND at RVA 0x%x (VA 0x%lx) ***\n",
                   frame, handler_rva, (uint64_t)(uintptr_t)handler);

            /* Verify handler is in expected function */
            uint64_t handler_rva_check = handler_rva;
            /* Handler should be in .text section */
            if (handler_rva_check >= 0x1000) {
                printf("[F%d] Handler section-relative offset: 0x%lx\n",
                       frame, handler_rva_check - 0x1000);
            }
        }

        record_frame(frame, parent_rip, parent_rsp, parent_rva, rf_idx,
                     rf->BeginAddress, rf->EndAddress, rf->UnwindInfo,
                     ui_flags, handler_rva, est_frame, status);

        if (handler) {
            printf("\n[RESULT] Handler discovered at Frame %d — stopping walk.\n", frame);
            break;
        }

        /* Read parent return address from [establisher_frame] */
        next_rip = *(uint64_t*)(uintptr_t)(est_frame);
        next_rsp = est_frame + 8;

        printf("[F%d] Parent: [0x%lx]=0x%lx RSP=0x%lx\n",
               frame, est_frame, next_rip, next_rsp);

        if (next_rip < img_base || next_rip >= img_end) {
            printf("[F%d] Next RIP 0x%lx outside PE — walk ends\n", frame, next_rip);
            break;
        }
    }

    printf("\n");
    print_frame_table();
    print_summary();
}

static void record_frame(int index, uint64_t rip, uint64_t rsp, uint64_t rva,
    int rf_idx, uint32_t rf_begin, uint32_t rf_end, uint32_t ui_rva,
    uint8_t ui_flags, uint32_t handler_rva, uint64_t establisher,
    const char* status) {
    if (g_num_frames >= MAX_FRAMES) return;
    struct frame_entry* f = &g_frames[g_num_frames++];
    f->index = index;
    f->rip = rip;
    f->rsp = rsp;
    f->rva = (uint32_t)rva;
    f->rf_index = rf_idx;
    f->rf_begin = rf_begin;
    f->rf_end = rf_end;
    f->ui_rva = ui_rva;
    f->ui_flags = ui_flags;
    f->ui_codes = 0; /* not recorded separately */
    f->handler_rva = handler_rva;
    f->establisher = establisher;
    f->status = status;
}

static void print_frame_table(void) {
    printf("\n=================================================================\n");
    printf("FRAME TABLE\n");
    printf("=================================================================\n");
    printf("Idx  RIP             RVA     RF[idx] Begin    End      UI_RVA  Flags  Handler  Status\n");
    printf("---  -------------  ------  ------  -------  -------  ------  -----  -------  ----------------\n");
    for (int i = 0; i < g_num_frames; i++) {
        struct frame_entry* f = &g_frames[i];
        printf("%-3d  0x%012lx  0x%04x  %-5d  0x%05x  0x%05x  0x%04x  0x%01x    0x%04x   %s\n",
               f->index, f->rip, f->rva, f->rf_index,
               f->rf_begin, f->rf_end, f->ui_rva, f->ui_flags,
               f->handler_rva, f->status);
    }
    printf("\n");
}

static void print_summary(void) {
    printf("=================================================================\n");
    printf("EXP-NEXT-2 SUMMARY\n");
    printf("=================================================================\n");

    /* Verify expectations */
    int pass = 1;

    /* Frame 0 should be func_C (no handler) */
    if (g_num_frames < 1) { printf("FAIL: No frames walked\n"); pass = 0; }
    else {
        struct frame_entry* f0 = &g_frames[0];
        printf("[CHECK] Frame 0: RVA=0x%x (expect func_C in 0x1000-0x1021 range)\n", f0->rva);
        /* func_C is at TEXT_RVA+0x0 .. TEXT_RVA+0x21 = 0x1000-0x1021 */
        if (f0->rva >= 0x1000 && f0->rva <= 0x1021) {
            printf("        PASS: Frame 0 is in func_C\n");
        } else {
            printf("        FAIL: Frame 0 RVA 0x%x not in func_C range\n", f0->rva);
            pass = 0;
        }
        if (f0->handler_rva == 0 && strcmp(f0->status, "no_handler") == 0) {
            printf("        PASS: Frame 0 has no EHANDLER\n");
        } else {
            printf("        FAIL: Frame 0 unexpectedly has handler 0x%x\n", f0->handler_rva);
            pass = 0;
        }
    }

    /* Frame 1 should be func_B (WITH handler) */
    if (g_num_frames < 2) {
        printf("FAIL: Only %d frame(s) walked, need at least 2\n", g_num_frames);
        pass = 0;
    } else {
        struct frame_entry* f1 = &g_frames[1];
        printf("[CHECK] Frame 1: RVA=0x%x (expect func_B at 0x1030-0x104f)\n", f1->rva);
        if (f1->rva >= 0x1030 && f1->rva <= 0x104f) {
            printf("        PASS: Frame 1 is in func_B\n");
        } else {
            printf("        FAIL: Frame 1 RVA 0x%x not in func_B range\n", f1->rva);
            pass = 0;
        }
        if (f1->handler_rva != 0 && strcmp(f1->status, "EHANDLER_FOUND") == 0) {
            printf("        PASS: Frame 1 has EHANDLER at RVA 0x%x\n", f1->handler_rva);
            /* Verify handler points to our synthetic handler */
            if (f1->handler_rva == 0x1060) {
                printf("        PASS: Handler RVA matches expected 0x1060\n");
            } else {
                printf("        FAIL: Handler RVA 0x%x != expected 0x1060\n", f1->handler_rva);
                pass = 0;
            }
        } else {
            printf("        FAIL: Frame 1 has no EHANDLER (status=%s)\n", f1->status);
            pass = 0;
        }
    }

    /* Frame 2 (optional) should be func_A (no handler) */
    if (g_num_frames >= 3) {
        struct frame_entry* f2 = &g_frames[2];
        printf("[CHECK] Frame 2: RVA=0x%x (expect func_A at 0x1050-0x105e)\n", f2->rva);
        if (f2->rva >= 0x1050 && f2->rva <= 0x105e) {
            printf("        PASS: Frame 2 is in func_A\n");
        } else {
            printf("        INFO: Frame 2 RVA 0x%x (may be outside PE)\n", f2->rva);
        }
    }

    printf("\n");
    if (pass) {
        printf("RESULT: PASS — Handler discovery via RtlVirtualUnwind works correctly.\n");
        printf("        RaiseException -> CONTEXT -> Frame0(func_C,no_handler) -> Frame1(func_B,EHANDLER)\n");
    } else {
        printf("RESULT: FAIL — See checks above for details.\n");
    }
    printf("=================================================================\n");
}

/* ============================================================
 * Main
 * ============================================================ */
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <synthetic_pe.exe>\n", argv[0]);
        return 1;
    }

    printf("[HARNESS] EXP-NEXT-2: Loading %s\n", argv[1]);

    /* 1. Load PE */
    load_pe(argv[1]);

    /* 2. Resolve imports (RaiseException -> our stub) */
    resolve_imports();

    /* 3. Verify .pdata entries */
    printf("\n[PDATA] Dumping RUNTIME_FUNCTION entries:\n");
    RUNTIME_FUNCTION* rf_base = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
    for (uint32_t i = 0; i < g_num_rt_functions; i++) {
        RUNTIME_FUNCTION* rf = &rf_base[i];
        if (rf->BeginAddress == 0 && rf->EndAddress == 0) continue; /* skip null entries */
        uint8_t* ui = g_image_base + rf->UnwindInfo;
        uint8_t flags = (ui[0] >> 3) & 0x03;
        printf("  [%u] begin=0x%x end=0x%x unwind=0x%x flags=0x%x\n",
               i, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo, flags);

        /* Dump first bytes at function start */
        uint8_t* fb = g_image_base + rf->BeginAddress;
        printf("       bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7]);
    }

    /* 4. Execute PE entry point */
    printf("\n[EXEC] Jumping to PE entry point RVA=0x%x (VA=0x%lx)\n",
           g_entry_point, (uint64_t)(uintptr_t)g_image_base + g_entry_point);
    printf("[EXEC] Call chain: func_A -> func_B -> func_C -> RaiseException(stub)\n");
    printf("[EXEC] Expecting: stub captures state, runs unwind walk, returns\n\n");

    /* Jump to entry point using inline asm to avoid ABI issues */
    void (*entry)(void) = (void (*)(void))(g_image_base + g_entry_point);
    printf("[EXEC] entry func ptr = 0x%lx\n", (uint64_t)(uintptr_t)entry);
    entry();

    printf("\n[EXEC] Returned from PE entry point.\n");

    return 0;
}
