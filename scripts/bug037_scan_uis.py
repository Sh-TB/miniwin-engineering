#!/usr/bin/env python3
"""Scan ALL UNWIND_INFOs in t08 (and optionally other exes) for spec violations:
descending code-offset order, offsets <= prolog size, legal v1 opcodes, slot
consumption consistency. Goal: is the CRT-startup array (RF 0x1470) unique or
systematic? Also decode neighbouring UIs for pattern comparison."""
import struct, sys
import pefile

def decode_ui(data, uoff, image_size):
    ver = data[uoff] & 7
    flags = (data[uoff] >> 3) & 7
    prolog = data[uoff+1]
    count = data[uoff+2]
    fb = data[uoff+3]
    cpos = uoff + 4
    slot = 0
    entries = []
    ok = True
    notes = []
    for i in range(count):
        if cpos + slot*2 + 1 >= len(data):
            notes.append('EOF'); ok = False; break
        cw = struct.unpack_from('<H', data, cpos + slot*2)[0]
        code_off = cw & 0xff
        op = (cw >> 8) & 0xf
        info = (cw >> 12) & 0xf
        ns = 1
        if op == 1: ns = 2 if info == 0 else 3
        elif op == 4: ns = 2
        elif op == 5: ns = 3
        elif op == 6: ns = 2
        elif op == 7: ns = 3
        elif op >= 9:
            notes.append(f'op{op}'); ok = False
            ns = 1 if op < 4 else 1
        if op == 8 and info not in (0,1):
            notes.append(f'machframe_info={info}'); ok = False
        entries.append((code_off, op, info, ns))
        slot += ns
    offs = [e[0] for e in entries]
    for a, b in zip(offs, offs[1:]):
        if b > a:
            ok = False
            notes.append(f'nondesc {a:x}>{b:x}')
    for o in offs:
        if o > prolog:
            notes.append(f'off {o:x}>prolog {prolog:x}')
    return dict(ver=ver, flags=flags, prolog=prolog, count=count, fb=fb,
                entries=entries, ok=ok, notes=notes)

def main(path):
    pe = pefile.PE(path, fast_load=True)
    data = pe.__data__
    base = pe.OPTIONAL_HEADER.ImageBase
    pd = [s for s in pe.sections if b'.pdata' in s.Name][0]
    off = pe.get_offset_from_rva(pd.VirtualAddress)
    total = bad = 0
    bad_list = []
    for i in range(0, pd.SizeOfRawData - 11, 12):
        bgn, end, ui = struct.unpack_from('<III', data, off + i)
        if not (0 < bgn < end < (1 << 32)) or ui == 0:
            continue
        try:
            uoff = pe.get_offset_from_rva(ui)  # RVA -> file offset (was: raw RVA used as offset — BUG)
        except Exception:
            continue
        if uoff + 4 > len(data):
            continue
        try:
            r = decode_ui(data, uoff, len(data))
        except Exception:
            continue
        total += 1
        if not r['ok']:
            bad += 1
            bad_list.append((bgn, end, ui, r))
    print(f'{path}: {total} UIs scanned, {bad} with violations')
    for (bgn, end, ui, r) in bad_list:
        ents = ' '.join(f'{o:x}/{op:x}{i:x}' for (o, op, i, ns) in r['entries'][:16])
        print(f'  RF 0x{bgn:x}-0x{end:x} ui=0x{ui:x} v{r["ver"]} fl={r["flags"]} '
              f'prolog={r["prolog"]} n={r["count"]} fb=0x{r["fb"]:02x}')
        print(f'    entries(off/opinfo): {ents}')
        print(f'    notes: {r["notes"]}')
    return 0

if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
