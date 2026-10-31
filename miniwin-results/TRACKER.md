# MiniWin ICON-TRAIN Precise Tracker (2026-10-31)

Wagon ledger for the 2026-10-31 session; every item verified against

the Wine 10.0/master source tree cloned to `ref/wine-src` (read-only).

Commits: 4463d8d (w1-7), 90ee42a (w8-14), 3bad8f7 (w15), 6f9788a (w16-17),

f9d9f67/6f597d1 (SWEEP-112b/ICON-TRAIN re-land), 3 wagons 18-20, 21-22, 23 (88ed316).


## 1. Program status

| program | PE type | run state | trace lines | unique APIs | unresolved imports |
|---|---|---|---|---|---|
| WinRAR.exe 7.12 | PE32+ x64 | runs, UI init complete, native spin in tail loop (frontier F1) | 10214 | 87 | 42 |
| notepad++.exe 8.8.2 | PE32+ x64 | runs, WM_NCCREATE+CREATE delivered, deep on_create init frontier (F2) | 22473 | 65 | 47 |
| idman.exe 6.42 b38 | PE32 i386 (magic 0x10b) | typed refusal: `Not a PE32+ file (magic=0x10b)` exit=1 — WOW64 subsystem is a loader-level blocker (F3) | 0 | 0 | n/a |

## 2. Wagon ledger (17 wagons)

| # | fix | Wine reference | detail |
|---|---|---|---|
| 1 | SHGetMalloc IMalloc rebuilt | `ole32/imalloc.c IMallocImpl` | vtable order QI/AddRef/Release/Alloc/Realloc/Free/GetSize/DidAlloc/HeapMinimize (was Realloc/Free swapped); ms_abi methods (self in RCX); QI accepts IID_IUnknown/IID_IMalloc; 1024-bucket live-pointer registry so Free/Realloc tolerate foreign CRT pointers |
| 2 | LoadLibraryExA delegates | `kernelbase/loader.c` | MSVC __delayLoadHelper2 treats NULL as failure -> 0xC06D007E; now forwards to the full known-DLL LoadLibraryA path |
| 3 | CharLowerW single-char mode | `MSDN CharLowerW / user32` | hi word 0 -> low word IS the char, return value is the converted char, not a pointer (RCX=1 SIGSEGV) |
| 4 | LoadStringW/A real RT_STRING | `user32/resource.c LoadStringW` | block id (id>>4)+1, 16 pascal strings per block, buflen==0 stores read-only resource ptr in buffer (Wine parity); verified: id=928 -> 19 chars from block 59 |
| 5 | RT_GROUP_ICON -> RT_ICON pipeline | `user32/cursoricon.c CURSORICON_Load` | CURSORICONDIRENTRY=14B in PE resources (ICONRESDIR+planes+bitcount+dwBytesInRes+wResId — NOT the 10B/16B variants); two-pass best fit (min |w-cx|+|h-cy|, then min |depth-bits|); LR_DEFAULTSIZE->32x32; create_icon_from_bmi frame copy (BITMAPINFOHEADER+palette+XOR+AND, biHeight packs both planes); verified: RT_ICON id=11 32x32 32bpp -> HICON 0x61001 |
| 6 | CreateIcon + icon frame registry | `user32/cursoricon.c` | 64-slot icon registry with real frame data; CreateIcon added to import table; GetIconInfo/CreateIconIndirect/CreateIconFromResourceEx return real frame geometry |
| 7 | DrawIconEx real blit | `user32/cursoricon.c` | AND-mask transparency + XOR color decode for 32/24/16/8/4/1bpp through mw_dc_surface |
| 8 | Window frame (کادر) | `user32/nonclient.c NC_HandleNCPaint` | DefWindowProcW answers WM_NCPAINT/NCACTIVATE/NCCALCSIZE/SETICON; frame (border + caption + title + min/max/close + caption icon) painted into g_surf before every EndPaint dump; verified: [NC] frame painted 800x600 |
| 9 | CW_USEDEFAULT | `user32 win.c` | 0x80000000/negative dims normalized to 800x600 (WinRAR's whole CreateWindow used CW_USEDEFAULT; was clamped to 32x32) |
| 10 | Paint pipeline un-gated | `user32 message pump` | PeekMessageW/GetMessageW deliver InvalidateRect paint pending without MW_GUI_EVENTS (scripted injection stays gated); budget guard in Peek; verified: dispatch WM_PAINT -> wndproc |
| 11 | FreeLibraryAndExitThread | `kernelbase/loader.c` | exits the calling THREAD, not the process (WinRAR splash thread was killing the process mid-init) |
| 12 | Per-class wndproc table | `user32 win.c class lookup` | RegisterClass* records name->lpfnWndProc; CreateWindow resolves the window's OWN class proc (5 WinRAR classes overwrote the single global; main window got RarCmtWindow's proc); verified: 'WinRarWindow'->0x1401a1870 etc. |
| 13 | SHGetFolderLocation honest failure | `shell32/shellpath.c` | typed E_FAIL + *ppidl=NULL; the stub's fake S_OK left ITEMIDLIST uninitialized -> WinRAR SIGSEGV at RVA 0x4f9d (mov rax,[rax+0x10]) |
| 14 | LVM_SETITEMTEXTW/A | `comctl32 listview.c` | WinRAR's pane-fill loop spins on msg=0x1074 until TRUE; both text setters now store into the LV state; mw_wm_name names LVM_*/TVM_*/CB_* + hex fallback |
| 15 | GDI+ flat API v2 (12 fns) | `gdiplus/graphics.c, image.c` | GdipDrawRectangleI/DrawRectangle, GdipDrawEllipseI, GdipFillEllipseI/FillEllipse, GdipFillPolygonI (even-odd scan), GdipBitmapGetPixel/SetPixel (32bpp ARGB scan0), GdipSetPenStartCap/EndCap/LineJoin/Width; DrawLineI upgraded to real Bresenham; Graphics remembers its HDC; WinRAR unresolved 58->42 |
| 16 | WM_CREATE no-gate | `user32 win.c create_window` | WM_CREATE is sent synchronously inside CreateWindowEx unconditionally (was MW_GUI_EVENTS-gated; NPP object init starved -> this=NULL at RVA 0x6fd34) |
| 17 | WM_NCCREATE before WM_CREATE | `user32 win.c create_window` | real order WM_NCCREATE -> WM_NCCALCSIZE -> WM_CREATE; NPP stores its C++ this in GWLP_USERDATA during NCCREATE and fetches it in CREATE; verified: WM_NCCREATE -> 1, SetWindowLongPtrW(0x10001,-21,0x38aa0a0) |

## 3. WinRAR.exe API surface (87 unique APIs called)

| API | calls | API | calls |
|---|---|---|---|
| HeapAlloc | 3856 | HeapFree | 3689 |
| SetLastError | 649 | RegCreateKeyExW | 384 |
| SendMessageW | 136 | GetProcAddress | 108 |
| InitializeCriticalSectionEx | 83 | RegSetValueExW | 81 |
| EnterCriticalSection | 57 | LeaveCriticalSection | 57 |
| RegOpenKeyExW | 52 | GetModuleHandleW | 48 |
| GetProcessAffinityMask | 45 | LoadStringW | 31 |
| LoadResource | 22 |  |  |

### 3a. Icon-chain evidence (before -> after)

| call | before this session | after |
|---|---|---|
| LoadStringW | `-> 0 (resource absent)` every call | `id=928 -> 19 chars (RT_STRING block 59)` — real RT_STRING walk |
| LoadImageW(IMAGE_ICON) | `-> 0x60001` blank-pool handle | `-> HICON 0x61002` real RT_ICON frame (16x16 32bpp) |
| LoadIconW(101) | `-> 0x00050002` legacy handle | `-> 0x61001 (RT_GROUP_ICON)` 32x32 32bpp frame |
| CreateIcon family | absent from import table | registered; frames carry real geometry |
| DrawIconEx | no-op | blits through mw_dc_surface with AND-mask transparency |

## 2b. Wagon ledger, continued (wagons 18-24, this session)

| # | fix | Wine/NPP/WinRAR reference | detail |
|---|---|---|---|
| 18 | PNG-compressed RT_ICON members | Wine 10.0 user32/cursoricon.c create_icon_frame + load_png | biSize==PNG_SIGN(0x474e5089) -> decode via libpng -> synthesize BITMAPINFOHEADER biHeight=2h bottom-up 32bpp -> re-enter DIB path. NPP RT_GROUP_ICON 1530/1531/1533/1534/1535 + group 100 #4 are PNG members; fixed the iconList::addIcon "LoadIcon() returns -1" loop (22 identical boxes -> 0) |
| 19 | NPP config world-state | NPP installer copy step | langs.model.xml + stylers.model.xml (v8.8.2 PowerEditor/src) staged next to the exe + as APPDATA langs.xml/stylers.xml; "Load stylers.xml failed!" gone |
| 20 | GetFileAttributesExW full struct | Wine kernelbase/file.c | filled the whole 36-byte WIN32_FILE_ATTRIBUTE_DATA (3 FILETIMEs + sizeHi/Lo; NPP 8.8.2 Parameters.cpp treats size==0 as corrupt langs.xml despite the 535 KB file) |
| 21 | MW_GUI_EVENTS_PARK | Wine win32u NtUserGetMessage blocks | main-thread idle parked in 20ms slices until paint/timer/posted/PostQuitMessage; NPP's pump no longer unwinds on the first dry iteration (was: clean ExitProcess(0) right after the last WM_PAINT, 0 GetMessage calls post-init). Headless batch contract unchanged (12/12) |
| 22 | menu band is NONCLIENT | DefWindowProc WM_NCPAINT | band + item texts drawn in the NC pass before every EndPaint dump; guests that paint their own client no longer wipe the menu bar |
| 23 | menu model 24x64 + opt bits + shared band renderer | wine user32/menu.c dynamic lists; NPP RT_MENU 1500 | 17 top entries now all load (was 16-cap truncated); MF_HELP/tab-prefixed/triangle items right-justified as a group ('+' 0xa029, 'v' U+25BC, 'x' 0xa02b — NPP's tab-bar gadgets); NPP band = File..Window ?  + v x, WinRAR band unchanged (pixel-verified) |
| 24a | TB_INSERTBUTTON 0x401 accepted | commctrl.h TB_INSERTBUTTONA=WM_USER+1 | WinRAR 7.12's pre-layout pass sends 0x401 x13 to ToolbarWindow32; was dropped in the default hole (tb_nbtn==0 -> blank chrome). 0x444/0x414 TB_ADDBUTTONW already handled |
| 25 | UpdateWindow child paint uses captured class procs | SW-1334/SW-1383 parity | the UpdateWindow dispatch only knew the GWLP_WNDPROC subclass channel; the SW-1398 pump round already honors captured class procs, so the two paint channels now agree (SendMessage routed via class_proc since SW-1383). Calc-style guests whose children carry guest-registered classes get WM_PAINT through both paths |
| 24b | WinRAR toolbar registry seed | any.run first-run traces + SW-1554 | artifacts/app3/winrar/registry_seed.bin: f./a.<id>=1 x20, Lock/View*/LargeButtons/ButtonsText, Band76_0..4=100; root-slot padding so the seeded keys land at slots >= 8 (SW-1419 discards 0..6). Verified: Buttons key opens (handle slot 8), all 40 f./a. reads succeed (was honest-miss storm) |

## 4. Frontier (next wagons, registered not forgotten)

- **F1 WinRAR native spin**: initialization tail loop at guest RVA 0x100cc (strcmp-heavy, no API calls, thread State=R) after the working-directory scan; measured by ptrace sampling (scripts/ptrace_stack.py). Not a loader crash — the process must be timed out.
- **F2 Notepad++ deep init**: RESOLVED this session — NPP now runs to a live message pump (wagons 18-23: PNG icons, config world-state, GetFileAttributesExW, park mode, NC menu band). Remaining NPP gap moved to F5.
- **F3 IDM WOW64**: loader refuses PE32 (magic 0x10b, machine 0x14c) with a typed error and exit=1. Supporting 32-bit guests requires a second loader mode (PE32 image layout + WOW64 thunks), a project-sized change documented here as an explicit blocker, not a silent failure.
- **F4 GDI+ paths**: GdipDrawPath/FillPath/DrawBezierI/AddPath* accept coordinates but do not accumulate/replay path geometry yet; direct shapes (line/rect/ellipse/polygon) render for real.
- **F5 NPP client content**: menus/icons/frame/title live; child WM_PAINT dispatch WORKS (SW-1398 pump round delivered WM_PAINT to Scintilla children 0x30018/0x3001c and the class procs ran) — the remaining white client is the Scintilla CONTENT quality (its GDI text/fill output through the engine's 8x8 font / GDI stubs; GFX-2 territory, issue #12), not a dispatch gap.
- **F6 WinRAR button-add gate (top of queue)**: with the registry seed the guest takes the completed-layout shortcut (Band76_0==100 -> je 0x145903) which runs helpers 0x145324 + 0x1494b0 and returns after SendMessage(hwnd,5=WM_SIZE) — the TB_ADDBUTTONW loop 0x140145948..BA0 (send @0x140145B1E) is a SEPARATE function never reached from the shortcut; inside 0x145324/0x1494b0 a gate ([rcx]==0 / [rcx+0x10]==0 heads) bails silently. The unseeded 13x0x401 fallback is flow-nondeterministic across runs. Next: decode 0x145324/0x1494b0 call trees + the walker 0x146FA0..0x147075, or satisfy the NM_CUSTOMDRAW contract so real comctl-style paint reaches GdipDrawImage.
- **F7 menu tail parse divergence**: the engine's RT_MENU 1500 tail parse yields "\tCtrl+N"/"\tCtrl+W" for the gadget titles where a linear file-offset walk reads U+FF0B/U+2715 — sub-item alignment inside the Help child list; renderer maps by command id so the visual is right, the byte-level divergence is queued.

## 5. G1 regression pin

Every wagon batch was gated by `scripts/g1_accept.sh` (clean build + UPX banner +
no crash + 12/12 regression corpus). Evidence: evidence/master/g1/20261031_115452,
_122626, _122853, _123303. No force-push; all commits in English.
