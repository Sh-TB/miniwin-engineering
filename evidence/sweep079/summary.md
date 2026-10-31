# SWEEP-079 — x86 (cpu32) GUI plumbing: 11 generic root causes + 2 new tools landed

## Baseline
- Started from origin/main 40eefc3 (SWEEP-078). Sandbox reset recovered:
  local was 5 commits behind; ff-only merge; stale 2.7MB binary rebuilt to
  byte-exact evidence/sweep078/engine_sha256.txt (70fbb03e...) BEFORE any
  change — the build is byte-reproducible across the reset.
- G1 PIN 12/12 at 20261026_121846 (pre-change), 20261026_132849 and
  20261026_140651 (post-change), final engine sha256 5a8d5a72... 4,398,504B.

## The headline: SW-1354 — winlister x86 decoded end-to-end (5 fixes)
The crash: host RIP=0x400000000 at the FIRST WM_CREATE delivery. Chain:

1. **SW-1354a — WNDCLASS/WNDCLASSEX i386 layouts.** All four RegisterClass*
   read lpfnWndProc with the x64 layout (qword @+8). For winlister's
   RegisterClassA (i386: style@0 proc@4) that read cbClsExtra|cbWndExtra<<32
   = 0|4<<32 = 0x400000000 — stored into the host-callable channel.
   Fixed: per-world offsets (i386 WNDCLASS proc@4 name@36 hbr@28;
   WNDCLASSEX proc@8 name@40 hbr@32) + GetClassInfoEx 4-byte fills.
2. **SW-1354b — guest EIP never in a host fn pointer.** mw_gui_store_wndproc
   for cpu32 stores g_main_class_proc32 and routes the channel through
   mw_wndproc32_shim. SetWindowLongPtrW(GWLP_WNDPROC) x86 path same.
   Plus: bounded api32 gate-call ring (slot/name/esp/ret/arity) printed by
   both crash reports — decoded every later frontier without trace floods.
3. **SW-1354c — cpu32_call built an UPSIDE-DOWN frame.** Sentinel pushed
   BELOW args put arg0 where the return address lives: any stdcall callee
   with args popped its first argument as EIP (winlister wndproc: EIP=0x10001
   = the hwnd). All earlier cpu32 re-entries were 0-arg (atexit class) so the
   defect was invisible. Fixed: args below, sentinel on top.
4. **SW-1354d — ordinal GetProcAddress returned RAW host pointers.** The
   gate's thunk conversion skipped the ordinal case; the UPX stub stored
   mw_winapi_optional_stub into the rebuilt IAT and the guest executed host
   loader text (EIP=0x2008620). Fixed: dedicated unresolved-semantics thunk
   ("ordinal:N@dll" label, honest trace + 0 return + bounded pop).
5. **SW-1354e — every native guest-callback site routed mode-aware.**
   EnumWindows, EnumFontFamilies x4, DialogBox family (mw_dialog_run),
   CreateDialogParam* (mw_dlg_create_common), CallWindowProcA/W,
   SendMessageA/W class-proc + subclass routes, DefSubclassProc,
   UpdateWindow child paint. i386 CREATESTRUCT staging: mw_gui_send_create
   built the x64 struct on the HOST stack — unreadable by cpu32 (shim
   truncates lp to 32 bits); now staged at MW32_CS32_VA (arena +0x4000).

## More x86 root causes in the same wagon
6. **SW-1355 — ONE process identity + i386 NtQSI record.** TEB32 ClientId
   (0x1DB0) vs NtQSI (1234) vs GetCurrentProcessId (1234) disagreed; the
   class-5 record was x64-layout (pid@0x50) while winlister reads
   ImageName.Buffer@0x3C + pid@0x44 (decoded from its own consumer code).
   Fixed: 1234 everywhere; XP i386 record layout under g_cpu32_active.
7. **SW-1355b — guest wild buffers fail the API, never fault the host.**
   WideCharToMultiByte probed dst with process_vm_writev (winlister passed
   dst=0x4!) -> clean ERROR_INVALID_USER_BUFFER 1784.
8. **SW-1357 — i386 STARTUPINFO (68B).** GetStartupInfoA/W memset 104 bytes
   over the guest's 68-byte stack slot — Winobj's saved EBP/RA zeroed ->
   leave;ret -> EIP=0 (a clean-looking exit, no window ever created).
   Fixed: 68-byte x86 layout (cb@0 dwFlags@44 std handles @56/60/64).
9. **SW-1358 — SSE2 additions: 0F 6F/7F (movdqa/movdqu) + 0F 70 (pshufd/
   pshuflw/pshufhw/pshufw).** Winobj's UCRT init uses them for 16-byte
   block moves. Same class as SW-1319.
10. **SW-1358b — explicit arity entries win.** The found/not-found overload
    (not-found returns 0, explicit 32 == MW32_ARITY_DEFAULT) let
    LCMapStringEx (8 args, stdcall) fall through to the CRT cdecl default:
    32-byte desync -> ret into stack data. Added the Ex-family locale
    arities (LCMapStringEx/CompareStringEx 32, GetStringTypeEx 28,
    LCIDToLocaleName 12, GetLocaleInfoEx 20, ResolveLocaleName 12,
    *DefaultLocaleName 8, calendar 24...) + g_arity_found flag.
11. **SW-1358c/1358d — GP binding + guest-heap operator new.** (c) the
    GetProcAddress path bound every late-bound name under "kernel32.dll",
    defeating the CRT cdecl default (shmnview: __set_app_type arity 32 ->
    28-byte over-pop); now binds under the REAL dll from the pseudo-handle.
    (d) x86 operator new/delete allocate/free in the GUEST heap — host-
    malloc blocks are identity-writable by the guest and its CRT bookkeeping
    trashed glibc's main arena (malloc.c sysmalloc assert).

## Downloads (official sources, URL + SHA-256 in dl_sha256.tsv)
- LAND: shmnview.zip (ShellMenuView x64 — the correct asset name found:
  shellmenuview.zip and shmenuview-x64.zip are 404) and FreeCommander XE
  portable (24MB, server throttled ~10KB/s, resumed).
- REFETCH+MATCH: all 20 sgtatham puzzles re-downloaded and byte-verified
  against evidence/sweep074/targets.tsv after sandbox artifact loss.
- Still honest BLOCKED_DOWNLOAD (this window): Explorer++ (404 version
  path), SciTE (conn refused), KeePass (HTML gate), WinSCP (HTML page),
  HxD (conn refused), Resource Hacker (409), Ditto (404 asset),
  FastCopy (404 version path). No mirror substitutes used.

## Verification
- Corpus 7/7 parity EXACT x3 (winmine 82, sol 69, freecell 10, calc 1,
  breakout 10, snake 13; spider 506-643 wall-clock band) — ZERO crashes,
  ZERO fatals, ZERO regressions from all 11 fixes.
- 20/20 puzzles PASS_INPUT_STATE_CHANGE x3 (sweep077 battery v2: scripted
  cursor keys with the SW-1344 extended bit + Enter/Space + 'n' + 12 board
  clicks; verdict = rc==0 x3 + no crash + post-input frame differs).
- G1 PIN 12/12 (evidence/master/g1/20261026_140651).
- New-tool battery: shmnview x86 FRONTIER_SW1358e (was BLOCKED until this
  window; now 907K steps deep with its dialog UI building — SW-1354a..e,
  SW-1358a..d all landed on its path); FreeCommander x86 FRONTIER_SW1359
  (Delphi-class, dies at step 544 in early startup — queued).

## Honest frontiers (next wagons)
- SW-1356: guest-SEH delivery for API-internal faults (winlister's node
  build aborts where real Windows raises SEH the guest swallows).
- SW-1358e: shmnview x86 wild indirect call after 907K steps.
- SW-1359: FreeCommander/Delphi-class early-startup decode.
- WinObj x86: past CRT init (was EIP=0), now dies inside its detour/ locale
  machinery (0x4997xx) — recorded with logs in this directory.
