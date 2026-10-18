/* MiniWinCalc — SWEEP-008 simple-GUI acceptance target for MiniWin.
 *
 * User mandate (2026-10-18): «حداقل یک برنامه گرافیکی ساده ماشین حساب
 * یا برنامه دیگه رو هم تست بکن» — a real Win32 GUI calculator, built as
 * PE x64 and run under the loader's event pump with a scripted key
 * sequence: 1, 2, +, 3, Enter(=), ESC. The display and keypad are drawn
 * with real GDI (FillRect / Rectangle / TextOutA); every WM_PAINT frame
 * is rasterized by the loader into screenshots/frame_NNN.bmp, and the
 * final frame must show "1+2=3" as program output.
 *
 * Build (PE x64):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o artifacts/sweep008/calc.exe tests/games/miniwin_calc.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run (event pump + scripted keys):
 *   MW_GUI_EVENTS=1 MW_GUI_KEYS=49,50,187,51,13,27 \
 *       ./minwin_loader artifacts/sweep008/calc.exe
 *
 * Key routing note: the pump's TranslateMessage maps VK 0x20..0x5A to
 * WM_CHAR (digits and letters); operator VKs (OEM_PLUS/MULTIPLY/DIVIDE)
 * arrive as WM_KEYDOWN, so this program reads ALL input in WM_KEYDOWN —
 * a plain keyboard calculator. Deterministic: integer-only arithmetic,
 * no rand()/time(). */
#include <windows.h>
#include <stdint.h>
#include <string.h>
/* Note: no stdio.h — the zig/mingw static-CRT snprintf builds a fake FILE
 * that map_crt_stream refuses (SW-6xx worklist); wsprintfA is the proven
 * stream-free path (same choice as miniwin_snake). Values fit int. */

#define WW 320
#define WH 440
#define BTN_W 70
#define BTN_H 56
#define GRID_X 10
#define GRID_Y 96

static long long g_acc = 0;      /* accumulated left operand */
static long long g_cur = 0;      /* number being typed */
static int   g_op = 0;           /* pending op: 0 none, '+', '-', '*', '/' */
static int   g_have_op = 0;      /* an operator was entered */
static int   g_error = 0;
static char  g_expr[128];        /* expression trace (deterministic) */
static char  g_result[64];       /* right-aligned display */
static int   g_last_key = 0;     /* highlights the last pressed button */
static char  g_msg[160];

static void fmt_ll(long long v, char* out, size_t cap) {
    if (g_error) { wsprintfA(out, "ERROR"); return; }
    if (v < -999999999LL || v > 999999999LL) { wsprintfA(out, "OVERFLOW"); return; }
    wsprintfA(out, "%d", (int)v);
}
static long long apply_op(long long a, long long b, int op) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': if (b == 0) { g_error = 1; return 0; } return a / b;
    default:  return b;
    }
}
static void press_digit(int d) {
    if (g_error) return;
    if (g_cur > 99999999LL) return;
    g_cur = g_cur * 10 + d;
    g_last_key = '0' + d;
    size_t n = strlen(g_expr);
    if (n + 2 < sizeof(g_expr)) { g_expr[n++] = (char)('0' + d); g_expr[n] = 0; }
}
static void press_op(int op) {
    if (g_error) return;
    if (g_have_op) { /* chain: fold pending op */
        g_acc = apply_op(g_acc, g_cur, g_op);
    } else {
        g_acc = g_cur;
        g_have_op = 1;
    }
    g_cur = 0;
    g_op = op;
    g_last_key = op;
    size_t n = strlen(g_expr);
    if (n + 3 < sizeof(g_expr)) {
        g_expr[n++] = (char)op;
        g_expr[n] = 0;
    }
}
static void press_equals(void) {
    if (g_error) return;
    long long r = g_have_op ? apply_op(g_acc, g_cur, g_op) : g_cur;
    g_last_key = '=';
    size_t n = strlen(g_expr);
    if (n + 3 < sizeof(g_expr)) { g_expr[n++] = '='; g_expr[n] = 0; }
    g_acc = r;
    g_cur = r;
    g_have_op = 0;
    g_op = 0;
}
static void press_clear(void) {
    g_acc = 0; g_cur = 0; g_op = 0; g_have_op = 0;
    g_error = 0; g_expr[0] = 0; g_last_key = 'C';
}

static void fill_box(HDC dc, int x, int y, int w2, int h2, uint32_t rgb) {
    RECT r = { x, y, x + w2, y + h2 };
    HBRUSH b = CreateSolidBrush(rgb);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void draw_button(HDC dc, int col, int row, const char* label, int label_vk) {
    int x = GRID_X + col * (BTN_W + 6);
    int y = GRID_Y + row * (BTN_H + 6);
    fill_box(dc, x, y, BTN_W, BTN_H, RGB(70, 70, 80));            /* border */
    fill_box(dc, x + 2, y + 2, BTN_W - 4, BTN_H - 4,
             g_last_key == label_vk ? RGB(255, 210, 120) : RGB(210, 210, 215));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(20, 20, 30));
    int tw = lstrlenA(label) * 8;
    TextOutA(dc, x + (BTN_W - tw) / 2, y + BTN_H / 2 - 7, label, lstrlenA(label));
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        if (w >= '0' && w <= '9') press_digit((int)(w - '0'));
        else if (w == VK_OEM_PLUS)  press_op('+');
        else if (w == VK_OEM_MINUS) press_op('-');
        else if (w == VK_MULTIPLY)  press_op('*');
        else if (w == VK_DIVIDE)    press_op('/');
        else if (w == VK_RETURN)    press_equals();
        else if (w == 'C' || w == 'c') press_clear();
        else return 0;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        /* backdrop */
        HBRUSH bg = CreateSolidBrush(RGB(240, 240, 245));
        RECT full = { 0, 0, WW, WH };
        FillRect(dc, &full, bg);
        DeleteObject(bg);
        /* display panel */
        fill_box(dc, 10, 10, WW - 20, 76, RGB(120, 125, 140));    /* border */
        fill_box(dc, 12, 12, WW - 24, 72, RGB(24, 26, 34));       /* panel  */
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(150, 200, 255));
        TextOutA(dc, 20, 20, g_expr[0] ? g_expr : "ready", g_expr[0] ? lstrlenA(g_expr) : 5);
        fmt_ll(g_cur, g_result, sizeof(g_result));
        SetTextColor(dc, RGB(245, 245, 245));
        TextOutA(dc, WW - 24 - lstrlenA(g_result) * 8, 52, g_result, lstrlenA(g_result));
        /* keypad */
        draw_button(dc, 0, 0, "7", '7'); draw_button(dc, 1, 0, "8", '8');
        draw_button(dc, 2, 0, "9", '9'); draw_button(dc, 3, 0, "/", VK_DIVIDE);
        draw_button(dc, 0, 1, "4", '4'); draw_button(dc, 1, 1, "5", '5');
        draw_button(dc, 2, 1, "6", '6'); draw_button(dc, 3, 1, "*", VK_MULTIPLY);
        draw_button(dc, 0, 2, "1", '1'); draw_button(dc, 1, 2, "2", '2');
        draw_button(dc, 2, 2, "3", '3'); draw_button(dc, 3, 2, "-", VK_OEM_MINUS);
        draw_button(dc, 0, 3, "0", '0'); draw_button(dc, 1, 3, "C", 'C');
        draw_button(dc, 2, 3, "=", VK_RETURN); draw_button(dc, 3, 3, "+", VK_OEM_PLUS);
        /* status line */
        SetTextColor(dc, RGB(90, 90, 110));
        wsprintfA(g_msg, "MiniWin Calc on MiniWin | keys: digits + - * / = C ESC");
        TextOutA(dc, 10, WH - 24, g_msg, lstrlenA(g_msg));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hp; (void)cl;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "MWCalc";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "MWCalc", "MiniWin Calc (SWEEP-008 on MiniWin)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        40, 40, WW, WH, NULL, NULL, hi, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
