# SWEEP-112 — SW-1563: the IDM GUI payload frontier closed + corpus-wide arity-drift cure

Binary: this round's minwin_loader (G1 PIN 12/12, evidence/master/g1/20261031_094535).
Baseline reconciled: local main reset to origin/main a6acc60 (SWEEP-031..111r2 merge,
140 commits; the recycled sandbox had rolled the workspace back to SWEEP-030 — local
re-derivations of P7-P12 were discarded in favor of the canonical remote evolution).

## Problem train (measured, engine-generic)

1. **SW-1563 root cause — stdcall arity under-pop in the 32-bit plane.**
   IDM5.tmp (the extracted 6,490,992-byte PE32 GUI payload of the IDM 6.42 b2
   installer) died rc=9 with `SetUnhandledExceptionFilter(NULL) -> UEF ->
   TerminateProcess(0xC0000409)` — the SW-1561 shape. The trace tail showed the
   real killer: **94 stdcall APIs resolved with "arity unknown -> under-pop 0"**
   (SetDllDirectoryW, SetSearchPathMode, LockFile, UnlockFile, DuplicateHandle,
   GetProfileIntA, GetKeyState, SetPropW/A, GrayStringA, ...). For stdcall the
   CALLEE pops its args; under-pop 0 leaves the args on the guest stack, and
   after the third such call the CRT epilogue pops a data pointer as a return
   address / misaligns the frame until the /GS cookie check raises 0xC0000409
   (the guest's own UEF chain — same class as SW-1560 AllowSetForegroundWindow).
   Fix: 98 arities added to `g_api32_arity` (Win32 SDK x86 stack bytes).

2. **SW-1563b — 28 A-variant imports resolved to stub thunks.** Only the W names
   were registered, so the PE32 name-only dispatch stubbed CopyFileA,
   CreateDirectoryA, MoveFileA, RemoveDirectoryA, GetVolumeInformationA,
   OpenMutexA, WriteConsoleA, EnumResourceLanguagesA, FindResourceExA,
   ConvertDefaultLocale, GlobalFindAtomA, GlobalGetAtomNameA, RegQueryValueA,
   CloseWindow, CopyAcceleratorTableA, CreateDialogIndirectParamA, DrawStateA,
   GetPropA, RemovePropA, SetPropA, SetClassLongA, GrayStringA, CopyMetaFileA,
   GetFileTitleA, PathFindExtensionA, PathIsUNCA, PathStripToRootA,
   GetClassInfoA. Fix: real A variants per Wine (A = widen-then-W;
   dlls/kernel32/path.c pattern) + import-table entries. Atom family caveat
   documented: the engine atom table stores HOST 4-byte wchar_t (wcscmp), so
   GlobalFindAtomA widens byte-per-unit, not to UTF-16.
   GetFileTitleA/PathFindExtensionA/PathIsUNCA/PathStripToRootA are REAL
   implementations (basename minus extension; last-dot-after-basename; UNC
   prefix check; X:\ root strip).

3. Round 2 stragglers: GetClassInfoA (entry), arities ChangeClipboardChain 8 /
   GetCharWidthA 16 / SHGetFolderPathA 20 / SetClassLongA 12.

## Measured results

- **IDM5.tmp: rc=9 (0xC0000409 UEF) -> rc=0**, cpu32_run rc=0 steps=416,607,
  **0 unresolved, 0 unknown arities** (was 28 / 94). Trace:
  evidence/sweep112/idm5_payload_trace.json. The full installer chain now:
  parent extracts 65 files -> spawns IDM1.tmp -> child second-stage runs
  END-TO-END -> the GUI payload runs its CRT init + window-class phase to a
  clean exit (previously impossible: pre-census death).
- **Corpus x3 (evidence/sweep112/corpus_sweep_p13.tsv): every program exit 0
  x3 except pageant (124 = ALIVE_PUMP by design). putty 139->0, rammap64
  139->0, procexp64 139->0, winmergeu 133->0, zoomit64 1->0 — the same
  arity-drift class had been silently corrupting five more guests' stdcall
  flows; zero unresolved on all 45 rows.**
- idman installer battery rc=0 x3 crash=0 (parent 289M-step interpreted run
  incl. extraction + child spawn; evidence/p13_idm/).
- G1 PIN 12/12. No force-push.
