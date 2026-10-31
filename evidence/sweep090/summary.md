# SWEEP-090 — SW-1305 FindFirstFileA struct contract + two new wagons

Date: 2026-10-27 · Base: origin/main 0f61e2f (SWEEP-089) · G1 PIN 12/12 (evidence/master/g1/20261027_081203)

## Context

Session resumed on a STALE local snapshot (SWEEP-030-era checkout). Local-only
work there re-derived drive-sandbox/registry-tree/handle-release fixes that the
mainline had already landed better (SW-1234/1402/1405/1407/1408/1244, SWEEP-047
through SWEEP-089). Per the no-history-rewrite rule the mainline was adopted
(branch sweep031-local-divergent keeps the divergent work); the ONE genuinely
still-live finding was ported forward.

## SW-1305 (ported): FindFirstFileA/FindNextFileA wrote the W struct into A buffers

FindFirstFileA callers pass WIN32_FIND_DATAA (318 bytes, CHAR name arrays) but
fill_find_data always wrote the WIN32_FIND_DATAW layout (592 bytes, wide names
@+44..564) -> 274-byte stack overflow -> guest /GS cookie smash -> FASTFAIL(2).
Latent since BUG-049 (every prior A-variant call hit a missing directory, fill
never ran). Proven on the stale snapshot: putty FindFirstFileA("C:\Windows\*")
enumerated a SEEDED dir -> System32 entry -> overflow -> FASTFAIL; difflib
trace alignment isolated the exact call; fixing the A layout restored putty/
puttygen to rc=0. Port: W3Find.wide flag + fill_find_data_a (318B, CHAR names
@+44, cAlternate @+304) + find_first_impl(wide) + FindFirstFileA/NextA paths;
FindFirstFileW/ExW/NextW unchanged.

## New wagons (train protocol: one new game + one new program)

- netgame (Simon Tatham Net, puzzles.zip sha a5b00f63): PASS_INPUT_STATE_CHANGE
  first contact — rc=0 x3, frames=20 uniq=10 (plugins/netgame, evidence/sweep090/netgame)
- shexview (NirSoft ShellExView x64 2.01, zip sha 73248fb4): PASS_LAUNCH_RENDER
  first contact — rc=0 x3, 1 main-window frame; GUI ListView walking HKCR CLSID
  (pairs with the installer-written shell-extension keys from SWEEP-086 regtree)
  (plugins/shexview, evidence/sweep090/shexview)

## Gates

- Clean build, binary 4,615,008 bytes; G1 PIN 12/12 (20261027_081203).
- Corpus 21/21 x3 rc=0 crash=0 fatal=0 (evidence/sweep090/corpus_parity.tsv);
  vs SWEEP-089: identical except breakout/snake now render frames (10/13 vs 0)
  — improvement, no regressions.

## Not ported (deliberate) / queued

- Drive-tree sandbox + registry-tree mirror + handle-base separation: mainline
  equivalents already train-hardened (SW-1234 cwd drive root, SW-1402 regtree,
  SW-1405 skeleton+WINTEMP, SW-1407/1407b module path + staging, SW-1244
  CloseHandle release). Handle VALUE-space collision (file 4..67 vs kobj small
  ints) remains a latent hazard — queued SW-1416.
- SW-1306 PE32/WoW64 (32-bit NSIS stubs: nppsetup/winrarinstd-class installers)
  and SW-1307 real guest-DLL module loading (7z.exe codec 0x8007007E) unchanged.
