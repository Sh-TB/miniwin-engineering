# SW-1293b / SW-1296 — DriverView.exe Death Decode (smoking gun)

- Task ID: 2-a (recon only, no source edits)
- Date: 2026-10-25
- Guest: `artifacts/sweep062/x/driverview/DriverView.exe` (36,352 B,
  sha256 `c5611a45ae0872776c4f0bd03a4e5c532895fdcb62361b5550861cb314899d33`)
- Loader: local build `./minwin_loader` at HEAD `0aa2f0b` (SWEEP-066)
- Evidence: `miniwin-results/DriverView.exe/api_trace.json` (MW32_TRACE=1),
  `/tmp/dv67_stderr.txt` + `/tmp/dv67s_stderr.txt` (MW32_STEP_TRACE=1 rerun),
  step-trace scripts in `artifacts/sweep067/scripts/`.

## 0. Verdict (one paragraph)

DriverView is **UPX-packed** (sections UPX0/UPX1/.rsrc, EP 0x414250 = UPX stub).
It dies **inside the UPX stub's import-rebuild loop**, at step 794,071, because
the cpu32 **REP-string handler advances ESI on `repne scasb`** — an ISA-semantics
bug in `src/loader/loader.c` (string-op case 0xA4..0xAF: `src += dir*sz;
dst += dir*sz` at L39860 and unconditional `R32b(ESIi)=src; R32b(EDIi)=dst` at
L39869 — SCAS must touch EDI only). The corrupted ESI (0x401000 → 0x401023,
+0x23 = the name-scan length) makes the stub's `call [esi+0x1536c]` read its
GetProcAddress thunk slot from **0x41638F instead of 0x41636C** (a 1-byte-misaligned
dword = IAT padding `0x00` + low 3 bytes of USER32!GetDC thunk `0xA0007E`), so the
guest "calls" **0xA0007E00** — an unmapped VA ≈2.5 GiB above the 16-page thunk
page — and the host fetch path takes SIGSEGV.
**EBP=0xdead0001 at death is a red herring**: it is the *by-design* pseudo
kernel32 handle returned by `LoadLibraryA("KERNEL32.DLL")` (api_trace.json
"[API32 #4] <- LoadLibraryA = dead0001") that the UPX stub parks in EBP via
`xchg ebp,eax` @0x414370. It was pushed as a *stack argument* to GetProcAddress,
never used as the call target. The SWEEP-066 note "0xdead0001 uxtheme escape" is
thereby corrected: the handle is **kernel32**, and it did not cause the transfer.

## 1. Fatal state (reproduced twice, byte-identical fatal context)

```
rc=139 (SIGSEGV in host), steps=794071
[CRASH] Signal 11 at RIP=0x20d92b7   (inside minwin_loader, fetch fast-path
  `movzx ebx, byte [edx]` with RDX=guest EIP 0xa0007e00 — host VA unmapped)
[PE32] guest EIP=0xa0007e00 ESP=0x3fefefd4 EBP=0xdead0001 steps=794071
[PE32] eip-hist: 00a00038 00a0003d 00a00000 00414370 00414371 00414373 00414374
                 00414376 00414378 0041437a 00414383 00414384 00414385 00414387
                 00414388 a0007e00
(NOT inside loaded PE)   — no [PE32-FATAL] line: the host SIGSEGV pre-empted it
```

## 2. The last [API32] tail (complete — the run made exactly ONE API call)

From `miniwin-results/DriverView.exe/api_trace.json` (lines 29–31; total `[API32`
count = 2 — the crash occurs before API #2 can be traced):

```
[API32 #4] LoadLibraryA esp=0x3fefefdc args=[4163d1 0 0 3feff000] ret=414370 next=95 8a 07 47 08 c0
LoadLibraryA(KERNEL32.DLL)
[API32 #4] <- LoadLibraryA = dead0001 esp_after=0x3fefefe0
```

- arg0 0x4163d1 = `"KERNEL32.DLL"` in UPX1; ret 0x414370; `next=` bytes decode to
  `xchg ebp,eax; mov al,[edi]; inc edi; or al,al` — the poison-site preview.
- 0xdead0001 = g_known_dlls[0] = kernel32 (pseudo-handle table, loader.c L1148+).
- The would-be call #2 (`GetProcAddress`, slot #6) never reached its gate, so it
  never appears in the trace — its target was garbage (see §3).

Loader-log context right before the crash: `imports: 12 resolved, 1 unresolved
(thunk page 13 APIs)` — the 1 unresolved is `COMCTL32.dll!Ordinal_17`
(InitCommonControls, SW-1204c) → `#ordinal` stub thunk.

## 3. Instruction-grain causal chain (MW32_STEP_TRACE=1)

UPX stub import-rebuild loop (disassembled from the packed image, base-relative
to ESI=0x401000):

```
0x41434E  lea  edi,[esi+0x12000]        ; import array in decompressed data
0x414354  mov  eax,[edi]                ; dll-name RVA        (0x169)
0x41435A  mov  ebx,[edi+4]              ; original IAT RVA    (0xA064 → 0x40B064)
0x41435D  lea  eax,[eax+esi+0x15268]    ; → 0x4163d1 "KERNEL32.DLL"
0x414366  push eax                      ; lpLibFileName
0x41436A  call dword [esi+0x15364]      ; → [0x416364] = LoadLibraryA thunk 0xA00038
0x414370  xchg ebp,eax                  ; EBP = 0xdead0001   ← pseudo-handle parked
0x414371  mov  al,[edi]  / 0x414373 inc edi / 0x414374 or al,al / 0x414376 je next-DLL
0x414378  mov  ecx,edi  / 0x41437A jns 0x414383     ; plain-ASCII name path
0x414383  push edi                      ; lpProcName (decompressed name blob)
0x414385  f2 ae  repne scasb            ; scan to NUL: ECX 0x413009→0x412FE6 (−0x23),
                                        ;   EDI 0x413009→0x41302C (+0x23),
                                        ;   ESI 0x401000→0x401023 (+0x23)  ← THE BUG
0x414387  push ebp                      ; hModule = 0xdead0001 (stack arg, harmless)
0x414388  call dword [esi+0x1536c]      ; EA = 0x401023+0x1536C = 0x41638F  (NOT 0x41636C!)
0x41438E  or eax,eax / 0x414390 je 0x414399 (ExitProcess failure path — never reached)
0x414392  mov [ebx],eax / add ebx,4 / jmp 0x414371  ; store resolved fn, next import
```

Target arithmetic, byte-exact (verified with pefile + loader thunk order):

```
IAT slots: #4 LoadLibraryA=0xA00038  #5 ExitProcess=0xA00042  #6 GetProcAddress=0xA0004C
           ...                      #11 GetDC=0xA0007E       (slot i → 0xA00010+10*i)
bytes at 0x41638C..0x416393 (loaded image):
           00 00 00 00 | 7E 00 A0 00     (4 B IAT padding, then GetDC thunk 0xA0007E)
dword read at misaligned 0x41638F = 00 7E 00 A0 → 0xA0007E00   == observed guest EIP
```

So the call consumed `padding(0x00) + GetDC-thunk(0xA0007E)` shifted by one byte —
the observed crash target is *derivable to the byte* from the +0x23 ESI drift.
0xA0007E00 is unmapped (thunk page = 0x00A00000..0x00B00000; arena 0x10000000+
starts at 0x10000000), the interpreter's identity-mapped fetch read host VA
0xA0007E00 → SIGSEGV → crash_handler dump.

Who wrote 0xdead0001 into EBP (correlation demanded by the task): **API32 #4
LoadLibraryA("KERNEL32.DLL")**, via the gate return path `t_cpu32.r[EAXi]=ret`
(loader.c L39277), then guest `xchg ebp,eax` @0x414370 (step #794062 is the
first step with EBP=0xdead0001). Not uxtheme, not GetModuleHandle — the guest
never got past its first API call.

## 4. Root cause in engine source

`src/loader/loader.c` L39830–39870, the shared string-op handler:

```c
case 0xA4..0xAF: {                       /* MOVS/CMPS/STOS/LODS/SCAS */
    uint32_t src = R32b(ESIi), dst = R32b(EDIi);   /* L39834 */
    ...
    src += dir * sz; dst += dir * sz;    /* L39860 — BOTH advanced for every op */
    ...
    R32b(ESIi) = src; R32b(EDIi) = dst;  /* L39869 — BOTH written back */
```

Per Intel SDM: STOS (0xAA/AB) and SCAS (0xAE/AF) modify **EDI only**; LODS
(0xAC/AD) modifies **ESI only**. Only MOVS (0xA4/A5) and CMPS (0xA6/A7) use both.
The handler's uniform advance/write-back leaks `src` drift into ESI on every
SCAS/STOS (and `dst` drift into EDI on every LODS) — for non-rep forms too
(the `for(;;)` runs once; L39869 still writes both back).

## 5. SW-1296 hypothesis verdict

*"NirSoft tiny tools need aggressive function discovery; find the API sequence
that poisons EBP with 0xdead0001."*

- **Refuted as the crash mechanism.** The single completed API call is
  LoadLibraryA (not an aggressive discovery sequence); EBP=0xdead0001 is the
  expected pseudo-handle parked by the UPX stub and is NOT used as the call
  target that died. The killer is the REP-SCAS ESI corruption.
- **Partially true for the post-fix path**: once unblocked, the stub will resolve
  the unpacked exe's full import set via GetProcAddress(0xdead0001, name) at
  runtime. That is already supported (pseudo-handle range check loader.c
  L1110/L1126 → dispatch-table by-name resolve → `mw32_thunk_for_host_fn` wraps
  returned host ptrs into guest thunk VAs, L39251–39258, SW-1281 contract), so
  no new "function discovery" machinery is required — the gate trace will show
  the entire resolved set for free.

## 6. Recommended honest fixes (trade-offs)

1. **PRIMARY — ISA-correct string-op register write-back (≈6 lines).**
   Advance/write back only the index register(s) each opcode uses:
   STOS/SCAS → EDI; LODS → ESI; MOVS/CMPS → both. Trade-offs: none known —
   pure engine correctness; removes a whole latent bug class for every guest
   using `rep scas`/`rep stos`/`rep lods` (string/hash routines everywhere).
   Expected effect: stub completes LoadLibraryA → GetProcAddress×N rebuild →
   VirtualProtect → jump to OEP; all 13+ UPX-packed NirSoft PE32 guests
   (SWEEP-065 rc=139 class) unblocked in one move.
2. **SECONDARY — typed fatal instead of raw SIGSEGV (defense in depth).**
   In the interpreter call/transfer path, validate targets against
   {image span, thunk page, arena-exec}; on miss emit `[PE32-FATAL] bad transfer
   target=… from eip=… slot=…` with the eip-hist ring, then stop cleanly.
   Trade-off: small hot-path cost (one range check per control transfer);
   converts future engine bugs from "host segfault + generic dump" into
   one-line diagnosable guest fatals. (Complements the existing m32_* guard.)
3. **NOT recommended — re-plumbing pseudo-handles.** Making LoadLibraryA return
   something other than 0xdead0001 would break the handle-equality contract
   (GetModuleHandleA/LoadLibraryA/GetProcAddress must agree; dozens of guests
   late-bind through it, SWEEP-002/SW-509/SW-1297). The handle is not the bug.

## 7. Post-fix verification plan (for the next agent)

1. Rebuild, re-run: `MW32_TRACE=1 MW32_MAX_STEPS=800000 ./minwin_loader
   artifacts/sweep062/x/driverview/DriverView.exe`.
2. Expect `[API32 #6] GetProcAddress args=[dead0001 <name>]` ×N, results stored
   at 0x40B064+ (rebuilt IAT), then `VirtualProtect`, then EP-jump into 0x401000
   region (real .text).
3. Watch the first proc name (35-byte scasb scan from 0x413009): if
   GetProcAddress returns 0 the stub takes its own ExitProcess path @0x414399 —
   a clean, traceable exit (a far better failure mode than SIGSEGV) that names
   the unresolved import directly in the trace.
4. Then batch-re-run the sweep062 UPX set (heapmemview, faview, htmlastext, …)
   — same stub family, same fix.
