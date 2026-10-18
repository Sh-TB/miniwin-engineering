/* calc.c — MiniWin calculator acceptance guest (user demand 2026-10-19:
 * «حداقل یک برنامه گرافیکی ساده ماشین حساب یا برنامه دیگه رو هم تست بکن»).
 *
 * A REAL Win32 GUI calculator: main window + EDIT display + 16 BUTTON
 * children, WM_COMMAND routing, SendMessage text I/O, GetDlgItem,
 * SetDlgItemInt, GDI painting. Engine exercises covered:
 *   RegisterClassExW / CreateWindowExW(children) / GetDlgItem /
 *   SendMessageW(WM_GETTEXT|WM_SETTEXT) / SetDlgItemInt / BeginPaint /
 *   FillRect / Rectangle / DrawTextW / TextOutW / GetMessage pump /
 *   PostQuitMessage / WM_COMMAND(MAKEWPARAM(id,BN_CLICKED)).
 *
 * Headless E2E: engine env MINWIN_CLICKS="109,110,116,105,106,114" simulates
 * the user pressing  1 2 + 3 4 =  (ids ID_1,ID_2,ID_ADD,ID_4,ID_5,ID_EQ)
 * => 12+34, result printed as "[CALC] 12+34 = 46" on stdout so the
 * acceptance script asserts REAL end-to-end arithmetic through the GUI
 * pipeline. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define IDC_DISPLAY 100
/* button ids: grid row-major */
#define ID_7 101
#define ID_8 102
#define ID_9 103
#define ID_DIV 104
#define ID_4 105
#define ID_5 106
#define ID_6 107
#define ID_MUL 108
#define ID_1 109
#define ID_2 110
#define ID_3 111
#define ID_SUB 112
#define ID_0 113
#define ID_EQ 114
#define ID_CLR 115
#define ID_ADD 116

static HWND g_main, g_display;
static wchar_t g_expr[160];   /* full expression so far (guest-side mirror) */
static wchar_t g_disp[64];    /* current number being typed */
static long   g_acc = 0;      /* accumulator */
static wchar_t g_op = 0;      /* pending operator */
static int    g_fresh = 1;    /* next digit starts a new number */
static char   g_log[6][80];   /* last computed lines (for paint + stdout) */
static int    g_logn = 0;

static void push_log(const char* s) {
    if (g_logn < 6) {
        snprintf(g_log[g_logn], sizeof(g_log[0]), "%s", s);
        g_logn++;
    } else {
        for (int i = 0; i < 5; i++)
            memcpy(g_log[i], g_log[i + 1], sizeof(g_log[0]));
        snprintf(g_log[5], sizeof(g_log[0]), "%s", s);
    }
}

static void disp_set(const wchar_t* s) {
    wchar_t tmp[64];
    int i = 0;
    for (; s[i] && i < 62; i++) tmp[i] = s[i];
    tmp[i] = 0;
    /* round-trip through the ENGINE control registry (WM_GETTEXT/WM_SETTEXT) */
    SendMessageW(g_display, WM_SETTEXT, 0, (LPARAM)tmp);
    wchar_t back[64];
    SendMessageW(g_display, WM_GETTEXT, 64, (LPARAM)back);
    int j;
    for (j = 0; j < 63 && back[j]; j++) g_disp[j] = back[j];
    g_disp[j] = 0;
}

static void disp_append(wchar_t c) {
    wchar_t cur[64];
    int n = GetWindowTextW(g_display, cur, 64);
    if (g_fresh) { n = 0; g_fresh = 0; }
    if (n < 62) cur[n++] = c;
    cur[n] = 0;
    disp_set(cur);
}

static long long apply_op(long long a, long long b, wchar_t op) {
    switch (op) {
    case L'+': return a + b;
    case L'-': return a - b;
    case L'*': return a * b;
    case L'/': return b ? (a / b) : 0;
    default:   return b;
    }
}

static void parse_disp(long long* out) {
    long long arg = 0, sign = 1;
    const wchar_t* d = g_disp;
    if (*d == L'-') { sign = -1; d++; }
    for (; *d >= L'0' && *d <= L'9'; d++) arg = arg * 10 + (*d - L'0');
    *out = arg * sign;
}

static void fmt_ll(wchar_t* buf, long long res) {
    wchar_t tmpd[32]; int tn = 0, rn = 0;
    unsigned long long v = (res < 0) ? (unsigned long long)(-res) : (unsigned long long)res;
    if (res < 0) buf[rn++] = L'-';
    if (!v) tmpd[tn++] = L'0';
    while (v) { tmpd[tn++] = (wchar_t)('0' + (v % 10)); v /= 10; }
    while (tn) buf[rn++] = tmpd[--tn];
    buf[rn] = 0;
}

static void do_compute(void) {
    long long arg = 0;
    parse_disp(&arg);
    long long res = apply_op(g_acc, arg, g_op ? g_op : L'+');
    g_acc = (long)res;

    char line[160];
    wchar_t resw[32];
    fmt_ll(resw, res);
    snprintf(line, sizeof(line), "%ls%lld = %ls", g_expr, arg, resw);
    push_log(line);
    printf("[CALC] %s\n", line);
    fflush(stdout);

    /* expression grows: expr(includes op) + arg + '=' + result */
    wchar_t e[176];
    int n = 0, i = 0;
    for (; g_expr[i] && n < 140; i++) e[n++] = g_expr[i];
    {
        wchar_t argw[32];
        fmt_ll(argw, arg);
        for (i = 0; argw[i] && n < 150; i++) e[n++] = argw[i];
    }
    if (n < 152) e[n++] = L'=';
    for (i = 0; resw[i] && n < 158; i++) e[n++] = resw[i];
    e[n] = 0;
    for (i = 0; i < 159 && e[i]; i++) g_expr[i] = e[i];
    g_expr[i] = 0;

    g_fresh = 1;
    SetDlgItemInt(g_main, IDC_DISPLAY, (unsigned int)(res & 0xFFFFFFFFu), TRUE);
    /* keep g_disp consistent with the registry text the guest just wrote */
    wchar_t back[64];
    SendMessageW(g_display, WM_GETTEXT, 64, (LPARAM)back);
    int j;
    for (j = 0; j < 63 && back[j]; j++) g_disp[j] = back[j];
    g_disp[j] = 0;
}

static void do_op(wchar_t op) {
    long long arg = 0;
    parse_disp(&arg);
    if (g_op && !g_fresh) {
        g_acc = (long)apply_op(g_acc, arg, g_op);
    } else if (!g_op) {
        g_acc = (long)arg;
    }
    g_op = op;
    g_fresh = 1;
    /* append current arg + op to expression text */
    wchar_t num[40];
    fmt_ll(num, arg);
    wchar_t e[176];
    int k = 0, i = 0;
    for (i = 0; g_expr[i] && k < 150; i++) e[k++] = g_expr[i];
    for (i = 0; num[i] && k < 158; i++) e[k++] = num[i];
    if (k < 159) e[k++] = op;
    e[k] = 0;
    for (i = 0; i < 159 && e[i]; i++) g_expr[i] = e[i];
    g_expr[i] = 0;
}

static void do_clear(void) {
    g_expr[0] = 0; g_disp[0] = 0; g_acc = 0; g_op = 0; g_fresh = 1;
    push_log("C (clear)");
    printf("[CALC] C\n");
    fflush(stdout);
    SendMessageW(g_display, WM_SETTEXT, 0, (LPARAM)L"0");
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND: {
        UINT id = LOWORD(wp);
        HWND ctl = (HWND)lp;
        HWND expect = GetDlgItem(h, id);
        if (ctl != expect)
            printf("[CALC] WARN: WM_COMMAND ctl=%p != GetDlgItem=%p (id=%u)\n",
                   (void*)ctl, (void*)expect, id);
        if (ctl && IsWindow(ctl) && !IsWindowEnabled(ctl)) break;
        switch (id) {
        case ID_0: disp_append(L'0'); break;
        case ID_1: disp_append(L'1'); break;
        case ID_2: disp_append(L'2'); break;
        case ID_3: disp_append(L'3'); break;
        case ID_4: disp_append(L'4'); break;
        case ID_5: disp_append(L'5'); break;
        case ID_6: disp_append(L'6'); break;
        case ID_7: disp_append(L'7'); break;
        case ID_8: disp_append(L'8'); break;
        case ID_9: disp_append(L'9'); break;
        case ID_ADD: do_op(L'+'); break;
        case ID_SUB: do_op(L'-'); break;
        case ID_MUL: do_op(L'*'); break;
        case ID_DIV: do_op(L'/'); break;
        case ID_EQ:  do_compute(); break;
        case ID_CLR: do_clear(); break;
        }
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc = { 0, 0, 280, 360 };
        HBRUSH wb = (HBRUSH)GetStockObject(WHITE_BRUSH);
        FillRect(dc, &rc, wb);
        Rectangle(dc, 0, 0, 280, 360);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(16, 16, 160));
        RECT tr = { 12, 6, 268, 24 };
        DrawTextW(dc, L"MiniWin Calculator", -1, &tr, DT_LEFT);
        SetTextColor(dc, RGB(0, 0, 0));
        /* display mirror + full expression + log lines */
        RECT dr = { 12, 30, 268, 48 };
        DrawTextW(dc, g_expr[0] ? g_expr : L"(empty)", -1, &dr, DT_LEFT);
        RECT mr = { 12, 52, 268, 70 };
        DrawTextW(dc, g_disp[0] ? g_disp : L"0", -1, &mr, DT_LEFT);
        SetTextColor(dc, RGB(90, 90, 90));
        int y = 250;
        for (int i = 0; i < g_logn; i++) {
            wchar_t line[80];
            int j = 0;
            for (; g_log[i][j] && j < 78; j++) line[j] = (wchar_t)(unsigned char)g_log[i][j];
            line[j] = 0;
            RECT lr = { 12, y, 268, y + 16 };
            DrawTextW(dc, line, -1, &lr, DT_LEFT);
            y += 16;
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HWND mkbtn(const wchar_t* label, int id, int x, int y, int w, int hbtn) {
    return CreateWindowExW(0, L"BUTTON", label,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, hbtn, g_main, (HMENU)(INT_PTR)id, NULL, NULL);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"MiniCalc";
    if (!RegisterClassExW(&wc)) return 20;

    g_main = CreateWindowExW(0, L"MiniCalc", L"MiniWin Calculator",
        WS_OVERLAPPEDWINDOW, 40, 40, 280, 360,
        NULL, NULL, inst, NULL);
    if (!g_main) return 21;

    g_display = CreateWindowExW(0, L"EDIT", L"0",
        WS_CHILD | WS_VISIBLE | ES_RIGHT,
        8, 8, 248, 32, g_main, (HMENU)(INT_PTR)IDC_DISPLAY, inst, NULL);
    if (!g_display) return 22;

    int y0 = 90, bw = 56, bh = 48, gx = 8;
    mkbtn(L"7", ID_7, gx + 0 * (bw + 4), y0 + 0 * (bh + 4), bw, bh);
    mkbtn(L"8", ID_8, gx + 1 * (bw + 4), y0 + 0 * (bh + 4), bw, bh);
    mkbtn(L"9", ID_9, gx + 2 * (bw + 4), y0 + 0 * (bh + 4), bw, bh);
    mkbtn(L"/", ID_DIV, gx + 3 * (bw + 4), y0 + 0 * (bh + 4), bw, bh);
    mkbtn(L"4", ID_4, gx + 0 * (bw + 4), y0 + 1 * (bh + 4), bw, bh);
    mkbtn(L"5", ID_5, gx + 1 * (bw + 4), y0 + 1 * (bh + 4), bw, bh);
    mkbtn(L"6", ID_6, gx + 2 * (bw + 4), y0 + 1 * (bh + 4), bw, bh);
    mkbtn(L"*", ID_MUL, gx + 3 * (bw + 4), y0 + 1 * (bh + 4), bw, bh);
    mkbtn(L"1", ID_1, gx + 0 * (bw + 4), y0 + 2 * (bh + 4), bw, bh);
    mkbtn(L"2", ID_2, gx + 1 * (bw + 4), y0 + 2 * (bh + 4), bw, bh);
    mkbtn(L"3", ID_3, gx + 2 * (bw + 4), y0 + 2 * (bh + 4), bw, bh);
    mkbtn(L"-", ID_SUB, gx + 3 * (bw + 4), y0 + 2 * (bh + 4), bw, bh);
    mkbtn(L"0", ID_0, gx + 0 * (bw + 4), y0 + 3 * (bh + 4), bw, bh);
    mkbtn(L"=", ID_EQ, gx + 1 * (bw + 4), y0 + 3 * (bh + 4), bw, bh);
    mkbtn(L"C", ID_CLR, gx + 2 * (bw + 4), y0 + 3 * (bh + 4), bw, bh);
    mkbtn(L"+", ID_ADD, gx + 3 * (bw + 4), y0 + 3 * (bh + 4), bw, bh);

    printf("[CALC] main=%p display=%p btn1=%p btnadd=%p btneq=%p\n",
           (void*)g_main, (void*)g_display,
           (void*)GetDlgItem(g_main, ID_1),
           (void*)GetDlgItem(g_main, ID_ADD),
           (void*)GetDlgItem(g_main, ID_EQ));
    fflush(stdout);

    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_COMMAND)
            printf("[CALC] WM_COMMAND id=%lu processed\n",
                   (unsigned long)LOWORD(msg.wParam));
    }
    printf("[CALC] exit loop, final display='%ls' expr='%ls' acc=%ld\n",
           g_disp, g_expr, g_acc);
    fflush(stdout);
    return (int)msg.wParam;
}
