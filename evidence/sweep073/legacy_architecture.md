# The smallest reusable compatibility base — research-grounded architecture note

Research sources (upstream code read, not marketing): Wine master
(dlls/user32/resource.c, dlls/win32u/menu.c + input.c, server/queue.c,
dlls/ntdll/loader.c, tools/wrc/genres.c), ReactOS master
(win32ss/user/user32/windows/accel.c + dialog.c,
sdk/lib/crt/startup/crtexe.c, sdk/lib/ucrt/startup/initterm.cpp,
sdk/lib/rtl/image.c, modules/rostests/apitests/*), DOSBox Staging/DOSBox-X
trees (src/dos/*, include/dos_inc.h, CPU cores), FreeDOS kernel/FreeCOM
(license audit), RetDec (VB6 detection), PuTTY 0.83 (shinydialogbox.c,
window.c), 7-Zip license/LZMA SDK (public domain), Notepad++ (GPL).

## What this sweep PROVED about the base (measured, not theorized)
Four small, shared contracts moved or unblocked 25+ distinct guests:
1. CRT init exactness (pointer-width _initterm, onexit registry, initenv,
   winmain-command-line) — unlocked 7zr startup AND the entire 20-puzzle
   family's pre-main path with ~120 lines of code.
2. HMODULE = image base with readable headers — the packer/tamper-check/
   FindResource-on-system-module family (N++ proven; K32/proc tools next).
3. Late-bind thunks with real stdcall arity — any guest that resolves
   APIs after load (7zr, and every delayed-load user).
4. Key state + accelerator framing — the input contract for games and
   hotkey-driven tools.

## Smallest base per family (recommendation, upstream-cited)
A. **DOS**: clean-room MiniDOS module written from Ralf Brown's Interrupt
   List facts (GPL-free), reusing the existing i386 interpreter: 256-byte
   PSP builder (sPSP fields per dosbox include/dos_inc.h as STRUCTURE
   reference only), INT 21h subset {4C/4D,30,25/35,3D/3E/3F/40/42,4E/4F,
   39/3B/41,48/49/4A,2A/2C,09/02}, INT 20h, INT 16h/10h text. Estimated
   2-4k LOC. DOSBox-X/Staging are GPLv2 — NO code reuse; behavior oracle
   only. Do NOT claim Win32 runs under it; MZ-DOS is a separate backend
   behind an MZ magic gate.
B. **Native PE32/PE32+ (in scope, continue)**: the five highest-leverage
   contracts to adopt next (order by measured impact):
   1. Real dialog engine (SW-1313): DLGTEMPLATE/EX parsing,
      CreateDialogIndirectParam, WM_INITDIALOG focus protocol,
      IsDialogMessage routing (wine dlls/user32/dialog.c
      DIALOG_ParseTemplate32/DIALOG_CreateIndirect; reactos dialog.c) —
      unlocks PuTTY config, installers, N++ find/replace.
   2. Menu WM_INITMENU/WM_INITMENUPOPUP sequencing before accelerator
      dispatch (wine win32u/menu.c translate_accelerator).
   3. Two-level key state (async desktop 0x80|0x40 one-shot 0x0001 vs
      queue 0x81) with L/R modifier synthesis (wine server/queue.c
      L1595-1690, win32u/input.c L896-925) — refinement of SW-1318;
      regression oracle: reactos apitests/user32/GetKeyState.c.
   4. Export directory synthesis on pseudo-modules (module enumeration,
      K32 tools) — extends SW-1322.
   5. UCRT stdio fidelity: map guest-constructed FILE blocks (SW-1324
      follow-up: real __acrt_iob_func-compatible layout registry).
C. **VB6**: DETECT (RetDec contract: msvbvm50/60.dll import + EP `push
   imm32` -> 'VB5!' header) and report honestly; `nForms==0` Sub-Main
   utilities are the only plausible execution slice (rtc* subset);
   forms engine = out of scope for a 4-5MB loader.
D. **Per-application extensions**: a small manifest (launch env, optional
   dependencies, test probes) — no core patches per app. Postpone until
   the shared-contract ladder above saturates; nothing in this sweep
   needed an app-specific hack, which validates the approach.
E. **Win16/NE**: no realistic plan in the current engine (no NE loader,
   no 16-bit protected-mode path in the interpreter) — out of scope,
   stated honestly.
F. **Managed (VB.NET/C#)**: .NET runtime path is a different world
   (mscoree is stubbed today); keep out of scope for the compat base.

## Regression contracts worth adapting (license-respecting, spec-only)
- reactos apitests/user32/GetKeyState.c (key-state divergence)
- reactos apitests/win32u NtUserCreateAcceleratorTable.c (table ABI)
- reactos apitests/user32/CreateDialog.c (dialog sequencing, SW-1313)
- reactos apitests/ntdll/LdrFindResource_U.c (resource walking)
- UCRT initterm semantics from sdk/lib/ucrt/startup/initterm.cpp
(All GPL — used as behavioral specifications, no code copied.)
