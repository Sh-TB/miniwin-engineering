#!/usr/bin/env python3
"""
EXP-NEXT-2: Build a minimal PE test binary with 3 functions and proper UNWIND metadata.

Call chain: A -> B -> C -> RaiseException(IAT)
Function B has UNW_FLAG_EHANDLER set, pointing to a synthetic handler.

PE Layout:
  .text  (0x1000) - code for func_A, func_B, func_C, handler
  .rdata (0x2000) - IAT, ILT, import directory, string table
  .pdata (0x3000) - RUNTIME_FUNCTION entries for A, B, C
  .xdata (0x4000) - UNWIND_INFO for A, B, C
  .reloc (0x5000) - empty (no relocs needed for fixed base)

ImageBase: 0x140000000
"""
import struct
import sys
import os

def align(v, a):
    return (v + a - 1) & ~(a - 1)

def p8(v):  return struct.pack('<B', v & 0xFF)
def p16(v): return struct.pack('<H', v & 0xFFFF)
def p32(v): return struct.pack('<I', v & 0xFFFFFFFF)
def p64(v): return struct.pack('<Q', v & 0xFFFFFFFFFFFFFFFF)

IMAGE_BASE  = 0x140000000
SECTION_ALIGN = 0x1000
FILE_ALIGN   = 0x200

# ===================================================================
# Section RVAs (after headers)
# ===================================================================
HEADERS_SIZE = align(0x400, FILE_ALIGN)  # 0x400

TEXT_RVA   = align(HEADERS_SIZE, SECTION_ALIGN)  # 0x1000
RDATA_RVA  = TEXT_RVA  + 0x1000                    # 0x2000
PDATA_RVA  = RDATA_RVA + 0x1000                    # 0x3000
XDATA_RVA  = PDATA_RVA + 0x1000                    # 0x4000
RELOC_RVA  = XDATA_RVA + 0x1000                    # 0x5000

IMAGE_SIZE = RELOC_RVA + 0x1000                     # 0x6000

# ===================================================================
# Code: Build function bytes
# ===================================================================
# We build code in .text at offset 0 from TEXT_RVA.
# All addresses below are RVAs within .text section.

code = bytearray()

# --- Helper: append Intel x86-64 bytes ---
# We'll use known byte sequences for simple functions.

# Function C (throws exception):
#   ; MS ABI: rcx=code, rdx=flags, r8=nargs, r9=args
#   ; But we want to use 4 parameters, which fit in registers
#   mov eax, 0xE0000003      ; exception code (synthetic, not GCC)
#   mov edx, 0               ; flags = 0
#   mov r8d, 0               ; nargs = 0
#   xor r9, r9               ; args = NULL
#   ; Jump to RaiseException through IAT
#   ; IAT entry for RaiseException is at RVA (RDATA_RVA + IAT_OFFSET_RE)
#   mov rax, QWORD [rip + disp32]  ; load IAT entry address
#   jmp [rax]                       ; indirect jump to RaiseException
#
# Actually, simpler: call [IAT entry] directly
#   call QWORD [rip + disp32]
#   ret

# Let's compute offsets first. We need to know where each function starts.

# func_C_rva = 0x000 (relative to TEXT_RVA)
# Prolog: push rbx (nonvolatile, for unwind test)
#         sub rsp, 0x20  (32 bytes = 4 slots, 16-byte aligned with shadow)
#         mov rbx, rcx   (save parameter)
# Body:  mov ecx, 0xE0000003
#         xor edx, edx
#         xor r8d, r8d
#         xor r9d, r9d
#         call [rip+disp32]  ; call RaiseException via IAT
#         ; RaiseException should not return in our test, but just in case:
#         add rsp, 0x20
#         pop rbx
#         ret
# Epilog: add rsp, 0x20; pop rbx; ret

# func_C bytes (we'll fill in the IAT disp later)
func_C_bytes = bytearray()
func_C_bytes += b'\x53'                           # push rbx
func_C_bytes += b'\x48\x83\xec\x20'              # sub rsp, 0x20
func_C_bytes += b'\x48\x89\xcb'                  # mov rbx, rcx
# mov ecx, 0xE0000003
func_C_bytes += b'\xb9\x03\x00\x00\xe0'
# xor edx, edx
func_C_bytes += b'\x31\xd2'
# xor r8d, r8d
func_C_bytes += b'\x45\x31\xc0'
# xor r9d, r9d
func_C_bytes += b'\x45\x31\xc9'
# call QWORD [rip + 0x00000000] -- placeholder, 6 bytes
# opcode = FF 15 xx xx xx xx (call [rip+disp32])
func_C_bytes += b'\xff\x15\x00\x00\x00\x00'  # placeholder disp32
# After this call: add rsp, 0x20
func_C_bytes += b'\x48\x83\xc4\x20'              # add rsp, 0x20
func_C_bytes += b'\x5b'                           # pop rbx
func_C_bytes += b'\xc3'                           # ret

assert len(func_C_bytes) <= 256, f"func_C too large: {len(func_C_bytes)}"

func_C_rva = 0x000
func_C_end = len(func_C_bytes)

# func_B_rva = aligned
func_B_rva = align(func_C_end, 16)  # align to 16 for safety
func_B_code_offset = func_B_rva

# Function B (has exception handler):
#   push rbp
#   push rbx
#   sub rsp, 0x30  ; 48 bytes = 6 slots (16-byte aligned with 2 pushes)
#   mov rbp, rsp
#   mov rbx, rcx   ; save parameter
#   ; call C with one parameter (exception code)
#   mov ecx, 0xC0DE0001  ; arbitrary parameter for func_C
#   call func_C_rel32
#   ; after C returns (if handler caught it)
#   xor eax, eax
#   add rsp, 0x30
#   pop rbx
#   pop rbp
#   ret

func_B_bytes = bytearray()
func_B_bytes += b'\x55'                           # push rbp
func_B_bytes += b'\x53'                           # push rbx
func_B_bytes += b'\x48\x83\xec\x30'              # sub rsp, 0x30
func_B_bytes += b'\x48\x89\xe5'                  # mov rbp, rsp
func_B_bytes += b'\x48\x89\xcb'                  # mov rbx, rcx
# mov ecx, 0xC0DE0001
func_B_bytes += b'\xb9\x01\x00\xde\xc0'
# call rel32 to func_C -- placeholder, 5 bytes: E8 xx xx xx xx
func_B_bytes += b'\xe8\x00\x00\x00\x00'  # placeholder
# xor eax, eax
func_B_bytes += b'\x31\xc0'
# add rsp, 0x30
func_B_bytes += b'\x48\x83\xc4\x30'
# pop rbx
func_B_bytes += b'\x5b'
# pop rbp
func_B_bytes += b'\x5d'
# ret
func_B_bytes += b'\xc3'

func_B_end_offset = func_B_rva + len(func_B_bytes)

# func_A_rva
func_A_rva = align(func_B_end_offset, 16)
func_A_code_offset = func_A_rva

# Function A (calls B, no handler):
#   sub rsp, 0x28  ; 40 bytes = 5 slots
#   call func_B_rel32
#   add rsp, 0x28
#   ret

func_A_bytes = bytearray()
func_A_bytes += b'\x48\x83\xec\x28'              # sub rsp, 0x28
# call rel32 to func_B -- placeholder
func_A_bytes += b'\xe8\x00\x00\x00\x00'
# add rsp, 0x28
func_A_bytes += b'\x48\x83\xc4\x28'
# ret
func_A_bytes += b'\xc3'

func_A_end_offset = func_A_rva + len(func_A_bytes)

# Exception handler for func_B (just a simple ret, the handler is never actually called
# in this experiment - we only need to DISCOVER it)
handler_rva = align(func_A_end_offset, 16)

handler_bytes = bytearray()
# Simple handler that returns DISP_ExceptionContinueSearch
# __attribute__((ms_abi)) EXCEPTION_DISPOSITION handler(
#     EXCEPTION_RECORD* rec, void* est_frame, CONTEXT* ctx, void* disp_ctx)
# {
#     return DISP_ExceptionContinueSearch;  /* = 1 */
# }
handler_bytes += b'\xb8\x01\x00\x00\x00'      # mov eax, 1 (DISP_ExceptionContinueSearch)
handler_bytes += b'\xc3'                           # ret

handler_end = handler_rva + len(handler_bytes)

# Entry point = func_A
ENTRY_RVA = TEXT_RVA + func_A_rva

# ===================================================================
# Build .text section
# ===================================================================
text_size = align(handler_end, FILE_ALIGN)
text_section = bytearray(text_size)

# Place func_C
text_section[func_C_rva:func_C_rva+len(func_C_bytes)] = func_C_bytes

# Place func_B
text_section[func_B_rva:func_B_rva+len(func_B_bytes)] = func_B_bytes

# Place func_A
text_section[func_A_rva:func_A_rva+len(func_A_bytes)] = func_A_bytes

# Place handler
text_section[handler_rva:handler_rva+len(handler_bytes)] = handler_bytes

# Fix up call in func_B -> func_C
# The call is at func_B_rva + offset_of_E8_instruction
# E8 is at func_B_bytes[14] (after push rbp, push rbx, sub rsp, mov rbp, mov rbx, mov ecx)
# Let's count: push rbp(1) + push rbx(1) + sub rsp,0x30(4) + mov rbp,rsp(3) + mov rbx,rcx(3) + mov ecx,imm32(5) = 17 bytes
# So E8 is at index 17
call_B_E8_offset = 17  # index within func_B_bytes
assert func_B_bytes[call_B_E8_offset] == 0xE8, f"Expected E8 at index {call_B_E8_offset}, got 0x{func_B_bytes[call_B_E8_offset]:02x}"

# rel32 = target - (call_address + 5)
target_C_va = IMAGE_BASE + TEXT_RVA + func_C_rva
call_B_va = IMAGE_BASE + TEXT_RVA + func_B_rva + call_B_E8_offset
rel32_B_to_C = target_C_va - (call_B_va + 5)
struct.pack_into('<I', text_section, func_B_rva + call_B_E8_offset + 1, rel32_B_to_C & 0xFFFFFFFF)

# Fix up call in func_A -> func_B
call_A_E8_offset = 4  # sub rsp,0x28 is 4 bytes, then E8
assert func_A_bytes[call_A_E8_offset] == 0xE8
target_B_va = IMAGE_BASE + TEXT_RVA + func_B_rva
call_A_va = IMAGE_BASE + TEXT_RVA + func_A_rva + call_A_E8_offset
rel32_A_to_B = target_B_va - (call_A_va + 5)
struct.pack_into('<I', text_section, func_A_rva + call_A_E8_offset + 1, rel32_A_to_B & 0xFFFFFFFF)

# ===================================================================
# Build .rdata section (IAT, ILT, Import Directory, strings)
# ===================================================================
# Layout within .rdata:
#   0x00: Import Directory Entry (20 bytes, null-terminated after 1 entry)
#   0x14: Null terminator (20 bytes of zeros)
#   0x28: ILT for KERNEL32 (2 entries: RaiseException + NULL)
#   0x30: IAT for KERNEL32 (2 entries: will be filled by loader + NULL)
#   0x38: Hint/Name table entry for RaiseException
#   0x3B: "RaiseException" string
#   0x4B: "KERNEL32.DLL" string
#   0x58: padding to FILE_ALIGN

ILT_OFFSET = 0x28
IAT_OFFSET = 0x30
HINTNAME_OFFSET = 0x38
HINTNAME_RAISE_OFFSET = 0x3A  # after 2-byte hint
DLL_NAME_OFFSET = 0x4B

def build_rdata():
    rdata = bytearray(align(0x100, FILE_ALIGN))
    
    # Import Directory Entry for KERNEL32.DLL
    # ALL RVAs must be IMAGE-RELATIVE, not section-relative
    struct.pack_into('<I', rdata, 0x00, RDATA_RVA + ILT_OFFSET)       # OriginalFirstThunk (ILT RVA)
    struct.pack_into('<I', rdata, 0x04, 0)                             # TimeDateStamp
    struct.pack_into('<I', rdata, 0x08, 0)                             # ForwarderChain
    struct.pack_into('<I', rdata, 0x0C, RDATA_RVA + DLL_NAME_OFFSET)  # Name RVA
    struct.pack_into('<I', rdata, 0x10, RDATA_RVA + IAT_OFFSET)       # FirstThunk (IAT RVA)
    
    # Null terminator entry (20 bytes of zeros) - already 0
    
    # ILT entry for RaiseException (RVA to Hint/Name)
    struct.pack_into('<I', rdata, ILT_OFFSET, RDATA_RVA + HINTNAME_OFFSET)
    # ILT null terminator
    struct.pack_into('<I', rdata, ILT_OFFSET + 4, 0)
    
    # IAT entry for RaiseException (same as ILT initially, loader overwrites with function pointer)
    struct.pack_into('<I', rdata, IAT_OFFSET, RDATA_RVA + HINTNAME_OFFSET)
    # IAT null terminator
    struct.pack_into('<I', rdata, IAT_OFFSET + 4, 0)
    
    # Hint/Name: 2-byte hint + null-terminated string
    struct.pack_into('<H', rdata, HINTNAME_OFFSET, 0)  # Hint = 0
    name_str = b'RaiseException\x00'
    rdata[HINTNAME_RAISE_OFFSET:HINTNAME_RAISE_OFFSET+len(name_str)] = name_str
    
    # DLL name
    dll_name = b'KERNEL32.DLL\x00'
    rdata[DLL_NAME_OFFSET:DLL_NAME_OFFSET+len(dll_name)] = dll_name
    
    return rdata

rdata_section = build_rdata()

# Fix up call in func_C -> RaiseException via IAT
# The FF 15 is at func_C_bytes[14+5+4+2+2+2+2] = let me count properly
# push rbx(1) + sub rsp,0x20(4) + mov rbx,rcx(3) + mov ecx,imm32(5) + xor edx,edx(2) + xor r8d,r8d(3) + xor r9d,r9d(3) = 21 bytes
# So FF 15 is at index 21
call_C_FF15_offset = 21
assert func_C_bytes[call_C_FF15_offset] == 0xFF, f"Expected FF at index {call_C_FF15_offset}, got 0x{func_C_bytes[call_C_FF15_offset]:02x}"
assert func_C_bytes[call_C_FF15_offset+1] == 0x15, f"Expected 15 at index {call_C_FF15_offset+1}"

# FF 15 [rip+disp32]: disp32 = target_addr - (instruction_addr + 6)
# target = IAT entry VA = IMAGE_BASE + RDATA_RVA + IAT_OFFSET
IAT_entry_va = IMAGE_BASE + RDATA_RVA + IAT_OFFSET
call_C_instr_va = IMAGE_BASE + TEXT_RVA + func_C_rva + call_C_FF15_offset
disp32 = IAT_entry_va - (call_C_instr_va + 6)
struct.pack_into('<I', text_section, func_C_rva + call_C_FF15_offset + 2, disp32 & 0xFFFFFFFF)

print(f"func_C RaiseException call: FF 15 [rip+0x{disp32:x}] -> IAT at VA 0x{IAT_entry_va:x}")
print(f"  IAT RVA = 0x{RDATA_RVA + IAT_OFFSET:x}")

# ===================================================================
# Build .xdata section (UNWIND_INFO for A, B, C)
# ===================================================================
# UNWIND_INFO format:
#   Byte 0: Version(3 bits) | Flags(3 bits) | SizeOfProlog(2 bits high of byte 1)
#   Byte 1: SizeOfProlog (8 bits)
#   Byte 2: CountOfCodes
#   Byte 3: FrameRegister(4 bits high) | FrameOffset(4 bits low)
#   Then: UNWIND_CODE array (2 bytes each)
#   Then: optional handler RVA (4 bytes) + LSDA RVA (4 bytes)

# func_C UNWIND_INFO:
#   Push rbx       -> UWOP_PUSH_NONVOL (RBX=3), offset 0
#   Sub rsp, 0x20  -> UWOP_ALLOC_SMALL (0x20/8 - 1 = 3), offset 1
#   (total 2 unwind codes)
#   No handler

func_C_xdata = bytearray()
func_C_xdata += p8((1 & 0x07) | ((0 & 0x03) << 3) | ((0x09 & 0x03) << 6))  # version=1, flags=0, prolog high bits
func_C_xdata += p8(0x09)  # prolog size = 9 bytes (push rbx(1) + sub rsp,0x20(4) + mov rbx,rcx(3))
func_C_xdata += p8(2)    # count of codes = 2
func_C_xdata += p8(0)    # no frame register
# Unwind codes (array of uint16, each code is 2 bytes)
# Code 0: UWOP_PUSH_NONVOL(RBX=3) at offset 0
#   [15:12]=op=0, [11:8]=info=3, [7:0]=offset=0 -> 0x0300
func_C_xdata += p16(0x0300)
# Code 1: UWOP_ALLOC_SMALL(info=3, meaning (3+1)*8=32) at offset 1
#   op=2, info=3, offset=1 -> 0x2301
func_C_xdata += p16(0x2301)

# func_B UNWIND_INFO:
#   Push rbp       -> UWOP_PUSH_NONVOL (RBP=5), offset 0
#   Push rbx       -> UWOP_PUSH_NONVOL (RBX=3), offset 1
#   Sub rsp, 0x30  -> UWOP_ALLOC_SMALL (0x30/8 - 1 = 5), offset 2
#   UNW_FLAG_EHANDLER -> handler RVA + LSDA RVA after codes

func_B_xdata = bytearray()
func_B_xdata += p8((1 & 0x07) | ((1 & 0x03) << 3) | ((0x0E & 0x03) << 6))  # version=1, flags=EHANDLER(0x01), prolog high bits
func_B_xdata += p8(0x0E)  # prolog size = 14 bytes (push rbp(1)+push rbx(1)+sub rsp,0x30(4)+mov rbp,rsp(3)+mov rbx,rcx(3)+mov ecx,imm32(5))... actually the prolog is up to and including the last "setup" instruction before the call. Let me be more precise.
# The Windows convention: prolog = all instructions before the first "body" instruction.
# For func_B: push rbp(1) + push rbx(1) + sub rsp,0x30(4) + mov rbp,rsp(3) + mov rbx,rcx(3) = 12 bytes
# The mov ecx,0xC0DE0001(5) is arguably still prolog setup. Let's say prolog = 17 bytes (includes the mov ecx)
func_B_xdata[1] = 0x11  # prolog size = 17 bytes
func_B_xdata += p8(3)    # count of codes = 3
func_B_xdata += p8(0)    # no frame register
# Unwind codes:
# Code 0: UWOP_PUSH_NONVOL(RBP=5) at offset 0 -> [0,5,0] = 0x0500
func_B_xdata += p16(0x0500)
# Code 1: UWOP_PUSH_NONVOL(RBX=3) at offset 1 -> [0,3,1] = 0x0301
func_B_xdata += p16(0x0301)
# Code 2: UWOP_ALLOC_SMALL(info=5, meaning (5+1)*8=48) at offset 2 -> [2,5,2] = 0x2502
func_B_xdata += p16(0x2502)
# Pad to 4-byte alignment for handler
h_off_pre = 4 + 3 * 2
if h_off_pre % 4:
    func_B_xdata += b'\x00' * (4 - h_off_pre % 4)
# Handler RVA (4 bytes) - relative to image base
handler_rva_abs = TEXT_RVA + handler_rva  # full RVA, not section-relative
func_B_xdata += p32(handler_rva_abs)
# LSDA RVA (4 bytes) - 0 for now (no LSDA data)
func_B_xdata += p32(0)

# func_A UNWIND_INFO:
#   Sub rsp, 0x28  -> UWOP_ALLOC_SMALL (0x28/8 - 1 = 4), offset 0
#   No handler

func_A_xdata = bytearray()
func_A_xdata += p8((1 & 0x07) | ((0 & 0x03) << 3) | ((0x04 & 0x03) << 6))  # version=1, flags=0, prolog high bits
func_A_xdata += p8(0x04)  # prolog size = 4 bytes (sub rsp, 0x28)
func_A_xdata += p8(1)    # count of codes = 1
func_A_xdata += p8(0)    # no frame register
# Code 0: UWOP_ALLOC_SMALL(info=4, meaning (4+1)*8=40) at offset 0 -> 0x2400
func_A_xdata += p16(0x2400)

# Build .xdata section
xdata_offset = 0
func_C_xdata_rva = xdata_offset
xdata_offset += align(len(func_C_xdata), 4)

func_B_xdata_rva = xdata_offset
xdata_offset += align(len(func_B_xdata), 4)

func_A_xdata_rva = xdata_offset
xdata_offset += align(len(func_A_xdata), 4)

xdata_section = bytearray(align(xdata_offset, FILE_ALIGN))
xdata_section[func_C_xdata_rva:func_C_xdata_rva+len(func_C_xdata)] = func_C_xdata
xdata_section[func_B_xdata_rva:func_B_xdata_rva+len(func_B_xdata)] = func_B_xdata
xdata_section[func_A_xdata_rva:func_A_xdata_rva+len(func_A_xdata)] = func_A_xdata

print(f"func_C xdata at .xdata+0x{func_C_xdata_rva:x}, size={len(func_C_xdata)}")
print(f"func_B xdata at .xdata+0x{func_B_xdata_rva:x}, size={len(func_B_xdata)}")
print(f"func_A xdata at .xdata+0x{func_A_xdata_rva:x}, size={len(func_A_xdata)}")

# ===================================================================
# Build .pdata section (RUNTIME_FUNCTION entries)
# ===================================================================
# Entries must be sorted by BeginAddress
pdata_section = bytearray(align(3 * 12, FILE_ALIGN))

# Entry for func_C (lowest IMAGE-RELATIVE RVA — must be sorted!)
struct.pack_into('<III', pdata_section, 0,
    TEXT_RVA + func_C_rva,
    TEXT_RVA + func_C_end,
    XDATA_RVA + func_C_xdata_rva)

# Entry for func_B
struct.pack_into('<III', pdata_section, 12,
    TEXT_RVA + func_B_rva,
    TEXT_RVA + func_B_end_offset,
    XDATA_RVA + func_B_xdata_rva)

# Entry for func_A
struct.pack_into('<III', pdata_section, 24,
    TEXT_RVA + func_A_rva,
    TEXT_RVA + func_A_end_offset,
    XDATA_RVA + func_A_xdata_rva)

print(f"\nRUNTIME_FUNCTION entries (.pdata):")
print(f"  func_A: begin=0x{TEXT_RVA+func_A_rva:x} end=0x{TEXT_RVA+func_A_end_offset:x} unwind=0x{XDATA_RVA+func_A_xdata_rva:x}")
print(f"  func_B: begin=0x{TEXT_RVA+func_B_rva:x} end=0x{TEXT_RVA+func_B_end_offset:x} unwind=0x{XDATA_RVA+func_B_xdata_rva:x}")
print(f"  func_C: begin=0x{TEXT_RVA+func_C_rva:x} end=0x{TEXT_RVA+func_C_end:x} unwind=0x{XDATA_RVA+func_C_xdata_rva:x}")
print(f"  handler: RVA=0x{TEXT_RVA+handler_rva:x}")
print(f"  entry point: RVA=0x{ENTRY_RVA:x}")

# ===================================================================
# Build .reloc section (empty but present)
# ===================================================================
reloc_section = bytearray(align(FILE_ALIGN, FILE_ALIGN))

# ===================================================================
# Assemble the PE file
# ===================================================================

# Section raw data pointers
TEXT_RAW   = HEADERS_SIZE
RDATA_RAW  = TEXT_RAW + len(text_section)
PDATA_RAW  = RDATA_RAW + len(rdata_section)
XDATA_RAW  = PDATA_RAW + len(pdata_section)
RELOC_RAW  = XDATA_RAW + len(xdata_section)

TOTAL_SIZE = RELOC_RAW + len(reloc_section)

pe = bytearray(TOTAL_SIZE)

# Copy sections into PE
pe[TEXT_RAW:TEXT_RAW+len(text_section)] = text_section
pe[RDATA_RAW:RDATA_RAW+len(rdata_section)] = rdata_section
pe[PDATA_RAW:PDATA_RAW+len(pdata_section)] = pdata_section
pe[XDATA_RAW:XDATA_RAW+len(xdata_section)] = xdata_section
pe[RELOC_RAW:RELOC_RAW+len(reloc_section)] = reloc_section

# ===================================================================
# Build PE Headers
# ===================================================================

# DOS Header (64 bytes)
pe[0:2] = b'MZ'
struct.pack_into('<I', pe, 0x3C, 0x80)  # e_lfanew = 0x80

# PE Signature at 0x80
pe[0x80:0x84] = b'PE\x00\x00'

# COFF Header at 0x84
off = 0x84
struct.pack_into('<H', pe, off, 0x8664); off += 2  # Machine = AMD64
struct.pack_into('<H', pe, off, 5);      off += 2  # NumberOfSections
struct.pack_into('<I', pe, off, 0);      off += 4  # TimeDateStamp
struct.pack_into('<I', pe, off, 0);      off += 4  # PointerToSymbolTable
struct.pack_into('<I', pe, off, 0);      off += 4  # NumberOfSymbols
struct.pack_into('<H', pe, off, 0xF0);   off += 2  # SizeOfOptionalHeader (240 bytes)
struct.pack_into('<H', pe, off, 0x22);   off += 2  # Characteristics = EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

# PE32+ Optional Header at 0x98
opt_off = 0x98
struct.pack_into('<H', pe, opt_off, 0x020B); opt_off += 2   # Magic = PE32+
pe[opt_off] = 14; opt_off += 1    # MajorLinkerVersion
pe[opt_off] = 0;  opt_off += 1    # MinorLinkerVersion
struct.pack_into('<I', pe, opt_off, len(text_section)); opt_off += 4  # SizeOfCode
struct.pack_into('<I', pe, opt_off, len(rdata_section) + len(pdata_section) + len(xdata_section)); opt_off += 4  # SizeOfInitializedData
struct.pack_into('<I', pe, opt_off, 0); opt_off += 4  # SizeOfUninitializedData
struct.pack_into('<I', pe, opt_off, ENTRY_RVA); opt_off += 4  # AddressOfEntryPoint
struct.pack_into('<I', pe, opt_off, TEXT_RVA); opt_off += 4  # BaseOfCode
struct.pack_into('<Q', pe, opt_off, IMAGE_BASE); opt_off += 8  # ImageBase
struct.pack_into('<I', pe, opt_off, SECTION_ALIGN); opt_off += 4  # SectionAlignment
struct.pack_into('<I', pe, opt_off, FILE_ALIGN); opt_off += 4  # FileAlignment
struct.pack_into('<H', pe, opt_off, 6); opt_off += 2  # MajorOperatingSystemVersion
struct.pack_into('<H', pe, opt_off, 0); opt_off += 2  # MinorOperatingSystemVersion
struct.pack_into('<H', pe, opt_off, 0); opt_off += 2  # MajorImageVersion
struct.pack_into('<H', pe, opt_off, 0); opt_off += 2  # MinorImageVersion
struct.pack_into('<H', pe, opt_off, 6); opt_off += 2  # MajorSubsystemVersion
struct.pack_into('<H', pe, opt_off, 0); opt_off += 2  # MinorSubsystemVersion
struct.pack_into('<I', pe, opt_off, 0); opt_off += 4  # Win32VersionValue
struct.pack_into('<I', pe, opt_off, IMAGE_SIZE); opt_off += 4  # SizeOfImage
struct.pack_into('<I', pe, opt_off, HEADERS_SIZE); opt_off += 4  # SizeOfHeaders
struct.pack_into('<I', pe, opt_off, 0); opt_off += 4  # CheckSum
struct.pack_into('<H', pe, opt_off, 3); opt_off += 2  # Subsystem = CONSOLE
struct.pack_into('<H', pe, opt_off, 0); opt_off += 2  # DllCharacteristics
struct.pack_into('<Q', pe, opt_off, 0x100000); opt_off += 8  # SizeOfStackReserve
struct.pack_into('<Q', pe, opt_off, 0x1000);  opt_off += 8  # SizeOfStackCommit
struct.pack_into('<Q', pe, opt_off, 0x100000); opt_off += 8  # SizeOfHeapReserve
struct.pack_into('<Q', pe, opt_off, 0x1000);  opt_off += 8  # SizeOfHeapCommit
struct.pack_into('<I', pe, opt_off, 0); opt_off += 4  # LoaderFlags
struct.pack_into('<I', pe, opt_off, 16); opt_off += 4  # NumberOfRvaAndSizes

# Data Directories (16 entries, each 8 bytes)
DD_OFFSET = opt_off
# DD[0] Export
struct.pack_into('<II', pe, DD_OFFSET + 0*8, 0, 0)
# DD[1] Import
struct.pack_into('<II', pe, DD_OFFSET + 1*8, RDATA_RVA, len(rdata_section))
# DD[2] Resource
struct.pack_into('<II', pe, DD_OFFSET + 2*8, 0, 0)
# DD[3] Exception (.pdata)
struct.pack_into('<II', pe, DD_OFFSET + 3*8, PDATA_RVA, len(pdata_section))
# DD[4] Security
struct.pack_into('<II', pe, DD_OFFSET + 4*8, 0, 0)
# DD[5] Base Reloc
struct.pack_into('<II', pe, DD_OFFSET + 5*8, RELOC_RVA, len(reloc_section))
# DD[6-15] = 0 (already zero)

# Section Headers (immediately after optional header + data directories)
# Optional header ends at opt_off + 16*8 = opt_off + 128
sec_hdr_off = DD_OFFSET + 16 * 8

def write_section_header(pe, off, name, vsize, vaddr, rawsize, rawptr, chars):
    pe[off:off+8] = name.ljust(8, b'\x00')[:8]
    struct.pack_into('<I', pe, off+8,  vsize)
    struct.pack_into('<I', pe, off+12, vaddr)
    struct.pack_into('<I', pe, off+16, rawsize)
    struct.pack_into('<I', pe, off+20, rawptr)
    struct.pack_into('<I', pe, off+24, 0)  # PointerToRelocations
    struct.pack_into('<I', pe, off+28, 0)  # PointerToLinenumbers
    struct.pack_into('<H', pe, off+32, 0)  # NumberOfRelocations
    struct.pack_into('<H', pe, off+34, 0)  # NumberOfLinenumbers
    struct.pack_into('<I', pe, off+36, chars)

# .text
write_section_header(pe, sec_hdr_off, b'.text',
    len(text_section), TEXT_RVA, len(text_section), TEXT_RAW,
    0x60000020)  # EXECUTE | READ | CODE
sec_hdr_off += 40

# .rdata
write_section_header(pe, sec_hdr_off, b'.rdata',
    len(rdata_section), RDATA_RVA, len(rdata_section), RDATA_RAW,
    0x40000040)  # READ | INITIALIZED_DATA
sec_hdr_off += 40

# .pdata
write_section_header(pe, sec_hdr_off, b'.pdata',
    len(pdata_section), PDATA_RVA, len(pdata_section), PDATA_RAW,
    0x40000040)  # READ | INITIALIZED_DATA
sec_hdr_off += 40

# .xdata
write_section_header(pe, sec_hdr_off, b'.xdata',
    len(xdata_section), XDATA_RVA, len(xdata_section), XDATA_RAW,
    0x40000040)  # READ | INITIALIZED_DATA
sec_hdr_off += 40

# .reloc
write_section_header(pe, sec_hdr_off, b'.reloc',
    len(reloc_section), RELOC_RVA, len(reloc_section), RELOC_RAW,
    0x42000040)  # DISCARDABLE | READ | INITIALIZED_DATA

# ===================================================================
# Write PE file
# ===================================================================
outpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'exp_next2', 'synthetic_test.exe')
outpath = os.path.normpath(outpath)
with open(outpath, 'wb') as f:
    f.write(pe)

# Verify
import subprocess
result = subprocess.run(['file', outpath], capture_output=True, text=True)
print(f"\nOutput: {outpath}")
print(f"Size: {len(pe)} bytes")
print(f"File type: {result.stdout.strip()}")

# Dump key structures for verification
print(f"\n=== VERIFICATION ===")
print(f"Entry Point: RVA 0x{ENTRY_RVA:x} (VA 0x{IMAGE_BASE+ENTRY_RVA:x})")
print(f"Exception Dir: RVA 0x{PDATA_RVA:x} size 0x{len(pdata_section):x} ({len(pdata_section)//12} entries)")
print(f"Import Dir: RVA 0x{RDATA_RVA:x} size 0x{len(rdata_section):x}")

# Verify UNWIND_INFO byte 0 for func_B
b_ui_offset = XDATA_RAW + func_B_xdata_rva
b_flags = (pe[b_ui_offset] >> 3) & 0x03
print(f"\nfunc_B UNWIND_INFO byte0=0x{pe[b_ui_offset]:02x} flags=0x{b_flags:x} (expect 0x1 = EHANDLER)")
h_off = 4 + pe[b_ui_offset+2] * 2
if h_off % 4: h_off += 2
h_rva = struct.unpack_from('<I', pe, b_ui_offset + h_off)[0]
print(f"func_B handler RVA=0x{h_rva:x} (expect 0x{TEXT_RVA+handler_rva:x})")

# Verify call fixups in .text
print(f"\nfunc_C call [rip+disp32] at .text+0x{func_C_rva+call_C_FF15_offset:x}:")
d = struct.unpack_from('<i', pe, TEXT_RAW + func_C_rva + call_C_FF15_offset + 2)[0]
print(f"  disp32=0x{d & 0xFFFFFFFF:x} -> target VA=0x{IAT_entry_va:x}")

d = struct.unpack_from('<i', pe, TEXT_RAW + func_B_rva + call_B_E8_offset + 1)[0]
print(f"func_B call func_C: rel32=0x{d & 0xFFFFFFFF:x}")

d = struct.unpack_from('<i', pe, TEXT_RAW + func_A_rva + call_A_E8_offset + 1)[0]
print(f"func_A call func_B: rel32=0x{d & 0xFFFFFFFF:x}")

print(f"\nDONE. Synthetic PE built successfully.")
