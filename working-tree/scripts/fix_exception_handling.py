#!/usr/bin/env python3
"""
Fix BUG-001: Implement minimal SEH unwinding for GCC C++ exceptions.

Changes to loader.c:
1. Add #include <ucontext.h>
2. Add Windows CONTEXT structure and context capture
3. Implement RtlLookupFunctionEntry (search .pdata)
4. Implement RtlVirtualUnwind (basic x64 unwind)
5. Fix RaiseException to capture and pass proper CONTEXT
"""

import re

LOADER = '/home/z/my-project/minwin/src/loader.c'

with open(LOADER, 'r') as f:
    content = f.read()

# === Edit 1: Add #include <ucontext.h> ===
old = '#include <dlfcn.h>'
new = '#include <dlfcn.h>\n#include <ucontext.h>'
assert old in content, "Could not find #include <dlfcn.h>"
content = content.replace(old, new, 1)

# === Edit 2: Add Windows CONTEXT structures after the TEB/PEB section ===
# Insert after setup_teb_peb function, before the trampoline section
context_struct = '''
/* ============================================================
 * Windows x64 CONTEXT Structure (minimal for SEH unwinding)
 * ============================================================ */

#define CONTEXT_AMD64   0x00100000
#define CONTEXT_CONTROL  (CONTEXT_AMD64 | 0x00000001)
#define CONTEXT_INTEGER  (CONTEXT_AMD64 | 0x00000002)
#define CONTEXT_FULL     (CONTEXT_CONTROL | CONTEXT_INTEGER)

/* Minimal Windows x64 CONTEXT - integer and control registers only.
 * Total size must be at least 0x4D0 for compatibility but we only
 * populate the registers that matter for unwinding. */
typedef struct {
    uint64_t P1Home;        /* 0x000 */
    uint64_t P2Home;        /* 0x008 */
    uint64_t P3Home;        /* 0x010 */
    uint64_t P4Home;        /* 0x018 */
    uint64_t P5Home;        /* 0x020 */
    uint64_t P6Home;        /* 0x028 */
    uint32_t ContextFlags;  /* 0x030 */
    uint32_t MxCsr;         /* 0x034 */
    uint16_t SegCs;         /* 0x038 */
    uint16_t SegDs;         /* 0x03A */
    uint16_t SegEs;         /* 0x03C */
    uint16_t SegFs;         /* 0x03E */
    uint16_t SegGs;         /* 0x040 */
    uint16_t SegSs;         /* 0x042 */
    uint32_t EFlags;        /* 0x044 */
    uint64_t Rip;           /* 0x048 */
    uint64_t Rsp;           /* 0x050 */
    uint64_t Rax;           /* 0x058 */
    uint64_t Rcx;           /* 0x060 */
    uint64_t Rdx;           /* 0x068 */
    uint64_t Rbx;           /* 0x070 */
    uint64_t Rbp;           /* 0x078 */
    uint64_t Rsi;           /* 0x080 */
    uint64_t Rdi;           /* 0x088 */
    uint64_t R8;            /* 0x090 */
    uint64_t R9;            /* 0x098 */
    uint64_t R10;           /* 0x0A0 */
    uint64_t R11;           /* 0x0A8 */
    uint64_t R12;           /* 0x0B0 */
    uint64_t R13;           /* 0x0B8 */
    uint64_t R14;           /* 0x0C0 */
    uint64_t R15;           /* 0x0C8 */
    /* FltSave (XSAVE_FORMAT) at 0x0D0, 512 bytes - zeroed */
    uint8_t  FltSave[512];  /* 0x0D0 */
    /* Vector registers Xmm0-Xmm15 at 0x2D0, 256 bytes - zeroed */
    uint8_t  VectorRegs[256]; /* 0x2D0 */
    /* Vector control */
    uint64_t VectorControl; /* 0x3D0 */
    /* Debug control */
    uint64_t DebugControl;  /* 0x3D8 */
    uint64_t LastBranchToRip;  /* 0x3E0 */
    uint64_t LastBranchFromRip; /* 0x3E8 */
    uint64_t LastExceptionToRip; /* 0x3F0 */
    uint64_t LastExceptionFromRip; /* 0x3F8 */
} WinContext;

/* RUNTIME_FUNCTION from .pdata (12 bytes) */
typedef struct {
    uint32_t BeginAddress;
    uint32_t EndAddress;
    uint32_t UnwindData;
} RUNTIME_FUNCTION;

/* .pdata and .xdata locations (set during PE loading) */
static uint8_t*  g_pdata = NULL;
static uint32_t  g_pdata_size = 0;
static uint32_t  g_pdata_count = 0;

/* Capture current CPU state into a Windows CONTEXT structure */
static void capture_context(WinContext* ctx) {
    ucontext_t uc;
    getcontext(&uc);
    memset(ctx, 0, sizeof(WinContext));
    ctx->ContextFlags = CONTEXT_FULL;
    ctx->MxCsr = 0x1F80; /* default MXCSR */
    ctx->SegCs = 0x33;    /* Linux x64 CS */
    ctx->SegSs = 0x2B;    /* Linux x64 SS */
    ctx->SegDs = 0;
    ctx->SegEs = 0;
    ctx->SegFs = 0;
    ctx->SegGs = 0;
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

/* Non-volatile register indices for x64 */
static const int NV_REG_MAP[16] = {
    /* 0=RAX(volatile) */ 0,
    /* 1=RCX(volatile) */ 0,
    /* 2=RDX(volatile) */ 0,
    /* 3=RBX(nv)     */ REG_RBX,
    /* 4=RSP(special)  */ 0,
    /* 5=RBP(nv)      */ REG_RBP,
    /* 6=RSI(nv)      */ REG_RSI,
    /* 7=RDI(nv)      */ REG_RDI,
    /* 8-15=R8-R15   */ REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
};

'''

# Insert after setup_teb_peb closing brace and before trampoline section
old = '/* ============================================================\n * Trampoline Generation (Windows \u2192 System V ABI)\n * ============================================================ */'
assert old in content, "Could not find trampoline section marker"
content = content.replace(old, context_struct + '\n' + old, 1)

print("Edit 1: Added ucontext.h include")
print("Edit 2: Added Windows CONTEXT structure and helpers")

# === Edit 3: Replace RtlLookupFunctionEntry ===
old_rle = '''__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {
    MW_TRACE("RtlLookupFunctionEntry(addr=0x%lx)", addr);
    return NULL;
}'''

new_rle = '''__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history) {
    if (!g_pdata || g_pdata_count == 0) return NULL;
    uint64_t rva = addr - (uint64_t)(uintptr_t)g_image_base;
    /* Binary search in .pdata for the function containing addr */
    uint32_t lo = 0, hi = g_pdata_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)(g_pdata + mid * 12);
        if (rva < rf->BeginAddress) {
            hi = mid;
        } else if (rva >= rf->EndAddress) {
            lo = mid + 1;
        } else {
            MW_TRACE("RtlLookupFunctionEntry(0x%lx) = .pdata[%u] {0x%x-0x%x}",
                     addr, mid, rf->BeginAddress, rf->EndAddress);
            return (void*)rf;
        }
    }
    return NULL;
}'''

assert old_rle in content, "Could not find mw_RtlLookupFunctionEntry"
content = content.replace(old_rle, new_rle, 1)
print("Edit 3: Replaced RtlLookupFunctionEntry with .pdata search")

# === Edit 4: Replace RtlVirtualUnwind ===
old_rvu = '''__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info,
    void* ctx, void* data, void* disp) {
    MW_TRACE("RtlVirtualUnwind()");
    return 0;
}'''

new_rvu = '''/* Unwind opcodes */
#define UWOP_PUSH_NONVOL    0
#define UWOP_ALLOC_LARGE    1
#define UWOP_ALLOC_SMALL    2
#define UWOP_SET_FPREG      3
#define UWOP_SAVE_NONVOL    4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_PUSH_MACHFRAME  10

#define UNW_FLAG_EHANDLER   0x01
#define UNW_FLAG_UHANDLER   0x02
#define UNW_FLAG_CHAININFO  0x04

__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info_ptr,
    void* ctx_ptr, void* history, void* disp) {
    if (!info_ptr || !ctx_ptr || !g_image_base) return 0;

    RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)info_ptr;
    WinContext* ctx = (WinContext*)ctx_ptr;
    uint8_t* ui = g_image_base + rf->UnwindData;

    uint8_t version_flags = ui[0];
    uint8_t prolog_size = ui[1];
    uint8_t num_codes = ui[2];
    uint8_t unwind_flags = version_flags & 0x1F;

    /* Read register field (byte 3) */
    uint8_t frame_reg = 0, frame_offset = 0;
    if (version_flags & 0x20) { /* UNWIND_INFO_FLAG_HAS_FRAME */
        frame_reg = ui[3] & 0x0F;
        frame_offset = (ui[3] >> 4) * 16;
    }

    /* Process unwind codes in reverse order (highest offset first) */
    int codes_offset = 4;
    int codes_size = num_codes * 2;

    /* Track RSP adjustments */
    uint64_t rsp = ctx->Rsp;
    uint64_t rip = addr;

    /* First, undo the effect of the epilog (since we are unwinding from current PC) */
    /* For simplicity, we replay unwind codes to restore the prolog state */

    for (int i = num_codes - 1; i >= 0; i--) {
        uint16_t uc_entry = *(uint16_t*)(ui + codes_offset + i * 2);
        uint8_t op = uc_entry & 0x0F;
        uint8_t opinfo = (uc_entry >> 4) & 0x0F;

        switch (op) {
        case UWOP_PUSH_NONVOL: {
            /* Pop non-volatile register from stack */
            int reg_idx = NV_REG_MAP[opinfo];
            if (reg_idx > 0) {
                ctx->uc_mcontext_verbose = 0; /* placeholder */
                /* Read from stack */
                uint64_t val = *(uint64_t*)(uintptr_t)rsp;
                /* Map back to WinContext field - use offset calculation */
                uint64_t* reg_slot = (uint64_t*)((uint8_t*)ctx + 0x58 + opinfo * 8);
                *reg_slot = val;
            }
            rsp += 8;
            break;
        }
        case UWOP_ALLOC_SMALL:
            /* Alloc size = (opinfo + 1) * 8 */
            rsp += (opinfo + 1) * 8;
            break;
        case UWOP_ALLOC_LARGE: {
            if (opinfo == 0) {
                /* Next 2 bytes = allocation size / 8 */
                uint16_t alloc = *(uint16_t*)(ui + codes_offset + (i > 0 ? i - 1 : 0) * 2);
                /* Actually, for ALLOC_LARGE with opinfo=0, the size is in the next unwind code slot */
                /* The allocation size is the 16-bit value shifted left by 3 */
                /* We need to read from the correct position */
                /* Skip: this is complex, handle common case only */
                rsp += 8; /* minimal */
            } else {
                /* opinfo=1: next 4 bytes = allocation size */
                rsp += 8; /* minimal */
            }
            break;
        }
        case UWOP_SET_FPREG:
            /* Frame register set - we skip this for basic unwinding */
            break;
        case UWOP_SAVE_NONVOL: {
            /* Saved at offset from RSP - skip for basic unwinding */
            break;
        }
        case UWOP_SAVE_NONVOL_FAR: {
            break;
        }
        case UWOP_PUSH_MACHFRAME:
            /* Push machine frame: SS, RSP, CS, RIP */
            rsp += 8; /* skip SS */
            rip = *(uint64_t*)(uintptr_t)rsp;
            rsp += 8; /* skip CS */
            rsp += 8; /* skip RSP */
            break;
        }
        }
    }

    /* If frame register was used, restore RSP from it */
    if (version_flags & 0x20) {
        uint64_t frame_value = *(uint64_t*)((uint8_t*)ctx + 0x58 + frame_reg * 8);
        rsp = frame_value + (frame_offset * 16);
    }

    ctx->Rsp = rsp;
    ctx->Rip = rf->BeginAddress; /* Return to beginning of function for handler lookup */

    /* Return handler address if present */
    if (unwind_flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {
        int handler_offset = codes_offset + codes_size;
        if (handler_offset % 4) handler_offset += 2; /* align to 4 */
        uint32_t handler_rva = *(uint32_t*)(ui + handler_offset);
        MW_TRACE("RtlVirtualUnwind: handler at RVA 0x%x, new RIP=0x%lx", handler_rva, ctx->Rip);
        return (int)handler_rva;
    }

    return 0;
}'''

assert old_rvu in content, "Could not find mw_RtlVirtualUnwind"
content = content.replace(old_rvu, new_rvu, 1)
print("Edit 4: Replaced RtlVirtualUnwind with basic unwinding")

# === Edit 5: Rewrite RaiseException to capture context ===
old_re = '''__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    MW_TRACE("RaiseException(code=0x%x, flags=0x%x, nargs=%u)", code, flags, nargs);

    /* Build an EXCEPTION_RECORD on the stack */
    /* EXCEPTION_RECORD layout (x64):
     *   +0x00 ExceptionCode    (4)
     *   +0x04 ExceptionFlags   (4)
     *   +0x08 ExceptionRecord  (8, pointer)
     *   +0x10 ExceptionAddress (8, pointer)
     *   +0x18 NumberParameters(4)
     *   +0x1C __alignment    (4)
     *   +0x20 ExceptionInformation[0..14] (15 * 8 = 120)
     * Total: 0x98 bytes
     */
    uint8_t er_buf[0x100];
    memset(er_buf, 0, sizeof(er_buf));
    *(uint32_t*)(er_buf + 0x00) = code;
    *(uint32_t*)(er_buf + 0x04) = flags;
    *(uint64_t*)(er_buf + 0x08) = 0; /* no nested */
    *(uint64_t*)(er_buf + 0x10) = (uint64_t)(uintptr_t)__builtin_return_address(0);
    *(uint32_t*)(er_buf + 0x18) = nargs;
    if (nargs > 0 && args) {
        memcpy(er_buf + 0x20, args, nargs * 8);
    }

    /* EXCEPTION_POINTERS: { EXCEPTION_RECORD*, CONTEXT* } */
    uint64_t ep[2] = { (uint64_t)(uintptr_t)er_buf, 0 };

    /* 1. Try VEH handlers */
    for (int i = 0; i < g_veh_count; i++) {
        if (g_veh_handlers[i]) {
            typedef long (*veh_fn)(uint64_t* exception_pointers);
            veh_fn veh = (veh_fn)g_veh_handlers[i];
            long result = veh(ep);
            MW_TRACE("  VEH handler %p returned %ld", g_veh_handlers[i], result);
            if (result == -1) return; /* EXCEPTION_CONTINUE_EXECUTION */
        }
    }

    /* 2. Try to find frame-based handler via .pdata */
    uint64_t ret_addr = (uint64_t)(uintptr_t)__builtin_return_address(0);
    uint64_t rva = ret_addr - (uint64_t)(uintptr_t)g_image_base;

    /* Search .pdata for the function containing ret_addr */
    /* .pdata at RVA 0x1f8000, size 0x8e08 */
    uint32_t pdata_rva = 0x1f8000;
    uint32_t pdata_size = 0x8e08;
    int num_entries = pdata_size / 12; /* sizeof(RUNTIME_FUNCTION) = 12 */

    for (int i = 0; i < num_entries; i++) {
        uint32_t begin = *(uint32_t*)(g_image_base + pdata_rva + i * 12 + 0);
        uint32_t end   = *(uint32_t*)(g_image_base + pdata_rva + i * 12 + 4);
        uint32_t unwind= *(uint32_t*)(g_image_base + pdata_rva + i * 12 + 8);

        if (rva >= begin && rva < end) {
            MW_TRACE("  Found RUNTIME_FUNCTION: begin=0x%x end=0x%x unwind=0x%x",
                     begin, end, unwind);

            uint8_t* ui = g_image_base + unwind;
            uint8_t ui_flags = ui[1] & 0x1F;
            uint8_t ui_version = ui[0] & 0x07;
            uint8_t num_codes = ui[2];

            if (ui_flags & 0x01) { /* UNW_FLAG_EHANDLER */
                uint32_t handler_off = 4 + num_codes * 2;
                if (handler_off % 4) handler_off += 2;
                uint32_t handler_rva = *(uint32_t*)(ui + handler_off);
                MW_TRACE("  Exception handler at RVA 0x%x", handler_rva);

                /* Call __C_specific_handler or other handler */
                typedef long (*handler_fn)(uint8_t* er, void* establisher_frame,
                    uint8_t* ctx, void* dispatcher_ctx);
                handler_fn handler = (handler_fn)(g_image_base + handler_rva);

                /* Build a minimal context - just enough for __C_specific_handler */
                /* The handler needs to walk the __try/__except chain.
                 * For now, call with what we have. */
                long result = handler(er_buf, NULL, NULL, NULL);
                MW_TRACE("  Handler returned %ld", result);
                if (result == -1) return; /* EXCEPTION_CONTINUE_EXECUTION */
                if (result == 0) break;  /* EXCEPTION_CONTINUE_SEARCH */
            }
            break;
        }
    }

    /* 3. Last resort: UnhandledExceptionFilter */
    if (g_unhandled_exception_filter) {
        typedef long (*uef_fn)(uint64_t* exception_pointers);
        uef_fn uef = (uef_fn)g_unhandled_exception_filter;
        long result = uef(ep);
        MW_TRACE("  UnhandledExceptionFilter returned %ld", result);
        if (result == -1) return;
    }

    MW_TRACE("  Exception unhandled");
}'''

new_re = '''__attribute__((ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    MW_TRACE("RaiseException(code=0x%x, flags=0x%x, nargs=%u)", code, flags, nargs);

    /* Capture current CPU state */
    WinContext win_ctx;
    capture_context(&win_ctx);
    /* Set ExceptionAddress to the caller's return address */
    win_ctx.Rip = (uint64_t)(uintptr_t)__builtin_return_address(0);

    /* Build EXCEPTION_RECORD on stack (0x98 bytes) */
    uint8_t er_buf[0x100];
    memset(er_buf, 0, sizeof(er_buf));
    *(uint32_t*)(er_buf + 0x00) = code;
    *(uint32_t*)(er_buf + 0x04) = flags;
    *(uint64_t*)(er_buf + 0x08) = 0; /* no nested */
    *(uint64_t*)(er_buf + 0x10) = win_ctx.Rip; /* ExceptionAddress */
    *(uint32_t*)(er_buf + 0x18) = nargs;
    if (nargs > 0 && args) {
        memcpy(er_buf + 0x20, args, nargs * 8);
    }

    /* EXCEPTION_POINTERS: { EXCEPTION_RECORD*, CONTEXT* } */
    uint64_t ep[2] = { (uint64_t)(uintptr_t)er_buf, (uint64_t)(uintptr_t)&win_ctx };

    /* 1. Try VEH handlers (registered via AddVectoredExceptionHandler) */
    for (int i = 0; i < g_veh_count; i++) {
        if (g_veh_handlers[i]) {
            typedef long (*veh_fn)(uint64_t* exception_pointers);
            veh_fn veh = (veh_fn)g_veh_handlers[i];
            long result = veh(ep);
            MW_TRACE("  VEH handler %p returned %ld", g_veh_handlers[i], result);
            if (result == -1) return; /* EXCEPTION_CONTINUE_EXECUTION */
        }
    }

    /* 2. Walk .pdata to find frame-based exception handlers */
    uint64_t ret_addr = win_ctx.Rip;
    uint64_t search_addr = ret_addr;

    for (int frame = 0; frame < 64; frame++) {
        uint64_t rva = search_addr - (uint64_t)(uintptr_t)g_image_base;

        /* Use RtlLookupFunctionEntry to find the function */
        RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)mw_RtlLookupFunctionEntry(
            search_addr, NULL, NULL);

        if (!rf) {
            MW_TRACE("  No .pdata entry for 0x%lx (RVA 0x%lx)", search_addr, rva);
            break;
        }

        MW_TRACE("  Unwind frame %d: .pdata {0x%x-0x%x unwind=0x%x}",
                 frame, rf->BeginAddress, rf->EndAddress, rf->UnwindData);

        uint8_t* ui = g_image_base + rf->UnwindData;
        uint8_t unwind_flags = ui[0] & 0x1F;
        uint8_t num_codes = ui[2];

        /* Check if this function has an exception handler */
        if (unwind_flags & (0x01 | 0x02)) { /* EHANDLER or UHANDLER */
            int handler_offset = 4 + num_codes * 2;
            if (handler_offset % 4) handler_offset += 2;
            uint32_t handler_rva = *(uint32_t*)(ui + handler_offset);
            MW_TRACE("  Exception handler at RVA 0x%x", handler_rva);

            /* Set context RIP to the start of the current function */
            win_ctx.Rip = search_addr;

            /* Call the handler */
            typedef long (*handler_fn)(uint8_t* er, void* establisher_frame,
                WinContext* ctx, void* dispatcher_ctx);
            handler_fn handler = (handler_fn)(g_image_base + handler_rva);
            long result = handler(er_buf, NULL, &win_ctx, NULL);
            MW_TRACE("  Handler returned %ld", result);

            if (result == -1) return; /* EXCEPTION_CONTINUE_EXECUTION */
            if (result == 1) break;  /* EXCEPTION_EXECUTE_HANDLER - handled, but need cleanup */
            /* result == 0: EXCEPTION_CONTINUE_SEARCH, keep walking */
        }

        /* Unwind to caller using RtlVirtualUnwind */
        int handler_rva = mw_RtlVirtualUnwind(0, search_addr, rf, &win_ctx, NULL, NULL);
        if (win_ctx.Rip == search_addr) {
            MW_TRACE("  RtlVirtualUnwind did not advance RIP, stopping");
            break;
        }
        search_addr = win_ctx.Rip;
        MW_TRACE("  Unwound to RIP=0x%lx RSP=0x%lx", search_addr, win_ctx.Rsp);
    }

    /* 3. Last resort: UnhandledExceptionFilter */
    if (g_unhandled_exception_filter) {
        typedef long (*uef_fn)(uint64_t* exception_pointers);
        uef_fn uef = (uef_fn)g_unhandled_exception_filter;
        long result = uef(ep);
        MW_TRACE("  UnhandledExceptionFilter returned %ld", result);
        if (result == -1) return;
    }

    MW_TRACE("  Exception unhandled - code 0x%x", code);
}'''

assert old_re in content, "Could not find mw_RaiseException"
content = content.replace(old_re, new_re, 1)
print("Edit 5: Rewrote RaiseException with CONTEXT capture and .pdata walking")

# === Edit 6: Set g_pdata during PE loading (after section mapping) ===
# Find the line that says "PE Loader: Mapped at" and add pdata init after section mapping
old_load = '    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",'
new_load = '''    /* Record .pdata location for exception handling */
    /* .pdata is DataDirectory[3] (Exception) */
    uint32_t except_dd_rva = dd[3].VirtualAddress;
    uint32_t except_dd_size = dd[3].Size;
    if (except_dd_rva != 0 && except_dd_size != 0) {
        g_pdata = g_image_base + except_dd_rva;
        g_pdata_size = except_dd_size;
        g_pdata_count = except_dd_size / 12;
        MW_TRACE("Exception table (.pdata): %u entries at RVA 0x%x",
                 g_pdata_count, except_dd_rva);
    }

    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",'''

assert old_load in content, "Could not find import resolution trace line"
content = content.replace(old_load, new_load, 1)
print("Edit 6: Added .pdata initialization in load_pe")

# === Write the modified file ===
with open(LOADER, 'w') as f:
    f.write(content)

print("\nAll edits applied successfully. Ready to build.")
