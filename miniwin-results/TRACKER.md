# MiniWin ICON-TRAIN Precise Tracker (2026-10-31)

Wagon ledger for the 2026-10-31 session; every item verified against

the Wine 10.0/master source tree cloned to `ref/wine-src` (read-only).

Commits: 4463d8d (w1-7), 90ee42a (w8-14), 3bad8f7 (w15), 6f9788a (w16-17).


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

## 4. Frontier (next wagons, registered not forgotten)

- **F1 WinRAR native spin**: initialization tail loop at guest RVA 0x100cc (strcmp-heavy, no API calls, thread State=R) after the working-directory scan; measured by ptrace sampling (scripts/ptrace_stack.py). Not a loader crash — the process must be timed out.
- **F2 Notepad++ deep init**: fault inside guest RVA 0x131ad0 (on_create chain after WM_NCCREATE/CREATE both delivered and this=0x38aa0a0 stored). Next wagon will trace the member-init call tree.
- **F3 IDM WOW64**: loader refuses PE32 (magic 0x10b, machine 0x14c) with a typed error and exit=1. Supporting 32-bit guests requires a second loader mode (PE32 image layout + WOW64 thunks), a project-sized change documented here as an explicit blocker, not a silent failure.
- **F4 GDI+ paths**: GdipDrawPath/FillPath/DrawBezierI/AddPath* accept coordinates but do not accumulate/replay path geometry yet; direct shapes (line/rect/ellipse/polygon) render for real.

## 5. G1 regression pin

Every wagon batch was gated by `scripts/g1_accept.sh` (clean build + UPX banner +
no crash + 12/12 regression corpus). Evidence: evidence/master/g1/20261031_115452,
_122626, _122853, _123303. No force-push; all commits in English.
