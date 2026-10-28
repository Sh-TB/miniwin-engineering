# SWEEP-105 — internet-acquisition round: 3 stale-source guests unblocked
# (winnc / renamer / shexview re-pinned by SHA), mini.flood SEED rebuilt,
# SW-1493 runner fix (winnc first run ever), SW-1494 renamer halt-204 decode
# (mechanism fully mapped), quartet + regression batteries all match the
# SWEEP-104 reference taxonomy.

Date: 2026-10-29 · Base: origin/main ad1288d (SWEEP-104) ·
Final binary sha256 7f08acaa83d44178d2caca55a497da5012e48efa7be50b41c4b014c3dba911cf
(G1 PIN 12/12, evidence/master/g1/20261028_201610; byte-identical rebuild of the
SWEEP-104 pin a09aa334 was reproduced first, then the SW-1494 watch line re-pinned)

## Sandbox-reset recovery (start of round)

Local clone was 74 commits behind (242af84 SWEEP-030 -> ad1288d SWEEP-104 after
fetch). Stale untracked artifacts/ moved out of tree, git reset --hard origin/main.
Deterministic rebuild re-measured a09aa334 EXACTLY (the SWEEP-104 G1 pin) before
any edits — build reproducibility re-confirmed from a cold sandbox.

## Wagon 1 — internet re-pin hunt (user granted internet use this round)

- winnc.installer: winnc.de is DEAD. winnc.com/setup.exe serves the EXACT pinned
  bytes (bef0fcafd7ce5b111beb61923b304c3f001a53061200ba65cdbc19c441ec011a,
  12964000 B, SHA-verified). manifest col4 UNVERIFIED_URL -> VERIFIED_URL.
- renamer.portable: den4b.com serves 7.5 portable behind a tokenized download
  page (GET page -> href "?key=<hex>" -> zip). Byte-exact match
  (b11342a59f22d9c48482a7f08e060109da21bb25cfc8acdd42d2b40ea6489168, 4565468 B).
  fetch_guest.sh gained a documented one-shot tokenized-retry: if the fetched
  blob is an HTML interstitial carrying ?key= links, retry each once; the
  SHA-256 gate is unchanged (bytes stay pinned; only the human click is
  automated). End-to-end fetch_guest VERIFIED for both guests.
- shexview: UNKNOWN_REPIN_REQUIRED -> fresh download from the exact pinned
  nirsoft URL re-pinned (73248fb48ee6387cadbd6bdde5265cbef465cea7fca8f3b72eac5c408f4e7ccb,
  103904 B); fetch_guest VERIFIED.
- mini.flood SEED: rebuilt from tracked tests/games/miniwin_flood.c via ziglang
  0.17.0 after sandbox reset; 223744 B byte-count matches the SWEEP-093 pin
  exactly; manifest re-pinned (9b06ed2c28e87dcc94de23b2eb324b4603c5f93305d2a4def76bbd207d2d2905;
  zig-timestamp non-reproducibility documented per SWEEP-098/099/100 precedent).
- hxd: mh-nexus.de unreachable (curl timeout); a web.archive.org snapshot exists
  (20260925180045) but the wayback transfer timed out twice at 240s+. Stays
  honestly BLOCKED; snapshot URL recorded for the next attempt.

## Wagon 2 — SW-1493: runner fix, winnc runs for the first time

scripts/plugin_run.sh: the non-sandbox/non-portable run branch read bare
$PORTABLE_CWD under `set -u`; the variable is only set by the portable-zip
branch (SW-1470), so EVERY plain-plugin battery died with
"PORTABLE_CWD: unbound variable" before its first run (winnc, unpack=none,
was the first plugin shaped that way since SWEEP-104). Fixed with the
${PORTABLE_CWD:-} guard (line 280 branch; the line-266 read was already
guarded). Latent since SWEEP-104; found by the first winnc battery.

## Wagon 3 — batteries (SWEEP=sweep105, foreground, idle)

- nppsetup: rc=0 x3, crash=0 (SW-1477..1486 frontier stays closed; no regression).
- notepad2mod: PASS_INPUT_STATE_CHANGE frames=4 uniq=3 x3 (reproduces SWEEP-104).
- produkey: ALIVE_PUMP_AT_TIMEOUT frames=1; everything: ALIVE_PUMP_AT_TIMEOUT
  frames=0; tcpview64: SPIN_AT_TIMEOUT; winrarinstd: SPIN_AT_TIMEOUT — the
  quartet taxonomy matches SWEEP-104 exactly (SW-1489 wait fix caused no
  drift; they never reach the wait gate).
- winnc (FIRST RUN EVER): rc=86 x3 deterministic, crash=0, frames=0.
  Decode: the Inno Setup stub walks resources (FindResourceW/LoadResource/
  LockResource at 0x4a17cb..0x4a180f), then the guest jumps INTO ITS OWN STACK
  (0x3fefeffc/0x3fefeffe) and hits `[PE32-FATAL] opcode not implemented
  opcode=0xef at EIP=0x3fefefff` — the SW-1281 interpreter abort (exit 86).
  Frontier registered: a bad indirect call target right after LockResource
  (bad function-pointer read or LockResource return-value contract). NEXT.
- renamer: EXIT rc=204 frames=0 x3 deterministic, crash=0 (see Wagon 4).
- corpus gate sweep105: 17 rows, rc=0 in 12, crash total 0 (canonical; 5 xp.*
  SEED rows honest-MISSING as always).

## Wagon 4 — SW-1494: renamer halt-204 decode (mechanism mapped)

ReNamer.exe (Delphi, PE32, base 0x400000, EP 0x343670) exits with
ExitProcess(204) after ~917 API events, before any window API. Evidence chain
(all OBSERVED via MW32_WATCH / MW32_DUMP_VA / MW32_STEP_TRACE, instruments:
SW-1482 watch + new SW-1494 W8 byte-store watch in m32_st8):

1. Init: GetModuleHandleA x3, GlobalAlloc, GetStartupInfoA+W, TlsAlloc, then
   ~55 iterations of {SetLastError(0), GetLastError()=0, TlsGetValue(1)=0x11000000,
   <arena slot 0xa00196> -> 39} inside function ~0x4127xx.
2. Guest executes `mov WORD ds:0x7447a0, 0xCC` at 0x410cac (watch W16 val=000000cc
   eip-post=0x410cb5) — 0x7447a0 is the Delphi ExitCode global (.data), file
   initial value 00000000.
3. `call _Halt @0x410c40` with eax=0xCC: `mov ds:0x8ec000, eax` (watch W32
   val=000000cc) -> optional exit-callback [0x7448a0]=nil (skipped) ->
   _Halt0 chain (exit-proc ptr [0x8ee020], secondary [0x8ec3d0]) ->
   `push DWORD ds:0x8ec000` at 0x415123 -> import stub 0x401030 (jmp [0x8f5978],
   FirstThunk mirror of INT 0x4f5978) -> ExitProcess(204).
4. 204 = Delphi runtime error reInvalidPtr (invalid pointer operation) raised
   during unit init -> Halt(204). The halt machinery is NOT the bug; the
   invalid pointer event that Delphi's memory manager detected during init is.
5. UNRESOLVED (queued): which early-init semantics corrupt the Delphi heap.
   Candidates: VirtualAlloc/VirtualQuery contract for the FastMM reserve,
   TlsGetValue returning a fixed 0x11000000 arena block for slot 1, or the
   unlogged arena slot 0xa00196 (returns 39). Two static puzzles documented:
   objdump linear-sweep misalignment vs the real instruction stream (verified
   by pre-run MW32_DUMP_VA: `a3 00 c0 8e 00` IS at 0x410c40), and the
   misaligned-disassembly artifact at 0x410c3f that the interpreter never
   executed (the earlier 0xCC .bss write theory was disproven by the watch —
   the store was the real `mov [0x8ec000],eax` at _Halt).

## Gates

- G1 PIN 12/12 (evidence/master/g1/20261028_201610, binary 7f08acaa; the
  pre-edit rebuild reproduced a09aa334 byte-for-byte at 20261028_193409).
- corpus_gate sweep105: 17 rows, rc=0 in 12, crash total 0 (canonical).
- manifest.tsv: 4 rows changed (winnc/renamer/shexview re-pins + mini.flood
  SEED re-pin), all via python field-writers (a MultiEdit tab-mangling bug
  that space-wrecked 26 TSV rows was caught by the gate and reverted).
- No loader.c behavior change: only the SW-1494 W8 watch log line in m32_st8
  (gated by MW32_WATCH, zero cost when unset).

## Queued (not started)

- SW-1495: winnc LockResource->stack-jump decode (opcode 0xef fatal).
- SW-1494 completion: the renamer invalid-pointer trigger (FastMM/TLS/slot-0xa00196).
- SW-1492: the rare load-adjacent decompression-error dialog (SWEEP-104 reg).
- Spin-frontier decodes: tcpview64 msg=0x43f storm + winrarinstd SW-1412 loop.
- hxd via web.archive.org snapshot 20260925180045 (transfer needs resumption).
- xp.* SEED guests remain honest-MISSING (licensed-media acquisition path).

No force-push.
