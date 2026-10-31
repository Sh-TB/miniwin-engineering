# SWEEP-032 — train: winrar / notepad++ / idm (internet-first)

Date: 2026-10-29. Rule honored: internet+GitHub searched FIRST for every
target (rarlab.com, download.tuxfamily.org, api.github.com,
internetdownloadmanager.com) before any bespoke debugging.

RECONCILE NOTE: push found origin/main ahead (SWEEP-104..106 parallel
timeline). Rebased onto upstream as branch commit; upstream already landed
most of this sweep's gate batches (parallel discovery). This commit keeps
only the true gap (14 names incl. OemToCharA A->A contract) + the verdicts
below, re-verified on the upstream base: WinRAR round trip 3/3 ROUNDTRIP OK
(random-payload sha-exact), G1 12/12, NPP frontier MOVED FORWARD — 0
unresolved, CreateWindowExW real HWND 0x10001, new frontier = guest thread
CRT startup SIGSEGV at RVA 0x3b94 (was: exit 9 before any window).

## Targets downloaded (curl, all archived under samples/real/)
| Target | Source | Arch |
|---|---|---|
| WinRAR 4.11 x64 (SFX) | rarlab.com/rar/winrar-x64-411.exe | x64 |
| Notepad++ 5.9.6.2 portable | tuxfamily archive | **i386** |
| Notepad++ 8.9.8.1 portable x64 | GitHub releases v8.9.8.1 | x64 |
| IDM 6.43 build 16 | download.internetdownloadmanager.com | **i386** |

## VERDICTS (honest)
### LADDER-082 WinRAR 4.11 x64 — **VERIFIED (console), RUNS (GUI)**
- Guest `Rar.exe a -m5 out.rar payload` -> exit 0, real archive written.
- Guest `Rar.exe x out.rar` -> correct name extraction, SHA-256 byte-exact
  vs source (evidence/master/ladder/LADDER-082_roundtrip_*), 3/3.
- Cross-verified: host unrar 7.11 lists+extracts guest archive, byte-exact.
- GUI WinRAR.exe: 2 windows + WM_PAINT + pump, exit 0, 0 unresolved (3/3).

### LADDER-087 Notepad++ 8.9.8.1 x64 — **RUNS-PARTIAL (frontier named)**
- 67 -> 0 unresolved imports after SWEEP-032b (60-gate batch).
- Death: exit 9 __fastfail(5) — static-UCRT per-thread state init loop:
  FLS state array re-created N times (Set(1,arr)/Set(1,-1) sentinel dance),
  then _invalid_parameter(state+0x3b8 handler NULL) -> abort.
  RA chain: 0x401792 (abort trampoline) <- 0x401692 (_invalid_parameter
  caller) <- 0x41bba6 (state-array maker). 3/3 deterministic.
- Frontier class: static-UCRT multi-state init needs deeper FLS/state
  semantics (state block initializer at 0x41b91c fails, retry loop).

### LADDER-077 Notepad++ 5.x / IDM 6.43 — **BLOCKED-BY-DESIGN (i386)**
- Both are PE32 (0x10b); loader is PE32+ (0x20b) x64-only. Clean honest
  refusal: "[ERROR] Not a PE32+ file" exit 1, 3/3 (LADDER-IDM evidence).

## ENGINE FIXES LANDED (common-blocker classes)
1. **SWEEP-032 (21 gates, Rar.exe first light)**: GetEnvironmentStrings(A),
   WriteConsoleA, SetHandleCount, GetStringTypeA (CT_CTYPE1 real),
   CreateDirectoryA/RemoveDirectoryA/MoveFileA/SetFileAttributesA (route W),
   GetVolumeInformationA, BackupRead/BackupSeek (typed FALSE),
   Get/SetFileSecurityA + GetSecurityDescriptorLength, CharUpperA,
   CharToOemA/BuffA/BuffW, OemToCharA/BuffA, FreeEnvironmentStringsA reg.
2. **LoadStringW/A REAL** (was hardcoded "resource absent"): RT_STRING walk
   over the existing resource directory — Rar.exe printed its full usage
   text; string resources now real for every program.
3. **BUG FIX OemToCharA/OemToCharBuffA contract**: Windows OemToCharA/BuffA
   take LPSTR dst (A->A), NOT LPWSTR. The W-style dst corrupted every
   converted name to its first char ("abc.bin" -> "a") — proven by Rar/UnRAR
   extraction path (IntToExt). Fixed to memmove identity (cp437->cp1252 ASCII).
4. **SWEEP-032b (60 gates, NPP x64)**: GetDateFormatEx/GetTimeFormatEx
   (real SYSTEMTIME format), GetProductInfo, Register/UnregisterApplication
   Restart (S_OK), GetApplicationRestartSettings (NOT_FOUND), CopyFileExW,
   CompareStringOrdinal (real), GetStringTypeExA, lstrcmpiA, QueueUserAPC
   (typed fail), MonitorFromRect, ToAscii, IsCharAlphaW/AlphaNumericW,
   Add/RemoveClipboardFormatListener, EnumThreadWindows (FALSE), mouse_event,
   ExtCreatePen (pen pool), GdiAlphaBlend (->AlphaBlend), GetClipRgn,
   ImageList_SetIconSize, _TrackMouseEvent alias, ImageNtHeader (REAL),
   Path{Append,Canonicalize,CompactPathEx,GetDriveNumber,IsRelative,
   RemoveExtension,StripPath}W (REAL string surgery), AssocQueryStringW,
   ColorAdjustLuma (real math), SHCreateItemFromParsingName/
   SHOpenFolderAndSelectItems (E_FAIL), DragQueryPoint, Crypt* family
   (typed), WinVerifyTrust (TRUST_E_NOSIGNATURE), IsNetworkAlive/
   IsDestinationReachableW (no-network), InternetCrackUrlW (typed fail),
   Imm* (IME closed), Begin/EndBufferedAnimation + DrawThemeTextEx +
   GetThemeBackgroundContentRect + GetThemeTransitionDuration (E_NOTIMPL
   classic fallback), DwmGetColorizationColor (E_NOTIMPL).
5. **SWEEP-032d (15 tails, WinRAR GUI)**: IsCharUpperW, IsTextUnicode
   (real heuristic), GetPriorityClass, CreateIcon, DragAcceptFiles,
   FindExecutableW, EnumResourceLanguagesW, Find*ChangeNotification,
   Begin/End/UpdateResourceW, OleSetClipboard.
6. **SWEEP-032c per-thread guest TLS+TEB (BUG-062 follow-through)**:
   g_tls_ptr_array now __thread; static-TLS template snapshot; each guest
   thread gets a fresh static-TLS block + TEB copy (own ClientId/stack) +
   ARCH_SET_GS. Root-cause from NPP: shared static TLS let a worker's UCRT
   state pointer overwrite the main thread's.
7. Diagnostics kept env-gated: MINWIN_SETLASTERR_RIP (SetLastError(2) guest
   RIP), FASTFAIL 64-word stack window (MINWIN_READDUMP).

## MEASURED
- Build clean; G1 PIN 12/12 (evidence/master/g1/20261029_060051, 064326);
  zstd/lz4 --version x3 exit0; 7za round trip x3 OK; corpus zero regressions.
- Ladder 3x: LADDER-082 roundtrip 0/0/0 (sha-verified), gui-pump 0/0/0;
  LADDER-087 9/9/9 (frontier); LADDER-IDM 1/1/1 (i386 refusal).
