#!/usr/bin/env python3
"""BUG-001 fix v4: Correct UNWIND_INFO flags extraction."""
LOADER = '/home/z/my-project/minwin/src/loader.c'
with open(LOADER) as f: c = f.read()
print(f'Input: {c.count(chr(10))} lines')

# 1. Add ucontext.h
c = c.replace('#include <dlfcn.h>\n', '#include <dlfcn.h>\n#include <ucontext.h>\n', 1)
print('1. Added ucontext.h')

# 2. CONTEXT block before Trampoline
TMARKER = '/* ============================================================\n * Trampoline Generation (Windows \u2192 System V ABI)\n * ============================================================ */\n'
CTX = '''\n\n/* ============================================================\n * Windows x64 CONTEXT + .pdata for SEH unwinding\n * ============================================================ */\n
#define CONTEXT_AMD64   0x00100000\n#define CONTEXT_CONTROL  (CONTEXT_AMD64 | 0x00000001)\n#define CONTEXT_INTEGER  (CONTEXT_AMD64 | 0x00000002)\n#define CONTEXT_FULL     (CONTEXT_CONTROL | CONTEXT_INTEGER)\n
typedef struct {\n    uint64_t P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;\n    uint32_t ContextFlags, MxCsr;\n    uint16_t SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;\n    uint32_t EFlags;\n    uint64_t Rip, Rsp, Rax, Rcx, Rdx, Rbx, Rbp, Rsi, Rdi;\n    uint64_t R8, R9, R10, R11, R12, R13, R14, R15;\n    uint8_t  FltSave[512];\n    uint8_t  VectorRegs[256];\n    uint64_t VectorControl, DebugControl;\n    uint64_t LastBranchToRip, LastBranchFromRip;\n    uint64_t LastExceptionToRip, LastExceptionFromRip;\n} WinContext;\n
typedef struct { uint32_t BeginAddress; uint32_t EndAddress; uint32_t UnwindData; } RUNTIME_FUNCTION;\n\nstatic uint8_t*  g_pdata = NULL;\nstatic uint32_t  g_pdata_size = 0;\nstatic uint32_t  g_pdata_count = 0;\n\nstatic void capture_context(WinContext* ctx) {\n    ucontext_t uc;\n    getcontext(&uc);\n    memset(ctx, 0, sizeof(WinContext));\n    ctx->ContextFlags = CONTEXT_FULL;\n    ctx->MxCsr = 0x1F80;\n    ctx->SegCs = 0x33; ctx->SegSs = 0x2B;\n    ctx->EFlags = uc.uc_mcontext.gregs[REG_EFL];\n    ctx->Rip = uc.uc_mcontext.gregs[REG_RIP];\n    ctx->Rsp = uc.uc_mcontext.gregs[REG_RSP];\n    ctx->Rax = uc.uc_mcontext.gregs[REG_RAX];\n    ctx->Rcx = uc.uc_mcontext.gregs[REG_RCX];\n    ctx->Rdx = uc.uc_mcontext.gregs[REG_RDX];\n    ctx->Rbx = uc.uc_mcontext.gregs[REG_RBX];\n    ctx->Rbp = uc.uc_mcontext.gregs[REG_RBP];\n    ctx->Rsi = uc.uc_mcontext.gregs[REG_RSI];\n    ctx->Rdi = uc.uc_mcontext.gregs[REG_RDI];\n    ctx->R8  = uc.uc_mcontext.gregs[REG_R8];\n    ctx->R9  = uc.uc_mcontext.gregs[REG_R9];\n    ctx->R10 = uc.uc_mcontext.gregs[REG_R10];\n    ctx->R11 = uc.uc_mcontext.gregs[REG_R11];\n    ctx->R12 = uc.uc_mcontext.gregs[REG_R12];\n    ctx->R13 = uc.uc_mcontext.gregs[REG_R13];\n    ctx->R14 = uc.uc_mcontext.gregs[REG_R14];\n    ctx->R15 = uc.uc_mcontext.gregs[REG_R15];\n}\n\n''
c = c.replace(TMARKER, CTX + TMARKER, 1)
print('2. Inserted CONTEXT block')

# 3. Forward declarations before RaiseException
OLD_RE = '__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,\n    uint32_t nargs, uint64_t* args) {'
FWD = '/* Forward decls */\n__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t, void*, void*);\n__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t, uint64_t, void*, void*, void*, void*);\n\n'
assert OLD_RE in c, 'Cannot find RaiseException'
c = c.replace(OLD_RE, FWD + OLD_RE, 1)
print('3. Added forward declarations')

# 4. Replace RtlLookupFunctionEntry
OLD_RLE = '__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {\n    MW_TRACE("RtlLookupFunctionEntry(addr=0x%lx)", addr);\n    return NULL;\n}'
NEW_RLE = '''__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {\n    (void)base; (void)history;\n    if (!g_pdata || g_pdata_count == 0) return NULL;\n    uint64_t rva = addr - (uint64_t)(uintptr_t)g_image_base;\n    uint32_t lo = 0, hi = g_pdata_count;\n    while (lo < hi) {\n        uint32_t mid = (lo + hi) / 2;\n        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(g_pdata + mid * 12);\n        if (rva < rf->BeginAddress) hi = mid;\n        else if (rva >= rf->EndAddress) lo = mid + 1;\n        else {\n            MW_TRACE("RtlLookupFunctionEntry(0x%lx) = .pdata[%u]", addr, mid);\n            return (void*)rf;\n        }\n    }\n    return NULL;\n}\n\n''
assert OLD_RLE in c, 'Cannot find RtlLookupFunctionEntry'
c = c.replace(OLD_RLE, NEW_RLE, 1)
print('4. Replaced RtlLookupFunctionEntry')

# 5. Replace RtlVirtualUnwind
OLD_RVU = '__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info,\n    void* ctx, void* data, void* disp) {\n    MW_TRACE("RtlVirtualUnwind()");\n    return 0;\n}'
NEW_RVU = '''/* Unwind opcodes */
#define UWOP_PUSH_NONVOL    0
#define UWOP_ALLOC_LARGE    1
#define UWOP_ALLOC_SMALL    2
#define UWOP_SET_FPREG      3
#define UWOP_SAVE_NONVOL    4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_PUSH_MACHFRAME  10
#define UNW_FLAG_EHANDLER   0x01
#define UNW_FLAG_UHANDLER   0x02

__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info_ptr,\n    void* ctx_ptr, void* history, void* disp) {
    (void)code; (void)history; (void)disp;
    if (!info_ptr || !ctx_ptr || !g_image_base) return 0;
    RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)info_ptr;
    WinContext* ctx = (WinContext*)ctx_ptr;
    uint8_t* ui = g_image_base + rf->UnwindData;
    uint8_t num_codes = ui[2];
    /* CORRECT: flags are bits 3-4, not 0-4 */
    uint8_t unwind_flags = (ui[0] >> 3) & 0x03;\n    uint8_t frame_reg = 0, frame_offset = 0;
    if (ui[0] & 0x20) { frame_reg = ui[3] & 0x0F; frame_offset = (ui[3] >> 4) * 16; }
    int codes_offset = 4;
    uint64_t rsp = ctx->Rsp;
    for (int i = num_codes - 1; i >= 0; i--) {\n        uint16_t uc_entry = *(uint16_t*)(ui + codes_offset + i * 2);\n        uint8_t op = uc_entry & 0x0F;\n        uint8_t opinfo = (uc_entry >> 4) & 0x0F;\n        switch (op) {\n        case UWOP_PUSH_NONVOL: {\n            uint64_t val = 0;\n            if (rsp >= 0x1000) val = *(uint64_t*)(uintptr_t)rsp;\n            uint64_t* slot = (uint64_t*)((uint8_t*)ctx + 0x58 + opinfo * 8);\n            *slot = val; rsp += 8; break;\n        }\n        case UWOP_ALLOC_SMALL: rsp += (uint64_t)(opinfo + 1) * 8; break;\n        case UWOP_ALLOC_LARGE:\n            if (opinfo == 0) {\n                uint16_t slots = *(uint16_t*)(ui + codes_offset + i * 2 + 2);\n                rsp += (uint64_t)slots * 8; i--;\n            } else {\n                uint32_t big = *(uint32_t*)(ui + codes_offset + i * 2 + 2);\n                rsp += big; i -= 2;\n            } break;\n        case UWOP_SET_FPREG: break;\n        case UWOP_SAVE_NONVOL: case UWOP_SAVE_NONVOL_FAR: break;\n        case UWOP_PUSH_MACHFRAME: rsp += 32; break;\n        default: break;\n        }\n    }\n    if (ui[0] & 0x20) {\n        uint64_t fv = *(uint64_t*)((uint8_t*)ctx + 0x58 + frame_reg * 8);\n        rsp = fv + (uint64_t)frame_offset;\n    }\n    ctx->Rsp = rsp;\n    ctx->Rip = (uint64_t)rf->BeginAddress;\n    if (unwind_flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {\n        int ho = codes_offset + num_codes * 2;\n        if (ho % 4) ho += 2;\n        uint32_t hr = *(uint32_t*)(ui + ho);\n        MW_TRACE("RtlVirtualUnwind: handler RVA 0x%x", hr);\n        return (int)hr;\n    }\n    return 0;\n}\n\n''
assert OLD_RVU in c, 'Cannot find RtlVirtualUnwind'
c = c.replace(OLD_RVU, NEW_RVU, 1)
print('5. Replaced RtlVirtualUnwind')

# 6. Replace RaiseException
OLD_RE_START = '__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,\n    uint32_t nargs, uint64_t* args) {'
OLD_RE_END = '    MW_TRACE("  Exception unhandled");\n\n'
assert OLD_RE_START in c, 'Cannot find RE start'
assert OLD_RE_END in c, 'Cannot find RE end'
iS = c.index(OLD_RE_START)
iE = c.index(OLD_RE_END) + len(OLD_RE_END)

NEW_RE = '''__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,\n    uint32_t nargs, uint64_t* args) {\n    MW_TRACE("RaiseException(code=0x%x, flags=0x%x, nargs=%u)", code, flags, nargs);\n\n    WinContext win_ctx;\n    capture_context(&win_ctx);\n    win_ctx.Rip = (uint64_t)(uintptr_t)__builtin_return_address(0);\n\n    uint8_t er_buf[0x100];\n    memset(er_buf, 0, sizeof(er_buf));\n    *(uint32_t*)(er_buf + 0x00) = code;\n    *(uint32_t*)(er_buf + 0x04) = flags;\n    *(uint64_t*)(er_buf + 0x10) = win_ctx.Rip;\n    *(uint32_t*)(er_buf + 0x18) = nargs;\n    if (nargs > 0 && args) memcpy(er_buf + 0x20, args, nargs * 8);\n\n    uint64_t ep[2] = { (uint64_t)(uintptr_t)er_buf, (uint64_t)(uintptr_t)&win_ctx };\n\n    for (int i = 0; i < g_veh_count; i++) {\n        if (g_veh_handlers[i]) {\n            typedef long (*veh_fn)(uint64_t*);\n            long result = ((veh_fn)g_veh_handlers[i])(ep);\n            MW_TRACE("  VEH %p returned %ld", g_veh_handlers[i], result);\n            if (result == -1) return;\n        }\n    }\n\n    uint64_t search_addr = win_ctx.Rip;\n    for (int frame = 0; frame < 64; frame++) {\n        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)mw_RtlLookupFunctionEntry(search_addr, NULL, NULL);\n        if (!rf) break;\n\n        /* CORRECT: flags are bits 3-4 of byte 0 */\n        uint8_t uf = (ui[0] >> 3) & 0x03;\n        uint8_t nc = ui[2;\n\n        if (uf & UNW_FLAG_EHANDLER) {\n            int ho = 4 + nc * 2;\n            if (ho % 4) ho += 2;\n            uint32_t handler_rva = *(uint32_t*)(ui + ho);\n            MW_TRACE("  Frame %d: handler RVA 0x%x", frame, handler_rva);\n            win_ctx.Rip = search_addr;\n            typedef long (*hfn)(uint8_t*, void*, WinContext*, void*);\n            long result = ((hfn)(g_image_base + handler_rva))(er_buf, NULL, &win_ctx, NULL);\n            MW_TRACE("  Handler returned %ld", result);\n            if (result == -1) return;\n            if (result == 1) break;\n        }\n\n        mw_RtlVirtualUnwind(0, search_addr, rf, &win_ctx, NULL, NULL);\n        if (win_ctx.Rip == search_addr) break;\n        search_addr = win_ctx.Rip;\n        MW_TRACE("  Unwound to 0x%lx", search_addr);\n    }\n\n    if (g_unhandled_exception_filter) {\n        typedef long (*uef_fn)(uint64_t*);\n        long result = ((uef_fn)g_unhandled_exception_filter)(ep);\n        MW_TRACE("  UEF returned %ld", result);\n        if (result == -1) return;\n    }\n    MW_TRACE("  Exception unhandled (code 0x%x)", code);\n}\n\n''
c = c[:iS] + NEW_RE + c[iE:]
print('6. Replaced RaiseException')

# 7. .pdata init
OLD_TRACE = '    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",\n' 
c = c.replace(OLD_TRACE, '''    /* Record .pdata for exception handling */\n    {\n        uint32_t ex_rva = dd[3].VirtualAddress;\n        uint32_t ex_sz = dd[3].Size;\n        if (ex_rva && ex_sz) {\n            g_pdata = g_image_base + ex_rva;\n            g_pdata_size = ex_sz;\n            g_pdata_count = ex_sz / 12;\n            MW_TRACE("Exception table: %u entries at RVA 0x%x", g_pdata_count, ex_rva);\n        }\n    }\n    ''' + OLD_TRACE, 1)
print('7. Added .pdata init')

# 8. Fix truncated main() - add missing code after load_pe error check
OLD_END = '''    if (load_pe(exe_path) != 0) {\n        fprintf(stderr, "[ERROR] Failed to load PE\\n");\n        return 1;\n    }\n'''
NEW_END = '''    if (load_pe(exe_path) != 0) {\n        fprintf(stderr, "[ERROR] Failed to load PE\\n");\n        return 1;\n    }\n\n    setup_teb_peb();\n\n    MW_TRACE("Executing entry point at 0x%lx", (unsigned long)g_entry_point);\n    printf("[MiniWin] Executing...\\n");\n    fflush(stdout);\n\n    {\n        typedef int (*ep_fn)(void);\n        ep_fn ep = (ep_fn)(g_image_base + g_entry_point);\n        int ret = ep();\n        MW_TRACE("Entry point returned %d", ret);\n        (void)ret;\n    }\n\n    if (g_trace_log) {\n        fprintf(g_trace_log, "  ]\\n}\\n");\n        fclose(g_trace_log);\n        g_trace_log = NULL;\n    }\n\n    printf("[MiniWin] Execution complete.\\n");\n    return 0;\n}\n''
c = c.replace(OLD_END, NEW_END, 1)
print('8. Fixed main()')

with open(LOADER, 'w') as f: f.write(c)
print(f'Output: {c.count(chr(10))} lines')
