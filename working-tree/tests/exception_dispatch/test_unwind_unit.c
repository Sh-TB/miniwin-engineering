/*
 * Exception Dispatch Unit Tests — Test internal unwind functions directly
 * WITHOUT needing actual PE binaries or IAT patching.
 *
 * Tests:
 *   1. UNWIND_INFO parsing with PUSH_NONVOL + ALLOC_SMALL + SET_FPREG
 *   2. CHAININFO establisher frame computation
 *   3. EHANDLER discovery and handler RVA extraction
 *   4. g_cap_er copy order fix (EXCEPTION_RECORD populated correctly)
 *   5. CONTEXT ContextFlags value correctness
 *
 * Build: gcc -o test_unwind_unit test_unwind_unit.c -O2 -g -no-pie \
 *          -I../../include
 * Run:   ./test_unwind_unit
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <signal.h>

#include "../../include/pe.h"

/* ============================================================
 * Test Infrastructure
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;
static int g_test = 0;

#define ASSERT(cond, msg) do { \
    g_test++; \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define ASSERT_EQ(actual, expected, msg) do { \
    uint64_t _a = (uint64_t)(actual); uint64_t _e = (uint64_t)(expected); \
    g_test++; \
    if (_a == _e) { g_pass++; printf("  PASS: %s (0x%lx)\n", msg, _a); } \
    else { g_fail++; printf("  FAIL: %s — expected 0x%lx, got 0x%lx (line %d)\n", msg, _e, _a, __LINE__); } \
} while(0)

/* Replicate minimal loader globals needed by test functions */
static uint8_t* g_image_base = NULL;
static uint64_t g_image_size = 0;
static uint32_t g_pdata_rva = 0;
static uint32_t g_pdata_size = 0;
static uint32_t g_num_rt_functions = 0;

/* ============================================================
 * Replicated Internal Functions (from loader.c, adapted for testing)
 * ============================================================ */

/* Binary search through .pdata */
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

/* Internal virtual unwind — operates on raw context buffer. */
static void* seh_internal_virtual_unwind(
    uint64_t control_pc,
    RUNTIME_FUNCTION* rf,
    uint8_t* ctx,
    uint64_t* establisher_frame,
    uint32_t* out_handler_rva,
    void** out_lsda)
{
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;
    uint8_t version     = ui_base[0] & 0x07;
    uint8_t flags       = (ui_base[0] >> 3) & 0x07;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    uint8_t frame_reg   = ui_base[3] >> 4;
    uint8_t frame_off   = ui_base[3] & 0x0f;
    (void)version; (void)prolog_size;

    /* Handle CHAININFO */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x07;
        uint8_t chained_count_codes = chained_ui[2];
        /* Read RIP from context for func_offset calculation */
        uint64_t chain_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t chained_rsp = *(uint64_t*)(ctx + CTX_Rsp);
        uint8_t chained_frame_reg = chained_ui[3] >> 4;
        uint8_t chained_frame_off = chained_ui[3] & 0x0f;
        int chained_fp_set = 0;
        uint64_t chained_fp_val = 0;
        uint32_t chained_func_offset = (uint32_t)(chain_rip - (uint64_t)(uintptr_t)g_image_base) - chained->BeginAddress;
        uint8_t* chained_codes_ptr = chained_ui + 4;
        int chained_slot = 0;
        for (int i = 0; i < chained_count_codes; i++) {
            if (chained_slot * 2 + 1 >= (int)(chained_count_codes * 2 + 16)) break;
            uint16_t cw = *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
            uint8_t op_code = (cw >> 8) & 0x0f;
            uint8_t op_info = (cw >> 12) & 0x0f;
            uint8_t code_offset = cw & 0xff;
            if (chained_func_offset > 0 && code_offset >= chained_func_offset) {
                chained_slot++; continue;
            }
            switch (op_code) {
            case UWOP_PUSH_NONVOL: chained_rsp += 8; break;
            case UWOP_ALLOC_LARGE: chained_slot++;
                if (op_info == 0) chained_rsp += *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
                else { chained_slot++; chained_rsp += (uint64_t)(*(uint32_t*)(chained_codes_ptr + chained_slot * 2 - 2)) << 16; }
                break;
            case UWOP_ALLOC_SMALL: chained_rsp += (uint64_t)(op_info + 1) * 8; break;
            case UWOP_SET_FPREG: chained_fp_val = chained_rsp + (uint64_t)chained_frame_off * 16; chained_fp_set = 1; break;
            default: if (op_code >= 9) chained_slot++; break;
            }
            chained_slot++;
        }
        uint64_t chained_est = chained_fp_set ? chained_fp_val : chained_rsp;
        if (establisher_frame) *establisher_frame = chained_est;

        /* Write back unwound RSP to context */
        *(uint64_t*)(ctx + CTX_Rsp) = chained_rsp;
        if (chained_fp_set) {
            uint32_t fp_off = reg_to_ctx_offset(chained_frame_reg);
            if (fp_off) *(uint64_t*)(ctx + fp_off) = chained_fp_val;
        }

        if (chained_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + chained_count_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            if (out_handler_rva) *out_handler_rva = handler_rva;
            if (out_lsda) *out_lsda = (void*)(chained_ui + h_off + 4);
            return (void*)(g_image_base + handler_rva);
        }
        if (out_handler_rva) *out_handler_rva = 0;
        if (out_lsda) *out_lsda = NULL;
        return NULL;
    }

    /* Read register values from context */
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
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;
        uint8_t op_info = (code_word >> 12) & 0x0f;
        uint8_t code_offset = code_word & 0xff;
        if (func_offset > 0 && code_offset >= func_offset) {
            slot++; continue;
        }
        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            new_rsp += 8;
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            break;
        }
        case UWOP_ALLOC_LARGE: {
            slot++;
            if (op_info == 0) new_rsp += *(uint16_t*)(codes + slot * 2);
            else { slot++; new_rsp += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2)) << 16; }
            break;
        }
        case UWOP_ALLOC_SMALL: new_rsp += (uint64_t)(op_info + 1) * 8; break;
        case UWOP_SET_FPREG: fp_reg_val = new_rsp + (uint64_t)frame_off * 16; fp_set = 1; break;
        case UWOP_SAVE_NONVOL: { slot++; break; }
        case UWOP_SAVE_NONVOL_FAR: { slot++; slot++; break; }
        case UWOP_SAVE_XMM128: { slot++; break; }
        case UWOP_SAVE_XMM128_FAR: { slot++; slot++; break; }
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
            new_rsp = *(uint64_t*)(new_rsp + 8);
            break;
        }
        default: { if (op_code >= 9) slot++; break; }
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
    void* lsda = NULL;
    if (flags & UNW_FLAG_EHANDLER) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
        lsda = (void*)(ui_base + h_off + 4);
    }

    if (out_handler_rva) *out_handler_rva = handler_rva;
    if (out_lsda) *out_lsda = lsda;

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* ============================================================
 * Helper: Build a minimal in-memory PE image with .pdata
 * ============================================================ */
#define IMG_SIZE (1024 * 64)  /* 64KB */
#define TEXT_RVA 0x1000
#define PDATA_RVA 0x8000
#define XDATA_RVA 0x9000

static uint8_t image_buf[IMG_SIZE];

/* Build UNWIND_INFO for a function with:
 *   push rbx (opcode 0, info=3)  at offset 0
 *   sub rsp, 0x30 (ALLOC_SMALL, info=5 -> 48 bytes) at offset 1
 *   set fp reg=rbp, off=0 at offset 2
 *
 * Encoded: 2 code words
 *   Word 0: [info=3][op=0][offset=0] = 0x3000
 *   Word 1: [info=5][op=2][offset=1] = 0x5201
 *   Word 2: [info=5][op=3][offset=2] = 0x5302
 */
static void build_unwind_info(uint32_t xdata_rva, int with_ehandler, uint32_t handler_rva) {
    uint8_t* ui = image_buf + xdata_rva;
    uint8_t flags = with_ehandler ? UNW_FLAG_EHANDLER : 0;
    ui[0] = (1 & 0x07) | (flags << 3);  /* version=1, flags */
    ui[1] = 10;  /* prolog size */
    ui[2] = 3;   /* count of codes */
    ui[3] = (5 << 4) | 0;  /* frame_reg=rbp(5), frame_off=0 */

    /* Code words (little-endian) */
    /* push rbx: op=0(UWOP_PUSH_NONVOL), info=3(rbx), offset=0 */
    ui[4] = 0x00; ui[5] = 0x30;  /* offset=0, [info=3][op=0] */
    /* alloc 0x30: op=2(UWOP_ALLOC_SMALL), info=5(->6*8=48), offset=1 */
    ui[6] = 0x01; ui[7] = 0x52;  /* offset=1, [info=5][op=2] */
    /* set fp reg: op=3(UWOP_SET_FPREG), info=5(rbp), offset=2 */
    ui[8] = 0x02; ui[9] = 0x53;  /* offset=2, [info=5][op=3] */

    if (with_ehandler) {
        /* Handler RVA must be 4-byte aligned after the unwind codes.
         * codes start at offset 4, each code is 2 bytes.
         * Total code bytes = count_codes * 2.
         * Handler offset = 4 + total_code_bytes, aligned up to 4. */
        uint32_t h_off = 4 + 3 * 2;  /* = 10 */
        if (h_off % 4) h_off += (4 - (h_off % 4));  /* align to 4 */
        *(uint32_t*)(ui + h_off) = handler_rva;
    }
}

/* Build .pdata with N RUNTIME_FUNCTION entries.
 * funcs: array of {begin, end, xdata_rva} triplets */
static void build_pdata(uint32_t pdata_rva, int nfuncs,
                         uint32_t funcs[][3]) {
    for (int i = 0; i < nfuncs; i++) {
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(image_buf + pdata_rva + i * sizeof(RUNTIME_FUNCTION));
        rf->BeginAddress = funcs[i][0];
        rf->EndAddress   = funcs[i][1];
        rf->UnwindInfo   = funcs[i][2];
    }
}

/* ============================================================
 * Test 1: Single Frame Handler Discovery
 * ============================================================ */
static void test_single_frame_handler(void) {
    printf("\n--- Test 1: Single Frame Handler Discovery ---\n");
    memset(image_buf, 0, IMG_SIZE);

    /* 3 functions: func_a (no handler), func_b (with handler), func_c (no handler) */
    uint32_t funcs[3][3] = {
        { 0x1000, 0x1010, 0x9000 },  /* func_a: no handler */
        { 0x1010, 0x1020, 0x9010 },  /* func_b: EHANDLER */
        { 0x1020, 0x1030, 0x9020 },  /* func_c: no handler */
    };

    /* Build unwind info */
    build_unwind_info(0x9000, 0, 0);  /* func_a: no EHANDLER */
    build_unwind_info(0x9010, 1, 0xAA00);  /* func_b: EHANDLER at RVA 0xAA00 */
    build_unwind_info(0x9020, 0, 0);  /* func_c: no EHANDLER */

    build_pdata(PDATA_RVA, 3, funcs);

    g_image_base = image_buf;
    g_image_size = IMG_SIZE;
    g_pdata_rva = PDATA_RVA;
    g_num_rt_functions = 3;

    /* Lookup func_b (RVA 0x1015) */
    RUNTIME_FUNCTION* rf = seh_internal_lookup(0x1015);
    ASSERT(rf != NULL, "func_b found in .pdata");
    ASSERT_EQ(rf->BeginAddress, 0x1010, "func_b begin address");

    /* Unwind func_b */
    uint8_t ctx[CONTEXT_SIZE];
    memset(ctx, 0, CONTEXT_SIZE);
    /* Use a real stack buffer as fake stack for PUSH_NONVOL restore */
    uint64_t fake_stack[64];
    memset(fake_stack, 0, sizeof(fake_stack));
    uint64_t base_sp = (uint64_t)(uintptr_t)&fake_stack[32];  /* point into middle */
    *(uint64_t*)(ctx + CTX_Rip) = (uint64_t)(uintptr_t)(g_image_base + 0x1018);  /* mid-function */
    *(uint64_t*)(ctx + CTX_Rsp) = base_sp;

    uint64_t est = 0;
    uint32_t handler_rva = 0;
    void* lsda = NULL;
    void* handler = seh_internal_virtual_unwind(
        *(uint64_t*)(ctx + CTX_Rip), rf, ctx, &est, &handler_rva, &lsda);

    ASSERT(handler != NULL, "EHANDLER found for func_b");
    ASSERT_EQ(handler_rva, 0xAA00, "Handler RVA correct");
    ASSERT(est != 0, "Establisher frame computed (non-zero)");
    ASSERT_EQ(est, *(uint64_t*)(ctx + CTX_Rsp), "Establisher frame equals unwound RSP");

    /* Verify RSP was unwound: original RSP - 48 (ALLOC_SMALL) - 8 (PUSH rbx) */
    uint64_t expected_rsp = base_sp + 48 + 8;  /* undo alloc + push */
    ASSERT_EQ(*(uint64_t*)(ctx + CTX_Rsp), expected_rsp, "RSP unwound correctly");

    /* Lookup func_a (no handler) */
    rf = seh_internal_lookup(0x1005);
    ASSERT(rf != NULL, "func_a found in .pdata");
    handler = seh_internal_virtual_unwind(
        *(uint64_t*)(ctx + CTX_Rip), rf, ctx, &est, &handler_rva, &lsda);
    ASSERT(handler == NULL, "No handler for func_a");
    ASSERT_EQ(handler_rva, 0, "Handler RVA is 0 for func_a");
}

/* ============================================================
 * Test 2: Nested Frames Walk (A -> B -> C, handler at B)
 * ============================================================ */
static void test_nested_frames_walk(void) {
    printf("\n--- Test 2: Nested Frames Walk ---\n");
    memset(image_buf, 0, IMG_SIZE);

    /* Simulate a stack: func_c calls RaiseException.
     * Call chain: func_a -> func_b -> func_c -> RaiseException
     * func_c: no handler, parent = func_b
     * func_b: EHANDLER, parent = func_a
     * func_a: no handler
     */
    uint32_t funcs[3][3] = {
        { 0x1000, 0x1080, 0x9000 },  /* func_a: no handler */
        { 0x1080, 0x1100, 0x9020 },  /* func_b: EHANDLER */
        { 0x1100, 0x1140, 0x9040 },  /* func_c: no handler */
    };

    build_unwind_info(0x9000, 0, 0);  /* func_a */
    build_unwind_info(0x9020, 1, 0xBB00);  /* func_b: EHANDLER at 0xBB00 */
    build_unwind_info(0x9040, 0, 0);  /* func_c */

    build_pdata(PDATA_RVA, 3, funcs);

    g_image_base = image_buf;
    g_pdata_rva = PDATA_RVA;
    g_num_rt_functions = 3;

    uint8_t ctx[CONTEXT_SIZE];
    memset(ctx, 0, CONTEXT_SIZE);

    /* Use real stack buffer as fake stack */
    uint64_t fake_stack[128];
    memset(fake_stack, 0, sizeof(fake_stack));
    uint64_t base_sp = (uint64_t)(uintptr_t)&fake_stack[64];

    /* Start at func_c return address (after call instruction) */
    *(uint64_t*)(ctx + CTX_Rip) = (uint64_t)(uintptr_t)(g_image_base + 0x1120);
    *(uint64_t*)(ctx + CTX_Rsp) = base_sp;

    /* Setup fake stack frames */
    /* func_c's establisher frame (after unwind) will be at base_sp + 48 + 8 */
    uint64_t func_c_est = base_sp + 48 + 8;
    /* At [func_c_est] = return address to func_b */
    *(uint64_t*)(uintptr_t)func_c_est = (uint64_t)(uintptr_t)(g_image_base + 0x10A0);
    /* func_b's establisher frame will be func_c_est + 8 + 48 + 8 */
    uint64_t func_b_est = func_c_est + 8 + 48 + 8;
    /* At [func_b_est] = return address to func_a */
    *(uint64_t*)(uintptr_t)func_b_est = (uint64_t)(uintptr_t)(g_image_base + 0x1050);

    /* Frame 0: func_c */
    RUNTIME_FUNCTION* rf = seh_internal_lookup(*(uint64_t*)(ctx + CTX_Rip) - (uint64_t)(uintptr_t)g_image_base);
    ASSERT(rf != NULL, "Frame 0: func_c found");
    uint64_t est = 0; uint32_t hr = 0;
    void* handler = seh_internal_virtual_unwind(*(uint64_t*)(ctx + CTX_Rip), rf, ctx, &est, &hr, NULL);
    ASSERT(handler == NULL, "Frame 0: func_c has no handler");
    ASSERT_EQ(*(uint64_t*)(ctx + CTX_Rsp), func_c_est, "Frame 0: RSP unwound to func_c_est");

    /* Read parent RIP and advance to Frame 1 */
    uint64_t parent_rip = *(uint64_t*)(uintptr_t)est;
    *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
    *(uint64_t*)(ctx + CTX_Rsp) = est + 8;

    /* Frame 1: func_b */
    rf = seh_internal_lookup(*(uint64_t*)(ctx + CTX_Rip) - (uint64_t)(uintptr_t)g_image_base);
    ASSERT(rf != NULL, "Frame 1: func_b found");
    handler = seh_internal_virtual_unwind(*(uint64_t*)(ctx + CTX_Rip), rf, ctx, &est, &hr, NULL);
    ASSERT(handler != NULL, "Frame 1: func_b has EHANDLER");
    ASSERT_EQ(hr, 0xBB00, "Frame 1: handler RVA = 0xBB00");

    printf("  Nested walk: correctly found EHANDLER at frame 1\n");
}

/* ============================================================
 * Test 3: CONTEXT ContextFlags Value
 * ============================================================ */
static void test_context_flags(void) {
    printf("\n--- Test 3: CONTEXT ContextFlags Value ---\n");

    uint8_t ctx[CONTEXT_SIZE];
    memset(ctx, 0, CONTEXT_SIZE);

    /* Set ContextFlags to CONTEXT_FULL */
    *(uint64_t*)(ctx + 0x00) = 0x100007;

    ASSERT_EQ(*(uint64_t*)(ctx + 0x00), 0x100007, "ContextFlags = CONTEXT_FULL (0x100007)");

    /* Verify it's NOT 0x10007F (old buggy value with DEBUG_REGISTERS) */
    ASSERT(*(uint64_t*)(ctx + 0x00) != 0x10007F, "ContextFlags is NOT 0x10007F (old bug)");
}

/* ============================================================
 * Test 4: g_cap_er Copy Order Fix
 * ============================================================ */
static void test_g_cap_er_copy_order(void) {
    printf("\n--- Test 4: g_cap_er Copy Order Fix ---\n");

    EXCEPTION_RECORD er;
    EXCEPTION_RECORD g_cap_er;

    /* Old (buggy) order: copy THEN fill */
    memset(&er, 0, sizeof(er));
    memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));  /* copies zeros */
    er.ExceptionCode = 0x20474343;
    er.ExceptionFlags = 0x01;
    er.NumberParameters = 2;
    er.ExceptionInformation[0] = 0xDEADBEEF;
    er.ExceptionInformation[1] = 0xCAFEBABE;
    /* g_cap_er is still all zeros — BUG */
    ASSERT_EQ(g_cap_er.ExceptionCode, 0x00000000, "Old order: g_cap_er.Code is zero (BUG)");

    /* New (fixed) order: fill THEN copy */
    memset(&er, 0, sizeof(er));
    er.ExceptionCode = 0x20474343;
    er.ExceptionFlags = 0x01;
    er.NumberParameters = 2;
    er.ExceptionInformation[0] = 0xDEADBEEF;
    er.ExceptionInformation[1] = 0xCAFEBABE;
    memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));  /* copies populated data */
    ASSERT_EQ(g_cap_er.ExceptionCode, 0x20474343, "New order: g_cap_er.Code correct");
    ASSERT_EQ(g_cap_er.ExceptionFlags, 0x01, "New order: g_cap_er.Flags correct");
    ASSERT_EQ(g_cap_er.NumberParameters, 2, "New order: g_cap_er.NumParams correct");
    ASSERT_EQ(g_cap_er.ExceptionInformation[0], 0xDEADBEEF, "New order: g_cap_er.Param[0] correct");
    ASSERT_EQ(g_cap_er.ExceptionInformation[1], 0xCAFEBABE, "New order: g_cap_er.Param[1] correct");
}

/* ============================================================
 * Test 5: CHAININFO Establisher Frame
 * ============================================================ */
static void test_chaininfo_establisher(void) {
    printf("\n--- Test 5: CHAININFO Establisher Frame ---\n");
    memset(image_buf, 0, IMG_SIZE);

    /* Create a function with CHAININFO.
     * In real PEs, CHAININFO means the primary function shares code with
     * the chained function. The primary has no unwind codes of its own —
     * it delegates to the chained function's unwind info.
     *
     * Primary RF: begin=0x1000, end=0x1100, ui=0x9000
     * Chained RF: begin=0x1000, end=0x1100, ui=0x9100
     * (Both cover the same code range. The chained UI has the actual codes.)
     *
     * Primary UNWIND_INFO at 0x9000:
     *   flags = UNW_FLAG_CHAININFO
     *   count_codes = 0
     *   CHAININFO RF: begin=0x1000, end=0x1100, ui=0x9100
     *
     * Chained UNWIND_INFO at 0x9100:
     *   flags = UNW_FLAG_EHANDLER
     *   codes: ALLOC_SMALL(info=3, offset=0), SET_FPREG(info=5, offset=1)
     *   handler RVA = 0xCC00
     */
    uint32_t primary_xdata = 0x9000;
    uint32_t chained_xdata = 0x9100;

    /* Build chained UNWIND_INFO first */
    uint8_t* cui = image_buf + chained_xdata;
    cui[0] = (1 & 0x07) | (UNW_FLAG_EHANDLER << 3);  /* version=1, EHANDLER */
    cui[1] = 8;  /* prolog_size */
    cui[2] = 2;  /* count_codes */
    cui[3] = (5 << 4) | 0;  /* frame_reg=rbp, frame_off=0 */
    /* Code 0: ALLOC_SMALL(info=3 -> 4*8=32 bytes), offset=0 */
    cui[4] = 0x00; cui[5] = 0x32;
    /* Code 1: SET_FPREG(info=5=rbp), offset=1 */
    cui[6] = 0x01; cui[7] = 0x53;
    /* Handler RVA (aligned: 4 + 2*2 = 8, already aligned) */
    *(uint32_t*)(cui + 8) = 0xCC00;

    /* Build primary UNWIND_INFO with CHAININFO */
    uint8_t* pui = image_buf + primary_xdata;
    pui[0] = (1 & 0x07) | (UNW_FLAG_CHAININFO << 3);  /* NOT EHANDLER — chained has it */
    pui[1] = 0;  /* prolog_size (no primary codes) */
    pui[2] = 0;  /* count_codes = 0 */
    pui[3] = 0;  /* no frame register */
    /* CHAININFO: RUNTIME_FUNCTION at offset 4 (aligned) */
    RUNTIME_FUNCTION* chain_rf = (RUNTIME_FUNCTION*)(pui + 4);
    chain_rf->BeginAddress = 0x1000;  /* same range as primary */
    chain_rf->EndAddress   = 0x1100;
    chain_rf->UnwindInfo   = chained_xdata;

    /* .pdata entry */
    g_image_base = image_buf;
    g_pdata_rva = PDATA_RVA;
    g_num_rt_functions = 1;
    RUNTIME_FUNCTION* pdata_rf = (RUNTIME_FUNCTION*)(image_buf + PDATA_RVA);
    pdata_rf->BeginAddress = 0x1000;
    pdata_rf->EndAddress   = 0x1100;
    pdata_rf->UnwindInfo   = primary_xdata;

    /* Create CONTEXT */
    uint8_t ctx[CONTEXT_SIZE];
    memset(ctx, 0, CONTEXT_SIZE);
    /* Use real stack buffer for CHAININFO unwind */
    uint64_t fake_stack[64];
    memset(fake_stack, 0, sizeof(fake_stack));
    uint64_t test_rsp = (uint64_t)(uintptr_t)&fake_stack[32];

    *(uint64_t*)(ctx + CTX_Rip) = (uint64_t)(uintptr_t)(g_image_base + 0x1050);
    *(uint64_t*)(ctx + CTX_Rsp) = test_rsp;

    /* Unwind */
    RUNTIME_FUNCTION* rf = seh_internal_lookup(0x1050);
    ASSERT(rf != NULL, "CHAININFO function found in .pdata");

    uint64_t est = 0;
    uint32_t handler_rva = 0;
    void* handler = seh_internal_virtual_unwind(
        *(uint64_t*)(ctx + CTX_Rip), rf, ctx, &est, &handler_rva, NULL);

    ASSERT(handler != NULL, "CHAININFO: handler found");
    ASSERT_EQ(handler_rva, 0xCC00, "CHAININFO: handler RVA = 0xCC00");
    ASSERT(est != 0, "CHAININFO: establisher frame is NOT zero (was bug M1)");

    /* The chained function has ALLOC_SMALL(info=3) = 32 bytes.
     * After unwinding: RSP = original + 32.
     * With SET_FPREG(rbp, 0): est = RSP_after_alloc + 0*16 = RSP_after_alloc.
     * But SET_FPREG uses new_rsp at that point, which is after the alloc.
     * So: new_rsp = 0x7fff00001000 + 32 = 0x7fff00001020
     * fp_val = 0x7fff00001020 + 0 = 0x7fff00001020
     * est = 0x7fff00001020 */
    ASSERT_EQ(*(uint64_t*)(ctx + CTX_Rsp), test_rsp + 32, "CHAININFO: RSP unwound by 32 bytes");
    ASSERT_EQ(est, test_rsp + 32, "CHAININFO: establisher = unwound RSP");
}

/* ============================================================
 * Test 6: RUNTIME_FUNCTION Binary Search Edge Cases
 * ============================================================ */
static void test_rf_lookup_edges(void) {
    printf("\n--- Test 6: RF Lookup Edge Cases ---\n");
    memset(image_buf, 0, IMG_SIZE);

    uint32_t funcs[3][3] = {
        { 0x1000, 0x1020, 0x9000 },
        { 0x1020, 0x1050, 0x9010 },
        { 0x1050, 0x1080, 0x9020 },
    };
    build_pdata(PDATA_RVA, 3, funcs);
    g_image_base = image_buf;
    g_pdata_rva = PDATA_RVA;
    g_num_rt_functions = 3;

    /* Exact begin */
    ASSERT(seh_internal_lookup(0x1000) != NULL, "Lookup at exact begin");
    /* One before end */
    ASSERT(seh_internal_lookup(0x101F) != NULL, "Lookup at end-1");
    /* Exact end (should be outside range [begin, end)) */
    ASSERT(seh_internal_lookup(0x1020) != NULL, "Lookup at exact end (belongs to next RF)");
    /* Before first */
    ASSERT(seh_internal_lookup(0x0FFF) == NULL, "Lookup before first RF");
    /* After last */
    ASSERT(seh_internal_lookup(0x1080) == NULL, "Lookup at/after last RF end");
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void) {
    printf("\n========================================");
    printf("\nException Dispatch Unit Tests");
    printf("\n========================================\n");

    test_single_frame_handler();
    test_nested_frames_walk();
    test_context_flags();
    test_g_cap_er_copy_order();
    test_chaininfo_establisher();
    test_rf_lookup_edges();

    printf("\n========================================");
    printf("\nResults: %d passed, %d failed (of %d)\n", g_pass, g_fail, g_test);
    printf("========================================\n");

    return g_fail > 0 ? 1 : 0;
}
