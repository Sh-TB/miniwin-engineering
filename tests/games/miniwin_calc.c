/* MiniWinCalc — SWEEP-008 GUI acceptance target: a simple Win32 calculator.
 *
 * Build (PE x64):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o artifacts/sweep008/calc.exe tests/games/miniwin_calc.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run (SWEEP-005 event pump + scripted keys):
 *   MW_GUI_EVENTS=1 MW_GUI_KEYS=<VK script> ./minwin_loader calc.exe
 * Acceptance: scripts/sweep008_calc_accept.sh — 3x exit0, identical frames.
 *
 * A REAL Win32 GDI calculator: RegisterClassA -> CreateWindowExA -> message
 * loop, WM_KEYDOWN digits/operators, GDI drawing (FillRect/Rectangle/
 * TextOutA), deterministic result, PostQuitMessage exit. No rand()/time().
 *
 * Layout: 640x400 window; display text on top; a 4x4 keypad of button
 * outlines; the pressed key flashes. Script keys (VK codes):
 *   '1','2','+','3','4','=','5','6','*','7','=' then ESC(27) to quit.
 * Expected final display: 12+34=46, 56*7=392 -> last line "392".
 */
#include <windows.h>

#define W 640
#define H 400

static char  g_display[64] = "0";   /* current entry/result */
static char  g_hist[128]   = "";    /* history line */
static long  g_acc  = 0;
static long  g_cur  = 0;
static int   g_op   = 0;            /* 0 none, '+' '*' */
static int   g_flash = -1;          /* button index being pressed */
static int   g_done = 0;

static void fmt(char* dst, long v) {
    /* minimal deterministic long-to-str (avoid CRT varargs drift) */
    char tmp[32]; int neg = 0, n = 0, k = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 31) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) dst[k++] = '-';
    while (n > 0) dst[k++] = tmp[--n];
    dst[k] = 0;
}
static void set_disp(long v) { fmt(g_display, v); }

static void apply_op(void) {
    if (g_op == '+') g_acc += g_cur;
    else if (g_op == '*') g_acc *= g_cur;
    else g_acc = g_cur;
    g_op = 0;
}

/* key handler; k = ASCII of key */
static void press(int k) {
    if (k >= '0' && k <= '9') {
        g_cur = g_cur * 10 + (k - '0');
        set_disp(g_cur);
        return;
    }
    if (k == '+') {
        apply_op(); g_op = '+'; g_cur = 0;
        lstrcpyA(g_hist, g_display); lstrcatA(g_hist, " +");
        return;
    }
    if (k == '*') {
        apply_op(); g_op = '*'; g_cur = 0;
        lstrcpyA(g_hist, g_display); lstrcatA(g_hist, " *");
        return;
    }
    if (k == '=') {
        apply_op(); set_disp(g_acc); g_cur = g_acc;
        lstrcatA(g_hist, " ="); g_done = 1;
        return;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    PAINTSTRUCT ps;
    HDC hdc;
    RECT r;
    HBRUSH bg, btn, hi;
    int i, gx, gy, cols = 4;
    const char* labels[12] = { "7","8","9","+","4","5","6","*","1","2","3","=" };

    switch (m) {
    case WM_CREATE:
        SetTimer(h, 1, 50, 0); /* repaint cadence — drives the event pump */
        return 0;
    case WM_TIMER:
        InvalidateRect(h, 0, 1);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { PostQuitMessage(0); return 0; }
        if (wp >= '0' && wp <= '9') { g_flash = -1; }
        if (wp == '+') g_flash = 3;
        if (wp == '*') g_flash = 7;
        if (wp == '=') g_flash = 11;
        if (wp >= '0' && wp <= '9') {
            g_flash = 8 + (wp - '1'); if (wp == '0') g_flash = -1;
        }
        press((int)wp);
        InvalidateRect(h, 0, 1);
        return 0;
    case WM_PAINT:
        hdc = BeginPaint(h, &ps);
        bg = CreateSolidBrush(RGB(255, 255, 255));
        btn = CreateSolidBrush(RGB(225, 230, 240));
        hi = CreateSolidBrush(RGB(120, 180, 255));
        r.left = 0; r.top = 0; r.right = W; r.bottom = H;
        FillRect(hdc, &r, bg);
        /* display */
        r.left = 16; r.top = 16; r.right = W - 16; r.bottom = 72;
        FillRect(hdc, &r, btn);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 60));
        TextOutA(hdc, 28, 22, g_hist, lstrlenA(g_hist));
        SetTextColor(hdc, RGB(0, 0, 0));
        TextOutA(hdc, 28, 44, g_display, lstrlenA(g_display));
        /* keypad 4x3 */
        for (i = 0; i < 12; i++) {
            gx = 16 + (i % cols) * 150;
            gy = 96 + (i / cols) * 90;
            r.left = gx; r.top = gy; r.right = gx + 134; r.bottom = gy + 74;
            FillRect(hdc, &r, (i == g_flash) ? hi : btn);
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutA(hdc, gx + 56, gy + 26, labels[i], lstrlenA(labels[i]));
        }
        if (g_done) {
            SetTextColor(hdc, RGB(0, 128, 0));
            TextOutA(hdc, 16, H - 40, "DONE", 4);
        }
        EndPaint(h, &ps);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    WNDCLASSA wc;
    HWND w;
    MSG msg;
    (void)prev; (void)cmd;

    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "MiniWinCalc";
    RegisterClassA(&wc);

    w = CreateWindowExA(0, "MiniWinCalc", "MiniWin Calculator",
                        0x00CF0000 /*WS_OVERLAPPEDWINDOW*/, 40, 40, W, H,
                        0, 0, inst, 0);
    ShowWindow(w, show);
    UpdateWindow(w);

    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
