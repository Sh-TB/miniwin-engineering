/* t17_rebar_contract — TRAIN-20 (SW-1572) rebar height-model regression PE.
 *
 * Locks the engine's REBAR child model at the wine-9.0 dlls/comctl32/rebar.c
 * contract (include/commctrl.h x64 REBARBANDINFOW):
 *   cyChild lives at 0x4C and is present only when cbSize >= 0x50
 *     (the old engine read 0x38 = RBBIM_SIZE cx -> width poisoned height)
 *   RBBS_BREAK (0x1) starts a new row; row height = max band height
 *   RB_GETBARHEIGHT (WM_USER+27) = sum of row heights
 *   RB_GETROWHEIGHT (WM_USER+14, row) = that row's height
 *   RB_GETRECT (WM_USER+9) band rect: y = row top, h = row height
 *   the rebar RESIZES ITSELF to the bar height (REBAR_SizeToHeight)
 * WinRAR 7.12 measured (W7 oracle + SWEEP-036 settled frame): file-list pane
 * positions itself below the bar; crushed toolbar children were the bug.
 * House style: no stdio.h, wsprintfA log, exit 0 = all pass.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    UINT    cbSize;        /* 0x00 */
    UINT    fMask;         /* 0x04 */
    UINT    fStyle;        /* 0x08 */
    COLORREF clrFore;      /* 0x0C */
    COLORREF clrBack;      /* 0x10 */
    LPWSTR  lpText;        /* 0x18 */
    UINT    cch;           /* 0x20 */
    int     iImage;        /* 0x24 */
    HWND    hwndChild;     /* 0x28 */
    UINT    cxMinChild;    /* 0x30 */
    UINT    cyMinChild;    /* 0x34 */
    UINT    cx;            /* 0x38 */
    HBITMAP hbmBack;       /* 0x40 */
    UINT    wID;           /* 0x48 */
    UINT    cyChild;       /* 0x4C  (present iff cbSize >= 0x50) */
} REBARBANDINFO_X64;

#define RBBIM_STYLE     0x1u
#define RBBIM_CHILD     0x10u
#define RBBIM_CHILDSIZE 0x20u
#define RBBIM_SIZE      0x40u
#define RBBIM_ID        0x100u
#define RBBS_BREAK      0x1u

#define RB_INSERTBANDW  (WM_USER+10)
#define RB_GETBANDCOUNT (WM_USER+12)
#define RB_GETROWHEIGHT (WM_USER+14)
#define RB_GETBARHEIGHT (WM_USER+27)

static int  g_fail = 0, g_pass = 0;
static char g_log[8192];
static int  g_log_n = 0;

static void check(int cond, const char* what) {
    if (cond) g_pass++;
    else g_fail++;
    if (g_log_n < (int)sizeof(g_log) - 160)
        g_log_n += wsprintfA(g_log + g_log_n, "%s %s\r\n",
                             cond ? "[PASS]" : "[FAIL]", what);
}

static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)b; (void)c; (void)d;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = a;
    wc.lpszClassName = "t17frame";
    RegisterClassA(&wc);

    HWND main = CreateWindowExA(0, "t17frame", "t17 rebar contract",
                                WS_OVERLAPPEDWINDOW, 40, 40, 800, 600,
                                NULL, NULL, a, NULL);
    check(main != NULL, "main frame created");
    ShowWindow(main, SW_SHOW);

    HWND rb = CreateWindowExA(0, "ReBarWindow32", NULL,
                              WS_CHILD | WS_VISIBLE | 0x800, /* CCS_NODIVIDER */
                              0, 0, 800, 0, main, (HMENU)20, a, NULL);
    check(rb != NULL, "rebar child created (ReBarWindow32 route)");

    /* band child 0: a combo-like toolbar, created 400x30 */
    HWND tb0 = CreateWindowExA(0, "ToolbarWindow32", NULL,
                               WS_CHILD | WS_VISIBLE, 0, 0, 400, 30,
                               rb, (HMENU)21, a, NULL);
    check(tb0 != NULL, "band child 0 (toolbar 400x30) created");
    /* band child 1: toolbar, created 300x23 */
    HWND tb1 = CreateWindowExA(0, "ToolbarWindow32", NULL,
                               WS_CHILD | WS_VISIBLE, 0, 0, 300, 23,
                               rb, (HMENU)22, a, NULL);
    check(tb1 != NULL, "band child 1 (toolbar 300x23) created");

    /* band 0: cbSize 0x50 (carries cyChild@0x4C = 30), cx = 5000 on purpose:
     * the old bug read cx into cyChild -> bar height poisoned by width. */
    REBARBANDINFO_X64 b0;
    memset(&b0, 0, sizeof(b0));
    b0.cbSize = 0x50;
    b0.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    b0.fStyle = 0;
    b0.hwndChild = tb0;
    b0.cxMinChild = 100; b0.cyMinChild = 0;
    b0.cx = 5000;
    b0.wID = 100;
    b0.cyChild = 30;
    BOOL r0 = (BOOL)SendMessageW(rb, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&b0);
    check(r0, "RB_INSERTBANDW band0 (cbSize 0x50, cyChild 30, cx 5000) TRUE");

    /* band 1: V3 prefix cbSize 0x4C (NO cyChild field), RBBS_BREAK -> row 2 */
    REBARBANDINFO_X64 b1;
    memset(&b1, 0, sizeof(b1));
    b1.cbSize = 0x4C;
    b1.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    b1.fStyle = RBBS_BREAK;
    b1.hwndChild = tb1;
    b1.cxMinChild = 100; b1.cyMinChild = 0;
    b1.cx = 300;
    b1.wID = 101;
    BOOL r1 = (BOOL)SendMessageW(rb, RB_INSERTBANDW, (WPARAM)-1, (LPARAM)&b1);
    check(r1, "RB_INSERTBANDW band1 (cbSize 0x4C V3, RBBS_BREAK) TRUE");

    check((int)SendMessageW(rb, RB_GETBANDCOUNT, 0, 0) == 2,
          "RB_GETBANDCOUNT == 2");

    /* row 0 height: cyChild 30 must WIN over cx 5000 (the SW-1572 offset bug) */
    int row0 = (int)SendMessageW(rb, RB_GETROWHEIGHT, 0, 0);
    check(row0 == 30, "RB_GETROWHEIGHT(0) == 30 (cyChild, not cx=5000)");

    /* row 1: V3 band, no cyChild -> max(cyMinChild=0, child height 23) */
    int row1 = (int)SendMessageW(rb, RB_GETROWHEIGHT, 1, 0);
    check(row1 >= 23 && row1 <= 40,
          "RB_GETROWHEIGHT(1) in [23,40] (child-height fallback)");

    int bar = (int)SendMessageW(rb, RB_GETBARHEIGHT, 0, 0);
    check(bar == row0 + row1, "RB_GETBARHEIGHT == row0+row1");

    RECT rc;
    memset(&rc, 0, sizeof(rc));
    BOOL gr = (BOOL)SendMessageW(rb, 0x409 /*RB_GETRECT*/, 1, (LPARAM)&rc);
    check(gr && rc.top == row0 && (rc.bottom - rc.top) == row1,
          "RB_GETRECT(band1): top=row0, height=row1 (rows stack)");

    /* wine REBAR_SizeToHeight: the rebar window ITSELF carries the height */
    RECT rr;
    GetWindowRect(rb, &rr);
    check((int)(rr.bottom - rr.top) == bar,
          "rebar self-height == RB_GETBARHEIGHT (REBAR_SizeToHeight)");

    /* RB_GETBANDINFO round-trips cyChild at 0x4C for cbSize 0x50 */
    REBARBANDINFO_X64 g0;
    memset(&g0, 0, sizeof(g0));
    g0.cbSize = 0x50;
    g0.fMask = RBBIM_CHILDSIZE;
    BOOL gi = (BOOL)SendMessageW(rb, 0x41C /*RB_GETBANDINFOW*/, 0, (LPARAM)&g0);
    check(gi && g0.cyChild == 30, "RB_GETBANDINFOW round-trip cyChild==30 @0x4C");

    g_log_n += wsprintfA(g_log + g_log_n,
                         "t17 rebar: rows %d+%d bar %d rect t%d h%d self %d\r\n",
                         row0, row1, bar, (int)rc.top,
                         (int)(rc.bottom - rc.top), (int)(rr.bottom - rr.top));
    MessageBoxA(main, g_log, "t17_rebar_contract", MB_OK);
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
