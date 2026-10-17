/* MiniWinSnake — SW-501 acceptance target for MiniWin (https://github.com/Sh-tB/miniwin-engineering)
 *
 * Build (PE x64):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o artifacts/sweep005/snake.exe tests/games/miniwin_snake.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run (SWEEP-005 event pump + scripted keys):
 *   MW_GUI_EVENTS=1 MW_GUI_KEYS=40,39,38,39,40,39,38,39,40,39,27 \
 *       ./minwin_loader artifacts/sweep005/snake.exe
 * Acceptance: scripts/sweep005_snake_accept.sh (3x exit0, 35 frames,
 * all frames byte-identical SHA-256).
 *
 * A real Win32 GDI game: full window lifecycle (RegisterClassA -> CreateWindowExA ->
 * ShowWindow -> UpdateWindow), a WM_TIMER game loop, WM_KEYDOWN input, GDI drawing
 * (FillRect / Rectangle / Ellipse / TextOutA via SelectObject + CreateSolidBrush),
 * and a clean PostQuitMessage exit. Compiled as PE x64 (zig cc -target
 * x86_64-windows-gnu). MiniWin runs it with MW_GUI_EVENTS=1 and scripts the
 * keyboard via MW_GUI_KEYS; every WM_PAINT frame is rasterized by the loader's
 * GDI-lite surface into screenshots/frame_NNN.bmp.
 *
 * Determinism: food placement uses a fixed-seed LCG (no rand()/time), so the
 * same key script yields byte-identical frame sequences across runs. */
#include <windows.h>

#define GW 40          /* grid width  (cells) */
#define GH 30          /* grid height (cells) */
#define CELL 16        /* pixels per cell -> 640x480 client */

typedef struct { int x, y; } MWPoint;

static MWPoint g_snake[200];
static int  g_len = 8;
static int  g_dirx = 1, g_diry = 0;
static int  g_score = 0, g_alive = 1;
static MWPoint g_food;
static unsigned g_lcg = 12983; /* seed 12983: first food lands on (11,8), the
                                * end of the scripted first leg — score>0 is
                                * deterministic under the SWEEP-005 key script */
static char     g_msg[96];

static MWPoint next_food(void) {
    MWPoint p;
    do {
        g_lcg = g_lcg * 1103515245u + 12345u;
        p.x = (int)((g_lcg >> 16) % GW);
        g_lcg = g_lcg * 1103515245u + 12345u;
        p.y = (int)((g_lcg >> 16) % GH);
    } while (p.x < 1 || p.y < 1 || p.x >= GW - 1 || p.y >= GH - 1);
    return p;
}
static int on_snake(int x, int y) {
    for (int i = 0; i < g_len; i++)
        if (g_snake[i].x == x && g_snake[i].y == y) return 1;
    return 0;
}
static void place_food(void) {
    do { g_food = next_food(); } while (on_snake(g_food.x, g_food.y));
}
static void die(HWND h, const char* why) {
    g_alive = 0;
    MessageBoxA(h, why, "Game Over", MB_ICONERROR);
    PostQuitMessage(0);
}
static void step(HWND h) {
    if (!g_alive) return;
    int nx = g_snake[0].x + g_dirx, ny = g_snake[0].y + g_diry;
    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) { die(h, "Snake hit the wall."); return; }
    for (int i = 0; i < g_len - 1; i++)
        if (g_snake[i].x == nx && g_snake[i].y == ny) { die(h, "Snake ate itself."); return; }
    for (int i = g_len - 1; i > 0; i--) g_snake[i] = g_snake[i - 1];
    g_snake[0].x = nx; g_snake[0].y = ny;
    if (nx == g_food.x && ny == g_food.y) {
        g_score += 10;
        if (g_len < 190) { g_snake[g_len] = g_snake[g_len - 1]; g_len++; }
        place_food();
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        step(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_LEFT  && g_dirx != 1) { g_dirx = -1; g_diry = 0; }
        else if (w == VK_RIGHT && g_dirx != -1) { g_dirx = 1; g_diry = 0; }
        else if (w == VK_UP    && g_diry != 1) { g_dirx = 0; g_diry = -1; }
        else if (w == VK_DOWN  && g_diry != -1) { g_dirx = 0; g_diry = 1; }
        else if (w == VK_ESCAPE) { PostQuitMessage(0); }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        /* board background */
        HBRUSH hbg = CreateSolidBrush(RGB(16, 16, 28));
        RECT full = { 0, 0, GW * CELL, GH * CELL };
        FillRect(dc, &full, hbg);
        DeleteObject(hbg);
        /* border walls */
        HBRUSH hwall = CreateSolidBrush(RGB(90, 90, 110));
        RECT wt = { 0, 0, GW * CELL, CELL };
        RECT wb = { 0, (GH - 1) * CELL, GW * CELL, GH * CELL };
        RECT wl = { 0, 0, CELL, GH * CELL };
        RECT wr = { (GW - 1) * CELL, 0, GW * CELL, GH * CELL };
        FillRect(dc, &wt, hwall); FillRect(dc, &wb, hwall);
        FillRect(dc, &wl, hwall); FillRect(dc, &wr, hwall);
        DeleteObject(hwall);
        /* food: red apple */
        HBRUSH hfood = CreateSolidBrush(RGB(225, 60, 60));
        SelectObject(dc, hfood);
        Ellipse(dc, g_food.x * CELL + 3, g_food.y * CELL + 3,
                     g_food.x * CELL + CELL - 3, g_food.y * CELL + CELL - 3);
        DeleteObject(hfood);
        /* snake body + brighter head */
        HBRUSH hbody = CreateSolidBrush(RGB(60, 200, 90));
        SelectObject(dc, hbody);
        for (int i = 1; i < g_len; i++)
            Rectangle(dc, g_snake[i].x * CELL + 1, g_snake[i].y * CELL + 1,
                           g_snake[i].x * CELL + CELL - 1, g_snake[i].y * CELL + CELL - 1);
        DeleteObject(hbody);
        HBRUSH hhead = CreateSolidBrush(RGB(140, 240, 160));
        SelectObject(dc, hhead);
        Rectangle(dc, g_snake[0].x * CELL + 1, g_snake[0].y * CELL + 1,
                       g_snake[0].x * CELL + CELL - 1, g_snake[0].y * CELL + CELL - 1);
        DeleteObject(hhead);
        /* HUD */
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(235, 235, 235));
        wsprintfA(g_msg, "MiniWin Snake  |  Score: %d  |  Len: %d", g_score, g_len);
        TextOutA(dc, 10, 4, g_msg, lstrlenA(g_msg));
        if (!g_alive) {
            SetTextColor(dc, RGB(255, 90, 90));
            TextOutA(dc, GW * CELL / 2 - 60, GH * CELL / 2, "GAME OVER", 9);
        }
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
    wc.lpszClassName = "MWGame";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "MWGame", "MiniWin Snake (SW-501 on MiniWin)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        40, 40, GW * CELL, GH * CELL, NULL, NULL, hi, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    /* deterministic start: 8-segment snake at row 6 heading right;
     * head (index 0) is the RIGHTMOST segment so dir=(1,0) is clear */
    for (int i = 0; i < g_len; i++) { g_snake[i].x = 11 - i; g_snake[i].y = 6; }
    place_food();
    SetTimer(hwnd, 1, 50, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
