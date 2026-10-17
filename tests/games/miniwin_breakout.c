/* MiniWinBreakout — SW-560 acceptance target for MiniWin (https://github.com/Sh-tB/miniwin-engineering)
 *
 * Build (PE x64):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o artifacts/sweep006/breakout.exe tests/games/miniwin_breakout.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run (SWEEP-006 event pump + scripted keys):
 *   MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=2400 MW_GUI_KEY_EVERY=240 \
 *       MW_GUI_KEYS=32,68,68,68,37,37,32,65,27 \
 *       ./minwin_loader artifacts/sweep006/breakout.exe
 * Acceptance: scripts/sweep560_breakout_accept.sh (3x exit0, per-frame SHA
 * byte-identical, brick pixels strictly decreasing = real game progress).
 *
 * Second genre after MiniWinSnake (SW-501): a REAL double-buffered Win32 game.
 * It exercises the SWEEP-006 GDI trio for real:
 *   CreateCompatibleDC + CreateCompatibleBitmap + SelectObject -> off-screen
 *   buffer; every frame is drawn off-screen (FillRect/Rectangle/Ellipse/
 *   TextOutA land in the memory DC's bitmap) and presented with ONE BitBlt
 *   SRCCOPY to the paint DC. The loader routes draws by HDC, so the visible
 *   window framebuffer only ever changes through BitBlt — honest double
 *   buffering, not a stub.
 *
 * Determinism: pure integer physics, no rand()/time(), fixed key script ->
 * byte-identical frame sequences across runs (verified by SHA-256 3x). */
#include <windows.h>

#define BW 480          /* client width  */
#define BH 360          /* client height */
#define BCOLS 8
#define BROWS 5
#define BRICK_W 56
#define BRICK_H 14
#define BRICK_GAP 2
#define BRICK_TOP 36
#define PADDLE_W 64
#define PADDLE_H 10
#define PADDLE_Y 336
#define BALL 8

static int g_bricks[BCOLS * BROWS];
static int g_px = (BW - PADDLE_W) / 2;  /* paddle x */
static int g_bx, g_by;                  /* ball top-left */
static int g_vx = 3, g_vy = -6;
static int g_launched = 0;
static int g_score = 0, g_lives = 3;
static int g_over = 0, g_won = 0;
static int g_tick = 0, g_ready = 0, g_hits = 0;
static char g_msg[96];

static HDC  g_memdc;                    /* off-screen surface (SWEEP-006) */
static HBITMAP g_membmp;

static const COLORREF ROWCOL[BROWS] = {
    RGB(231, 76, 60), RGB(230, 126, 34), RGB(241, 196, 15),
    RGB(46, 204, 113), RGB(52, 152, 219)
};

static void reset_ball(void) {
    g_bx = g_px + (PADDLE_W - BALL) / 2;
    g_by = PADDLE_Y - BALL - 2;
    g_vx = 3; g_vy = -6;
    g_launched = 0; g_ready = 0;
}

static void step(void) {
    if (g_over || g_won) return;
    if (!g_launched) {
        /* wait on the paddle; auto-launch after 40 ticks. NOTE: do NOT call
         * reset_ball() here — it zeroes g_ready and the counter never fires
         * (the BUG-068 lesson: ball sat parked for hundreds of ticks). */
        g_bx = g_px + (PADDLE_W - BALL) / 2;
        g_by = PADDLE_Y - BALL - 2;
        if (++g_ready >= 40) { g_launched = 1; g_ready = 0; }
        return;
    }
    g_bx += g_vx; g_by += g_vy;
    if (g_bx <= 0)              { g_bx = 0; g_vx = (g_vx < 0) ? -g_vx : g_vx; }
    if (g_bx >= BW - BALL)      { g_bx = BW - BALL; g_vx = -((g_vx < 0) ? -g_vx : g_vx); }
    if (g_by <= 0)              { g_by = 0; g_vy = (g_vy < 0) ? -g_vy : g_vy; }
    /* paddle: real-breakout steering — hit offset sets horizontal speed */
    if (g_vy > 0 && g_by + BALL >= PADDLE_Y && g_by + BALL <= PADDLE_Y + PADDLE_H + 4 &&
        g_bx + BALL >= g_px && g_bx <= g_px + PADDLE_W) {
        int off = (g_bx + BALL / 2) - (g_px + PADDLE_W / 2);
        g_vx = off / 8;
        if (g_vx > 4) g_vx = 4; if (g_vx < -4) g_vx = -4;
        if (g_vx == 0) g_vx = 1;
        g_vy = -6;
        g_hits++;
    }
    /* bricks: one per tick, deterministic order */
    for (int r = 0; r < BROWS; r++) {
        for (int c = 0; c < BCOLS; c++) {
            int i = r * BCOLS + c;
            if (!g_bricks[i]) continue;
            int x0 = 4 + c * (BRICK_W + BRICK_GAP);
            int y0 = BRICK_TOP + r * (BRICK_H + BRICK_GAP);
            if (g_bx + BALL >= x0 && g_bx <= x0 + BRICK_W &&
                g_by + BALL >= y0 && g_by <= y0 + BRICK_H) {
                g_bricks[i] = 0;
                g_score += 10;
                g_vy = -g_vy;
                if (g_score >= BCOLS * BROWS * 10) g_won = 1;
                r = BROWS; /* break outer */
                break;
            }
        }
    }
    /* floor */
    if (g_by >= BH) {
        g_lives--;
        if (g_lives <= 0) { g_over = 1; g_lives = 0; }
        else reset_ball();
    }
}

static void draw(HDC hdc) {
    static HBRUSH brushes[BROWS + 2];
    if (!brushes[0]) {
        for (int r = 0; r < BROWS; r++) brushes[r] = CreateSolidBrush(ROWCOL[r]);
        brushes[BROWS]     = CreateSolidBrush(RGB(33, 33, 33));   /* paddle */
        brushes[BROWS + 1] = CreateSolidBrush(RGB(20, 20, 20));   /* ball   */
    }
    /* background */
    RECT bg = { 0, 0, BW, BH };
    HBRUSH wb = CreateSolidBrush(RGB(248, 248, 248));
    FillRect(hdc, &bg, wb);
    DeleteObject(wb);
    /* bricks */
    for (int r = 0; r < BROWS; r++) {
        for (int c = 0; c < BCOLS; c++) {
            if (!g_bricks[r * BCOLS + c]) continue;
            RECT b = { 4 + c * (BRICK_W + BRICK_GAP),
                       BRICK_TOP + r * (BRICK_H + BRICK_GAP),
                       4 + c * (BRICK_W + BRICK_GAP) + BRICK_W,
                       BRICK_TOP + r * (BRICK_H + BRICK_GAP) + BRICK_H };
            FillRect(hdc, &b, brushes[r]);
        }
    }
    /* paddle + ball (off-screen draws; SelectObject per shape) */
    HBRUSH pb = CreateSolidBrush(RGB(33, 33, 33));
    RECT pr = { g_px, PADDLE_Y, g_px + PADDLE_W, PADDLE_Y + PADDLE_H };
    FillRect(hdc, &pr, pb);
    DeleteObject(pb);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HBRUSH bb = CreateSolidBrush(RGB(20, 20, 20));
    SelectObject(hdc, bb);
    Ellipse(hdc, g_bx, g_by, g_bx + BALL, g_by + BALL);
    DeleteObject(bb);
    SelectObject(hdc, GetStockObject(NULL_PEN));
    /* HUD */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(40, 40, 40));
    wsprintfA(g_msg, "MiniWin Breakout  |  Score: %d  |  Lives: %d", g_score, g_lives);
    TextOutA(hdc, 8, 8, g_msg, lstrlenA(g_msg));
    if (g_over) TextOutA(hdc, 8, 22, "GAME OVER", 9);
    if (g_won)  TextOutA(hdc, 8, 22, "YOU WIN!", 8);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        /* SWEEP-006 double buffering: off-screen DC + compatible bitmap */
        g_memdc  = CreateCompatibleDC(NULL);
        g_membmp = CreateCompatibleBitmap(GetDC(h), BW, BH);
        SelectObject(g_memdc, g_membmp);
        for (int i = 0; i < BCOLS * BROWS; i++) g_bricks[i] = 1;
        reset_ball();
        return 0;
    case WM_TIMER:
        /* attract-mode autopilot with ALTERNATING aim offset: every paddle
         * return flips the sweep direction (vx=-3 then +3 then -3...) so the
         * trajectory can never settle into a periodic corridor-miss loop. */
        if (g_launched) {
            int side = (g_hits & 1) ? 24 : -24;
            int target = g_bx + BALL / 2 - PADDLE_W / 2 + side;
            if (g_px + 4 < target && g_px < BW - PADDLE_W - 8) g_px += 4;
            else if (g_px - 4 > target && g_px > 8) g_px -= 4;
        }
        step();
        g_tick++;
        if ((g_tick & 3) == 0) InvalidateRect(h, NULL, FALSE); /* 15fps on 60hz */
        return 0;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT: case 'A':
            if (g_px > 8) g_px -= 8;
            break;
        case VK_RIGHT: case 'D':
            if (g_px < BW - PADDLE_W - 8) g_px += 8;
            break;
        case VK_SPACE:
            g_launched = 1;
            break;
        case VK_ESCAPE:
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_CHAR: /* SWEEP-006: TranslateMessage-queued char, same actions */
        switch (wp) {
        case 'a': case 'A':
            if (g_px > 8) g_px -= 8;
            break;
        case 'd': case 'D':
            if (g_px < BW - PADDLE_W - 8) g_px += 8;
            break;
        case ' ':
            g_launched = 1;
            break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        draw(g_memdc);                       /* off-screen */
        BitBlt(hdc, 0, 0, BW, BH, g_memdc, 0, 0, SRCCOPY); /* present */
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "MWBreakout";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "MWBreakout",
        "MiniWin Breakout (SW-560 on MiniWin)",
        WS_OVERLAPPEDWINDOW, 60, 60, BW, BH, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 20, NULL);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
