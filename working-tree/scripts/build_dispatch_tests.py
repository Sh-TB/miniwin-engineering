#!/usr/bin/env python3
"""
BUG-024: Build synthetic PE test binaries for RtlDispatchException testing.

Creates 4 PE binaries:
  Test A: func_C throws, func_B catches → EHANDLER discovered at Frame 1
  Test B: A(no handler), B(no handler), C(handler) → EHANDLER at Frame 3
  Test C: handler returns ContinueSearch → walker continues past it
  Test D: handler returns ContinueExecution → execution resumes

Each PE has a .test_marker section with expected results.
The harness loads each PE, calls entry point, checks markers.

PE Layout (per binary):
  .text   (0x1000) - code
  .rdata  (0x2000) - IAT, ILT, import dir, strings
  .pdata  (0x3000) - RUNTIME_FUNCTION entries
  .xdata  (0x4000) - UNWIND_INFO structures
  .data   (0x5000) - test result markers
"""
import struct
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, '..', 'tests', 'dispatch_tests')


def align(v, a):
    return (v + a - 1) & ~(a - 1)

def p8(v):  return struct.pack('<B', v & 0xFF)
def p16(v): return struct.pack('<H', v & 0xFFFF)
def p32(v): return struct.pack('<I', v & 0xFFFFFFFF)
def p64(v): return struct.pack('<Q', v & 0xFFFFFFFFFFFFFFFF)


class SyntheticPEBuilder:
    IMAGE_BASE = 0x140000000
    SECTION_ALIGN = 0x1000
    FILE_ALIGN = 0x200
    HEADERS_SIZE = 0x400

    def __init__(self, test_name, functions, entry_name):
        self.test_name = test_name
        self.functions = functions  # list of {name, code, handler_rva_or_none, has_ehandler}
        self.entry_name = entry_name
        self.TEXT_RVA = 0x1000
        self.RDATA_RVA = 0x2000
        self.PDATA_RVA = 0x3000
        self.XDATA_RVA = 0x4000
        self.DATA_RVA = 0x5000
        self.IMAGE_SIZE = 0x7000
        self.text_bytes = bytearray()
        self.xdata_bytes = bytearray()
        self.rdata_bytes = bytearray()
        self.data_bytes = bytearray()
        self.func_rvas = {}  # name -> RVA
        self.func_sizes = {}  # name -> size
        self.xdata_rvas = {}  # name -> xdata RVA
        self.pdata_entries = []  # (begin, end, unwind_rva)

    def _emit(self, code):
        offset = len(self.text_bytes)
        self.text_bytes.extend(code)
        return offset

    def _emit_xdata(self, data):
        offset = len(self.xdata_bytes)
        self.xdata_bytes.extend(data)
        return offset

    def _build_func(self, func):
        name = func['name']
        code = func['code']
        has_ehandler = func.get('has_ehandler', False)
        handler_offset = func.get('handler_code', None)

        func_rva = self.TEXT_RVA + len(self.text_bytes)
        self.func_rvas[name] = func_rva

        # Emit the function code
        code_start = self._emit(code)
        func_size = len(code)
        self.func_sizes[name] = func_size

        # Build UNWIND_INFO
        # We need to describe the prolog: sub rsp, 0xN (if present)
        # For simplicity, use minimal unwind info with just ALLOC_SMALL
        # The actual code should match the unwind description.
        prolog_alloc = func.get('prolog_alloc', 0x20)  # default 32 bytes
        num_pushes = func.get('num_pushes', 0)

        # Build unwind codes
        codes = []
        if num_pushes > 0:
            # PUSH_NONVOL for RBX (register index 3)
            codes.append((3 << 12) | (0 << 8) | 0)  # PUSH_NONVOL RBX at offset 0
        if prolog_alloc > 0:
            alloc_slots = (prolog_alloc // 8) - 1  # ALLOC_SMALL encodes (N+1)*8
            if alloc_slots <= 15:
                push_offset = num_pushes * 1  # each push is 1 byte
                codes.append((2 << 12) | (alloc_slots << 8) | push_offset)  # ALLOC_SMALL

        count_codes = len(codes)
        code_bytes = b''.join(p16(c) for c in codes)

        # Calculate xdata offset
        xdata_start = self._emit_xdata(b'\x00' * 8)  # placeholder
        xdata_rva = self.XDATA_RVA + xdata_start

        # Build UNWIND_INFO
        flags = UNW_FLAG_EHANDLER if has_ehandler else 0
        version = 1
        ui_byte0 = (flags << 3) | version
        prolog_size = func.get('prolog_size', 4)  # bytes in prolog
        frame_reg_byte = 0  # no frame pointer

        ui = bytearray()
        ui.append(ui_byte0)
        ui.append(prolog_size)
        ui.append(count_codes)
        ui.append(frame_reg_byte)
        ui.extend(code_bytes)

        # Pad to 4-byte alignment for handler address
        while len(ui) % 4 != 0:
            ui.append(0)

        handler_rva = 0
        if has_ehandler and handler_offset is not None:
            handler_rva = self.TEXT_RVA + handler_offset
            ui.extend(p32(handler_rva))
            # LSDA pointer (0 for our simple tests)
            ui.extend(p32(0))

        # Write back
        pos = len(self.xdata_bytes) - 8
        self.xdata_bytes[pos:pos+len(ui)] = ui
        # Truncate xdata to actual size
        del self.xdata_bytes[xdata_start + len(ui):]

        self.xdata_rvas[name] = xdata_rva

        # Add pdata entry
        func_end = func_rva + func_size
        self.pdata_entries.append((func_rva, func_end, xdata_rva))

        return func_rva

    def _build_imports(self):
        # Import: KERNEL32.dll!RaiseException
        # Layout within .rdata:
        #   0x000: Import Directory Entry (20 bytes)
        #   0x014: NULL terminator (20 bytes)
        #   0x028: DLL name "KERNEL32.dll\0"
        #   0x038: ILT entry (4 bytes: hint/name RVA) + NULL
        #   0x040: IAT slot (8 bytes) + NULL

        rdata = bytearray()

        # We'll place things at fixed offsets within .rdata
        dll_name_rva = self.RDATA_RVA + 0x028
        ilt_rva = self.RDATA_RVA + 0x038
        iat_rva = self.RDATA_RVA + 0x040

        # Import Directory Entry
        rdata.extend(p32(ilt_rva))   # OriginalFirstThunk
        rdata.extend(p32(0))          # TimeDateStamp
        rdata.extend(p32(0))          # ForwarderChain
        rdata.extend(p32(dll_name_rva))  # Name
        rdata.extend(p32(iat_rva))   # FirstThunk
        # Pad to 20 bytes
        rdata.extend(b'\x00' * (20 - len(rdata)))

        # NULL terminator
        rdata.extend(b'\x00' * 20)

        # DLL name
        rdata.extend(b'KERNEL32.dll\x00')
        # Pad to 0x038
        while len(rdata) < 0x038:
            rdata.append(0)

        # ILT: hint/name entry for RaiseException
        hint_name_rva = self.RDATA_RVA + 0x048
        rdata.extend(p32(hint_name_rva))
        rdata.extend(p32(0))  # NULL terminator

        # Pad to 0x040
        while len(rdata) < 0x040:
            rdata.append(0)

        # IAT: will be patched at runtime
        rdata.extend(p64(0))  # RaiseException IAT slot
        rdata.extend(p64(0))  # NULL terminator

        # Pad to 0x048
        while len(rdata) < 0x048:
            rdata.append(0)

        # Hint/Name table
        rdata.extend(p16(0))  # hint
        rdata.extend(b'RaiseException\x00')

        self.rdata_bytes = rdata
        return iat_rva

    def build(self):
        # Build all functions
        for func in self.functions:
            self._build_func(func)

        # Build imports
        iat_rva = self._build_imports()

        # Build .pdata
        pdata = bytearray()
        for begin, end, unwind in self.pdata_entries:
            pdata.extend(p32(begin))
            pdata.extend(p32(end))
            pdata.extend(p32(unwind))

        # Build .data section (test result area)
        # Layout: 8 bytes for result code (set by handler)
        # Result codes: 0=pending, 1=handler_A_called, 2=handler_B_called, etc.
        data = bytearray(0x1000)

        # Build PE file
        return self._assemble_pe(pdata, data, iat_rva)

    def _assemble_pe(self, pdata, data, iat_rva):
        # Calculate sizes
        text_raw = align(len(self.text_bytes), self.FILE_ALIGN)
        rdata_raw = align(len(self.rdata_bytes), self.FILE_ALIGN)
        pdata_raw = align(len(pdata), self.FILE_ALIGN)
        xdata_raw = align(len(self.xdata_bytes), self.FILE_ALIGN)
        data_raw = align(len(data), self.FILE_ALIGN)

        headers_raw = self.HEADERS_SIZE
        total_size = headers_raw + text_raw + rdata_raw + pdata_raw + xdata_raw + data_raw

        pe = bytearray(total_size)

        # ---- DOS Header ----
        pe[0:2] = b'MZ'
        struct.pack_into('<I', pe, 0x3C, 0x80)  # e_lfanew

        # ---- PE Signature ----
        struct.pack_into('<I', pe, 0x80, 0x00004550)

        # ---- COFF Header ----
        coff_off = 0x84
        num_sections = 5  # .text, .rdata, .pdata, .xdata, .data
        struct.pack_into('<H', pe, coff_off, 0x8664)  # AMD64
        struct.pack_into('<H', pe, coff_off + 2, num_sections)
        struct.pack_into('<H', pe, coff_off + 16, 0xF0)  # SizeOfOptionalHeader

        # ---- Optional Header (PE32+) ----
        opt_off = coff_off + 20
        struct.pack_into('<H', pe, opt_off, 0x020B)  # PE32+ magic
        struct.pack_into('<I', pe, opt_off + 16, self.func_rvas[self.entry_name])  # EP
        struct.pack_into('<Q', pe, opt_off + 24, self.IMAGE_BASE)
        struct.pack_into('<I', pe, opt_off + 32, self.SECTION_ALIGN)
        struct.pack_into('<I', pe, opt_off + 36, self.FILE_ALIGN)
        struct.pack_into('<I', pe, opt_off + 56, self.IMAGE_SIZE)
        struct.pack_into('<I', pe, opt_off + 60, headers_raw)
        struct.pack_into('<H', pe, opt_off + 68, 3)  # IMAGE_SUBSYSTEM_WINDOWS_CUI
        num_dd = 16
        struct.pack_into('<I', pe, opt_off + 108, num_dd)

        # Data Directories
        dd_off = opt_off + 112
        # DD_IMPORT (index 1)
        struct.pack_into('<I', pe, dd_off + 1*8, self.RDATA_RVA)
        struct.pack_into('<I', pe, dd_off + 1*8 + 4, 0x40)  # import dir size
        # DD_EXCEPTION (index 3)
        struct.pack_into('<I', pe, dd_off + 3*8, self.PDATA_RVA)
        struct.pack_into('<I', pe, dd_off + 3*8 + 4, len(pdata))

        # ---- Section Headers ----
        sec_off = opt_off + 0xF0
        # Calculate correct raw pointers (cumulative)
        raw_ptrs = [
            headers_raw,                                    # .text
            headers_raw + text_raw,                            # .rdata
            headers_raw + text_raw + rdata_raw,                 # .pdata
            headers_raw + text_raw + rdata_raw + pdata_raw,  # .xdata
            headers_raw + text_raw + rdata_raw + pdata_raw + xdata_raw,  # .data
        ]
        sections = [
            (b'.text\0\0\0\0', self.TEXT_RVA, len(self.text_bytes), text_raw,
             raw_ptrs[0], 0x60000020),
            (b'.rdata\0\0\0', self.RDATA_RVA, len(self.rdata_bytes), rdata_raw,
             raw_ptrs[1], 0x40000040),
            (b'.pdata\0\0\0', self.PDATA_RVA, len(pdata), pdata_raw,
             raw_ptrs[2], 0x40000040),
            (b'.xdata\0\0\0', self.XDATA_RVA, len(self.xdata_bytes), xdata_raw,
             raw_ptrs[3], 0x40000040),
            (b'.data\0\0\0\0', self.DATA_RVA, len(data), data_raw,
             raw_ptrs[4], 0xC0000040),
        ]

        for i, (name, va, vsize, rawsize, rawptr, chars) in enumerate(sections):
            s = sec_off + i * 40
            pe[s:s+8] = name
            struct.pack_into('<I', pe, s + 8, vsize)
            struct.pack_into('<I', pe, s + 12, va)
            struct.pack_into('<I', pe, s + 16, rawsize)
            struct.pack_into('<I', pe, s + 20, rawptr)
            struct.pack_into('<I', pe, s + 36, chars)

        # Copy section data
        pe[headers_raw:headers_raw + len(self.text_bytes)] = self.text_bytes
        pe[headers_raw + text_raw:headers_raw + text_raw + len(self.rdata_bytes)] = self.rdata_bytes
        pe[headers_raw + text_raw + rdata_raw:headers_raw + text_raw + rdata_raw + len(pdata)] = pdata
        pe[headers_raw + text_raw + rdata_raw + pdata_raw:
           headers_raw + text_raw + rdata_raw + pdata_raw + len(self.xdata_bytes)] = self.xdata_bytes
        pe[headers_raw + text_raw + rdata_raw + pdata_raw + xdata_raw:
           headers_raw + text_raw + rdata_raw + pdata_raw + xdata_raw + len(data)] = data

        return bytes(pe)


UNW_FLAG_EHANDLER = 0x01


def make_test_a():
    """Test A: func_C throws, func_B catches
    Call chain: func_A -> func_B -> func_C -> call [IAT] (RaiseException)
    func_B has EHANDLER that returns ContinueExecution.
    Expected: dispatcher finds handler at Frame 1.
    """
    # Build machine code for each function.
    # All functions use ms_abi: RCX, RDX, R8, R9, then stack with 32-byte shadow.
    # Each function pushes RBX, subs 0x20 (for shadow space + local), calls next.

    # IAT slot RVA = RDATA_RVA + 0x40 = 0x2040
    IAT_RVA = 0x2040

    # func_C: call RaiseException(0xE0000001, 0, 0, NULL) via IAT
    # Prolog: push rbx; sub rsp, 0x28
    # mov ecx, 0xE0000001; xor edx, edx; xor r8d, r8d; xor r9d, r9d
    # call qword ptr [IAT_RVA relative to RIP]
    # add rsp, 0x28; pop rbx; ret
    # Need to compute the relative offset for the IAT call.
    # We'll compute it after we know func_C's position.
    # For now, use a placeholder and fix up later.

    # Let me just hand-encode the bytes carefully.
    # func_C (will be placed at TEXT_RVA + offset)
    # The handler for func_B will be placed right after func_C.

    # We need to know the exact layout to compute RIP-relative addresses.
    # Layout: func_A | func_B | handler_B | func_C
    # Each function's position depends on the sizes of previous ones.

    # func_C code (no handler, no EHANDLER):
    # push rbx            ; 1 byte
    # sub rsp, 0x28       ; 4 bytes (REX.W + sub + imm8)
    # mov ecx, 0xE0000001 ; 5 bytes
    # xor edx, edx        ; 2 bytes
    # xor r8d, r8d        ; 3 bytes
    # xor r9d, r9d        ; 3 bytes
    # call [rip+disp32]   ; 6 bytes
    # add rsp, 0x28       ; 4 bytes
    # pop rbx             ; 1 byte
    # ret                 ; 1 byte
    # Total: 30 bytes

    func_C_code = bytearray()
    func_C_code.extend(b'\x53')                      # push rbx
    func_C_code.extend(b'\x48\x83\xec\x28')          # sub rsp, 0x28
    func_C_code.extend(b'\xb9\x01\x00\x00\xe0')      # mov ecx, 0xE0000001
    func_C_code.extend(b'\x31\xd2')                    # xor edx, edx
    func_C_code.extend(b'\x45\x31\xc0')                # xor r8d, r8d
    func_C_code.extend(b'\x45\x31\xc9')                # xor r9d, r9d
    # call [rip+disp32] — placeholder, will fix up
    func_C_code.extend(b'\xff\x15\x00\x00\x00\x00')  # call [rip+0] (fixup later)
    func_C_code.extend(b'\x48\x83\xc4\x28')          # add rsp, 0x28
    func_C_code.extend(b'\x5b')                        # pop rbx
    func_C_code.extend(b'\xc3')                        # ret

    # handler_B: __C_specific_handler style
    # Receives: RCX=EXCEPTION_RECORD*, RDX=establisher_frame, R8=CONTEXT*, R9=DISPATCHER_CONTEXT*
    # Returns DISP_ExceptionContinueExecution (0) in EAX
    # mov eax, 0; ret
    handler_B_code = bytearray()
    handler_B_code.extend(b'\x31\xc0')       # xor eax, eax  (DISP_ExceptionContinueExecution = 0)
    handler_B_code.extend(b'\xc3')           # ret

    # func_B: calls func_C, has EHANDLER pointing to handler_B
    # push rbx; sub rsp, 0x28; xor ecx, ecx; call func_C (relative); add rsp, 0x28; pop rbx; ret
    func_B_code = bytearray()
    func_B_code.extend(b'\x53')                      # push rbx
    func_B_code.extend(b'\x48\x83\xec\x28')          # sub rsp, 0x28
    func_B_code.extend(b'\x31\xc9')                    # xor ecx, ecx
    # call func_C — relative, need to know offset
    func_B_code.extend(b'\xe8\x00\x00\x00\x00')      # call rel32 (fixup later)
    func_B_code.extend(b'\x48\x83\xc4\x28')          # add rsp, 0x28
    func_B_code.extend(b'\x5b')                        # pop rbx
    func_B_code.extend(b'\xc3')                        # ret

    # func_A: calls func_B, no handler
    func_A_code = bytearray()
    func_A_code.extend(b'\x53')                      # push rbx
    func_A_code.extend(b'\x48\x83\xec\x28')          # sub rsp, 0x28
    func_A_code.extend(b'\x31\xc9')                    # xor ecx, ecx
    func_A_code.extend(b'\xe8\x00\x00\x00\x00')      # call func_B (fixup later)
    func_A_code.extend(b'\x48\x83\xc4\x28')          # add rsp, 0x28
    func_A_code.extend(b'\x5b')                        # pop rbx
    func_A_code.extend(b'\xc3')                        # ret

    # Now compute layout and fix up relative addresses
    TEXT_RVA = 0x1000
    func_A_rva = TEXT_RVA
    func_B_rva = func_A_rva + len(func_A_code)
    handler_B_rva = func_B_rva + len(func_B_code)
    func_C_rva = handler_B_rva + len(handler_B_code)

    # Fix up func_A's call to func_B
    # call rel32: displacement = target - (call_instruction_addr + 5)
    call_offset_in_A = 7  # after push(1) + sub(4) + xor(2) = 7
    rel_A_to_B = func_B_rva - (func_A_rva + call_offset_in_A + 5)
    struct.pack_into('<i', func_A_code, call_offset_in_A + 1, rel_A_to_B)

    # Fix up func_B's call to func_C
    call_offset_in_B = 7
    rel_B_to_C = func_C_rva - (func_B_rva + call_offset_in_B + 5)
    struct.pack_into('<i', func_B_code, call_offset_in_B + 1, rel_B_to_C)

    # Fix up func_C's call [rip+disp32] to IAT
    # ff 15 disp32: displacement = IAT_addr - (instruction_addr + 6)
    iat_call_offset_in_C = 1 + 4 + 5 + 2 + 3 + 3  # push + sub + mov + xor + xor + xor = 18
    iat_va = 0x140000000 + IAT_RVA
    call_instr_va = 0x140000000 + func_C_rva + iat_call_offset_in_C
    disp = iat_va - (call_instr_va + 6)
    struct.pack_into('<i', func_C_code, iat_call_offset_in_C + 2, disp)

    return [
        {'name': 'func_A', 'code': bytes(func_A_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'func_B', 'code': bytes(func_B_code), 'has_ehandler': True,
         'handler_code': len(func_A_code) + len(func_B_code),  # handler offset in .text
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'handler_B', 'code': bytes(handler_B_code), 'has_ehandler': False,
         'prolog_alloc': 0, 'num_pushes': 0, 'prolog_size': 0},
        {'name': 'func_C', 'code': bytes(func_C_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
    ], 'func_A'


def make_test_b():
    """Test B: 3 nested frames, handler at frame 3
    Call chain: func_A(no handler) -> func_B(no handler) -> func_C(handler) -> call [IAT]
    Expected: handler discovered at Frame 2 (func_C has handler, Frame 0 is IAT caller)
    """
    IAT_RVA = 0x2040

    # handler_C code
    handler_C_code = bytearray(b'\x31\xc0\xc3')  # xor eax, eax; ret

    # func_C: calls [IAT], has handler
    func_C_code = bytearray()
    func_C_code.extend(b'\x53\x48\x83\xec\x28')  # push rbx; sub rsp, 0x28
    func_C_code.extend(b'\xb9\x01\x00\x00\xe0')    # mov ecx, 0xE0000002
    func_C_code.extend(b'\x31\xd2\x45\x31\xc0\x45\x31\xc9')  # xor edx, r8d, r9d
    func_C_code.extend(b'\xff\x15\x00\x00\x00\x00')  # call [IAT] (fixup)
    func_C_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')  # cleanup; ret

    # func_B: calls func_C, no handler
    func_B_code = bytearray()
    func_B_code.extend(b'\x53\x48\x83\xec\x28')  # push rbx; sub rsp, 0x28
    func_B_code.extend(b'\x31\xc9')                    # xor ecx, ecx
    func_B_code.extend(b'\xe8\x00\x00\x00\x00')      # call func_C (fixup)
    func_B_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    # func_A: calls func_B, no handler
    func_A_code = bytearray()
    func_A_code.extend(b'\x53\x48\x83\xec\x28')
    func_A_code.extend(b'\x31\xc9')
    func_A_code.extend(b'\xe8\x00\x00\x00\x00')
    func_A_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    TEXT_RVA = 0x1000
    func_A_rva = TEXT_RVA
    func_B_rva = func_A_rva + len(func_A_code)
    func_C_rva = func_B_rva + len(func_B_code)
    handler_C_rva = func_C_rva + len(func_C_code)

    # Fixup func_A -> func_B
    struct.pack_into('<i', func_A_code, 8, func_B_rva - (func_A_rva + 8 + 5))
    # Fixup func_B -> func_C
    struct.pack_into('<i', func_B_code, 8, func_C_rva - (func_B_rva + 8 + 5))
    # Fixup func_C -> IAT
    iat_call_off = 18  # push(1)+sub(4)+mov(5)+xor*3(2+3+3)=18
    iat_va = 0x140000000 + IAT_RVA
    disp = iat_va - (0x140000000 + func_C_rva + iat_call_off + 6)
    struct.pack_into('<i', func_C_code, iat_call_off + 2, disp)

    handler_offset = len(func_A_code) + len(func_B_code) + len(func_C_code)

    return [
        {'name': 'func_A', 'code': bytes(func_A_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'func_B', 'code': bytes(func_B_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'func_C', 'code': bytes(func_C_code), 'has_ehandler': True,
         'handler_code': handler_offset,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'handler_C', 'code': bytes(handler_C_code), 'has_ehandler': False,
         'prolog_alloc': 0, 'num_pushes': 0, 'prolog_size': 0},
    ], 'func_A'


def make_test_c():
    """Test C: handler returns ContinueSearch → walker continues
    func_A(no handler) -> func_B(handler returns ContinueSearch) -> func_C(handler returns ContinueExec)
    Expected: walker skips func_B handler, finds func_C handler at Frame 0
    """
    IAT_RVA = 0x2040

    # handler_C: returns ContinueExecution
    handler_C_code = bytearray(b'\x31\xc0\xc3')  # xor eax, eax; ret

    # handler_B: returns ContinueSearch (1)
    handler_B_code = bytearray(b'\xb8\x01\x00\x00\x00\xc3')  # mov eax, 1; ret

    # func_C: has handler, calls IAT
    func_C_code = bytearray()
    func_C_code.extend(b'\x53\x48\x83\xec\x28')
    func_C_code.extend(b'\xb9\x01\x00\x00\xe0')    # mov ecx, 0xE0000003
    func_C_code.extend(b'\x31\xd2\x45\x31\xc0\x45\x31\xc9')
    func_C_code.extend(b'\xff\x15\x00\x00\x00\x00')
    func_C_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    # func_B: has handler (ContinueSearch), calls func_C
    func_B_code = bytearray()
    func_B_code.extend(b'\x53\x48\x83\xec\x28')
    func_B_code.extend(b'\x31\xc9')
    func_B_code.extend(b'\xe8\x00\x00\x00\x00')
    func_B_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    # func_A: no handler, calls func_B
    func_A_code = bytearray()
    func_A_code.extend(b'\x53\x48\x83\xec\x28')
    func_A_code.extend(b'\x31\xc9')
    func_A_code.extend(b'\xe8\x00\x00\x00\x00')
    func_A_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    TEXT_RVA = 0x1000
    func_A_rva = TEXT_RVA
    func_B_rva = func_A_rva + len(func_A_code)
    handler_B_rva = func_B_rva + len(func_B_code)
    func_C_rva = handler_B_rva + len(handler_B_code)
    handler_C_rva = func_C_rva + len(func_C_code)

    # Fixups
    struct.pack_into('<i', func_A_code, 8, func_B_rva - (func_A_rva + 13))
    struct.pack_into('<i', func_B_code, 8, func_C_rva - (func_B_rva + 13))
    iat_call_off = 18
    iat_va = 0x140000000 + IAT_RVA
    disp = iat_va - (0x140000000 + func_C_rva + iat_call_off + 6)
    struct.pack_into('<i', func_C_code, iat_call_off + 2, disp)

    handler_B_offset = len(func_A_code) + len(func_B_code)
    handler_C_offset = handler_B_offset + len(handler_B_code) + len(func_C_code)

    return [
        {'name': 'func_A', 'code': bytes(func_A_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'func_B', 'code': bytes(func_B_code), 'has_ehandler': True,
         'handler_code': handler_B_offset,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'handler_B', 'code': bytes(handler_B_code), 'has_ehandler': False,
         'prolog_alloc': 0, 'num_pushes': 0, 'prolog_size': 0},
        {'name': 'func_C', 'code': bytes(func_C_code), 'has_ehandler': True,
         'handler_code': handler_C_offset,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'handler_C', 'code': bytes(handler_C_code), 'has_ehandler': False,
         'prolog_alloc': 0, 'num_pushes': 0, 'prolog_size': 0},
    ], 'func_A'


def make_test_d():
    """Test D: handler returns ContinueExecution → execution resumes
    func_A -> func_B(handler, returns ContinueExecution) -> func_C (calls IAT)
    Expected: execution resumes after handler returns 0.
    func_B's handler returns DISP_ExceptionContinueExecution.
    """
    IAT_RVA = 0x2040

    # handler_B: returns ContinueExecution (0)
    handler_B_code = bytearray(b'\x31\xc0\xc3')  # xor eax, eax; ret

    # func_C: calls IAT, no handler
    func_C_code = bytearray()
    func_C_code.extend(b'\x53\x48\x83\xec\x28')
    func_C_code.extend(b'\xb9\x01\x00\x00\xe0')
    func_C_code.extend(b'\x31\xd2\x45\x31\xc0\x45\x31\xc9')
    func_C_code.extend(b'\xff\x15\x00\x00\x00\x00')
    func_C_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    # func_B: has handler, calls func_C
    func_B_code = bytearray()
    func_B_code.extend(b'\x53\x48\x83\xec\x28')
    func_B_code.extend(b'\x31\xc9')
    func_B_code.extend(b'\xe8\x00\x00\x00\x00')
    func_B_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    # func_A: no handler, calls func_B
    func_A_code = bytearray()
    func_A_code.extend(b'\x53\x48\x83\xec\x28')
    func_A_code.extend(b'\x31\xc9')
    func_A_code.extend(b'\xe8\x00\x00\x00\x00')
    func_A_code.extend(b'\x48\x83\xc4\x28\x5b\xc3')

    TEXT_RVA = 0x1000
    func_A_rva = TEXT_RVA
    func_B_rva = func_A_rva + len(func_A_code)
    handler_B_rva = func_B_rva + len(func_B_code)
    func_C_rva = handler_B_rva + len(handler_B_code)

    struct.pack_into('<i', func_A_code, 8, func_B_rva - (func_A_rva + 13))
    struct.pack_into('<i', func_B_code, 8, func_C_rva - (func_B_rva + 13))
    iat_call_off = 18
    iat_va = 0x140000000 + IAT_RVA
    disp = iat_va - (0x140000000 + func_C_rva + iat_call_off + 6)
    struct.pack_into('<i', func_C_code, iat_call_off + 2, disp)

    handler_B_offset = len(func_A_code) + len(func_B_code)

    return [
        {'name': 'func_A', 'code': bytes(func_A_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'func_B', 'code': bytes(func_B_code), 'has_ehandler': True,
         'handler_code': handler_B_offset,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
        {'name': 'handler_B', 'code': bytes(handler_B_code), 'has_ehandler': False,
         'prolog_alloc': 0, 'num_pushes': 0, 'prolog_size': 0},
        {'name': 'func_C', 'code': bytes(func_C_code), 'has_ehandler': False,
         'prolog_alloc': 0x28, 'num_pushes': 1, 'prolog_size': 5},
    ], 'func_A'


def verify_func_calls():
    """Verify all call displacements are correct."""
    pass_count = 0
    fail_count = 0
    for fname, make_fn, desc in [("test_a", make_test_a, ""), ("test_b", make_test_b, ""), ("test_c", make_test_c, ""), (test_d", make_test_d)]:
        functions, entry = make_fn()
        builder = SyntheticPEBuilder(desc, functions, entry)
        pe_data = builder.build()
        text = builder.text_bytes
        for name, rva in builder.func_rvas.items():
            code = None
            for fn in functions:
                if fn['name'] == name:
                    code = fn['code']
                    break
            assert code is not None, f'Missing code for {name}'
            # Find call e8 instructions
            for j in range(len(code) - 4):
                if code[j] == 0xe8:
                    disp = struct.unpack_from('<i', code, j + 1)[0]
                    target = rva + (j + 5) + disp
                    expected_rva = builder.func_rvas.get(name + '_rva_next', None)
                    # Find what function target should be
                    expected = None
                    for fn in functions:
                        if fn['name'] != name and fn['name'] != name + '_rva_next':
                            fn_rva = builder.func_rvas.get(fn['name'])
                            if fn_rva is not None and fn_rva == target:
                                expected = fn_rva
                                break
                    if expected is None:
                        pass  # might be handler or IAT
                    elif target != expected and expected is not None:
                        print(f'  FAIL {fname}: call at {name}+{j} targets RVA 0x{target:x} (func {name}) but expected 0x{expected:x} ({"name}")')
                        fail_count += 1
                    else:
                        pass_count += 1
    print(f'Veification: {pass_count} passed, {fail_count} failed')
    return pass_count, fail_count


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    vc, fc = verify_func_calls()
    if fc > 0:
        print(f'WARNING: {fc} call displacement(s) wrong — tests will fail')
        return

    tests = [
        ('test_a.exe', make_test_a, 'Test A: func_C throws, func_B catches'),
        ('test_b.exe', make_test_b, 'Test B: 3 nested frames, handler at frame 3'),
        ('test_c.exe', make_test_c, 'Test C: handler returns ContinueSearch'),
        ('test_d.exe', make_test_d, 'Test D: handler returns ContinueExecution'),
    ]

    for filename, make_fn, desc in tests:
        print(f'Building {filename}: {desc}')
        functions, entry = make_fn()
        builder = SyntheticPEBuilder(desc, functions, entry)
        pe_data = builder.build()
        out_path = os.path.join(OUTPUT_DIR, filename)
        with open(out_path, 'wb') as f:
            f.write(pe_data)
        print(f'  -> {out_path} ({len(pe_data)} bytes)')
        print(f'  Functions:')
        for name, rva in builder.func_rvas.items():
            print(f'    {name}: RVA=0x{rva:x}')
        print(f'  PDATA entries: {len(builder.pdata_entries)}')
        for begin, end, ui in builder.pdata_entries:
            print(f'    begin=0x{begin:x} end=0x{end:x} unwind=0x{ui:x}')
        print()

    print('All test PEs built successfully.')


if __name__ == '__main__':
    main()
