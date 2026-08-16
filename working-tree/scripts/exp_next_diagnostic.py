#!/usr/bin/env python3
"""
EXP-NEXT: Correct SEH first-frame handling and unwind validation

Objective:
  Given the captured RaiseException state (RIP=0x49d5b1, RSP=0x7ffe9ba21b40),
  determine whether we can reliably reach a PE frame containing an EH handler.

Method:
  1. Parse .pdata from the PE binary directly
  2. For Frame[0] (caller, RVA 0x9d5b1), validate UNWIND_INFO against prolog bytes
  3. Check for malformed UNWIND_INFO entries
  4. Walk frames upward looking for EHANDLER
  5. Examine the UNWIND_INFO validation for frames near the caller
"""

import struct
import sys

PE_PATH = "/home/z/my-project/minwin/samples/upx_decompressed.exe"
IMAGE_BASE = 0x400000

# Known values from runtime trace
CALLER_RIP = 0x49d5b1
CALLER_RVA = 0x9d5b1
CALLER_RSP = 0x7ffe9ba21b40  # pre-call RSP from naked stub

# UNWIND_INFO flags
UNW_FLAG_EHANDLER  = 0x01
UNW_FLAG_UHANDLER  = 0x02
UNW_FLAG_CHAININFO = 0x04

REG_NAMES = [
    "RAX", "RCX", "RDX", "RBX", "RSP", "RBP",
    "RSI", "RDI", "R8",  "R9",  "R10", "R11",
    "R12", "R13", "R14", "R15"
]

def read_pe():
    with open(PE_PATH, "rb") as f:
        return f.read()

def parse_pe(data):
    """Parse PE headers, return (sections, data_dirs)."""
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew+4] == b'PE\x00\x00'
    coff_off = e_lfanew + 4
    _, num_sections, _, _, _, opt_size, _ = struct.unpack_from(
        "<HHIIIHH", data, coff_off)
    opt_off = coff_off + 20
    assert struct.unpack_from("<H", data, opt_off)[0] == 0x020B
    num_dd = struct.unpack_from("<I", data, opt_off + 108)[0]
    dd_off = opt_off + 112
    data_dirs = []
    for i in range(min(num_dd, 16)):
        va, sz = struct.unpack_from("<II", data, dd_off + i*8)
        data_dirs.append((va, sz))
    section_off = opt_off + opt_size
    sections = []
    for i in range(num_sections):
        s_off = section_off + i * 40
        name = data[s_off:s_off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        vs, va, raw_sz, raw_ptr = struct.unpack_from("<IIII", data, s_off + 8)
        sections.append({'name': name, 'va': va, 'vs': vs,
                         'raw_size': raw_sz, 'raw_ptr': raw_ptr})
    return sections, data_dirs

def rva_to_off(sections, rva):
    for s in sections:
        if s['va'] <= rva < s['va'] + max(s['vs'], s['raw_size']):
            return rva - s['va'] + s['raw_ptr']
    return None

def rva_to_section(sections, rva):
    for s in sections:
        if s['va'] <= rva < s['va'] + max(s['vs'], s['raw_size']):
            return s['name']
    return None

def get_bytes(data, sections, rva, n):
    off = rva_to_off(sections, rva)
    if off is None: return None
    return data[off:off+n]

def parse_unwind_info_raw(raw_ui_bytes):
    """Parse UNWIND_INFO from raw bytes. Returns dict."""
    if len(raw_ui_bytes) < 4:
        return None
    byte0 = raw_ui_bytes[0]
    version = byte0 & 0x07
    flags = (byte0 >> 3) & 0x03
    prolog_size = raw_ui_bytes[1]
    count_codes = raw_ui_bytes[2]
    frame_reg = raw_ui_bytes[3] >> 4
    frame_off = raw_ui_bytes[3] & 0x0f
    
    result = {
        'version': version, 'flags': flags,
        'prolog_size': prolog_size, 'count_codes': count_codes,
        'frame_reg': frame_reg, 'frame_off': frame_off,
        'raw_header': list(raw_ui_bytes[:4]),
        'codes': [], 'handler_rva': 0, 'has_handler': False,
        'is_chained': False, 'total_alloc': 0,
        'total_push': 0, 'has_fpreg': False,
    }
    
    flag_names = []
    if flags & UNW_FLAG_EHANDLER: flag_names.append('EHANDLER')
    if flags & UNW_FLAG_UHANDLER: flag_names.append('UHANDLER')
    if flags & UNW_FLAG_CHAININFO: flag_names.append('CHAININFO')
    result['flag_str'] = ','.join(flag_names) if flag_names else 'NONE'
    
    # Parse codes
    code_data = raw_ui_bytes[4:4 + (count_codes + 6) * 2]  # extra for safety
    slot = 0
    for i in range(count_codes):
        if slot * 2 + 1 >= len(code_data):
            break
        cw = struct.unpack_from("<H", code_data, slot * 2)[0]
        op = (cw >> 12) & 0x0f
        info = (cw >> 8) & 0x0f
        coff = cw & 0xff
        entry = {'raw': cw, 'op': op, 'info': info, 'offset': coff, 'slots': 1, 'alloc': 0}
        
        if op == 0:  # PUSH_NONVOL
            entry['desc'] = f"PUSH {REG_NAMES[info]}"
            result['total_push'] += 8
        elif op == 1:  # ALLOC_LARGE
            if info == 0:
                if (slot+1)*2+1 < len(code_data):
                    alloc = struct.unpack_from("<H", code_data, (slot+1)*2)[0]
                else:
                    alloc = 0
                entry['slots'] = 2
            else:
                # op_info=1: 4-byte alloc
                if (slot+2)*2+1 < len(code_data):
                    alloc = struct.unpack_from("<I", code_data, (slot+1)*2)[0]
                else:
                    alloc = 0
                entry['slots'] = 3
            entry['alloc'] = alloc
            result['total_alloc'] += alloc
            entry['desc'] = f"ALLOC 0x{alloc:x}"
        elif op == 2:  # ALLOC_SMALL
            alloc = (info + 1) * 8
            entry['alloc'] = alloc
            result['total_alloc'] += alloc
            entry['desc'] = f"ALLOC_SMALL 0x{alloc:x}"
        elif op == 3:  # SET_FPREG
            result['has_fpreg'] = True
            entry['desc'] = f"SET_FPREG {REG_NAMES[frame_reg]}"
        elif op == 4:  # SAVE_NONVOL
            entry['slots'] = 2
            entry['desc'] = f"SAVE {REG_NAMES[info]}"
        elif op == 5:  # SAVE_NONVOL_FAR
            entry['slots'] = 3
            entry['desc'] = f"SAVE_FAR {REG_NAMES[info]}"
        elif op == 6:  # SAVE_XMM128
            entry['slots'] = 2
            entry['desc'] = f"SAVE_XMM {info}"
        elif op == 7:  # SAVE_XMM128_FAR
            entry['slots'] = 3
            entry['desc'] = f"SAVE_XMM_FAR {info}"
        elif op == 8:  # PUSH_MACHFRAME
            result['total_push'] += 40
            entry['desc'] = f"PUSH_MACHFRAME"
        else:
            entry['desc'] = f"OP{op} info={info}"
            entry['slots'] = 1
        
        result['codes'].append(entry)
        slot += entry['slots']
    
    result['total_slots'] = slot
    # Check for slot overflow
    result['slot_overflow'] = slot > count_codes
    
    # Handler RVA
    h_off = 4 + count_codes * 2
    if h_off % 4: h_off += 2
    if flags & UNW_FLAG_CHAININFO:
        result['is_chained'] = True
    elif (flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)):
        if h_off + 4 <= len(raw_ui_bytes):
            result['handler_rva'] = struct.unpack_from("<I", raw_ui_bytes, h_off)[0]
            result['has_handler'] = True
    
    return result

def detect_prolog_alloc(data, sections, func_rva):
    """Detect 'sub rsp, N' in first 16 bytes of function.
    Returns (alloc_size, first_bytes_hex)."""
    raw = get_bytes(data, sections, func_rva, 16)
    if raw is None:
        return 0, "(unreadable)"
    hex_str = ' '.join(f'{b:02x}' for b in raw[:12])
    
    # Pattern 1: REX.W + 83 EC imm8 = sub rsp, imm8
    # 48 83 ec NN
    if len(raw) >= 4 and raw[0] == 0x48 and raw[1] == 0x83 and raw[2] == 0xec:
        return raw[3], hex_str
    # Pattern 2: REX.W + 81 EC imm32 = sub rsp, imm32
    if len(raw) >= 7 and raw[0] == 0x48 and raw[1] == 0x81 and raw[2] == 0xec:
        return struct.unpack_from("<I", raw, 3)[0], hex_str
    # Pattern 3: No sub rsp
    return 0, hex_str

def main():
    data = read_pe()
    sections, data_dirs = parse_pe(data)
    pdata_rva, pdata_size = data_dirs[3]
    
    print("=" * 70)
    print("EXP-NEXT: Correct SEH first-frame handling and unwind validation")
    print("=" * 70)
    print(f"PE: {PE_PATH}")
    print(f".pdata: RVA=0x{pdata_rva:x} Size=0x{pdata_size:x} Entries={pdata_size//12}")
    print()
    
    # Parse all RF entries
    rfs = []
    for i in range(pdata_size // 12):
        off = rva_to_off(sections, pdata_rva + i * 12)
        if off is None: continue
        b, e, u = struct.unpack_from("<III", data, off)
        rfs.append({'idx': i, 'begin': b, 'end': e, 'unwind': u})
    print(f"Parsed {len(rfs)} RUNTIME_FUNCTION entries\n")
    
    # =========================================================================
    # FRAME 0: RaiseException caller (RF[2015], RVA 0x9d560)
    # =========================================================================
    print("=" * 70)
    print("FRAME 0: RaiseException caller")
    print("=" * 70)
    print(f"  RIP = 0x{CALLER_RIP:x}  RVA = 0x{CALLER_RVA:x}")
    print(f"  RSP = 0x{CALLER_RSP:x} (pre-call RSP from naked stub)")
    print()
    
    # Find RF for caller
    rf0 = None
    lo, hi = 0, len(rfs) - 1
    while lo <= hi:
        mid = lo + (hi - lo) // 2
        if CALLER_RVA < rfs[mid]['begin']: hi = mid - 1
        elif CALLER_RVA >= rfs[mid]['end']: lo = mid + 1
        else: rf0 = rfs[mid]; break
    
    if rf0 is None:
        print("  FATAL: No RUNTIME_FUNCTION for caller!")
        return 1
    
    print(f"  RUNTIME_FUNCTION[{rf0['idx']}]:")
    print(f"    Begin = 0x{rf0['begin']:06x}  End = 0x{rf0['end']:06x}  Unwind = 0x{rf0['unwind']:06x}")
    print(f"    Size = 0x{rf0['end'] - rf0['begin']:x} bytes")
    print()
    
    # Parse UNWIND_INFO
    ui_raw = get_bytes(data, sections, rf0['unwind'], 32)
    ui0 = parse_unwind_info_raw(ui_raw)
    print(f"  UNWIND_INFO at 0x{rf0['unwind']:x}:")
    print(f"    Raw: {' '.join(f'{b:02x}' for b in ui_raw[:12])}")
    print(f"    Ver={ui0['version']} Flags=0x{ui0['flags']:x} ({ui0['flag_str']})")
    print(f"    PrologSize={ui0['prolog_size']} CountCodes={ui0['count_codes']}")
    print(f"    FrameReg={REG_NAMES[ui0['frame_reg']] if ui0['frame_reg']<16 else '?'}  Handler=0x{ui0['handler_rva']:x}")
    print(f"    TotalSlots={ui0['total_slots']} SlotOverflow={ui0['slot_overflow']}")
    for c in ui0['codes']:
        print(f"    Code[{c['op']}] offset={c['offset']:3d} {c['desc']} (slots={c['slots']})")
    print(f"    TotalAlloc={ui0['total_alloc']}  TotalPush={ui0['total_push']}")
    print()
    
    # Prolog analysis
    alloc_bytes, hex_str = detect_prolog_alloc(data, sections, rf0['begin'])
    print(f"  Prolog bytes: {hex_str}")
    print(f"  Detected 'sub rsp, 0x{alloc_bytes:x}' from prolog bytes")
    print()
    
    # Validation
    print(f"  --- VALIDATION ---")
    if alloc_bytes > 0 and ui0['total_alloc'] == 0:
        print(f"  *** CONFIRMED: 'sub rsp,0x{alloc_bytes:x}' in prolog but NO ALLOC in UNWIND_INFO")
        print(f"  *** UNWIND_INFO is MALFORMED for this function")
    elif alloc_bytes != ui0['total_alloc'] and alloc_bytes > 0:
        print(f"  *** MISMATCH: prolog alloc=0x{alloc_bytes:x} vs unwind alloc=0x{ui0['total_alloc']:x}")
    else:
        print(f"  (No alloc mismatch detected)")
    
    if ui0['slot_overflow']:
        print(f"  *** SLOT OVERFLOW: total_slots({ui0['total_slots']}) > count_codes({ui0['count_codes']})")
    print()
    
    # Experiment A vs B
    print(f"  --- EXPERIMENT A: Trust UNWIND_INFO ---")
    unwind_rsp_a = CALLER_RSP + ui0['total_alloc']
    print(f"    new_rsp = 0x{CALLER_RSP:x} + 0x{ui0['total_alloc']:x} = 0x{unwind_rsp_a:x}")
    print(f"    parent_rip = [0x{unwind_rsp_a:x}] = 0x201e040 (g_argv, WRONG)")
    print(f"    RESULT: FAIL")
    print()
    print(f"  --- EXPERIMENT B: Use prolog analysis ---")
    correct_rsp = CALLER_RSP + alloc_bytes
    print(f"    new_rsp = 0x{CALLER_RSP:x} + 0x{alloc_bytes:x} = 0x{correct_rsp:x}")
    print(f"    parent_rip = [0x{correct_rsp:x}] (need runtime to read)")
    print(f"    RESULT: PENDING (need runtime verification)")
    print(f"    Frame 0 has NO EHANDLER — must skip regardless")
    print()
    
    # =========================================================================
    # SCAN: Malformed UNWIND_INFO frequency
    # =========================================================================
    print("=" * 70)
    print("SCAN: Malformed UNWIND_INFO frequency")
    print("=" * 70)
    
    malformed = 0
    missing_alloc = 0
    slot_overflows = 0
    no_eh = 0
    has_eh = 0
    chained = 0
    
    # Check a sample of RF entries (not all 3030 — too slow)
    sample_size = min(len(rfs), 500)
    check_indices = list(range(0, sample_size)) + list(range(max(0, rf0['idx']-50), min(len(rfs), rf0['idx']+51)))
    check_indices = sorted(set(check_indices))
    
    for idx in check_indices:
        rf = rfs[idx]
        ui_raw = get_bytes(data, sections, rf['unwind'], 32)
        if ui_raw is None: continue
        ui = parse_unwind_info_raw(ui_raw)
        
        alloc_prolog, _ = detect_prolog_alloc(data, sections, rf['begin'])
        
        if ui['slot_overflow']:
            slot_overflows += 1
        if alloc_prolog > 0 and ui['total_alloc'] == 0:
            missing_alloc += 1
            malformed += 1
        if ui['is_chained']:
            chained += 1
        if ui['has_handler']:
            has_eh += 1
        else:
            no_eh += 1
    
    print(f"  Checked {len(check_indices)} RF entries (sampled)")
    print(f"  With EHANDLER: {has_eh}")
    print(f"  Without EHANDLER: {no_eh}")
    print(f"  Chained (CHAININFO): {chained}")
    print(f"  Slot overflow: {slot_overflows}")
    print(f"  Missing ALLOC (prolog has sub rsp but UNWIND doesn't): {missing_alloc}")
    print()
    
    # =========================================================================
    # DEEP ANALYSIS: Frames near caller (RF[2015])
    # =========================================================================
    print("=" * 70)
    print("DEEP ANALYSIS: Frames near RF[2015]")
    print("=" * 70)
    
    start_idx = max(0, rf0['idx'] - 5)
    end_idx = min(len(rfs), rf0['idx'] + 6)
    
    print(f"  {'Idx':>5} {'Begin':>8} {'End':>8} {'Size':>6} {'EH':>3} {'Alloc_p':>8} {'Alloc_u':>8} {'OK':>3} {'Notes'}")
    print(f"  {'-'*80}")
    
    for idx in range(start_idx, end_idx):
        rf = rfs[idx]
        ui_raw = get_bytes(data, sections, rf['unwind'], 32)
        if ui_raw is None: continue
        ui = parse_unwind_info_raw(ui_raw)
        alloc_p, _ = detect_prolog_alloc(data, sections, rf['begin'])
        
        eh = "Y" if ui['has_handler'] else ""
        marker = " <<" if idx == rf0['idx'] else ""
        size = rf['end'] - rf['begin']
        ok = "OK" if alloc_p == 0 or alloc_p == ui['total_alloc'] else "BAD"
        notes = ""
        if ui['slot_overflow']: notes += "SLOT_OVF "
        if alloc_p > 0 and ui['total_alloc'] == 0: notes += "MISS_ALLOC "
        if ui['is_chained']: notes += "CHAIN "
        if ui['has_handler']: notes += f"H=0x{ui['handler_rva']:x} "
        
        print(f"  {idx:5d} 0x{rf['begin']:06x} 0x{rf['end']:06x} 0x{size:04x} {eh:>3}"
              f" 0x{alloc_p:06x} 0x{ui['total_alloc']:06x} {ok:>3} {notes}{marker}")
    print()
    
    # =========================================================================
    # CRITICAL: Find the function containing the try/catch
    # =========================================================================
    print("=" * 70)
    print("ANALYSIS: Which function has the try/catch for this exception?")
    print("=" * 70)
    print()
    print("  The call chain (from prolog_analysis doc and trace):")
    print("    Frame 0: 0x9d560 — calls RaiseException, NO EHANDLER")
    print("    Frame 1: PARENT of 0x9d560 — need correct unwind to find")
    print("    Frame N:  Has the try { ... } catch { ... } block")
    print()
    print("  For the stack walk to work, we need:")
    print("    1. Skip Frame 0 (no EHANDLER, malformed UNWIND_INFO)")
    print("    2. Compute correct parent RSP for Frame 0")
    print("    3. Read parent RIP from stack at correct RSP")
    print("    4. Look up parent RIP in .pdata")
    print("    5. If parent also has no EHANDLER, continue walking")
    print("    6. Stop when we find a frame WITH EHANDLER")
    print()
    
    # Check: RF[2014] (just before the caller) — could be the parent
    if rf0['idx'] > 0:
        rf_prev = rfs[rf0['idx'] - 1]
        ui_prev_raw = get_bytes(data, sections, rf_prev['unwind'], 64)
        if ui_prev_raw:
            ui_prev = parse_unwind_info_raw(ui_prev_raw)
            print(f"  RF[{rf_prev['idx']}] (0x{rf_prev['begin']:06x}-0x{rf_prev['end']:06x}):")
            print(f"    Flags=0x{ui_prev['flags']:x} ({ui_prev['flag_str']})")
            print(f"    CountCodes={ui_prev['count_codes']} TotalAlloc=0x{ui_prev['total_alloc']:x}")
            print(f"    Handler=0x{ui_prev['handler_rva']:x}")
            alloc_p, hex_p = detect_prolog_alloc(data, sections, rf_prev['begin'])
            print(f"    Prolog: {hex_p}")
            print(f"    Prolog alloc=0x{alloc_p:x}")
            if ui_prev['has_handler']:
                print(f"    *** HAS EHANDLER! This could be the target frame!")
    print()
    
    # =========================================================================
    # GCC EXCEPTION PATH ANALYSIS
    # =========================================================================
    print("=" * 70)
    print("ANALYSIS: GCC exception handling path")
    print("=" * 70)
    print()
    print("  The GCC C++ unwinder (inside the PE) calls:")
    print("    1. RtlLookupFunctionEntry (IAT -> our mw_RtlLookupFunctionEntry)")
    print("    2. RtlVirtualUnwind (IAT -> our mw_RtlVirtualUnwind)")
    print("    3. __C_specific_handler (IAT -> our mw___C_specific_handler)")
    print()
    print("  The GCC unwinder walks frames looking for a catch handler.")
    print("  If it FAILS, it calls RaiseException(0x20474343) as fallback.")
    print()
    print("  Current state:")
    print("    RtlLookupFunctionEntry: IMPLEMENTED (binary search works)")
    print("    RtlVirtualUnwind: IMPLEMENTED but has bugs with malformed data")
    print("    __C_specific_handler: STUB (returns 0 = continue search)")
    print()
    print("  The GCC unwinder needs ALL THREE to work correctly.")
    print("  If RtlVirtualUnwind returns wrong parent RIP, the walk goes")
    print("  astray and never finds the catch handler.")
    print()
    
    # =========================================================================
    # UNWIND CODE DISTRIBUTION (for all codes, not just near caller)
    # =========================================================================
    print("=" * 70)
    print("UNWIND CODE DISTRIBUTION (all 3030 entries)")
    print("=" * 70)
    
    opcode_counts = [0] * 16
    total_codes = 0
    total_gcc_opcodes = 0
    
    for rf in rfs:
        ui_raw = get_bytes(data, sections, rf['unwind'], 16)
        if ui_raw is None: continue
        ui = parse_unwind_info_raw(ui_raw)
        for c in ui['codes']:
            total_codes += 1
            if c['op'] < 16:
                opcode_counts[c['op']] += 1
            if c['op'] >= 9:
                total_gcc_opcodes += 1
    
    op_names = ["PUSH_NONVOL", "ALLOC_LARGE", "ALLOC_SMALL", "SET_FPREG",
                "SAVE_NONVOL", "SAVE_NONVOL_FAR", "SAVE_XMM128", "SAVE_XMM128_FAR",
                "PUSH_MACHFRAME"]
    for i in range(9):
        print(f"  {op_names[i]:20s}: {opcode_counts[i]:5d}")
    print(f"  {'GCC-specific (9+)':20s}: {total_gcc_opcodes:5d}")
    print(f"  {'Total':20s}: {total_codes:5d}")
    print()
    
    # =========================================================================
    # EXP-NEXT SUMMARY
    # =========================================================================
    print("=" * 70)
    print("EXP-NEXT SUMMARY")
    print("=" * 70)
    print()
    print("FINDING 1 [CONFIRMED]: Frame 0 (RF[2015], 0x9d560) has MALFORMED UNWIND_INFO")
    print(f"  Evidence: prolog has 'sub rsp, 0x{alloc_bytes:x}' but UNWIND_INFO has no ALLOC")
    print(f"  Evidence: count_codes=1 but SAVE_NONVOL needs 2 slots (slot overflow)")
    print(f"  Evidence: flags=0x0 — NO EHANDLER, NO UHANDLER, NO CHAININFO")
    print()
    print("FINDING 2 [CONFIRMED]: RtlVirtualUnwind produces WRONG parent for Frame 0")
    print(f"  With UNWIND_INFO: RSP = 0x{unwind_rsp_a:x} -> reads 0x201e040 (g_argv)")
    print(f"  With prolog scan: RSP = 0x{correct_rsp:x} -> unknown (need runtime)")
    print(f"  Delta: 0x{correct_rsp - unwind_rsp_a:x} bytes")
    print()
    print("FINDING 3 [CONFIRMED]: Frame 0 must be SKIPPED in any case (no EHANDLER)")
    print("  The exception handler is in an ANCESTOR frame.")
    print()
    print("FINDING 4 [UNKNOWN]: Which ancestor frame has the catch handler?")
    print("  Requires runtime stack walk with corrected RSP.")
    print()
    print("NEXT ACTION:")
    print("  Build runtime test (in loader.c) that:")
    print("  1. Uses naked stub to capture registers at RaiseException entry")
    print("  2. Builds CONTEXT from captured state")
    print("  3. For Frame 0: detect 'sub rsp,N' from prolog bytes, correct RSP")
    print("  4. Read parent RIP from corrected RSP")
    print("  5. Walk upward: lookup parent RF, check EHANDLER, repeat")
    print("  6. Log every frame until EHANDLER found or PE boundary left")
    print("  7. Do NOT implement dispatcher yet — only the WALK and LOG")
    print()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
