/* MiniWinFlood — SW-1451 acceptance game for MiniWin (source-only corpus).
 *
 * A very simple deterministic Flood-It puzzle: a 14x14 board of 5 colors,
 * flood the region containing the top-left cell by pressing '1'..'5'
 * (or clicking a palette cell). Win when the whole board is one color
 * within 25 moves; ESC quits cleanly.
 *
 * Build (PE x64, reproducible, no toolchain secrets):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o .cache/miniwin/built/miniwin_flood.exe tests/games/miniwin_flood.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run:
 *   MW_GUI_EVENTS=1 \
 *   MW_GUI_KEYS=49,50,51,52,53,49,50,51,52,53,27 \
 *   ./minwin_loader .cache/miniwin/built/miniwin_flood.exe
 *
 * Determinism: the board comes from a fixed-seed LCG (never rand()/time),
 * so the same key script yields byte-identical frame sequences across runs,
 * matching the SWEEP-005 snake/breakout acceptance discipline.
 *
 * Uses only the API surface already proven under MiniWin by miniwin_snake.c:
 * RegisterClassA/CreateWindowExA/ShowWindow/UpdateWindow, GetMessageA loop,
 * WM_TIMER + WM_KEYDOWN, FillRect/Rectangle/TextOutA with CreateSolidBrush,
 * and PostQuitMessage for a clean exit. */
#include <windows.h>

#define GW 14          /* grid width  (cells)                */
#define GH 14          /* grid height (cells)                */
#define CELL 30        /* pixels per cell                    */
#define TOP 24         /* status line height (px)            */
#define NCOLORS 5
#define MAXMOVES 25

static unsigned char g_board[GH][GW];
static int  g_moves = 0;
static int  g_over = 0;          /* 0 playing, 1 won, 2 game over */
static unsigned g_lcg = 20261027u;

static const COLORREF g_pal[NCOLORS] = {
    RGB(0xE0, 0x4F, 0x4F),   /* 1 red    */
    RGB(0x4F, 0xA0, 0xE0),   /* 2 blue   */
    RGB(0x55, 0xC0, 0x55),   /* 3 green  */
    RGB(0xE0, 0xC0, 0x40),   /* 4 yellow */
    RGB(0xA0, 0x60, 0xC0)    /* 5 purple */
};

static unsigned lcg_next(void) {
    g_lcg = g_lcg * 1103515245u + 12345u;
    return (g_lcg >> 16) & 0x7FFF;
}

static void board_init(void) {
    int x, y, c, first = -1;
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++) {
            c = (int)(lcg_next() % NCOLORS);
            g_board[y][x] = (unsigned char)c;
            if (first < 0) first = c;
        }
    /* guarantee a non-uniform start and a top-left region smaller than all */
    if (g_board[GH - 1][GW - 1] != (unsigned char)first)
        g_board[GH - 1][GW - 1] = (unsigned char)((first + 1) % NCOLORS);
    g_moves = 0;
    g_over = 0;
}

static int board_uniform(void) {
    int x, y;
    unsigned char c0 = g_board[0][0];
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++)
            if (g_board[y][x] != c0) return 0;
    return 1;
}

static void flood(int nc) {
    unsigned char old, queue[GH * GW][2];
    int qh = 0, qt = 0, x, y;
    if (g_over || nc < 0 || nc >= NCOLORS) return;
    old = g_board[0][0];
    if ((int)old == nc) return;              /* no-op move, do not burn a turn */
    queue[qt][0] = 0; queue[qt][1] = 0; qt++;
    while (qh < qt) {
        x = queue[qh][0]; y = queue[qh][1]; qh++;
        if (x < 0 || y < 0 || x >= GW || y >= GH) continue;
        if (g_board[y][x] != old) continue;
        g_board[y][x] = (unsigned char)nc;
        if (qt + 4 <= GH * GW) {
            queue[qt][0] = (unsigned char)(x + 1); queue[qt][1] = (unsigned char)y; qt++;
            queue[qt][0] = (unsigned char)(x - 1); queue[qt][1] = (unsigned char)y; qt++;
            queue[qt][0] = (unsigned char)x; queue[qt][1] = (unsigned char)(y + 1); qt++;
            queue[qt][0] = (unsigned char)x; queue[qt][1] = (unsigned char)(y - 1); qt++;
        }
    }
    g_moves++;
    if (board_uniform()) g_over = 1;
    else if (g_moves >= MAXMOVES) g_over = 2;
}

static void draw(HWND hwnd, HDC hdc) {
    RECT rc, cell, pal;
    HBRUSH br, black;
    int x, y, i, won = (g_over == 1);
    char status[128];
    (void)hwnd;
    GetClientRect(hwnd, &rc);
    black = CreateSolidBrush(RGB(0x10, 0x10, 0x10));
    FillRect(hdc, &rc, black);
    DeleteObject(black);
    for (y = 0; y < GH; y++)
        for (x = 0; x < GW; x++) {
            cell.left = x * CELL; cell.top = TOP + y * CELL;
            cell.right = cell.left + CELL; cell.bottom = cell.top + CELL;
            br = CreateSolidBrush(g_pal[g_board[y][x]]);
            FillRect(hdc, &cell, br);
            DeleteObject(br);
        }
    for (i = 0; i < NCOLORS; i++) {
        pal.left = 8 + i * 46; pal.top = TOP + GH * CELL + 10;
        pal.right = pal.left + 38; pal.bottom = pal.top + 26;
        br = CreateSolidBrush(g_pal[i]);
        FillRect(hdc, &pal, br);
        DeleteObject(br);
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
    if (won)
        wsprintf(status, "FLOOD-IT  Moves %d/%d  YOU WIN - press ESC", g_moves, MAXMOVES);
    else if (g_over == 2)
        wsprintf(status, "FLOOD-IT  Moves %d/%d  GAME OVER - press ESC", g_moves, MAXMOVES);
    else
        wsprintf(status, "FLOOD-IT  Moves %d/%d  keys 1-5 flood from top-left", g_moves, MAXMOVES);
    TextOutA(hdc, 6, 4, status, lstrlenA(status));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PAINTSTRUCT ps;
    HDC hdc;
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 100, NULL);
        return 0;
    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wp >= '1' && wp <= '5') { flood((int)(wp - '1')); InvalidateRect(hwnd, NULL, FALSE); }
        else if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        int i, py = TOP + GH * CELL + 10;
        if (my >= py && my <= py + 26)
            for (i = 0; i < NCOLORS; i++)
                if (mx >= 8 + i * 46 && mx <= 46 + i * 46) {
                    flood(i); InvalidateRect(hwnd, NULL, FALSE);
                }
        return 0;
    }
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);
        draw(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    (void)hPrev; (void)cmd;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "MiniWinFlood";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "MiniWinFlood", "MiniWin Flood-It",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           GW * CELL + 16, TOP + GH * CELL + 78,
                           NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
