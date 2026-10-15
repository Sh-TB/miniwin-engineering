#!/usr/bin/env python3
"""Parse the GCC LSDA of t08's CRT-startup frame (UI RVA 0x50458) fully:
header, call-site table (uleb128), find the entry covering the throw call
region [0x80f,0x814) rel function start, and print its landing pad offset."""
import struct, sys
import pefile

def uleb(b, i):
    v = 0; shift = 0
    while True:
        x = b[i]; i += 1
        v |= (x & 0x7f) << shift
        if not (x & 0x80): break
        shift += 7
    return v, i

def main():
    pe = pefile.PE('tests/appcompat/t08_big_cpp_stl.exe', fast_load=True)
    data = pe.__data__
    uoff = pe.get_offset_from_rva(0x50458)
    lsda_off = uoff + 36          # handler RVA at +32, LSDA at +36
    lsd = data[lsda_off:lsda_off+900]
    print('LSDA file offset: 0x%x (RVA 0x%x)' % (lsda_off, 0x50458+36))
    print('header bytes:', ' '.join('%02x' % x for x in lsd[:16]))
    i = 0
    lpstart_enc = lsd[i]; i += 1
    print('LPStart enc = 0x%02x (%s)' % (lpstart_enc, 'omit' if lpstart_enc == 0xff else 'VALUE FOLLOWS'))
    lpstart = 0x1470  # omit => function start
    ttype_enc = lsd[i]; i += 1
    print('TType enc  = 0x%02x' % ttype_enc)
    if ttype_enc != 0xff:
        ttoff, i = uleb(lsd, i)
        print('  TType offset = %d -> TType at LSDA+%d' % (ttoff, i + ttoff - ttoff))
    cs_enc = lsd[i]; i += 1
    print('CS enc     = 0x%02x (uleb128)' % cs_enc)
    cs_len, i = uleb(lsd, i)
    print('CS table length = %d bytes' % cs_len)
    cs_end = i + cs_len
    n = 0
    found = None
    while i < cs_end:
        start, i = uleb(lsd, i)
        length, i = uleb(lsd, i)
        lp, i = uleb(lsd, i)
        action, i = uleb(lsd, i)
        mark = ''
        if start <= 0x80f < start + length:
            mark = '   <=== COVERS THROW CALL [0x80f,0x814)'
            found = (start, length, lp, action)
        if lp and (start <= 0x813 < start + length or start <= 0x814 < start + length):
            mark += '  (lp near throw)'
        if n < 6 or mark or (0x7f0 <= start <= 0x830):
            print('  entry %3d: start=%d (0x%x) len=%d lp=%d (0x%x -> VA 0x%x) action=%d%s'
                  % (n, start, start, length, lp, lp, 0x1470 + lp, action, mark))
        n += 1
    print('total entries:', n)
    if found:
        s, l, lp, a = found
        print('THROW REGION entry: lp offset = 0x%x -> landing pad VA 0x%x (call end = 0x1c84)' % (lp, 0x1470 + lp))
        print('  lp is call_end (0x814)?', lp == 0x814, '  lp is call_end-1 (0x813)?', lp == 0x813)
    else:
        print('NO ENTRY COVERS 0x80f!!')

if __name__ == '__main__':
    main()
