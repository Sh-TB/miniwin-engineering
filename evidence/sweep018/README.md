# SWEEP-018 — (continue and push) elephant batch (2026-10-19)

User directive: (continue and push) + standing elephant/cheetah mandate (like the elephant and the cheetah) — single-pass batch execution, measured progress per change.

## What changed (one loader.c batch, five root-cause fixes)

1. **SW-802..805 WM_CREATE CREATESTRUCT (the 139 killer)**
   - Symptom: autoruns64/rammap64/cports/diskmon64/produkey all 139x3 at
     guest WndProc WM_CREATE paths dereferencing lParam.
   - Evidence: cports 0x140002119 / produkey 0x140022f24
     `cmp $1,%edx; mov (%r9),%rdx; mov %rcx,0x110(%rdx)`;
     diskmon64 0x14000671c `mov (%r9),%rax; movups (%rax),%xmm0`;
     rammap64 0x140004db7 `mov (%r9),%r8` before SetPropW-family call.
   - Fix: mw_gui_send_create builds a real 0x50-byte CREATESTRUCT with
     lpCreateParams = CreateWindowEx lpParam (scratch fallback), geometry,
     WS_OVERLAPPEDWINDOW|WS_VISIBLE, empty name/class strings; ExW/ExA
     forward the guest's lpParam (g_create_param).

2. **NirSoft GUI chain (SW-803)**: honest GetPixel (framebuffer read,
   CLR_INVALID outside), SetStretchBltMode (prev-mode return),
   StretchBlt (nearest-neighbor SRCCOPY/BLACKNESS/WHITENESS),
   CreateToolbarEx (TOOLBAR child), COMCTL32 ordinal #6 = CreateStatusWindowA
   (semantics proven by SendMessageA(h,0x404=SB_SETPARTS,1,&widths)
   follow-up), STATUSBAR child class, SB_SETPARTS->TRUE,
   LoadImageW resource RT_BITMAP DIB decode (1/4/8/24/32bpp BI_RGB) into
   HBITMAP-space objects consumable by ImageList_Add.
   Table growth: children 64->512, memDC/bitmap 8->32, ImageList 8->16.

3. **SW-813 crash-handler hardening**: SA_ONSTACK + 64KB altstack +
   MW_CRASH_PROBE sigsetjmp guard around every raw read in the dump —
   silent-139 class eliminated (produkey/rammap64 previously lost ALL
   diagnostics on stack-overflow crashes).

## Measured progress (user mandate: (changes without measured progress are worthless))

artifacts/sweep018/sweep/sweep018_sweep.tsv (MW_GUI_EVENTS=0, x3, all runs
deterministic — identical exit/stub/stdout/stderr per program):

| program      | SWEEP-016 baseline | SWEEP-018 now |
|--------------|-------------------|----------------|
| autoruns64   | 139 x3            | **0 x3**       |
| cports       | 139 x3            | **0 x3**       |
| diskmon64    | 139 x3            | **0 x3**       |
| produkey     | 139 x3            | **0 x3**       |
| rammap64     | 139 x3            | **0 x3**       |
| bginfo64     | 0 x3              | 0 x3 (held)    |
| coreinfoex64 | 0 x3              | 0 x3 (held)    |
| procexp64    | 139 x3            | 139 x3 (SW-809, unchanged signature) |
| puttygen     | 139 x3            | 139 x3 (SW-807, RIP=0 unchanged) |
| searchmyfiles| 139 x3            | 139 x3 (SW-814, signature recorded) |
| winmergeu    | 139 x3            | 139 x3 (SW-815, SSE frontier recorded) |
| pageant/procmon64/putty/zoomit64 | 1 x3 (own exit codes) | 1 x3 (held) |

Net: 5 of 9 previously-crashing programs now run to clean exit; zero
regressions.

## Gates after the batch (all green)

- G1 PIN OK sha256 616a9ba10b0d4be409070ececa85a4eb3f3e158182adc93e278d7572d23d35e1,
  regression 12/12 (evidence/master/g1/20261019_144635)
- gui calc acceptance 9/9 (A 12+45=57, C 9*8=72, D 84/12=7)
- snake 3x exit0, 30 frames, byte-identical x3; breakout 3x exit0,
  381 frames, bricks 40->13
- miniwin_calc rebuilt (untracked binary was lost — SW-801h class) and
  re-accepted: exit0 x3, 4 frames/run, all-frames SHA 7e8821ae16d0e242 x3

## Open frontiers recorded (with signatures, for next batch)

- SW-812 pump-on re-init loop (produkey/cports pump-off = exit0):
  10,468 SetMenu, menu handles 0x50001..0x5000f, DestroyMenu unresolved,
  8-slot menu table never freed; menu family queued SW-816..820.
- SW-814 searchmyfiles `cmpw $0x22,(%r8)` wild-pointer string scan.
- SW-815 winmergeu SSE path RCX=1.0 RDX=4.5 (MFC float init).
- SW-807 puttygen RIP=0; SW-809 procexp64 UTF-16-as-code (unchanged).
