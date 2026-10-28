# SWEEP-099 — SW-1478 $PLUGINSDIR separator-eating fixed + SW-1480 NSIS MGA
# ordinal plane (shell32!680 = IsUserAnAdmin) — language dialog fully
# interactive, +52.7M steps measured

Date: 2026-10-28 · Base: origin/main 35c820a (SWEEP-098) · G1 PIN 12/12
(evidence/master/g1/20261028_124332, binary sha 798d5005, 4,654,248 B)

## Context

Round = engineering train executing the SWEEP-098 entry point (SW-1478 next
micro-step: "watch 0x42a000 writers with per-hit bt"). Sandbox reset handled:
clone was stale at SWEEP-030 again; fast-forwarded 242af84->35c820a, stale
untracked artifacts backed up. gdb 16.3 re-provisioned user-space
(scripts/provision_gdb.sh), ziglang 0.17.0 via pip.

## SW-1478 ROOT CAUSE (OBSERVED — gdb watchpoint PROBE-F + trace archaeology)

The SWEEP-098 decode had the chain but not the writer. This round named it:

1. install_log.tsv (SW-1403 raw-guest-path column) pinned the corruption
   window: op1 CreateDirectoryW(C:\Windows\Temp\) clean; op2 DeleteFileW
   ALREADY POSIX ("/home/.../Temp//nsddd47.tmp" — double slash = $TEMP with
   trailing slash + NSIS-appended backslash). Between the two ops NO
   path-returning API ran (trace 230..235 = GetTempPathW, skeleton seed,
   one GetTickCount).
2. PROBE-F (scripts/probe_f_posix_writer.gdb): hw watchpoints on g_bufs[0]
   (0x427a00) and 0x42a000, armed at mw_GetTempPathW, conditioned on the
   watched u16 becoming '/' (foreign in DOS-world guest strings). FIRST hit
   (0x42a000): bt names the writer end-to-end —
   `w3_utf8_to_utf16le(src="/home/.../Windows/Temp/nsfe18a.tmp", dst=0x42a000)
   <- utf8_to_utf16 <- mw_GetTempFileNameW (slot 100, out=0x42a000) <-
   mw32_api_gate <- cpu32_step`. The exehead's Ui.c temp-name global lives at
   0x42a000 — exactly the address the SWEEP-098 round queued for watching.
3. So NSIS 3 DOES call GetTempFileNameW (path=C:\Windows\Temp\, uniq=0); the
   old "never called" note came from the event layer not logging it
   (no MW_TRACE in the old impl). The old mw_GetTempFileNameW returned the
   POSIX-MAPPED fopen target to the guest — the POSIX text entered guest
   string space at birth. NSIS validate_filename then ate '/' and ':' (NOT
   '\') from every composition: LoadLibraryA
   "homezmy-projectworkspaceminiwin-engineering...nsudd48.tmp\System.dll" ->
   CreateFile/LoadLibrary retry loop -> the error box (SWEEP-098 frontier).

## SW-1478 FIX (loader.c, mw_GetTempFileNameW rewritten to the real contract)

- the FILE is created at the mapped POSIX location (sandbox ground truth);
- the GUEST receives the DOS form `<path>[\\]<prefix><hex>.tmp` — separator
  ADDED when lpPathName lacks the trailing backslash (wine path.c behavior;
  NSIS passes "C:\Windows\Temp" bare — observed CreateDirectoryW
  "Tempnsf0001.tmp" without this);
- unique==0 walks 1..0xFFFF until fopen succeeds (MSDN loop; the old impl
  used time-based hex), unique!=0 uses the low 16 bits single-shot;
- returns the unique number (0 on failure), out buffer capped at MAX_PATH
  (260) — the old code wrote 4095 u16 into the caller's buffer
  (the SW-1477 overrun class).

## SW-1480 ROOT CAUSE + FIX (the next frontier, decoded same round)

After SW-1478 the exehead advanced to NSIS's myGetProcAddress(MGA_*) plane
and died at last_eip=0 (honest rc=0, no dialog). Decoded in three moves:

1. `GetProcAddress(module=0xdea30000, ordinal=680)` -> UNRESOLVED -> 0. The
   PE32 GetProcAddress-gate ordinal branch (SW-1354d) never consulted any
   ordinal->name map, and the PE32 import walker bound every ordinal import
   to a shared "#ordinal" stub.
2. NSIS 3 upstream (Source/exehead/util.c MGA_FUNCS, fetched from kichik/nsis
   master) is the authority: `{"SHELL32", (CHAR*)680}, // IsUserAnAdmin` —
   NOT SHGetFolderPathW (the 679/680=SHGetFolderPathA/W numbering is the
   shfolder-forwarding era). MGA_6 = shell32!680; MGA_8 = SHLWAPI!437 IsOS;
   MGA_10 = SHFOLDER!SHGetFolderPathW (shfolder.dll still missing — NULL is
   a handled fallback for NSIS).
3. Disassembly of the call site (0x405cb4): `push 6; call myGetProcAddress;
   call eax; test eax,eax` — UserIsAdminGrpMember() calls shell32!680 with
   ZERO args (0-arg BOOL). First bind attempt to SHGetFolderPathW (arity 20)
   made the gate pop 20 bytes the caller never pushed -> the caller's own
   `ret` fetched garbage EIP (0x43403a family, SW-1281 fatal rc=86 in one
   intermediate build). Also fixed en route: the SW-1354d fallback must
   overwrite ret UNCONDITIONALLY — ret holds the raw host pointer
   (&mw_winapi_optional_stub) there, and a guest-callable answer is
   mandatory.

FIX (loader.c): mw_IsUserAnAdmin model (0-arg BOOL -> TRUE, MW-TRACE'd; the
SW-1405 installer world runs as the elevated user — HKLM writes already
succeed); ENTRY + arity ("IsUserAnAdmin", 0); wine-spec ordinal rows added to
BOTH the PE32 import walker (ord32: shell32!680 IsUserAnAdmin, shlwapi!437
IsOS) and the PE32 GetProcAddress-gate branch (ordmap32, with the
mapped_thunk flag so the unresolved fallback stays unconditional).

## MEASURED (nppsetup x3, full battery env, deterministic)

- SWEEP-098 state: rc=139 crash=1, steps 140,016,399, FRAMES=1 (error box).
- SWEEP-099: rc=139 crash=1 x3, **steps 192,740,428** (+52,724,029 =
  +37.6%), language-dialog chain FULLY INTERACTIVE: GetTempFileNameW DOS
  loop (nsy/nsm/nsf + 0001-rescan after each delete), IsUserAnAdmin() ->
  TRUE, language dialog created + WM_COMMAND(IDOK) processed (proc 0x404169,
  GetDlgItem(1), SendMessage 0xf3/0x408) — the wizard ADVANCES past the
  entire language/temp/admin preamble.
- New frontier = **SW-1479** (queued since SWEEP-098): post-dialog
  bad-return 0x43403a — ret at 0x404090 (epilogue of the post-dialog
  cleanup fn, 0x404050-0x404090) pops a .data address; the caller-frame
  corruption predates the call (the return address was already garbage when
  the call happened — unbalanced callback-stack exit family, NOT the
  SW-1478 clobber: that is fixed).

## Gates

- Clean build; binary 4,654,248 B sha 798d5005; G1 PIN 12/12
  (evidence/master/g1/20261028_124332).
- corpus_gate sweep099: 17 rows, rc=0 in 12, crash total 0 (shape identical
  to SWEEP-098). mini.flood rebuilt from tracked source (ziglang 0.17.0,
  tests/games/miniwin_flood.c unchanged, 223744 B — byte-count matches the
  SWEEP-093 pin exactly; manifest re-pinned per the documented
  zig-timestamp non-reproducibility, b6b1030c..., gate-verified in-run).
  XP SEED guests honestly MISSING (licensed-media-only, docs/GUEST_ARTIFACTS.md).
- repo_size_guard: GUARD OK (two pre-existing documented WARNs: shexview,
  hxd.portable.setup).
- Battery x3 deterministic rc=139 crash=1 steps=192,740,428 all three runs.

## Probes persisted (outside the repo)

- scripts/provision_gdb.sh + gdb_env.sh (user-space gdb 16.3 rebuild)
- scripts/probe_f_posix_writer.gdb + probe_f_run.sh (PROBE-F writer catch)
- scripts/probe_g_sgf.gdb + probe_g_run.sh (PROBE-G SHGetFolderPathW arg
  capture — hwnd=0x40161b csidl=0x1c2c000 token=0x300b flags=0 path=nil:
  the junk-stack shape that identified the zero-arg predicate)

## Constraint compliance

No manifests other than the mini.flood sha re-pin (documented, gate-verified);
no new Issues/PRs; linear commit on top of 35c820a; no force-push.
