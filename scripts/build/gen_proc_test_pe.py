#!/usr/bin/env python3
"""SW-1500/1501/1502 acceptance PEs (issues #16/#9/#8).

  mw_proc_child.exe  - prints CHILD-UP then ExitProcess(42)
  mw_proc_parent.exe - battery:
    T1 CreateProcessW -> WaitForSingleObject -> GetExitCodeProcess == 42
    T2 CreateFileW -> SetFileAttributesW(READONLY) -> GetFileAttributesW bit0
    T3 registry: default-value read-back; RRF_RT_REG_SZ auto-expand of
       %MWTESTPATH%; RRF_RT_REG_DWORD type-mask rejection (5729)

stdout is grep-able KEY=VALUE. Layout: .text@0x1000 .rdata@0x2000
.data@0x3000, ImageBase 0x140000000, SizeOfImage 0x4000.
"""
import struct

p8  = lambda v: struct.pack('<B', v & 0xFF)
p16 = lambda v: struct.pack('<H', v & 0xFFFF)
p32 = lambda v: struct.pack('<I', v & 0xFFFFFFFF)
p64 = lambda v: struct.pack('<Q', v & 0xFFFFFFFFFFFFFFFF)
align = lambda v, a: (v + a - 1) & ~(a - 1)

IMAGE_BASE = 0x140000000
TEXT_RVA, RDATA_RVA, DATA_RVA = 0x1000, 0x2000, 0x3000
IMAGE_SIZE = 0x4000


def rel32(from_rva_end, target_rva):
    """RIP-relative disp between two IMAGE-RELATIVE offsets (both RVA)."""
    return p32((target_rva - from_rva_end) & 0xFFFFFFFF)


def wide(s):
    return s.encode('utf-16-le') + b'\x00\x00'


def ascii_z(s):
    return s.encode() + b'\x00'


def build_imports(spec):
    """spec: [(dll, [funcs])]. Returns (blob, iat_map, dir_rva, dir_size)."""
    iat, iat_map = bytearray(), {}
    rva = RDATA_RVA
    iat_starts = {}
    for dll, funcs in spec:
        iat_starts[dll] = rva + len(iat)
        iat_map[dll] = {}
        for f in funcs:
            iat_map[dll][f] = rva + len(iat)
            iat += p64(0)
        iat += p64(0)
    ilt_rva = rva + len(iat)
    ilt, hn = bytearray(), bytearray()
    hn_rva0 = ilt_rva + sum(8 * (len(fs) + 1) for _, fs in spec)
    hn_rva = hn_rva0
    hn_map = {}
    for dll, funcs in spec:
        for f in funcs:
            e = p16(0) + f.encode() + b'\x00'
            if len(e) & 1:
                e += b'\x00'
            hn_map[(dll, f)] = hn_rva
            hn += e
            hn_rva += len(e)
    nm_rva = align(hn_rva, 8)
    nm, dll_rva = bytearray(), {}
    for dll, _ in spec:
        dll_rva[dll] = nm_rva + len(nm)
        nm += ascii_z(dll)
    desc_rva = align(nm_rva + len(nm), 8)
    desc = bytearray()
    for dll, funcs in spec:
        # ILT entries for this dll
        pass
    # build ILT now (needs hn_map); x64 ILT entries are 8-byte slots
    ilt = bytearray()
    ilt_starts = {}
    cur = ilt_rva
    for dll, funcs in spec:
        ilt_starts[dll] = cur
        for f in funcs:
            ilt += p32(hn_map[(dll, f)]) + p32(0)
            cur += 8
        ilt += p32(0) + p32(0)  # null terminator slot
        cur += 8
    for dll, funcs in spec:
        desc += (p32(ilt_starts[dll]) + p32(0) + p32(0)
                 + p32(dll_rva[dll]) + p32(iat_starts[dll]))
    desc += p32(0) * 5
    blob = (iat
            + b'\x00' * (ilt_rva - (RDATA_RVA + len(iat)))
            + ilt
            + b'\x00' * (hn_rva0 - (ilt_rva + len(ilt)))
            + hn
            + b'\x00' * (nm_rva - hn_rva)
            + nm
            + b'\x00' * (desc_rva - (nm_rva + len(nm)))
            + desc)
    dir_size = (desc_rva + len(desc)) - RDATA_RVA
    # Data Directory[1] must point at the DESCRIPTOR block, not the IAT
    return bytes(blob), iat_map, desc_rva, len(desc)


class Code:
    def __init__(self):
        self.b = bytearray()

    @property
    def here(self):
        return TEXT_RVA + len(self.b)

    def _e(self, *parts):
        for p in parts:
            self.b += p

    # -- addressing ------------------------------------------------
    def lea(self, reg, target):        # reg: 1=rcx 2=rdx 8=r8 9=r9 0=rax
        rex = 0x48 | (0x04 if reg >= 8 else 0)  # REX.R extends modrm.reg
        self._e(bytes([rex, 0x8D, ((reg & 7) << 3) | 0x05]), rel32(self.here + 7, target))

    def mov_rip(self, reg, target):
        rex = 0x48 | (0x04 if reg >= 8 else 0)  # REX.R extends modrm.reg
        self._e(bytes([rex, 0x8B, ((reg & 7) << 3) | 0x05]), rel32(self.here + 7, target))

    def mov_eax_rip(self, target):
        self._e(b'\x8B\x05', rel32(self.here + 6, target))

    def store_eax(self, target):
        self._e(b'\x89\x05', rel32(self.here + 6, target))

    def movzx_edx_word_rip(self, target):
        self._e(b'\x0F\xB7\x15', rel32(self.here + 7, target))

    def movzx_r8d_word_rip(self, target):
        self._e(b'\x44\x0F\xB7\x05', rel32(self.here + 8, target))

    def movzx_r9d_word_rip(self, target):
        self._e(b'\x44\x0F\xB7\x0D', rel32(self.here + 8, target))

    # -- immediates ------------------------------------------------
    def xor_ecx(self): self._e(b'\x31\xC9')
    def xor_edx(self): self._e(b'\x31\xD2')
    def xor_r8d(self): self._e(b'\x45\x31\xC0')
    def xor_r9d(self): self._e(b'\x45\x31\xC9')
    def xor_eax(self): self._e(b'\x31\xC0')
    def mov_ecx(self, v): self._e(b'\xB9', p32(v))
    def mov_edx(self, v): self._e(b'\xBA', p32(v))
    def mov_r8d(self, v): self._e(b'\x41\xB8', p32(v))
    def mov_r9d(self, v): self._e(b'\x41\xB9', p32(v))
    def mov_edx_eax(self): self._e(b'\x89\xC2')
    def mov_r8d_edx(self): self._e(b'\x89\xD0')  # unused
    def and_eax(self, v): self._e(b'\x83\xE0', p8(v))

    # -- stack args ------------------------------------------------
    def st_q0(self, v): self._e(b'\x48\xC7\x44\x24', p8(0x20), p32(v))
    def st_q(self, k, v): self._e(b'\x48\xC7\x44\x24', p8(k), p32(v))
    def st_d(self, k, v): self._e(b'\xC7\x44\x24', p8(k), p32(v))
    def st_rax(self, k): self._e(b'\x48\x89\x44\x24', p8(k))

    # -- control ---------------------------------------------------
    def call(self, iat_rva):
        self._e(b'\xFF\x15', rel32(self.here + 6, iat_rva))

    def cmp_q_rip_0(self, target):     # cmp qword [rip+t], 0
        self._e(b'\x48\x83\x3D', rel32(self.here + 7, target), p8(0))

    def jz_fwd(self):                  # returns patch offset for rel32
        self._e(b'\x0F\x84', b'\x00\x00\x00\x00')
        return len(self.b) - 4

    def patch_jz(self, off):
        # target = current end; displacement relative to next insn (off+4)
        self.b[off:off + 4] = p32(len(self.b) - off - 4)

    def prolog(self, n): self._e(b'\x48\x83\xEC', p8(n))
    def epilog(self, n): self._e(b'\x48\x83\xC4', p8(n))
    def mov_ecx_v(self, v): self.mov_ecx(v)


def build_pe(entry, text, rdata, data):
    hdr = bytearray()
    hdr += b'MZ' + b'\x00' * 58 + p32(64)
    hdr += b'PE\x00\x00'
    hdr += p16(0x8664) + p16(3) + p32(0) + p32(0) + p32(0)
    hdr += p16(240) + p16(0x0022)
    oh = bytearray()
    oh += p16(0x020B) + p8(14) + p8(0)
    oh += p32(align(len(text), 0x200))
    oh += p32(align(len(rdata) + len(data), 0x200))
    oh += p32(0)
    oh += p32(entry) + p32(TEXT_RVA)
    oh += p64(IMAGE_BASE) + p32(0x1000) + p32(0x200)
    oh += p16(6) + p16(0) + p16(0) + p16(0) + p16(6) + p16(0) + p32(0)
    oh += p32(IMAGE_SIZE) + p32(0x400) + p32(0) + p16(3) + p16(0)
    oh += p64(0x100000) + p64(0x1000) + p64(0x100000) + p64(0x1000)
    oh += p32(0) + p32(16)
    dirs = [(0, 0)] * 16
    dirs[1] = (RDATA_RVA + 0, 0)  # patched by caller below
    hdr += oh
    return hdr  # assembled by caller with dir info


def emit_pe(entry, text, rdata, data, imp_rva, imp_size):
    secs = [(b'.text', TEXT_RVA, text, 0x60000020),
            (b'.rdata', RDATA_RVA, rdata, 0x40000040),
            (b'.data', DATA_RVA, data, 0xC0000040)]
    fsize = [align(len(b), 0x200) for _, _, b, _ in secs]
    foff = []
    cur = 0x400
    for fs in fsize:
        foff.append(cur)
        cur += fs
    hdr = bytearray()
    hdr += b'MZ' + b'\x00' * 58 + p32(64)
    hdr += b'PE\x00\x00'
    hdr += p16(0x8664) + p16(3) + p32(0) + p32(0) + p32(0)
    hdr += p16(240) + p16(0x0022)
    oh = bytearray()
    oh += p16(0x020B) + p8(14) + p8(0)
    oh += p32(fsize[0])
    oh += p32(fsize[1] + fsize[2])
    oh += p32(0)
    oh += p32(entry) + p32(TEXT_RVA)
    oh += p64(IMAGE_BASE) + p32(0x1000) + p32(0x200)
    oh += p16(6) + p16(0) + p16(0) + p16(0) + p16(6) + p16(0) + p32(0)
    oh += p32(IMAGE_SIZE) + p32(0x400) + p32(0) + p16(3) + p16(0)
    oh += p64(0x100000) + p64(0x1000) + p64(0x100000) + p64(0x1000)
    oh += p32(0) + p32(16)
    for i in range(16):
        r, s = (imp_rva, imp_size) if i == 1 else (0, 0)
        oh += p32(r) + p32(s)
    assert len(oh) == 240, len(oh)
    hdr += oh
    for (name, rva, blob, chars), fs, fo in zip(secs, fsize, foff):
        hdr += name.ljust(8, b'\x00') + p32(fs) + p32(rva) + p32(fs) + p32(fo) \
            + p32(0) + p32(0) + p16(0) + p16(0) + p32(chars)
    hdr += b'\x00' * (0x400 - len(hdr))
    assert len(hdr) == 0x400, len(hdr)
    out = bytearray(hdr)
    for (name, rva, blob, chars), fs, fo in zip(secs, fsize, foff):
        out += blob
        out += b'\x00' * (fs - len(blob))
    return bytes(out)


# ===================================================================
# CHILD: printf("CHILD-UP\n") ; ExitProcess(42)
# ===================================================================
def gen_child():
    spec = [('msvcrt.dll', ['printf']), ('KERNEL32.dll', ['ExitProcess'])]
    iblob, iat, drva, dsize = build_imports(spec)
    # .data layout
    data = bytearray()
    off0 = DATA_RVA
    s_child = off0 + len(data); data += ascii_z('CHILD-UP\n')
    f_s = off0 + len(data);     data += ascii_z('%s')
    data += b'\x00' * (align(len(data), 16) - len(data))
    c = Code()
    c.prolog(0x28)
    c.lea(1, f_s)
    c.lea(2, s_child)
    c.call(iat['msvcrt.dll']['printf'])
    c.mov_ecx(42)
    c.call(iat['KERNEL32.dll']['ExitProcess'])
    text = bytes(c.b)
    rdata = iblob + b'\x00' * (align(len(iblob), 0x200) - len(iblob))
    return emit_pe(TEXT_RVA, text, rdata, bytes(data), drva, dsize)


# ===================================================================
# PARENT: full battery
# ===================================================================
def gen_parent():
    spec = [
        ('KERNEL32.dll', ['CreateProcessW', 'WaitForSingleObject',
                          'GetExitCodeProcess', 'CloseHandle', 'CreateFileW',
                          'SetFileAttributesW', 'GetFileAttributesW',
                          'ExitProcess']),
        ('advapi32.dll', ['RegCreateKeyExW', 'RegSetValueExW',
                          'RegQueryValueExW', 'RegGetValueW', 'RegCloseKey']),
        ('msvcrt.dll', ['printf']),
    ]
    iblob, iat, drva, dsize = build_imports(spec)

    data = bytearray()
    off0 = DATA_RVA
    D = {}
    def put(name, blob, al=1):
        if al > 1:
            pad = (al - (len(data) % al)) % al
            data.extend(b'\x00' * pad)
        D[name] = off0 + len(data)
        data.extend(blob)
    # globals first (16-align pi)
    put('pi', b'\x00' * 24, 16)
    put('exitcode', b'\x00' * 4)
    put('hfile', b'\x00' * 4)
    put('attrs', b'\x00' * 4)
    put('hk', b'\x00' * 8)
    put('type', b'\x00' * 4)
    put('wlen', p32(256))
    put('wbuf', b'\x00' * 256)
    put('type2', b'\x00' * 4)
    put('len2', p32(256))
    put('wbuf2', b'\x00' * 256)
    put('type3', b'\x00' * 4)
    put('len3', p32(256))
    put('wbuf3', b'\x00' * 16)
    put('s_cmd', wide('artifacts/proc-test/mw_proc_child.exe 42'))
    put('s_attrpath', wide('attrtest.txt'))
    put('s_regpath', wide('Software\\MiniWinTest'))
    put('s_expname', wide('ExpVal'))
    put('s_expval', wide('%MWTESTPATH%\\sub'))
    put('s_defval', wide('defvalue'))
    # fmt strings
    put('f_create', ascii_z('CREATE=%u\n'))
    put('f_wait', ascii_z('WAIT=%u\n'))
    put('f_exit', ascii_z('EXIT=%u\n'))
    put('f_cf', ascii_z('CREATEFILE=%u\n'))
    put('f_sa', ascii_z('SETATTR=%u\n'))
    put('f_ga', ascii_z('ATTRS=%u\n'))
    put('f_ro', ascii_z('ROBIT=%u\n'))
    put('f_open', ascii_z('OPEN=%u\n'))
    put('f_sd', ascii_z('SETDEF=%u\n'))
    put('f_qd', ascii_z('QDEF=%u TYPE=%u D0=%u\n'))
    put('f_d1', ascii_z('D1=%u\n'))
    put('f_se', ascii_z('SETEXP=%u\n'))
    put('f_ge', ascii_z('GETEXP=%u TYPE2=%u LEN2=%u\n'))
    put('f_e0', ascii_z('EXP0=%u EXP1=%u\n'))
    put('f_un', ascii_z('UNSUP=%u\n'))
    put('f_hk', ascii_z('HK=%llu\n'))

    K = iat['KERNEL32.dll']
    A = iat['advapi32.dll']
    M = iat['msvcrt.dll']
    c = Code()
    c.prolog(0x78)

    # ---- T1: CreateProcessW ----
    c.xor_ecx()
    c.lea(2, D['s_cmd'])
    c.xor_r8d(); c.xor_r9d()
    c.st_q0(0)                 # inherit = 0
    c.st_d(0x28, 0)            # flags = 0
    c.st_q(0x30, 0)            # env = 0
    c.st_q(0x38, 0)            # dir = 0
    c.st_q(0x40, 0)            # si = 0
    c.lea(0, D['pi'])          # rax = &pi
    c.st_rax(0x48)             # pi
    c.call(K['CreateProcessW'])
    c.mov_edx_eax()
    c.lea(1, D['f_create'])
    c.xor_eax()
    c.call(M['printf'])
    c.cmp_q_rip_0(D['pi'])
    patch = c.jz_fwd()
    c.mov_rip(1, D['pi'])
    c.mov_edx(30000)
    c.call(K['WaitForSingleObject'])
    c.mov_edx_eax()
    c.lea(1, D['f_wait'])
    c.xor_eax()
    c.call(M['printf'])
    c.mov_rip(1, D['pi'])
    c.lea(2, D['exitcode'])
    c.call(K['GetExitCodeProcess'])
    c.mov_rip(2, D['exitcode'])     # rdx = exit code (second %u arg)
    c.lea(1, D['f_exit'])
    c.xor_eax()
    c.call(M['printf'])
    c.patch_jz(patch)

    # ---- T2: attributes ----
    c.lea(1, D['s_attrpath'])
    c.mov_edx(0x40000000)
    c.xor_r8d(); c.xor_r9d()
    c.st_d(0x20, 2)            # CREATE_ALWAYS
    c.st_d(0x28, 0x80)         # FILE_ATTRIBUTE_NORMAL
    c.st_q(0x30, 0)
    c.call(K['CreateFileW'])
    c.store_eax(D['hfile'])
    c.mov_edx_eax()
    c.lea(1, D['f_cf'])
    c.xor_eax()
    c.call(M['printf'])
    c.mov_eax_rip(D['hfile'])
    c._e(b'\x89\xC1')          # mov ecx, eax
    c.call(K['CloseHandle'])
    c.lea(1, D['s_attrpath'])
    c.mov_edx(1)
    c.call(K['SetFileAttributesW'])
    c.mov_edx_eax()
    c.lea(1, D['f_sa'])
    c.xor_eax()
    c.call(M['printf'])
    c.lea(1, D['s_attrpath'])
    c.call(K['GetFileAttributesW'])
    c.store_eax(D['attrs'])
    c.mov_edx_eax()
    c.lea(1, D['f_ga'])
    c.xor_eax()
    c.call(M['printf'])
    c.mov_eax_rip(D['attrs'])
    c.and_eax(1)
    c.mov_edx_eax()
    c.lea(1, D['f_ro'])
    c.xor_eax()
    c.call(M['printf'])

    # ---- T3: registry (create-or-open the test key) ----
    c.mov_ecx(0x80000001)
    c.lea(2, D['s_regpath'])
    c.xor_r8d()                 # reserved
    c.xor_r9d()                 # class = NULL
    c.st_q(0x20, 0)             # opts = 0
    c.st_q(0x28, 0x000F003F)    # KEY_ALL_ACCESS
    c.st_q(0x30, 0)             # sa = NULL
    c.lea(0, D['hk'])
    c.st_rax(0x38)              # result
    c.st_q(0x40, 0)             # disp = NULL
    c.call(A['RegCreateKeyExW'])
    c.mov_edx_eax()
    c.lea(1, D['f_open'])
    c.xor_eax()
    c.call(M['printf'])
    c.mov_rip(2, D['hk'])
    c.lea(1, D['f_hk'])
    c.xor_eax()
    c.call(M['printf'])
    # default value set: (hk, NULL, 0, REG_SZ, s_defval, 18)
    c.mov_rip(1, D['hk'])
    c.xor_edx(); c.xor_r8d()
    c.mov_r9d(1)
    c.lea(0, D['s_defval'])
    c.st_rax(0x20)
    c.st_q(0x28, 18)
    c.call(A['RegSetValueExW'])
    c.mov_edx_eax()
    c.lea(1, D['f_sd'])
    c.xor_eax()
    c.call(M['printf'])
    # default value query: (hk, NULL, NULL, &type, wbuf, &wlen)
    c.mov_rip(1, D['hk'])
    c.xor_edx(); c.xor_r8d()
    c.lea(9, D['type'])
    c.lea(0, D['wbuf'])
    c.st_rax(0x20)
    c.lea(0, D['wlen'])
    c.st_rax(0x28)
    c.call(A['RegQueryValueExW'])
    c.mov_edx_eax()            # edx = return
    c.mov_eax_rip(D['type'])
    c._e(b'\x41\x89\xC0')      # r8d = type
    c.movzx_r9d_word_rip(D['wbuf'])  # r9d = wbuf[0]
    c.lea(1, D['f_qd'])
    c.xor_eax()
    c.call(M['printf'])
    # D1 (second wide char of the default string)
    c.lea(0, D['wbuf'])
    c._e(b'\x0F\xB7\x50\x02')  # movzx edx, word [rax+2]
    c.lea(1, D['f_d1'])
    c.xor_eax()
    c.call(M['printf'])
    # expand set: (hk, L"ExpVal", 0, REG_EXPAND_SZ, s_expval, 34)
    c.mov_rip(1, D['hk'])
    c.lea(2, D['s_expname'])
    c.xor_r8d()
    c.mov_r9d(2)
    c.lea(0, D['s_expval'])
    c.st_rax(0x20)
    c.st_q(0x28, 34)
    c.call(A['RegSetValueExW'])
    c.mov_edx_eax()
    c.lea(1, D['f_se'])
    c.xor_eax()
    c.call(M['printf'])
    # RegGetValueW RRF_RT_REG_SZ: (hk, NULL, L"ExpVal", 2, &type2, wbuf2, &len2)
    c.mov_rip(1, D['hk'])
    c.xor_edx()
    c.lea(8, D['s_expname'])
    c.mov_r9d(2)
    c.lea(0, D['type2'])
    c.st_rax(0x20)
    c.lea(0, D['wbuf2'])
    c.st_rax(0x28)
    c.lea(0, D['len2'])
    c.st_rax(0x30)
    c.call(A['RegGetValueW'])
    c.mov_edx_eax()            # edx = return
    c.mov_eax_rip(D['type2'])
    c._e(b'\x41\x89\xC0')      # r8d = type2
    c.mov_eax_rip(D['len2'])
    c._e(b'\x41\x89\xC1')      # r9d = len2
    c.lea(1, D['f_ge'])
    c.xor_eax()
    c.call(M['printf'])
    # print EXP0/EXP1 from wbuf2
    c.lea(0, D['wbuf2'])
    c._e(b'\x0F\xB7\x10')              # movzx edx, word [rax]
    c._e(b'\x44\x0F\xB7\x40\x02')      # movzx r8d, word [rax+2]
    c.lea(1, D['f_e0'])
    c.xor_eax()
    c.call(M['printf'])
    # type-mask rejection: RRF_RT_REG_DWORD (0x10) on a REG_EXPAND_SZ value
    c.mov_rip(1, D['hk'])
    c.xor_edx()
    c.lea(8, D['s_expname'])
    c.mov_r9d(0x10)
    c.lea(0, D['type3'])
    c.st_rax(0x20)
    c.lea(0, D['wbuf3'])
    c.st_rax(0x28)
    c.lea(0, D['len3'])
    c.st_rax(0x30)
    c.call(A['RegGetValueW'])
    c.mov_edx_eax()
    c.lea(1, D['f_un'])
    c.xor_eax()
    c.call(M['printf'])
    # close + exit 0
    c.mov_rip(1, D['hk'])
    c.call(A['RegCloseKey'])
    c.xor_ecx()
    c.call(K['ExitProcess'])

    text = bytes(c.b)
    rdata = iblob + b'\x00' * (align(len(iblob), 0x200) - len(iblob))
    return emit_pe(TEXT_RVA, text, rdata, bytes(data), drva, dsize)


if __name__ == '__main__':
    import os
    out = os.path.join(os.path.dirname(__file__), '..', '..',
                       'artifacts', 'proc-test')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'mw_proc_child.exe'), 'wb') as f:
        f.write(gen_child())
    print('child written')
    with open(os.path.join(out, 'mw_proc_parent.exe'), 'wb') as f:
        f.write(gen_parent())
    print('parent written')
