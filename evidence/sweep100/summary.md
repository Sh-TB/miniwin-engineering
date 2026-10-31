# SWEEP-100 — SW-1479 CLOSED: the bad-ret was an NSIS MGA_2 arity-32
# over-pop (GetUserDefaultUILanguage); the installer now runs its FULL
# lifecycle — extract, register HKLM\SOFTWARE\Notepad++, clean up, exit 0

Date: 2026-10-28 · Base: origin/main 43fac42 (SWEEP-099b) · G1 PIN 12/12
(evidence/master/g1/20261028_150624, binary sha 8158af83)

## Context

Sandbox reset handled again: clone was stale at SWEEP-030 (242af84);
fast-forwarded to 43fac42, stale untracked artifacts backed up.
gdb 16.3 re-provisioned user-space; NPP installer re-fetched + hash-verified
(26f3bcccced7... exact); ziglang 0.17.0 reinstalled via pip.

## Decode (OBSERVED end-to-end, three new interpreter instruments)

gdb hw-watch probes cannot run this interpreter to completion (batch gdb
stalls the process — PROBE-K1, rc=124 zero hits in 300 s). Landed instead as
env-gated loader instrumentation (zero cost when unset):

- **SW-1482 MW32_WATCH="lo:hi"** — logs every interpreter ld/st in a guest-VA
  window (value, guest EIP/ESP/EBP, host return site).
- **SW-1483 MW32_ENTRY_TRACE="va,..."** — logs every arrival at guest EIPs
  (full regs + [esp] + previous EIP).
- **SW-1484 MW32_TRACE_RANGE="lo:hi"** — per-instruction I32 trace restricted
  to an EIP range (full-register trajectory, no 23 GB firehose).

PROBE-K2 (watch 0x3fefec30-0x3fefec80, evidence/sweep100/probes/k2_watch_tail.txt):
[0x3fefec6c] saw exactly ONE write all run (0x43403a) and the epilogue ret
popped it. 0x408208 = **USER32!CharNextW** (PE import parse) — the "0x43403a"
is a LEGITIMATE .ndata string cursor the caller's init saved with
`mov [esp+0x1c],ecx` at 0x403852 (the SWEEP-099 "g_stacktop family" guess is
corrected: unrelated to the VM stack, pure landfill overlap).

PROBE-K3 (entry trace, k3_entry_trace.txt): call site 0x403ab6 ESP=0x3fefec50;
driver 0x403dbb entry ESP=0x3fefec4c with [esp]=0x403abb — **the call-push DID
happen** (SWEEP-099's "no call-push ever writes it" was a gdb-stall probe
artifact). Driver frame base = 0x3fefec4c; epilogue expects entry-0x20.

PROBE-K4 (EIP-range trajectory 0x403dbb-0x404090, k4_esp_trajectory.txt):
prologue balanced (sub 0x10 + push ebx/ebp/esi/edi -> 0x3fefec2c), then at
**0x403dd5 `call eax`** ESP jumped to 0x3fefec4c: the callee popped 0x20 the
exehead never pushed. The epilogue's pops therefore ate [0x3fefec4c]=0x403abb
(the return address — EDI=0x403abb at crash, matches the crash dump), then
`ret` consumed [0x3fefec6c]=0x43403a -> fetch fault, rc=139.

Callee named: 0x406add = the NSIS MGA resolver (GetProcAddress through table
0x40a408); **MGA_2 = KERNEL32!GetUserDefaultUILanguage — a 0-arg LANGID**.

## Mechanism

The dispatch row lived under USER32.DLL; NSIS resolves MGA_2 from KERNEL32.
The miss fell to the optional-stub fallback, and mw32_thunk_for_host_fn()
wraps unknown raw host pointers with **arity=32** (MW32_ARITY_DEFAULT).
Zero-arg `call eax` -> +0x20 over-pop -> driver frame destroyed -> epilogue
ret into the caller's saved CharNextW cursor -> SIGSEGV. Same class as the
SW-1480 SHGetFolderPathW/IsUserAnAdmin decode, one plane deeper (the dynamic
GetProcAddress path instead of static imports).

## SW-1485 FIX (loader.c)

- g_api32_arity rows added (whole NSIS MGA family audited, not just the
  crashing one): GetUserDefaultUILanguage 0, RegDeleteKeyExW 16,
  InitiateShutdownW 20, IsOS 4, SHAutoComplete 8, SHGetKnownFolderPath 16.
- GetUserDefaultUILanguage dispatch row MOVED USER32.DLL -> KERNEL32.DLL
  (the real export home, Vista+).
- New dispatch rows + honest models: ADVAPI32 RegDeleteKeyExW (delegates to
  the real hive delete, wow64 flags ignored on the flat view) and ADVAPI32
  InitiateShutdownW (headless failure 0, mirrors mw_ExitWindowsEx).

## MEASURED (nppsetup x3, full battery env)

- Pre-fix: rc=139 crash=1, steps 192,740,428 (SWEEP-099b state).
- Post-fix: **NO CRASH in any run** (rc 124/0/0; crash=0). The installer now
  completes its ENTIRE exehead lifecycle: CreateDirectoryW Windows\Temp ->
  GetTempFileNameW DOS retry chain nsf/nsh/nsk0001 (SW-1478 contract) ->
  35 CreateFile plugin extraction (System.dll, LangDLL.dll, InstallOptions.dll,
  ioSpecial.ini) -> **HKLM\SOFTWARE\Notepad++ InstallerLanguage written
  (20-key registry manifest)** -> cleanup DeleteFileW x14 + RemoveDirectoryW
  x2 -> clean interpreter exit. install_log.tsv 63 ops; registry_tree HKCU+HKLM
  staged (evidence/sweep100/nppsetup/).
- Determinism NOTE (honest): rc 0 vs 124(timeout) varies run-to-run — the
  wizard interaction path is wall-clock-bistable. Registered as **SW-1486**
  (next round: pin the interaction schedule so x3 is identical).

## Gates

- G1 PIN 12/12 (evidence/master/g1/20261028_150624, binary 8158af83).
- corpus_gate sweep100: 17 rows, rc=0 in 12, crash total 0 — same shape as
  SWEEP-099. mini.flood rebuilt from tracked source (ziglang 0.17.0,
  tests/games/miniwin_flood.c unchanged, 223744 B — byte-count matches the
  SWEEP-093 pin exactly; manifest re-pinned per the documented
  zig-timestamp non-reproducibility, 6bbf1381..., gate-verified in-run).
- repo_size_guard: GUARD OK (two pre-existing documented WARNs: shexview,
  hxd.portable.setup). Remediation carried: evidence/sweep099/nppsetup/
  probe_sandbox/probe_install_log.tsv was tracked despite the
  evidence/**/sandbox/ policy — git rm --cached (file stays on disk).

## Instruments

Probe scripts for this round are the loader env flags themselves
(MW32_WATCH / MW32_ENTRY_TRACE / MW32_TRACE_RANGE, committed with the code);
raw excerpts in evidence/sweep100/probes/. The gdb-based PROBE-K1 attempt is
archived outside the repo (/home/z/my-project/scripts/probe_k1.gdb).

## Constraint compliance

Manifest touched only for the mini.flood sha re-pin (documented,
gate-verified); no new Issues/PRs; linear commit on top of 43fac42;
no force-push; no Persian content.
