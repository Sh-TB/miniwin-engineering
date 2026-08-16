#!/usr/bin/env python3
"""BUG-001 fix v3: Insert all changes cleanly."""

LOADER = '/home/z/my-project/minwin/src/loader.c'
with open(LOADER, 'r') as f:
    content = f.read()

# 1. Add ucontext.h
content = content.replace(
    '#include <dlfcn.h>\n',
    '#include <dlfcn.h>\n#include <ucontext.h>\n', 1)

# 2. Insert CONTEXT block BEFORE the Trampoline section comment.
#    Match the exact 3-line comment block and insert before it.
TRAMPOLINE_MARKER = '/** \n * ============================================================\n * Trampoline Generation (Windows → System V ABI)\n * ============================================================ */\n'

# Use the section comment as anchor - it's unique
if TRAMPOLINE_MARKER in content:
    content = content.replace(TRAMPOLINE_MARKER, CONTEXT_BLOCK + TRAMPOLINE_MARKER, 1)
    print("2. Inserted CONTEXT block")
else:
    # Try finding just the key line
    alt = ' * Trampoline Generation (Windows → System V ABI)\n'
    if alt in content:
        # Find the /* === line before it
        idx = content.index(alt)
        # Go back to find the /* line
        bol = content.rfind('\n/*', 0, idx)
        if bol >= 0:
            bol += 1  # past the \n
        print(f"2. Inserting CONTEXT before Trampoline at offset {bol}")
else:
    print("2. WARNING: Could not find Trampoline marker!")

# 3. Add forward declarations before SEH stubs
content = content.replace(
    '/* SEH/Exception stubs */\n',
    '/* Forward decls for EH */\n'
    '__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t, void*, void*);\n'
    '__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t, uint64_t, void*, void*, void*, void*);\n\n'
    '/* SEH/Exception stubs */\n', 1)
print("3. Added forward declarations")

# 4. Replace RtlLookupFunctionEntry
OLD_RLE = '''__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {
    MW_TRACE("RtlLookupFunctionEntry(addr=0x%lx)", addr);
    return NULL;
}'''

assert OLD_RLE in content, "4. Cannot find RtlLookupFunctionEntry"
content = content.replace(OLD_RLE, RTLLOOKUP_BLOCK, 1)
print("4. Replaced RtlLookupFunctionEntry")

# 5. Replace RtlVirtualUnwind
OLD_RVU = '''__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info,
    void* ctx, void* data, void* disp) {
    MW_TRACE("RtlVirtualUnwind()");
    return 0;
}'''

assert OLD_RVU in content, "5. Cannot find RtlVirtualUnwind"
content = content.replace(OLD_RVU, RTLUNWIND_BLOCK, 1)
print("5. Replaced RtlVirtualUnwind")

# 6. Replace RaiseException - use unique start+end markers
RE_START = '__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,\n    uint32_t nargs, uint64_t* args) {'
RE_END_MARKER = '    MW_TRACE("  Exception unhandled");\n}'

assert RE_START in content, "6. Cannot find RaiseException start"
assert RE_END_MARKER in content, "6. Cannot find RaiseException end"

idx_s = content.index(RE_START)
idx_e = content.index(RE_END_MARKER) + len(RE_END_MARKER)
content = content[:idx_s] + RAISEEXCEPTION_BLOCK + content[idx_e:]
print("6. Replaced RaiseException")

# 7. Add .pdata init before import resolution trace
content = content.replace(
    '    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",\n',
    PDATA_INIT_BLOCK + '    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",\n', 1)
print("7. Added .pdata init")

with open(LOADER, 'w') as f:
    f.write(content)
print("\nAll patches applied. Ready to build.")

# ============================================================
# Replacement blocks
# ============================================================

CONTEXT_BLOCK = '''
/* ============================================================
 * Windows x64 CONTEXT + .pdata for SEH unwinding
 * ============================================================ */

#define CONTEXT_AMD64   0x00100000
#define CONTEXT_CONTROL  (CONTEXT_AMD64 | 0x00000001)
#define CONTEXT_INTEGER  (CONTEXT_AMD64 | 0x00000002)
#define CONTEXT_FULL     (CONTEXT_CONTROL | CONTEXT_INTEGER)

typedef struct {
    uint64_t P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    uint32_t ContextFlags, MxCsr;
    uint16_t SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    uint32_t EFlags;
    uint64_t Rip, Rsp, Rax, Rcx, Rdx, Rbx, Rbp, Rsi, Rdi;
    uint64_t R8, R9, R10, R11, R12, R13, R14, R15;
    uint8_t  FltSave[512];
    uint8_t  VectorRegs[256];
    uint64_t VectorControl, DebugControl;
    uint64_t LastBranchToRip, LastBranchFromRip;
    uint64_t LastExceptionToRip, LastExceptionFromRip;
} WinContext;

typedef struct { uint32_t BeginAddress; uint32_t EndAddress; uint32_t UnwindData; } RUNTIME_FUNCTION;

static uint8_t*  g_pdata = NULL;
static uint32_t  g_pdata_size = 0;
static uint32_t  g_pdata_count = 0;

static void capture_context(WinContext* ctx) {
    ucontext_t uc;
    getcontext(&uc);
    memset(ctx, 0, sizeof(WinContext));
    ctx->ContextFlags = CONTEXT_FULL;
    ctx->MxCsr = 0x1F80;
    ctx->SegCs = 0x33; ctx->SegSs = 0x2B;
    ctx->EFlags = uc.uc_mcontext.gregs[REG_EFL];
    ctx->Rip = uc.uc_mcontext.gregs[REG_RIP];
    ctx->Rsp = uc.uc_mcontext.gregs[REG_RSP];
    ctx->Rax = uc.uc_mcontext.gregs[REG_RAX];
    ctx->Rcx = uc.uc_mcontext.gregs[REG_RCX];
    ctx->Rdx = uc.uc_mcontext.gregs[REG_RDX];
    ctx->Rbx = uc.uc_mcontext.gregs[REG_RBX];
    ctx->Rbp = uc.uc_mcontext.gregs[REG_RBP];
    ctx->Rsi = uc.uc_mcontext.gregs[REG_RSI];
    ctx->Rdi = uc.uc_mcontext.gregs[REG_RDI];
    ctx->R8  = uc.uc_mcontext.gregs[REG_R8];
    ctx->R9  = uc.uc_mcontext.gregs[REG_R9];
    ctx->R10 = uc.uc_mcontext.gregs[REG_R10];
    ctx->R11 = uc.uc_mcontext.gregs[REG_R11];
    ctx->R12 = uc.uc_mcontext.gregs[REG_R12];
    ctx->R13 = uc.uc_mcontext.gregs[REG_R13];
    ctx->R14 = uc.uc_mcontext.gregs[REG_R14];
    ctx->R15 = uc.uc_mcontext.gregs[REG_R15];
}

'''

RTLLOOKUP_BLOCK = '''__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {
    (void)base; (void)history;
    if (!g_pdata || g_pdata_count == 0) return NULL;
    uint64_t rva = addr - (uint64_t)(uintptr_t)g_image_base;
    uint32_t lo = 0, hi = g_pdata_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(g_pdata + mid * 12);
        if (rva < rf->BeginAddress) hi = mid;
        else if (rva >= rf->EndAddress) lo = mid + 1;
        else {
            MW_TRACE("RtlLookupFunctionEntry(0x%lx) = .pdata[%u]", addr, mid);
            return (void*)rf;
        }
    }
    return NULL;
}

'''

RTLUNWIND_BLOCK = '''/* Unwind opcodes */
#define UWOP_PUSH_NONVOL    0
#define UWOP_ALLOC_LARGE    1
#define UWOP_ALLOC_SMALL    2
#define UWOP_SET_FPREG      3
#define UWOP_SAVE_NONVOL    4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_PUSH_MACHFRAME  10
#define UNW_FLAG_EHANDLER   0x01
#define UNW_FLAG_UHANDLER   0x02

__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info_ptr,
    void* ctx_ptr, void* history, void* disp) {
    (void)code; (void)history; (void)disp;
    if (!info_ptr || !ctx_ptr || !g_image_base) return 0;
    RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)info_ptr;
    WinContext* ctx = (WinContext*)ctx_ptr;
    uint8_t* ui = g_image_base + rf->UnwindData;
    uint8_t version_flags = ui[0];
    uint8_t num_codes = ui[2];
    uint8_t unwind_flags = version_flags & 0x1F;
    uint8_t frame_reg = 0, frame_offset = 0;
    if (version_flags & 0x20) { frame_reg = ui[3] & 0x0F; frame_offset = (ui[3] >> 4) * 16; }
    int codes_offset = 4;
    uint64_t rsp = ctx->Rsp;
    for (int i = num_codes - 1; i >= 0; i--) {
        uint16_t uc_entry = *(uint16_t*)(ui + codes_offset + i * 2);
        uint8_t op = uc_entry & 0x0F;
        uint8_t opinfo = (uc_entry >> 4) & 0x0F;
        switch (op) {
        case UWOP_PUSH_NONVOL: {
            uint64_t val = 0;
            if (rsp >= 0x1000) val = *(uint64_t*)(uintptr_t)rsp;
            uint64_t* slot = (uint64_t*)((uint8_t*)ctx + 0x58 + opinfo * 8);
            *slot = val; rsp += 8; break;
        }
        case UWOP_ALLOC_SMALL: rsp += (uint64_t)(opinfo + 1) * 8; break;
        case UWOP_ALLOC_LARGE:
            if (opinfo == 0) {
                uint16_t slots = *(uint16_t*)(ui + codes_offset + i * 2 + 2);
                rsp += (uint64_t)slots * 8; i--;
            } else {
                uint32_t big = *(uint32_t*)(ui + codes_offset + i * 2 + 2);
                rsp += big; i -= 2;
            } break;
        case UWOP_SET_FPREG: break;
        case UWOP_SAVE_NONVOL: case UWOP_SAVE_NONVOL_FAR: break;
        case UWOP_PUSH_MACHFRAME: rsp += 32; break;
        default: break;
        }
    }
    if (version_flags & 0x20) {
        uint64_t fv = *(uint64_t*)((uint8_t*)ctx + 0x58 + frame_reg * 8);
        rsp = fv + (uint64_t)frame_offset;
    }
    ctx->Rsp = rsp;
    ctx->Rip = (uint64_t)rf->BeginAddress;
    if (unwind_flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {
        int ho = codes_offset + num_codes * 2;
        if (ho % 4) ho += 2;
        uint32_t hr = *(uint32_t*)(ui + ho);
        MW_TRACE("RtlVirtualUnwind: handler RVA 0x%x", hr);
        return (int)hr;
    }
    return 0;
}

'''

RAISEEXCEPTION_BLOCK = '''__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    MW_TRACE("RaiseException(code=0x%x, flags=0x%x, nargs=%u)", code, flags, nargs);

    WinContext win_ctx;
    capture_context(&win_ctx);
    win_ctx.Rip = (uint64_t)(uintptr_t)__builtin_return_address(0);

    uint8_t er_buf[0x100];
    memset(er_buf, 0, sizeof(er_buf));
    *(uint32_t*)(er_buf + 0x00) = code;
    *(uint32_t*)(er_buf + 0x04) = flags;
    *(uint64_t*)(er_buf + 0x10) = win_ctx.Rip;
    *(uint32_t*)(er_buf + 0x18) = nargs;
    if (nargs > 0 && args) memcpy(er_buf + 0x20, args, nargs * 8);

    uint64_t ep[2] = { (uint64_t)(uintptr_t)er_buf, (uint64_t)(uintptr_t)&win_ctx };

    for (int i = 0; i < g_veh_count; i++) {
        if (g_veh_handlers[i]) {
            typedef long (*veh_fn)(uint64_t*);
            long result = ((veh_fn)g_veh_handlers[i])(ep);
            MW_TRACE("  VEH %p returned %ld", g_veh_handlers[i], result);
            if (result == -1) return;
        }
    }

    uint64_t search_addr = win_ctx.Rip;
    for (int frame = 0; frame < 64; frame++) {
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)mw_RtlLookupFunctionEntry(search_addr, NULL, NULL);
        if (!rf) break;

        uint8_t* ui = g_image_base + rf->UnwindData;
        uint8_t uf = ui[0] & 0x1F;
        uint8_t nc = ui[2];

        if (uf & 0x01) {
            int ho = 4 + nc * 2;
            if (ho % 4) ho += 2;
            uint32_t handler_rva = *(uint32_t*)(ui + ho);
            MW_TRACE("  Frame %d: handler RVA 0x%x", frame, handler_rva);
            win_ctx.Rip = search_addr;
            typedef long (*hfn)(uint8_t*, void*, WinContext*, void*);
            long result = ((hfn)(g_image_base + handler_rva))(er_buf, NULL, &win_ctx, NULL);
            MW_TRACE("  Handler returned %ld", result);
            if (result == -1) return;
            if (result == 1) break;
        }

        mw_RtlVirtualUnwind(0, search_addr, rf, &win_ctx, NULL, NULL);
        if (win_ctx.Rip == search_addr) break;
        search_addr = win_ctx.Rip;
        MW_TRACE("  Unwound to 0x%lx", search_addr);
    }

    if (g_unhandled_exception_filter) {
        typedef long (*uef_fn)(uint64_t*);
        long result = ((uef_fn)g_unhandled_exception_filter)(ep);
        MW_TRACE("  UEF returned %ld", result);
        if (result == -1) return;
    }
    MW_TRACE("  Exception unhandled (code 0x%x)", code);
}

'''

PDATA_INIT_BLOCK = '''    /* Record .pdata for exception handling */
    {
        uint32_t ex_rva = dd[3].VirtualAddress;
        uint32_t ex_sz = dd[3].Size;
        if (ex_rva && ex_sz) {
            g_pdata = g_image_base + ex_rva;
            g_pdata_size = ex_sz;
            g_pdata_count = ex_sz / 12;
            MW_TRACE("Exception table: %u entries at RVA 0x%x", g_pdata_count, ex_rva);
        }
    }
    '''
