#!/usr/bin/env python3
"""BUG-037 reference UNWIND_INFO decoder + ground-truth prologue disassembly.

Decodes the UNWIND_INFO for the mingw CRT-startup frame of t08_big_cpp_stl.exe
(RF begin=0x1470 end=0x23d9, UI @ RVA 0x50458) per the PE/x64 spec, and
disassembles the real prologue so the golden code-array can be validated
against what the compiler actually emitted.
"""
import struct, sys
import pefile

PE = 'tests/appcompat/t08_big_cpp_stl.exe'
RF_BEGIN = 0x1470
TARGET_PC = 0x1c84
UI_RVA = 0x50458

OPNAMES = {
    0: 'PUSH_NONVOL', 1: 'ALLOC_LARGE', 2: 'ALLOC_SMALL', 3: 'SET_FPREG',
    4: 'SAVE_NONVOL', 5: 'SAVE_NONVOL_FAR', 6: 'SAVE_XMM128', 7: 'SAVE_XMM128_FAR',
    8: 'PUSH_MACHFRAME', 9: 'EPilogue-v2??', 10: 'RESERVED10', 11: 'RESERVED11',
}
REGS = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi',
        'r8','r9','r10','r11','r12','r13','r14','r15']

def main():
    pe = pefile.PE(PE, fast_load=True)
    data = pe.__data__

    # 1) Find the RUNTIME_FUNCTION covering TARGET_PC in .pdata (independent of loader)
    pdata = None
    for s in pe.sections:
        if b'.pdata' in s.Name:
            pdata = s
    if pdata is None:
        print('no .pdata'); return 1
    prva = pdata.VirtualAddress
    psz = pdata.SizeOfRawData
    base = pe.OPTIONAL_HEADER.ImageBase
    off = pe.get_offset_from_rva(prva)
    rf = None
    for i in range(0, psz, 12):
        bgn, end, ui = struct.unpack_from('<III', data, off + i)
        if bgn <= TARGET_PC < end:
            rf = (bgn, end, ui)
            print(f'.pdata[{i//12}] @ rva 0x{prva+i:x}: begin=0x{bgn:x} end=0x{end:x} ui=0x{ui:x}')
            break
    if not rf:
        print('no RF found for pc'); return 1
    bgn, end, ui_rva = rf
    print(f'RF matches loader trace: begin={"0x%x"%bgn} ui=0x{ui_rva:x} (trace said 0x50458)')

    # 2) Decode UNWIND_INFO per spec
    uoff = pe.get_offset_from_rva(ui_rva)
    ver = data[uoff] & 0x7
    flags = (data[uoff] >> 3) & 0x7
    prolog = data[uoff+1]
    count = data[uoff+2]
    fb = data[uoff+3]
    print(f'\nUI @ rva 0x{ui_rva:x} (file 0x{uoff:x}): version={ver} flags={flags:#x} '
          f'prolog={prolog} count={count} frame_reg={fb>>4}({REGS[fb>>4] if (fb>>4)<16 else "?"}) frame_off={fb&0xf}')
    print('raw header:', ' '.join('%02x' % b for b in data[uoff:uoff+4]))

    cpos = uoff + 4
    slot = 0
    entries = []
    for i in range(count):
        cw = struct.unpack_from('<H', data, cpos + slot*2)[0]
        code_off = cw & 0xff
        op = (cw >> 8) & 0xf
        info = (cw >> 12) & 0xf
        desc = ''
        nslots = 1
        if op == 0:
            desc = f'push {REGS[info]}'
        elif op == 1:
            if info == 0:
                v = struct.unpack_from('<H', data, cpos + (slot+1)*2)[0]
                desc = f'alloc {v*8} (u16*8={v})'
                nslots = 2
            else:
                v = struct.unpack_from('<I', data, cpos + (slot+1)*2)[0]
                desc = f'alloc {v} (u32)'
                nslots = 3
        elif op == 2:
            desc = f'alloc {(info+1)*8}'
        elif op == 3:
            desc = f'set_fpreg fp = rsp + {(fb&0xf)*16}'
        elif op == 4:
            v = struct.unpack_from('<H', data, cpos + (slot+1)*2)[0]
            desc = f'save {REGS[info]} at [rsp+{v*8}]'
            nslots = 2
        elif op == 5:
            v = struct.unpack_from('<I', data, cpos + (slot+1)*2)[0]
            desc = f'save_far {REGS[info]} at [rsp+{v}]'
            nslots = 3
        elif op == 6:
            v = struct.unpack_from('<H', data, cpos + (slot+1)*2)[0]
            desc = f'save_xmm128 xmm{info} at [rsp+{v*16}]'
            nslots = 2
        elif op == 7:
            v = struct.unpack_from('<I', data, cpos + (slot+1)*2)[0]
            desc = f'save_xmm128_far xmm{info} at [rsp+{v}]'
            nslots = 3
        elif op == 8:
            desc = f'push_machframe info={info}'
        else:
            desc = f'RESERVED op={op} info={info} (v1: illegal!)'
            if op >= 9:
                nslots = 2  # best effort
        entries.append((slot, code_off, op, info, desc, nslots))
        slot += nslots
    for (s, co, op, info, desc, ns) in entries:
        mark = ' <-- NOT DESCENDING!' if len(entries) > 1 else ''
        print(f'  slot {s:2d}: off=0x{co:02x} op={OPNAMES[op]:16s} info={info:x}  {desc}  [{ns} slot(s)]')

    # monotonicity check (spec: codes stored in DECREASING offset order)
    offs = [e[1] for e in entries]
    bad = [(a, b) for a, b in zip(offs, offs[1:]) if b > a]
    print(f'\ncode offsets: {[hex(o) for o in offs]}')
    print('descending order OK' if not bad else f'VIOLATIONS: {[(hex(a),hex(b)) for a,b in bad]}')

    # handler RVA check
    h_off_pos = cpos + slot*2
    if h_off_pos % 4: h_off_pos += 2
    hrva = struct.unpack_from('<I', data, h_off_pos)[0]
    print(f'handler RVA (E flag): 0x{hrva:x}   (bug report says 0x131b0)')
    lsda = struct.unpack_from('<I', data, h_off_pos+4)[0]
    print(f'LSDA RVA: 0x{lsda:x}')

    # 3) Ground truth: disassemble the real prologue
    fo = pe.get_offset_from_rva(bgn)
    print(f'\n--- real code at RVA 0x{bgn:x} (first {prolog} bytes = prologue) ---')
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        code = data[fo:fo+prolog+16]
        for ins in md.disasm(code, base + bgn):
            print(f'  {ins.address - base:#x}: {ins.mnemonic} {ins.op_str}')
            if ins.address - base >= bgn + prolog:
                break
    except ImportError:
        print('capstone missing')

    # 4) Simulate the loader's C interpreter on this exact array (rsp-relative)
    print('\n--- loader interpreter simulation (func_offset = 0x%x) ---' % (TARGET_PC - bgn))
    func_off = TARGET_PC - bgn
    rsp = 0x7ffe0797d8a0  # traced CTX.Rsp
    new_rsp = rsp
    slot2 = 0
    for i in range(count):
        cw = struct.unpack_from('<H', data, cpos + slot2*2)[0]
        code_off = cw & 0xff
        op = (cw >> 8) & 0xf
        info = (cw >> 12) & 0xf
        if func_off > 0 and code_off >= func_off:
            print(f'  i={i} slot={slot2}: SKIP off=0x{code_off:x} (>= func_off)')
            slot2 += 1
            continue
        print(f'  i={i} slot={slot2}: EXEC off=0x{code_off:x} op={op} info={info:x} rsp_before=0x{new_rsp:x}', end='')
        if op == 0:
            new_rsp += 8
            print(f'  PUSH -> rsp=0x{new_rsp:x}  load [0x{new_rsp:x}]')
        elif op == 1:
            slot2 += 1
            if info == 0:
                v = struct.unpack_from('<H', data, cpos + slot2*2)[0]
                new_rsp += v * 8
                print(f'  ALLOC_LARGE {v*8} -> rsp=0x{new_rsp:x}')
            else:
                slot2 += 1
                v = struct.unpack_from('<I', data, cpos + slot2*2 - 2)[0]
                new_rsp += v
                print(f'  ALLOC_LARGE {v} -> rsp=0x{new_rsp:x}')
        elif op == 2:
            new_rsp += (info + 1) * 8
            print(f'  ALLOC_SMALL -> rsp=0x{new_rsp:x}')
        elif op == 3:
            print(f'  SET_FPREG fp=0x{new_rsp + (fb&0xf)*16:x}')
        elif op == 4:
            slot2 += 1
            v = struct.unpack_from('<H', data, cpos + slot2*2)[0]
            print(f'  SAVE_NONVOL [rsp+{v*8}]')
        elif op == 6:
            slot2 += 1
            print('  SAVE_XMM128')
        elif op == 5:
            slot2 += 2
            print('  SAVE_NONVOL_FAR')
        elif op == 7:
            slot2 += 2
            print('  SAVE_XMM128_FAR')
        else:
            print(f'  op={op} default')
        slot2 += 1
    print(f'final new_rsp=0x{new_rsp:x}  (entry rsp - total prologue alloc)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
