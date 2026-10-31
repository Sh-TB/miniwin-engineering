# notepad++.exe 8.8.2 x64 — TRAIN-10 precise tracker

Guest: artifacts/app3/notepadpp/notepad++.exe — PE32+ (0x20B), 8,696,832 bytes
Result this train: exit 139 (SIGSEGV RVA 0x15e4c8) — imports 100% resolved
(0 unresolved, was 44); WM_NCCREATE delivered; class menu attached
(HMENU 0x50001); paint surface 1024x768 armed; config bootstrap created
AppData/Notepad++ dirs; 2 MessageBoxes rendered (messagebox_1/2.bmp).

## Per-problem ledger

| # | Problem | Before | Fix (Wine ref) | Verified |
|---|---------|--------|----------------|----------|
| 38 | dwmapi!DwmGetColorizationColor unresolved -> stub S_OK with uninitialized COLORREF -> first SIGSEGV | crash at first DWM probe | typed: fill outputs, return E_FAIL so NPP takes the non-DWM path (Wine dwmapi types) | crash moved past DWM |
| 39 | uxtheme 6 imports (GetThemeTransitionDuration, GetThemeBackgroundContentRect, DrawThemeTextEx, BeginBufferedAnimation/BufferedPaintRenderAnimation/BufferedPaintStopAllAnimations/EndBufferedAnimation) | STUB->0 | typed E_FAIL/no-op (Wine uxtheme) | resolved |
| 40 | shlwapi Path* 7 imports (Compact/GetDriveNumber/IsRelative/RemoveExtension/Append/AddExtension/StripPath) | STUB->0 | real string surgery (Wine shlwapi/path.c) | resolved |
| 41 | ColorAdjustLuma | STUB->0 | real luma shift (Wine shlwapi) | resolved |
| 42 | ImageNtHeader | STUB->0 | returns real PE header (g_image_base + e_lfanew) | resolved |
| 43 | wininet/restart family (InternetCrackUrlW, Register/Unregister/GetApplicationRestartSettings) | STUB->0 | typed FALSE / S_OK / ERROR_NOT_FOUND | resolved |
| 44 | kernel32/user32 tails (lstrcmpiA, CopyFileExW real, QueueUserAPC, GetStringTypeExA, GetTimeFormatEx/GetDateFormatEx, GetProductInfo, ToAscii, MonitorFromRect, EnumThreadWindows, mouse_event) | STUB->0 | real or typed per Wine | resolved |
| 45 | imm32 4 imports | STUB->0 | typed FALSE (no IME) | resolved |
| 45b | ImageList_SetIconSize, _TrackMouseEvent, ExtCreatePen, GdiAlphaBlend (-> StretchBlt SRCCOPY), GetClipRgn | STUB->0 | real/typed | resolved |
| 46 | WM_NCCREATE never delivered | NPP WM_NCCREATE SetWindowLongPtr(GWLP_USERDATA) never ran | mw_gui_send_create sends 0x0081 before WM_CREATE, same CREATESTRUCT (Wine win.c order) | WM_NCCREATE in trace |
| 47 | IsTextUnicode home DLL (ADVAPI32) unregistered | 1 unresolved | advapi32 entry registered | 0 unresolved |

## Frontier (next train)
SIGSEGV RVA 0x15e4c8 during startup: guest C++ exception unwinding resumes
at the catch with CONTEXT RDI=0; first store `mov [rdi],rdi` faults. Same
frontier class as winmergeu (SW-1201, SWEEP-030). Fix belongs to SEH
resume-CONTEXT population — full register restore from the personality
callback's target CONTEXT (ReactOS RtlUnwindEx restore contract), not just
Rax/Rsp/Rip.
