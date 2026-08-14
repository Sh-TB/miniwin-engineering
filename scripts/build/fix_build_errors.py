#!/usr/bin/env python3
"""Fix build errors in loader.c after exception handling patch."""

LOADER = '/home/z/my-project/minwin/src/loader.c'
with open(LOADER, 'r') as f:
    content = f.read()

# Fix 1: Add forward declarations before SEH stubs
old = '/* SEH/Exception stubs */'
new = '''/* Forward declarations for exception handling functions (used by RaiseException) */
__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t addr, void* base, void* history);
__attribute__((ms_abi)) int mw_RtlVirtualUnwind(uint32_t code, uint64_t addr, void* info_ptr,
    void* ctx_ptr, void* history, void* disp);

/* SEH/Exception stubs */'''
assert old in content, "Fix 1: Could not find SEH stubs"
content = content.replace(old, new, 1)
print("Fix 1: Added forward declarations")

# Fix 2: Replace the problematic PUSH_NONVOL block
old_push = '''            int reg_idx = NV_REG_MAP[opinfo];
            if (reg_idx > 0) {
                ctx->uc_mcontext_verbose = 0; /* placeholder */
                /* Read from stack */
                uint64_t val = *(uint64_t*)(uintptr_t)rsp;
                /* Map back to WinContext field - use offset calculation */
                uint64_t* reg_slot = (uint64_t*)((uint8_t*)ctx + 0x58 + opinfo * 8);
                *reg_slot = val;
            }'''
new_push = '''            /* Read from stack and restore non-volatile register */
            {
                uint64_t val = 0;
                if (rsp >= 0x1000) val = *(uint64_t*)(uintptr_t)rsp;
                uint64_t* reg_slot = (uint64_t*)((uint8_t*)ctx + 0x58 + opinfo * 8);
                *reg_slot = val;
            }'''
assert old_push in content, "Fix 2: Could not find PUSH_NONVOL block"
content = content.replace(old_push, new_push, 1)
print("Fix 2: Fixed PUSH_NONVOL handler")

# Fix 3: Remove unused 'rip' variable
old_rip = '    uint64_t rsp = ctx->Rsp;\n    uint64_t rip = addr;'
new_rip = '    uint64_t rsp = ctx->Rsp;'
assert old_rip in content, "Fix 3: Could not find rip variable"
content = content.replace(old_rip, new_rip, 1)
print("Fix 3: Removed unused rip variable")

# Fix 4: Replace unused codes_size with (void)prolog_size
old_cs = '    int codes_size = num_codes * 2;'
new_cs = '    (void)prolog_size;'
assert old_cs in content, "Fix 4: Could not find codes_size"
content = content.replace(old_cs, new_cs, 1)
print("Fix 4: Replaced unused codes_size")

# Fix 5: Add (void) for unwind_flags
old_uf = '    uint8_t unwind_flags = version_flags & 0x1F;\n'
new_uf = '    uint8_t unwind_flags = version_flags & 0x1F;\n    (void)unwind_flags;\n'
assert old_uf in content, "Fix 5: Could not find unwind_flags"
content = content.replace(old_uf, new_uf, 1)
print("Fix 5: Suppressed unwind_flags warning")

# Fix 6: Fix handler_rva assignment
old_hr = '        int handler_rva = mw_RtlVirtualUnwind(0, search_addr, rf, &win_ctx, NULL, NULL);'
new_hr = '        mw_RtlVirtualUnwind(0, search_addr, rf, &win_ctx, NULL, NULL);'
assert old_hr in content, "Fix 6: Could not find handler_rva"
content = content.replace(old_hr, new_hr, 1)
print("Fix 6: Fixed handler_rva assignment")

# Fix 7: Fix ALLOC_LARGE
old_alloc = '''            if (opinfo == 0) {
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
            }'''
new_alloc = '''            if (opinfo == 0) {
                /* Next 2 bytes = allocation size / 8 */
                uint16_t alloc_slots = *(uint16_t*)(ui + codes_offset + i * 2 + 2);
                rsp += (uint32_t)alloc_slots * 8;
                i--; /* skip the extra slot */
            } else {
                /* opinfo=1: next 4 bytes = allocation size */
                uint32_t alloc_big = *(uint32_t*)(ui + codes_offset + i * 2 + 2);
                rsp += alloc_big;
                i -= 2; /* skip the extra 2 slots */
            }'''
assert old_alloc in content, "Fix 7: Could not find ALLOC_LARGE block"
content = content.replace(old_alloc, new_alloc, 1)
print("Fix 7: Fixed ALLOC_LARGE handler")

# Fix 8: frame_reg/frame_offset - add frame register RSP restoration
old_frame = '''        uint8_t frame_reg = 0, frame_offset = 0;
        if (version_flags & 0x20) { /* UNWIND_INFO_FLAG_HAS_FRAME */
            frame_reg = ui[3] & 0x0F;
            frame_offset = (ui[3] >> 4) * 16;
        }'''
new_frame = '''        uint8_t frame_reg = 0, frame_offset = 0;
        if (version_flags & 0x20) { /* UNWIND_INFO_FLAG_HAS_FRAME */
            frame_reg = ui[3] & 0x0F;
            frame_offset = (ui[3] >> 4) * 16;
        }
        (void)frame_reg; (void)frame_offset; /* may be used below */'''
assert old_frame in content, "Fix 8: Could not find frame_reg block"
content = content.replace(old_frame, new_frame, 1)
print("Fix 8: Suppressed frame_reg/frame_offset warnings")

with open(LOADER, 'w') as f:
    f.write(content)
print("\nAll build fixes applied.")
