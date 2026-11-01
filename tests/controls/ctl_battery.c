/* ctl_battery — W8 (SW-1591..1595) wine-l0 common-controls battery.
 *
 * FIFTEEN guest programs are built from this one source, one per
 * -DCTL_MODE=N compile (each output .exe is a distinct program):
 *   1  ctl01_tooltip_direct   TTM_ADDTOOLW direct text + relay -> show
 *   2  ctl02_tooltip_callback LPSTR_TEXTCALLBACK + guest TTN_NEEDTEXTW
 *   3  ctl03_tooltip_toolbar  toolbar tool tips via TOOLBAR_TTGetDispInfo
 *   4  ctl04_tooltip_menu     tip rect over the menu band (menu tooltips)
 *   5  ctl05_tooltip_track    TTM_TRACKACTIVATE + TTM_TRACKPOSITION + POP
 *   6  ctl06_scrollbar_h      SBS_HORZ + Set/GetScrollInfo + WM_HSCROLL
 *   7  ctl07_scrollbar_v      SBS_VERT + SetScrollRange/Pos + WM_VSCROLL
 *   8  ctl08_buttons          push/default/checkbox/radio/groupbox checks
 *   9  ctl09_static_frames    SS_*FRAME/SS_*RECT boxes
 *  10  ctl10_static_icon      SS_ICON + LoadIconA stock + WM_SETIMAGE
 *  11  ctl11_button_icon      BS_ICON + WM_SETIMAGE prev round-trip
 *  12  ctl12_listbox          LB_* content model
 *  13  ctl13_combobox         CB_* content model
 *  14  ctl14_edit_scroll      EDIT text round-trip + scroll state
 *  15  ctl15_progress_track   PBM_* + TBM_* + UDM_* models
 *
 * Contract sources (pinned wine-9.0 reference):
 *   dlls/comctl32/tooltips.c L360-470 (TTN_NEEDTEXT), L836/852 (SHOW/POP),
 *     L1486-1545 (RelayEvent); toolbar.c L1995-2060 (per-button tools);
 *   user32 scroll.c (SCROLL_DrawArrows/Interior); winuser.h style values.
 * House style: no stdio.h, wsprintfA log, MessageBoxA render, exit 0 pass.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <string.h>

#ifndef CTL_MODE
#define CTL_MODE 1
#endif

/* ---- tooltip constants (commctrl.h, x64 values) ---- */
#define CT_TTM_ACTIVATE        0x0401L
#define CT_TTM_ADDTOOLA        0x0404L
#define CT_TTM_ADDTOOLW        0x0432L
#define CT_TTM_ADJUSTRECT      0x041FL
#define CT_TTM_GETCURRENTTOOLW 0x043BL
#define CT_TTM_GETTEXTW        0x0438L
#define CT_TTM_GETTOOLCOUNT    0x040DL
#define CT_TTM_HITTESTW        0x0437L
#define CT_TTM_NEWTOOLRECTW    0x0434L
#define CT_TTM_POP             0x041CL
#define CT_TTM_RELAYEVENT      0x0407L
#define CT_TTM_SETDELAYTIME    0x0403L
#define CT_TTM_SETMAXTIPWIDTH  0x0418L
#define CT_TTM_SETTITLEW       0x0421L
#define CT_TTM_TRACKACTIVATE   0x0411L
#define CT_TTM_TRACKPOSITION   0x0412L
#define CT_TTM_UPDATETIPTEXTW  0x0439L
#define CT_TTN_GETDISPINFOW    (-530)
#define CT_TTN_SHOW            (-521)
#define CT_TTN_POP             (-522)
#define CT_WM_NOTIFY           0x004E
#define CT_TTS_ALWAYSTIP       0x01
#define CT_TTF_TRACK           0x0020
#define CT_TTF_ABSOLUTE        0x0080

typedef struct {
    UINT      cbSize;
    UINT      uFlags;
    HWND      hwnd;
    UINT_PTR  uId;
    RECT      rect;
    HINSTANCE hinst;
    LPWSTR    lpszText;
    LPARAM    lParam;
    void*     lpReserved;
} CTL_TOOLINFOW; /* 72 bytes x64 */

typedef struct {
    NMHDR    hdr;
    LPWSTR   lpszText;
    WCHAR    szText[80];
    HINSTANCE hinst;
    UINT     uFlags;
    LPARAM   lParam;
} CTL_NMTTDISPINFOW; /* 136 bytes x64 */

/* ---- scroll constants (winuser.h) ---- */
#define CT_SB_CTL     2
#define CT_SB_LINEUP  0
#define CT_SB_LINEDOWN 1
#define CT_SB_PAGEUP  2
#define CT_SB_PAGEDOWN 3
#define CT_SB_THUMBPOSITION 4
#define CT_SB_TOP     6
#define CT_SB_BOTTOM  7
#define CT_WM_HSCROLL 0x0114
#define CT_WM_VSCROLL 0x0115
#define CT_SIF_RANGE  0x1
#define CT_SIF_PAGE   0x2
#define CT_SIF_POS    0x4
#define CT_SIF_ALL    0x17
#define CT_SBS_VERT   0x1
#define CT_SBS_HORZ   0x0

/* SCROLLINFO: use the real winuser.h struct (identical x64 layout). */
#define CTL_SCROLLINFO SCROLLINFO

/* ---- progress/trackbar/updown constants ---- */
#define CT_PBM_SETRANGE   0x0401
#define CT_PBM_SETPOS     0x0402
#define CT_PBM_DELTAPOS   0x0403
#define CT_PBM_SETSTEP    0x0404
#define CT_PBM_STEPIT     0x0405
#define CT_PBM_SETRANGE32 0x0406
#define CT_PBM_GETRANGE   0x0407
#define CT_PBM_GETPOS     0x0408
#define CT_TBM_SETRANGE   0x040E
#define CT_TBM_SETPOS     0x0405
#define CT_TBM_GETPOS     0x0409
#define CT_TBM_GETRANGEMIN 0x040A
#define CT_TBM_GETRANGEMAX 0x040B
#define CT_UDM_SETRANGE   0x0465
#define CT_UDM_GETRANGE   0x0466
#define CT_UDM_SETRANGE32 0x046F
#define CT_UDM_SETPOS32   0x0471
#define CT_UDM_GETPOS32   0x0472

/* ---- listbox/combobox/button constants ---- */
#define CT_LB_ADDSTRING     0x0180
#define CT_LB_DELETESTRING  0x0182
#define CT_LB_RESETCONTENT  0x0184
#define CT_LB_SETCURSEL     0x0186
#define CT_LB_GETCURSEL     0x0188
#define CT_LB_GETTEXT       0x0189
#define CT_LB_GETTEXTLEN    0x018A
#define CT_LB_GETCOUNT      0x018B
#define CT_CB_ADDSTRING     0x0143
#define CT_CB_GETCOUNT      0x0146
#define CT_CB_GETCURSEL     0x0147
#define CT_CB_GETLBTEXT     0x0148
#define CT_CB_SETCURSEL     0x014E
#define CT_BM_SETCHECK      0x00F1
#define CT_BM_GETCHECK      0x00F2
#define CT_WM_SETIMAGE      0x0172
#define CT_IMAGE_ICON       1
#define CT_BS_GROUPBOX      0x7
#define CT_BS_ICON          0x40
#define CT_SS_ICON          0x3
#define CT_SS_BLACKRECT     0x4
#define CT_SS_GRAYRECT      0x5
#define CT_SS_WHITERECT     0x6
#define CT_SS_BLACKFRAME    0x7
#define CT_SS_GRAYFRAME     0x8
#define CT_SS_WHITEFRAME    0x9
#define CT_SS_ETCHEDFRAME   0x12
#define CT_TB_BUTTONSTRUCTSIZE 0x041E
#define CT_TB_SETBUTTONSIZE 0x041F
#define CT_TB_ADDBUTTONSW   0x0444
#define CT_TB_ADDSTRINGW    0x044D
#define CT_TBSTYLE_TOOLTIPS 0x0100

typedef struct {
    int32_t  iBitmap;
    int32_t  idCommand;
    uint8_t  fsState;
    uint8_t  fsStyle;
    uint8_t  bReserved[6];
    int64_t  dwData;
    int64_t  iString;
} CTL_TBBUTTON;

static int  g_fail = 0, g_pass = 0;
static char g_log[8192];
static int  g_log_n = 0;
static HWND g_main, g_tip;
static int  g_ttn_show_n = 0, g_ttn_pop_n = 0, g_needtext_n = 0;

static void check(int cond, const char* what) {
    if (cond) g_pass++;
    else g_fail++;
    if (g_log_n < (int)sizeof(g_log) - 160)
        g_log_n += wsprintfA(g_log + g_log_n, "%s %s\r\n",
                             cond ? "[PASS]" : "[FAIL]", what);
}

/* generic frame proc; TTN answering for the callback mode */
static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == CT_WM_NOTIFY) {
        NMHDR* nm = (NMHDR*)l;
        if (nm->code == CT_TTN_SHOW) { g_ttn_show_n++; return 0; }
        if (nm->code == CT_TTN_POP)  { g_ttn_pop_n++;  return 0; }
        if (nm->code == CT_TTN_GETDISPINFOW) {
            g_needtext_n++;
            CTL_NMTTDISPINFOW* di = (CTL_NMTTDISPINFOW*)l;
            lstrcpyW(di->szText, L"Hover tip W");
            di->lpszText = di->szText;
            return 0;
        }
    }
    return DefWindowProcA(h, m, w, l);
}

static HWND mk_tip(HWND parent) {
    return CreateWindowExW(0, L"tooltips_class32", NULL,
                           WS_POPUP | CT_TTS_ALWAYSTIP,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, parent, NULL,
                           GetModuleHandleW(NULL), NULL);
}

static void relay(HWND tip, HWND over, int x, int y) {
    MSG m;
    memset(&m, 0, sizeof(m));
    m.hwnd = over; m.message = WM_MOUSEMOVE; m.wParam = 0;
    m.lParam = MAKELPARAM(x, y);
    m.pt.x = x; m.pt.y = y; m.time = 1;
    SendMessageW(tip, CT_TTM_RELAYEVENT, 0, (LPARAM)&m);
}

/* ===================== mode implementations ===================== */

static void mode1(void) { /* tooltip_direct */
    g_tip = mk_tip(g_main);
    check(g_tip != NULL, "tooltips_class32 created");
    CTL_TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_main; ti.uId = 1;
    ti.rect.left = 100; ti.rect.top = 100; ti.rect.right = 220; ti.rect.bottom = 130;
    ti.lpszText = (LPWSTR)L"Save changes";
    LRESULT r = SendMessageW(g_tip, CT_TTM_ADDTOOLW, 0, (LPARAM)&ti);
    check(r == 1, "TTM_ADDTOOLW -> TRUE");
    r = SendMessageW(g_tip, CT_TTM_GETTOOLCOUNT, 0, 0);
    check(r == 1, "TTM_GETTOOLCOUNT == 1");
    relay(g_tip, g_main, 150, 120);          /* inside the tool rect */
    check(g_ttn_show_n == 1, "TTN_SHOW delivered on relay");
    CTL_TOOLINFOW gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi); gi.hwnd = g_main; gi.uId = 1;
    WCHAR buf[80];
    gi.lpszText = buf;
    r = SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(r == 0, "TTM_GETTEXTW returns 0");
    check(buf[0] == 'S' && buf[1] == 'a' && buf[2] == 'v' && buf[3] == 'e',
          "GETTEXT round-trip 'Save...'");
    CTL_TOOLINFOW ci;
    memset(&ci, 0, sizeof(ci));
    ci.cbSize = sizeof(ci);
    r = SendMessageW(g_tip, CT_TTM_GETCURRENTTOOLW, 0, (LPARAM)&ci);
    check(r == 1 && ci.uId == 1, "TTM_GETCURRENTTOOL -> tool uId 1");
    SendMessageW(g_tip, CT_TTM_POP, 0, 0);
    check(g_ttn_pop_n == 1, "TTN_POP delivered on TTM_POP");
    relay(g_tip, g_main, 10, 10);            /* outside every tool: hide */
    relay(g_tip, g_main, 150, 120);          /* re-enter: reshow */
    check(g_ttn_show_n == 2, "reshow after leaving the tool");
}

static void mode2(void) { /* tooltip_callback */
    g_tip = mk_tip(g_main);
    check(g_tip != NULL, "tooltips_class32 created");
    CTL_TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_main; ti.uId = 7;
    ti.rect.left = 0; ti.rect.top = 0; ti.rect.right = 300; ti.rect.bottom = 300;
    ti.lpszText = (LPWSTR)-1;                /* LPSTR_TEXTCALLBACKW */
    LRESULT r = SendMessageW(g_tip, CT_TTM_ADDTOOLW, 0, (LPARAM)&ti);
    check(r == 1, "TTM_ADDTOOLW TEXTCALLBACK -> TRUE");
    relay(g_tip, g_main, 50, 50);
    check(g_needtext_n >= 1, "TTN_GETDISPINFOW delivered to tool hwnd");
    check(g_ttn_show_n == 1, "TTN_SHOW after text resolution");
    CTL_TOOLINFOW gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi); gi.hwnd = g_main; gi.uId = 7;
    WCHAR buf[80];
    gi.lpszText = buf;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf[0] == 'H' && buf[1] == 'o' && buf[2] == 'v',
          "callback text 'Hover tip W' resolved");
    ti.lpszText = (LPWSTR)L"Updated";
    SendMessageW(g_tip, CT_TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    WCHAR buf2[80];
    gi.lpszText = buf2;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf2[0] == 'U' && buf2[1] == 'p', "UPDATETIPTEXT round-trip");
    SendMessageW(g_tip, CT_TTM_SETMAXTIPWIDTH, 240, 0);
    RECT rc;
    rc.left = 0; rc.top = 0; rc.right = 10; rc.bottom = 10;
    SendMessageW(g_tip, CT_TTM_ADJUSTRECT, TRUE, (LPARAM)&rc);
    check(rc.left == -2 && rc.right == 12, "TTM_ADJUSTRECT inflates");
}

static void mode3(void) { /* tooltip_toolbar — TOOLBAR_TTGetDispInfo */
    HWND tb = CreateWindowExW(0, L"ToolbarWindow32", NULL,
                              WS_CHILD | WS_VISIBLE | CT_TBSTYLE_TOOLTIPS,
                              0, 0, 480, 30, g_main, NULL,
                              GetModuleHandleW(NULL), NULL);
    check(tb != NULL, "TOOLBAR created (TBSTYLE_TOOLTIPS)");
    SendMessageW(tb, CT_TB_BUTTONSTRUCTSIZE, sizeof(CTL_TBBUTTON), 0);
    SendMessageW(tb, CT_TB_SETBUTTONSIZE, 0, MAKELPARAM(46, 26));
    int64_t strbase = SendMessageW(tb, CT_TB_ADDSTRINGW, 0, (LPARAM)L"Open\0Save\0");
    CTL_TBBUTTON btns[2];
    memset(btns, 0, sizeof(btns));
    btns[0].idCommand = 0x65; btns[0].iBitmap = 0; btns[0].fsState = 4;
    btns[0].iString = strbase;              /* iString = STRING index */
    btns[1].idCommand = 0x66; btns[1].iBitmap = 1; btns[1].fsState = 4;
    btns[1].iString = strbase + 1;          /* 'Save' is pool entry 1 */
    LRESULT r = SendMessageW(tb, CT_TB_ADDBUTTONSW, 2, (LPARAM)btns);
    check(r == 1, "TB_ADDBUTTONSW x2 -> TRUE");
    /* the guest replicates wine toolbar.c L2049-2059: create the tip, add
     * one tool PER BUTTON {hwnd=toolbar, uId=idCommand, TEXTCALLBACK} */
    g_tip = mk_tip(g_main);
    check(g_tip != NULL, "toolbar tooltip created");
    for (int i = 0; i < 2; i++) {
        CTL_TOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = tb; ti.uId = btns[i].idCommand;
        ti.rect.left = i * 46; ti.rect.top = 0;
        ti.rect.right = (i + 1) * 46; ti.rect.bottom = 26;
        ti.lpszText = (LPWSTR)-1;
        SendMessageW(g_tip, CT_TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(g_tip, CT_TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
    }
    check(SendMessageW(g_tip, CT_TTM_GETTOOLCOUNT, 0, 0) == 2,
          "2 toolbar tools added");
    relay(g_tip, tb, 10, 10);                /* over button 0 */
    /* wine contract: TTN_SHOW for toolbar-owned tools goes to the TOOLBAR's
     * own wndproc (comctl32 internal), never to the app — the engine
     * consuming it IS the parity. Verify visibility via GetCurrentTool. */
    CTL_TOOLINFOW curi;
    memset(&curi, 0, sizeof(curi));
    curi.cbSize = sizeof(curi);
    check(SendMessageW(g_tip, CT_TTM_GETCURRENTTOOLW, 0, (LPARAM)&curi) == 1 &&
          curi.uId == 0x65, "relay over button 0 -> current tool uId 0x65");
    CTL_TOOLINFOW gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi); gi.hwnd = tb; gi.uId = 0x65;
    WCHAR buf[80];
    gi.lpszText = buf;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf[0] == 'O' && buf[1] == 'p' && buf[2] == 'e' && buf[3] == 'n',
          "toolbar TTN text resolved to button label 'Open'");
    relay(g_tip, tb, 60, 10);                /* over button 1 */
    gi.uId = 0x66;
    gi.lpszText = buf;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf[0] == 'S' && buf[1] == 'a', "second tool resolves 'Save'");
    /* TTM_HITTEST over button 0 */
    struct { HWND hwnd; POINT pt; CTL_TOOLINFOW ti; } htt;
    memset(&htt, 0, sizeof(htt));
    htt.hwnd = tb; htt.pt.x = 10; htt.pt.y = 10;
    r = SendMessageW(g_tip, CT_TTM_HITTESTW, 0, (LPARAM)&htt);
    check(r == 1 && htt.ti.uId == 0x65, "TTM_HITTESTW -> tool uId 0x65");
}

static void mode4(void) { /* tooltip_menu — tips over the menu band */
    HMENU mbar = CreateMenu();
    HMENU mfile = CreatePopupMenu();
    AppendMenuW(mfile, MF_STRING, 0x11, L"&New");
    AppendMenuW(mfile, MF_STRING, 0x12, L"&Open");
    AppendMenuW(mbar, MF_POPUP, (UINT_PTR)mfile, L"&File");
    SetMenu(g_main, mbar);
    check(GetMenu(g_main) != NULL, "menu band attached");
    g_tip = mk_tip(g_main);
    check(g_tip != NULL, "menu tooltip created");
    /* the app attaches a tip tool per top-level item rect (WinRAR-style
     * menu help); the rect sits in the menu band area (y 0..20) */
    CTL_TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_main; ti.uId = 100;
    ti.rect.left = 0; ti.rect.top = 0; ti.rect.right = 60; ti.rect.bottom = 20;
    ti.lpszText = (LPWSTR)L"File menu tip";
    LRESULT r = SendMessageW(g_tip, CT_TTM_ADDTOOLW, 0, (LPARAM)&ti);
    check(r == 1, "menu-band TTM_ADDTOOLW -> TRUE");
    SendMessageW(g_tip, CT_TTM_SETTITLEW, 0, (LPARAM)L"Help");
    relay(g_tip, g_main, 30, 10);            /* hover over File */
    check(g_ttn_show_n == 1, "menu tip TTN_SHOW on band hover");
    CTL_TOOLINFOW gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi); gi.hwnd = g_main; gi.uId = 100;
    WCHAR buf[80];
    gi.lpszText = buf;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf[0] == 'F' && buf[1] == 'i' && buf[2] == 'l' && buf[3] == 'e',
          "menu tip text round-trip");
    SendMessageW(g_main, WM_MENUSELECT, MAKEWPARAM(0, MF_POPUP),
                 (LPARAM)mbar);
    check(g_ttn_pop_n == 0, "TTN_POP only on hide paths");
}

static void mode5(void) { /* tooltip_track */
    g_tip = mk_tip(g_main);
    check(g_tip != NULL, "tooltips_class32 created");
    CTL_TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_main; ti.uId = 9;
    ti.uFlags = CT_TTF_TRACK | CT_TTF_ABSOLUTE;
    ti.lpszText = (LPWSTR)L"Tracked tip";
    LRESULT r = SendMessageW(g_tip, CT_TTM_ADDTOOLW, 0, (LPARAM)&ti);
    check(r == 1, "TRACK tool added");
    SendMessageW(g_tip, CT_TTM_SETDELAYTIME, 3 /*TTDT_INITIAL*/, 250);
    SendMessageW(g_tip, CT_TTM_TRACKPOSITION, 0, MAKELPARAM(200, 150));
    r = SendMessageW(g_tip, CT_TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    check(r == 1, "TTM_TRACKACTIVATE -> TRUE");
    check(g_ttn_show_n == 1, "tracked tip TTN_SHOW");
    CTL_TOOLINFOW gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi); gi.hwnd = g_main; gi.uId = 9;
    WCHAR buf[80];
    gi.lpszText = buf;
    SendMessageW(g_tip, CT_TTM_GETTEXTW, 80, (LPARAM)&gi);
    check(buf[0] == 'T' && buf[1] == 'r', "tracked tip text round-trip");
    r = SendMessageW(g_tip, CT_TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    check(r == 1 && g_ttn_pop_n == 1, "deactivate -> TTN_POP");
    /* wine TOOLTIPS_Activate: while bActive=FALSE no new show fires; a
     * second TTM_ACTIVATE(FALSE) with no visible tool sends NO extra POP */
    SendMessageW(g_tip, CT_TTM_ACTIVATE, FALSE, 0);
    check(g_ttn_pop_n == 1, "TTM_ACTIVATE(FALSE) with no tool: no extra POP");
    SendMessageW(g_tip, CT_TTM_ACTIVATE, TRUE, 0);
    r = SendMessageW(g_tip, CT_TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    check(r == 1 && g_ttn_show_n == 2, "re-activate tracks again");
}

static void mode6(void) { /* scrollbar_h */
    HWND hbar = CreateWindowExA(0, "SCROLLBAR", NULL,
                                WS_CHILD | WS_VISIBLE | CT_SBS_HORZ,
                                8, 300, 400, 22, g_main, (HMENU)50,
                                GetModuleHandleA(NULL), NULL);
    check(hbar != NULL, "SBS_HORZ scrollbar created");
    CTL_SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = CT_SIF_RANGE | CT_SIF_POS | CT_SIF_PAGE;
    si.nMin = 0; si.nMax = 100; si.nPos = 30; si.nPage = 10;
    LRESULT r = SetScrollInfo(hbar, CT_SB_CTL, &si, TRUE);
    check(r == 30, "SetScrollInfo -> pos 30");
    CTL_SCROLLINFO go;
    memset(&go, 0, sizeof(go));
    go.cbSize = sizeof(go);
    go.fMask = CT_SIF_ALL;
    r = GetScrollInfo(hbar, CT_SB_CTL, &go);
    check(r != 0, "GetScrollInfo -> TRUE");
    check(go.nMin == 0 && go.nMax == 100, "range round-trip 0..100");
    check(go.nPos == 30, "pos round-trip 30");
    check(go.nPage == 10, "page round-trip 10");
    SendMessageW(hbar, CT_WM_HSCROLL, MAKEWPARAM(CT_SB_LINEDOWN, 0), (LPARAM)hbar);
    r = GetScrollPos(hbar, CT_SB_CTL);
    check(r == 31, "SB_LINEDOWN -> 31");
    SendMessageW(hbar, CT_WM_HSCROLL, MAKEWPARAM(CT_SB_PAGEDOWN, 0), (LPARAM)hbar);
    r = GetScrollPos(hbar, CT_SB_CTL);
    check(r == 41, "SB_PAGEDOWN -> 41");
    SendMessageW(hbar, CT_WM_HSCROLL,
                 MAKEWPARAM(CT_SB_THUMBPOSITION, 70), (LPARAM)hbar);
    r = GetScrollPos(hbar, CT_SB_CTL);
    check(r == 70, "SB_THUMBPOSITION 70 -> 70");
    SendMessageW(hbar, CT_WM_HSCROLL,
                 MAKEWPARAM(CT_SB_THUMBPOSITION, 999), (LPARAM)hbar);
    r = GetScrollPos(hbar, CT_SB_CTL);
    check(r == 100, "thumb clamped to max 100");
    SendMessageW(hbar, CT_WM_HSCROLL, MAKEWPARAM(CT_SB_TOP, 0), (LPARAM)hbar);
    check(GetScrollPos(hbar, CT_SB_CTL) == 0, "SB_TOP -> 0");
}

static void mode7(void) { /* scrollbar_v */
    HWND vbar = CreateWindowExA(0, "SCROLLBAR", NULL,
                                WS_CHILD | WS_VISIBLE | CT_SBS_VERT,
                                460, 40, 22, 300, g_main, (HMENU)51,
                                GetModuleHandleA(NULL), NULL);
    check(vbar != NULL, "SBS_VERT scrollbar created");
    SetScrollRange(vbar, CT_SB_CTL, 0, 50, TRUE);
    SetScrollPos(vbar, CT_SB_CTL, 10, TRUE);
    int mn = -1, mx = -1;
    GetScrollRange(vbar, CT_SB_CTL, &mn, &mx);
    check(mn == 0 && mx == 50, "SetScrollRange/GetScrollRange round-trip");
    check(GetScrollPos(vbar, CT_SB_CTL) == 10, "SetScrollPos round-trip 10");
    SendMessageW(vbar, CT_WM_VSCROLL, MAKEWPARAM(CT_SB_LINEDOWN, 0), (LPARAM)vbar);
    SendMessageW(vbar, CT_WM_VSCROLL, MAKEWPARAM(CT_SB_LINEDOWN, 0), (LPARAM)vbar);
    check(GetScrollPos(vbar, CT_SB_CTL) == 12, "2x SB_LINEDOWN -> 12");
    SendMessageW(vbar, CT_WM_VSCROLL, MAKEWPARAM(CT_SB_BOTTOM, 0), (LPARAM)vbar);
    check(GetScrollPos(vbar, CT_SB_CTL) == 50, "SB_BOTTOM -> 50");
    SendMessageW(vbar, CT_WM_VSCROLL, MAKEWPARAM(CT_SB_LINEUP, 0), (LPARAM)vbar);
    check(GetScrollPos(vbar, CT_SB_CTL) == 49, "SB_LINEUP -> 49");
    /* window-level bar state (SB_VERT=1) is a separate keyed slot */
    CTL_SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = CT_SIF_RANGE | CT_SIF_POS;
    si.nMin = 0; si.nMax = 200; si.nPos = 60;
    SetScrollInfo(g_main, 1 /*SB_VERT*/, &si, TRUE);
    CTL_SCROLLINFO go;
    memset(&go, 0, sizeof(go));
    go.cbSize = sizeof(go);
    go.fMask = CT_SIF_POS | CT_SIF_RANGE;
    GetScrollInfo(g_main, 1, &go);
    check(go.nPos == 60 && go.nMax == 200, "main SB_VERT state keyed per bar");
}

static void mode8(void) { /* buttons */
    HWND push = CreateWindowExA(0, "BUTTON", "OK",
                                WS_CHILD | WS_VISIBLE | 0x1 /*BS_DEFPUSHBUTTON*/,
                                10, 30, 80, 26, g_main, (HMENU)60,
                                GetModuleHandleA(NULL), NULL);
    HWND chk = CreateWindowExA(0, "BUTTON", "Autosave",
                               WS_CHILD | WS_VISIBLE | 0x3 /*BS_AUTOCHECKBOX*/,
                               10, 66, 120, 20, g_main, (HMENU)61,
                               GetModuleHandleA(NULL), NULL);
    HWND rad = CreateWindowExA(0, "BUTTON", "Compact",
                               WS_CHILD | WS_VISIBLE | 0x9 /*BS_AUTORADIOBUTTON*/,
                               10, 92, 120, 20, g_main, (HMENU)62,
                               GetModuleHandleA(NULL), NULL);
    HWND grp = CreateWindowExA(0, "BUTTON", "Settings",
                               WS_CHILD | WS_VISIBLE | WS_GROUP | CT_BS_GROUPBOX,
                               140, 30, 160, 120, g_main, (HMENU)63,
                               GetModuleHandleA(NULL), NULL);
    check(push && chk && rad && grp, "4 button family created");
    SendMessageW(chk, CT_BM_SETCHECK, 1, 0);
    check(SendMessageW(chk, CT_BM_GETCHECK, 0, 0) == 1, "BM_SETCHECK(1)/GETCHECK");
    SendMessageW(chk, CT_BM_SETCHECK, 0, 0);
    check(SendMessageW(chk, CT_BM_GETCHECK, 0, 0) == 0, "BM_SETCHECK(0)/GETCHECK");
    SendMessageW(rad, CT_BM_SETCHECK, 1, 0);
    check(SendMessageW(rad, CT_BM_GETCHECK, 0, 0) == 1, "radio check round-trip");
    SendMessageA(push, WM_SETTEXT, 0, (LPARAM)"Apply");
    char t[32];
    int n = GetWindowTextA(push, t, sizeof(t));
    check(n == 5 && !strcmp(t, "Apply"), "button WM_SETTEXT/GETTEXT");
    EnableWindow(chk, FALSE);
    check(!IsWindowEnabled(chk), "EnableWindow(FALSE) honored");
    EnableWindow(chk, TRUE);
    check(IsWindowEnabled(chk), "EnableWindow(TRUE) honored");
}

static void mode9(void) { /* static frames */
    const struct { uint32_t ss; const char* name; } frames[] = {
        {CT_SS_BLACKRECT, "SS_BLACKRECT"}, {CT_SS_GRAYRECT, "SS_GRAYRECT"},
        {CT_SS_WHITERECT, "SS_WHITERECT"}, {CT_SS_BLACKFRAME, "SS_BLACKFRAME"},
        {CT_SS_GRAYFRAME, "SS_GRAYFRAME"}, {CT_SS_WHITEFRAME, "SS_WHITEFRAME"},
        {CT_SS_ETCHEDFRAME, "SS_ETCHEDFRAME"},
    };
    for (int i = 0; i < 7; i++) {
        HWND st = CreateWindowExA(0, "STATIC", NULL,
                                  WS_CHILD | WS_VISIBLE | frames[i].ss,
                                  10 + i * 26, 30, 22, 60, g_main,
                                  (HMENU)(UINT_PTR)(70 + i),
                                  GetModuleHandleA(NULL), NULL);
        check(st != NULL, frames[i].name);
        if (st) check(IsWindow(st), "IsWindow on the box");
    }
    HWND txt = CreateWindowExA(0, "STATIC", "Label text",
                               WS_CHILD | WS_VISIBLE, 10, 110, 200, 18,
                               g_main, (HMENU)80, GetModuleHandleA(NULL), NULL);
    char t[32];
    GetWindowTextA(txt, t, sizeof(t));
    check(!strcmp(t, "Label text"), "STATIC text round-trip");
}

static void mode10(void) { /* static_icon */
    HICON ic = LoadIconA(NULL, (LPCSTR)32516 /*OIC_INFORMATION*/);
    check(ic != NULL, "LoadIconA(OIC_INFORMATION) -> HICON");
    HWND st = CreateWindowExA(0, "STATIC", NULL,
                              WS_CHILD | WS_VISIBLE | CT_SS_ICON,
                              10, 30, 34, 34, g_main, (HMENU)90,
                              GetModuleHandleA(NULL), ic);
    check(st != NULL, "SS_ICON static created with param icon");
    HICON prev = (HICON)SendMessageW(st, CT_WM_SETIMAGE,
                                     CT_IMAGE_ICON, (LPARAM)ic);
    check(prev == NULL || prev == ic, "WM_SETIMAGE prev (creation param)");
    HICON ic2 = LoadIconA(NULL, (LPCSTR)32515 /*OIC_WARNING*/);
    prev = (HICON)SendMessageW(st, CT_WM_SETIMAGE, CT_IMAGE_ICON, (LPARAM)ic2);
    check(prev == ic, "WM_SETIMAGE returns the PREVIOUS icon");
    HWND st2 = CreateWindowExA(0, "STATIC", NULL,
                               WS_CHILD | WS_VISIBLE | CT_SS_ICON,
                               54, 30, 34, 34, g_main, (HMENU)91,
                               GetModuleHandleA(NULL), ic2);
    check(st2 != NULL, "second SS_ICON static");
}

static void mode11(void) { /* button_icon */
    HICON ic = LoadIconA(NULL, (LPCSTR)32515);
    check(ic != NULL, "LoadIconA stock icon");
    HWND b = CreateWindowExA(0, "BUTTON", "Info",
                             WS_CHILD | WS_VISIBLE | CT_BS_ICON,
                             10, 30, 100, 30, g_main, (HMENU)95,
                             GetModuleHandleA(NULL), NULL);
    check(b != NULL, "BS_ICON button created");
    HICON prev = (HICON)SendMessageW(b, CT_WM_SETIMAGE, CT_IMAGE_ICON, (LPARAM)ic);
    check(prev == NULL, "first WM_SETIMAGE -> prev NULL");
    prev = (HICON)SendMessageW(b, CT_WM_SETIMAGE, CT_IMAGE_ICON, (LPARAM)ic);
    check(prev == ic, "second WM_SETIMAGE -> prev == first icon");
    char t[16];
    GetWindowTextA(b, t, sizeof(t));
    check(!strcmp(t, "Info"), "BS_ICON keeps its text");
}

static void mode12(void) { /* listbox */
    HWND lb = CreateWindowExA(0, "LISTBOX", NULL,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              1 /*LBS_NOTIFY*/,
                              10, 30, 200, 120, g_main, (HMENU)100,
                              GetModuleHandleA(NULL), NULL);
    check(lb != NULL, "LISTBOX with WS_VSCROLL created");
    LRESULT i0 = SendMessageW(lb, CT_LB_ADDSTRING, 0, (LPARAM)L"Alpha");
    LRESULT i1 = SendMessageW(lb, CT_LB_ADDSTRING, 0, (LPARAM)L"Beta");
    LRESULT i2 = SendMessageW(lb, CT_LB_ADDSTRING, 0, (LPARAM)L"Gamma");
    check(i0 == 0 && i1 == 1 && i2 == 2, "LB_ADDSTRING indexes 0,1,2");
    check(SendMessageW(lb, CT_LB_GETCOUNT, 0, 0) == 3, "LB_GETCOUNT == 3");
    WCHAR buf[48];
    SendMessageW(lb, CT_LB_GETTEXT, 1, (LPARAM)buf);
    check(buf[0] == 'B' && buf[1] == 'e' && buf[2] == 't', "LB_GETTEXT(1) 'Beta'");
    check(SendMessageW(lb, CT_LB_GETTEXTLEN, 1, 0) == 4, "LB_GETTEXTLEN == 4");
    SendMessageW(lb, CT_LB_SETCURSEL, 1, 0);
    check(SendMessageW(lb, CT_LB_GETCURSEL, 0, 0) == 1, "LB_SETCURSEL/GETCURSEL");
    LRESULT n = SendMessageW(lb, CT_LB_DELETESTRING, 0, 0);
    check(n == 2, "LB_DELETESTRING -> new count 2");
    SendMessageW(lb, CT_LB_RESETCONTENT, 0, 0);
    check(SendMessageW(lb, CT_LB_GETCOUNT, 0, 0) == 0, "LB_RESETCONTENT -> 0");
}

static void mode13(void) { /* combobox */
    HWND cb = CreateWindowExA(0, "COMBOBOX", NULL,
                              WS_CHILD | WS_VISIBLE | 0x3 /*CBS_DROPDOWNLIST*/,
                              10, 30, 160, 120, g_main, (HMENU)110,
                              GetModuleHandleA(NULL), NULL);
    check(cb != NULL, "COMBOBOX created");
    LRESULT r = SendMessageW(cb, CT_CB_ADDSTRING, 0, (LPARAM)L"First");
    check(r == 0, "CB_ADDSTRING 'First' -> 0");
    SendMessageW(cb, CT_CB_ADDSTRING, 0, (LPARAM)L"Second");
    check(SendMessageW(cb, CT_CB_GETCOUNT, 0, 0) == 2, "CB_GETCOUNT == 2");
    WCHAR buf[48];
    SendMessageW(cb, CT_CB_GETLBTEXT, 1, (LPARAM)buf);
    check(buf[0] == 'S' && buf[1] == 'e', "CB_GETLBTEXT(1) 'Second'");
    SendMessageW(cb, CT_CB_SETCURSEL, 1, 0);
    check(SendMessageW(cb, CT_CB_GETCURSEL, 0, 0) == 1, "CB_SETCURSEL/GETCURSEL");
}

static void mode14(void) { /* edit_scroll */
    HWND ed = CreateWindowExA(0, "EDIT", NULL,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              0x4 /*ES_MULTILINE*/ | 0x2 /*ES_AUTOVSCROLL*/,
                              10, 30, 300, 120, g_main, (HMENU)120,
                              GetModuleHandleA(NULL), NULL);
    check(ed != NULL, "multiline EDIT with WS_VSCROLL created");
    const char* body = "line one\r\nline two\r\nline three";
    SendMessageA(ed, WM_SETTEXT, 0, (LPARAM)body);
    char back[128];
    LRESULT n = SendMessageA(ed, WM_GETTEXT, sizeof(back), (LPARAM)back);
    check((int)n == (int)strlen(body), "WM_GETTEXT length round-trip");
    check(!strcmp(back, body), "WM_GETTEXT content round-trip");
    /* scroll state lives on the EDIT window (bar-keyed) */
    CTL_SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = CT_SIF_RANGE | CT_SIF_POS;
    si.nMin = 0; si.nMax = 30; si.nPos = 5;
    LRESULT r = SetScrollInfo(ed, 1 /*SB_VERT*/, &si, TRUE);
    check(r == 5, "SetScrollInfo on EDIT -> pos 5");
    CTL_SCROLLINFO go;
    memset(&go, 0, sizeof(go));
    go.cbSize = sizeof(go);
    go.fMask = CT_SIF_ALL;
    GetScrollInfo(ed, 1, &go);
    check(go.nMax == 30 && go.nPos == 5, "EDIT scroll state round-trip");
    check(go.nTrackPos == 5, "nTrackPos mirrors nPos");
    SendMessageW(ed, CT_WM_VSCROLL, MAKEWPARAM(CT_SB_PAGEDOWN, 0), (LPARAM)ed);
    check(GetScrollPos(ed, 1) == 8, "WM_VSCROLL PAGEDOWN on EDIT -> 8");
}

static void mode15(void) { /* progress_track */
    HWND pb = CreateWindowExA(0, "msctls_progress32", NULL,
                              WS_CHILD | WS_VISIBLE,
                              10, 30, 240, 20, g_main, (HMENU)130,
                              GetModuleHandleA(NULL), NULL);
    check(pb != NULL, "msctls_progress32 created");
    SendMessageW(pb, CT_PBM_SETRANGE32, 0, 100);
    LRESULT prev = SendMessageW(pb, CT_PBM_SETPOS, 42, 0);
    check(prev == 0, "PBM_SETPOS(42) -> prev 0");
    check(SendMessageW(pb, CT_PBM_GETPOS, 0, 0) == 42, "PBM_GETPOS == 42");
    SendMessageW(pb, CT_PBM_STEPIT, 0, 0);
    check(SendMessageW(pb, CT_PBM_GETPOS, 0, 0) == 52, "PBM_STEPIT (+10) -> 52");
    SendMessageW(pb, CT_PBM_DELTAPOS, (WPARAM)-10, 0);
    check(SendMessageW(pb, CT_PBM_GETPOS, 0, 0) == 42, "PBM_DELTAPOS(-10) -> 42");
    PBRANGE pr;
    LRESULT r = SendMessageW(pb, CT_PBM_GETRANGE, TRUE, (LPARAM)&pr);
    check(r == 100, "PBM_GETRANGE(TRUE) -> max 100");
    r = SendMessageW(pb, CT_PBM_GETRANGE, FALSE, (LPARAM)&pr);
    check(r == 0, "PBM_GETRANGE(FALSE) -> min 0");

    HWND tb = CreateWindowExA(0, "msctls_trackbar32", NULL,
                              WS_CHILD | WS_VISIBLE,
                              10, 60, 240, 30, g_main, (HMENU)131,
                              GetModuleHandleA(NULL), NULL);
    check(tb != NULL, "msctls_trackbar32 created");
    SendMessageW(tb, CT_TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
    check(SendMessageW(tb, CT_TBM_GETRANGEMIN, 0, 0) == 0, "TBM_GETRANGEMIN 0");
    check(SendMessageW(tb, CT_TBM_GETRANGEMAX, 0, 0) == 50, "TBM_GETRANGEMAX 50");
    SendMessageW(tb, CT_TBM_SETPOS, TRUE, 25);
    check(SendMessageW(tb, CT_TBM_GETPOS, 0, 0) == 25, "TBM_SETPOS/GETPOS 25");

    HWND ud = CreateWindowExA(0, "msctls_updown32", NULL,
                              WS_CHILD | WS_VISIBLE,
                              10, 100, 18, 40, g_main, (HMENU)132,
                              GetModuleHandleA(NULL), NULL);
    check(ud != NULL, "msctls_updown32 created");
    SendMessageW(ud, CT_UDM_SETRANGE32, 0, 99);  /* wParam=min, lParam=max (plain) */
    r = SendMessageW(ud, CT_UDM_GETRANGE, 0, 0);
    check((r & 0xFFFF) == 99 && ((r >> 16) & 0xFFFF) == 0,
          "UDM_GETRANGE MAKELONG(max,min)");
    SendMessageW(ud, CT_UDM_SETPOS32, 7, 0);
    check(SendMessageW(ud, CT_UDM_GETPOS32, 0, 0) == 7, "UDM_SETPOS32/GETPOS32 7");
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)b; (void)c; (void)d;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = a;
    wc.lpszClassName = "ctlbattery";
    RegisterClassA(&wc);

    g_main = CreateWindowExA(0, "ctlbattery", "ctl battery",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             40, 40, 520, 400, NULL, NULL, a, NULL);
    check(g_main != NULL, "main frame created");

    switch (CTL_MODE) {
    case 1:  mode1();  break;
    case 2:  mode2();  break;
    case 3:  mode3();  break;
    case 4:  mode4();  break;
    case 5:  mode5();  break;
    case 6:  mode6();  break;
    case 7:  mode7();  break;
    case 8:  mode8();  break;
    case 9:  mode9();  break;
    case 10: mode10(); break;
    case 11: mode11(); break;
    case 12: mode12(); break;
    case 13: mode13(); break;
    case 14: mode14(); break;
    case 15: mode15(); break;
    }

    /* one final repaint so the compositor shows the final state */
    InvalidateRect(g_main, NULL, TRUE);
    UpdateWindow(g_main);

    if (g_log_n < (int)sizeof(g_log) - 80)
        g_log_n += wsprintfA(g_log + g_log_n,
                             "---- ctl%02d: %d passed, %d failed ----\r\n",
                             CTL_MODE, g_pass, g_fail);
    MessageBoxA(g_main, g_log, "ctl battery", MB_OK);
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
