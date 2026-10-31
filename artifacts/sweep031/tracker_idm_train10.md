# idman642build37.exe (IDM 6.42 b37 installer) — TRAIN-10 precise tracker

Guest: artifacts/app3/idm/idman642build37.exe — PE32 (magic 0x10B, machine 0x14C),
12,698,904 bytes, 5 sections (.text 0xb6a7 / .rdata 0x36ca / .data 0x2ebc /
.rsrc 0x4758 / .reloc 0x1652), entry RVA 0x5b7a, import directory RVA = 0
(runtime-resolved imports).
Result this train: typed diagnostic (fix #48): "[ERROR] PE32 guest rejected:
magic=0x10b machine=0x14c — engine executes PE32+ (0x20B) only; 32-bit (WOW64)
execution is an open frontier".

## Problem ledger

| # | Problem | Before | Fix | Verified |
|---|---------|--------|-----|----------|
| 48 | PE32 guests rejected bare (Not a PE32+ file), exit 1, no diagnosis | no path forward | typed error naming magic+machine + WOW64 frontier (Wine runs PE32 through the same loader; engine is PE32+/x64-only) | message shows machine=0x14c |

## Frontier (next train) — the honest scope of 32-bit support
1. 32-bit ABI: stdcall/cdecl, EDX:EAX returns, 4-byte pointers.
2. 32-bit CONTEXT + WOW64_FLOATING_SAVE_AREA, 32-bit unwind codes
   (RtlUnwind32, 8-byte RUNTIME_FUNCTION entries).
3. Runtime import resolution (import RVA=0 for this installer).
4. 32-bit CRT startups (VS2019/VC6 mixed).
Wine precedent: dlls/ntdll/signal_x86_64.c keeps separate RtlWow64* paths —
the engine needs the analogous second ISA lane.
