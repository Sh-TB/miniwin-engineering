# SWEEP-098 — SW-1477 GetClassInfoW guest-ABI overrun fix (NPP installer chain)

Date: 2026-10-28 · Base: origin/main 161f3e4 (SWEEP-097) · G1 PIN 12/12
(evidence/master/g1/20261028_100920, binary sha 95c4e4c8, 4,653,768 B)

## Context

Round = engineering train executing the SWEEP-097 entry point ("frame-integrity
probe at 0x406726" for the nppsetup crash). Sandbox had been reset: clone was
stale at SWEEP-030 (242af84); fast-forwarded to origin/main 161f3e4 (98 commits,
SWEEP-031..097 adopted, no force). mini.flood rebuilt from tracked source with
ziglang 0.17.0 (223744 B — byte-count matches the SWEEP-093/095 pin exactly;
sha re-pinned in the manifest per the documented zig-timestamp non-reproducibility;
gate-verified in-run rc=0 x3, 13 frames, crash=0). XP SEED guests honestly
MISSING (licensed-media-only, per docs/GUEST_ARTIFACTS.md).

## SW-1477 ROOT CAUSE (all OBSERVED, gdb hardware-watchpoint + steptrace chain)

The nppsetup deterministic SIGSEGV rc=139 at ~139.97M steps, guest EIP 0x406742
(mov eax,[ecx], ecx=0) decoded end-to-end:

1. The exehead is NSIS 3.12. The crashing code is its langstring resolver
   (0x406726: negative index -> base-(idx+1)*4 from [0x428a3c], then
   header+offset*2) — reached from the language-dialog WM_INITDIALOG ComboBox
   fill (DialogBoxParamW template 0x69, proc 0x404169).
2. [0x428a3c] is the NSIS language-selector output (matched-entry+0xA,
   langstring base): the selector at 0x404091 scans 58 real language entries
   (stride 0x16a, lang ids 0x409/0x436/0x41c/... observed live) and stores it;
   PROBE-E caught [0x428a3c]=0x111301c2 valid at the first RegOpenKeyExW.
3. **mw_GetClassInfoW (slot 55) memset a HOST x86-64 struct (~72-80 B) through
   the guest's 40-byte i386 WNDCLASSW pointer (0x428a00)** — the exehead keeps
   adjacent globals in that .data region: 0x428a2c (flag), 0x428a3c (langstring
   base), 0x428a40 (language-dialog template id). Hardware watchpoint W#1:
   0x111301c2 -> 0x00000000 written INSIDE mw_GetClassInfoW (bt: loader.c:31376).
4. With both outputs zeroed the dialog opened as template 0+0x69 and the
   ComboBox fill dereferenced the zeroed base -> the SWEEP-096/097 frontier.

## SW-1477 FIX (loader.c)

mw_GetClassInfoW now writes EXACTLY the guest ABI: i386 guests get a 40-byte
(uint32_t[10]) zeroed WNDCLASSW, x64 guests 72 bytes; the MwClassA host struct
is gone. Return semantics unchanged (TRUE for the queried system class).

## MEASURED after/after

- nppsetup x3: rc=139 crash=1 (steps 139,972,856, frames=0) -> rc=139 crash=1
  (steps 140,016,399, **frames=1 uniq=1** — the language dialog now RENDERS
  720x242, welcome-page children id=1028..1046 created, SetDlgItemTextW(1028),
  70,010 API events vs 66,499 = +3,511 events of real progress).
- The crash site MOVED PAST the entire language-dialog chain: new frontier =
  NSIS's own error path (MessageBoxIndirectW style=0x12 'Notepad++ v8.9.8.1
  Setup', DestroyWindow, EndDialog(0), WM_COMMAND(IDOK)) then ret-to-garbage
  0x43403a (bad return address, eip-hist ...040408d 0404090 -> 0434 03a).

## SW-1478 (NEXT FRONTIER, decoded with probe evidence — queued)

$PLUGINSDIR path corruption: the guest builds nsfc669.tmp plugin paths whose
separators are EATEN ('\homezmy-projectworkspaceminiwin-engineering...WindowsTempnsfc669.tmp\System.dll'
— raw guest buffer dump at 0x40a5f8, u16len=54) -> CreateFileW/LoadLibraryA
retry loop -> the error box. Probe chain: mw_lstrcpynW(dst=0x40a5f8,
src=0x427a00) copies a POSIX-mapped path WITH slashes from g_bufs[0] (0x427a00,
watch-verified progressive writes '/','/h','/ho'); NSIS 3's validate_filename
( GetStringFromParm: id_<0 -> validate; strips  " < > | ? * : / \ by shifting)
then eats every separator. The POSIX text ENTERS guest string space from an
unidentified writer (GetTempFileNameW/GetCurrentDirectoryW never called;
GetTempPathW correctly wrote 'C:\Windows\Temp\' per SW-1405). Next micro-step:
watch 0x42a000 writers with per-hit bt to name the POSIX source API, then
decide loader-side (return DOS form) vs NSIS-behavior note.

## SW-1479 (queued, small)

The post-error-box bad return address 0x43403a (ret from 0x404090 lands in
.data): decode the caller-frame corruption — possibly the same clobber family
as SW-1478 or an unbalanced callback-stack exit.

## Gates

- Clean build; binary 4,653,768 B sha 95c4e4c8; G1 PIN 12/12
  (evidence/master/g1/20261028_100920, regression 12/12).
- corpus_gate sweep098: 17 rows, rc=0 in 12, crash total 0
  (evidence/sweep098/corpus_parity.tsv) — shape identical to SWEEP-097.
- repo_size_guard: GUARD OK (source-only policy clean; two pre-existing
  documented re-pin WARNs: shexview, hxd.portable.setup).
- Battery spot-checks x1: idman PASS_LAUNCH_RENDER rc=0 (1 frame), npp64 EXIT
  rc=0, winrar EXIT rc=0, winrarinstd TIMEOUT rc=124 (documented pre-message-
  loop state) — ZERO regressions on the three research apps.
- Tooling re-provisioned user-space after the reset: gdb 16.3 (~7 debs, no
  root, ptrace smoke-tested), ziglang 0.17.0 via pip. Probe scripts persisted
  at /home/z/my-project/scripts/npp_*.gdb (outside the repo).

## Constraint compliance

No manifests other than the mini.flood sha re-pin (documented, gate-verified);
no new Issues/PRs; linear commit on top of 161f3e4; no force-push.
