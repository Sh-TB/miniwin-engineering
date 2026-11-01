/* MiniWinTrains — «قطار» 10-wagon playable train on MiniWin
 * (https://github.com/Sh-tB/miniwin-engineering)
 *
 * Per user directive: قطار با ۱۰ واگن (10 wagons), playable, modern look.
 *
 * Build (PE x64):
 *   python3 -m ziglang cc -target x86_64-windows-gnu -O1 \
 *       -o artifacts/trains/trains.exe tests/games/miniwin_trains.c \
 *       -luser32 -lgdi32 -lkernel32
 * Run:
 *   MW_GUI_EVENTS=1 MW_GUI_PUMP_BLOCK=1 MW_GUI_MAX_FRAMES=120 \
 *   MW_GUI_KEYS=38,38,38,38,9,9,9,32,78,40,40,27 \
 *       ./minwin_loader artifacts/trains/trains.exe
 *
 * Gameplay:
 *   UP / DOWN  - throttle + / brake (8 speed steps, km/h HUD)
 *   TAB        - cycle selected wagon (accent highlight, HUD shows type)
 *   SPACE      - horn (smoke burst + HONK banner)
 *   N          - day / night (stars, headlight beam)
 *   ESC        - quit
 *
 * Modern flat design: gradient sky (banded, deterministic), parallax hills,
 * rounded-rect wagons in a Win10-class palette, soft shadows, hubcap wheels,
 * animated smoke particles. No rand()/time() — the whole animation is a pure
 * function of frame counter + key script => byte-identical frame captures.
 * Only APIs MiniWin's loader implements: FillRect/Rectangle/Ellipse/RoundRect/
 * Polygon/CreateSolidBrush/CreatePen/SelectObject/DeleteObject/MoveToEx/LineTo/
 * SetTextColor/SetBkMode/TextOutA/wsprintfA + the standard window/timer loop.
 */
#include <windows.h>

#define WND_W 1400
#define WND_H 560
#define N_WAGONS 10          /* directive: 10 wagons */
#define GROUND_Y 440

typedef struct {
    int type;      /* 0 container, 1 tanker, 2 hopper, 3 flat */
    COLORREF col;
    int cargo;     /* cargo fill 0..100 (varies per wagon) */
    int lit;       /* tail lights when night */
} Wagon;

static Wagon g_wag[N_WAGONS] = {
    { 0, RGB(0,120,215),  100, 0 },   /* container blue   */
    { 1, RGB(230,110,40), 100, 0 },   /* tanker orange    */
    { 2, RGB(90,200,120),  80, 0 },   /* hopper green     */
    { 3, RGB(160,90,200), 100, 0 },   /* flat violet      */
    { 0, RGB(220,80,90),  100, 0 },   /* container red    */
    { 1, RGB(70,170,230), 100, 0 },   /* tanker sky       */
    { 2, RGB(240,180,50),  60, 0 },   /* hopper yellow    */
    { 0, RGB(120,190,90),  90, 0 },   /* container lime   */
    { 3, RGB(230,120,170),100, 0 },   /* flat pink        */
    { 0, RGB(90,110,140),  70, 0 },   /* caboose steel    */
};

#define LOCO_X 24
#define WAG_PITCH 110
static int  g_speed   = 2;      /* 0..8 steps -> px/frame */
static int  g_world   = 0;      /* scroll accumulator     */
static int  g_frame   = 0;
static int  g_sel     = 0;      /* selected wagon (TAB)   */
static int  g_night   = 0;
static int  g_honk    = 0;      /* frames left of horn    */
static int  g_honk_x  = 0;

typedef struct { int x, y, r, age; } Puff;
static Puff g_puffs[64];
static int  g_npuffs = 0;

static char g_msg[128];

static void spawn_puff(int x, int y, int big) {
    if (g_npuffs >= 64) return;
    g_puffs[g_npuffs].x = x; g_puffs[g_npuffs].y = y;
    g_puffs[g_npuffs].r = big ? 10 : 5;
    g_puffs[g_npuffs].age = 0;
    g_npuffs++;
}

static void step_sim(void) {
    g_world += g_speed;
    g_frame++;
    if (g_honk > 0) g_honk--;
    /* smoke from the stack while rolling (stack is screen-anchored) */
    if (g_speed > 0 && (g_frame % 3) == 0)
        spawn_puff(LOCO_X + 79, GROUND_Y - 150, (g_frame % 9) == 0);
    for (int i = 0; i < g_npuffs; i++) {
        g_puffs[i].x -= g_speed + 1;
        g_puffs[i].y -= 2;
        g_puffs[i].r += 1;
        g_puffs[i].age++;
    }
    int w = 0;
    for (int i = 0; i < g_npuffs; i++) if (g_puffs[i].age < 40) g_puffs[w++] = g_puffs[i];
    g_npuffs = w;
}

static void sky_band(HDC dc, int y0, int y1, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    RECT r = { 0, y0, WND_W, y1 };
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void draw_sky(HDC dc) {
    /* 8-step gradient dusk sky, deterministic */
    if (!g_night) {
        COLORREF bands[8] = {
            RGB(38,79,125), RGB(52,98,150), RGB(66,118,172), RGB(84,140,190),
            RGB(104,160,204), RGB(126,180,216), RGB(150,199,227), RGB(176,216,236)
        };
        for (int i = 0; i < 8; i++)
            sky_band(dc, i * (GROUND_Y / 8), (i + 1) * (GROUND_Y / 8), bands[i]);
    } else {
        COLORREF bands[8] = {
            RGB(8,10,26), RGB(10,13,32), RGB(13,17,40), RGB(16,21,48),
            RGB(20,26,56), RGB(24,31,64), RGB(29,37,72), RGB(34,43,80)
        };
        for (int i = 0; i < 8; i++)
            sky_band(dc, i * (GROUND_Y / 8), (i + 1) * (GROUND_Y / 8), bands[i]);
        /* stars (fixed lattice, subtle twinkle by frame parity) */
        HBRUSH sb = CreateSolidBrush(RGB(230,235,245));
        SelectObject(dc, sb);
        for (int i = 0; i < 40; i++) {
            int sx = (i * 173 + 61) % WND_W;
            int sy = (i * 97 + 23) % (GROUND_Y - 120);
            if (((i + (g_frame >> 3)) & 7) == 0) continue;
            RECT r = { sx, sy, sx + 2, sy + 2 };
            FillRect(dc, &r, sb);
        }
        DeleteObject(sb);
    }
    /* clouds (parallax with world scroll) */
    HBRUSH cb = CreateSolidBrush(g_night ? RGB(40,48,74) : RGB(250,252,255));
    SelectObject(dc, cb);
    for (int c = 0; c < 3; c++) {
        int cx = (c * 340 + 120 - (g_world / 2)) % (WND_W + 300) - 150;
        int cy = 60 + c * 45;
        Ellipse(dc, cx, cy, cx + 130, cy + 34);
        Ellipse(dc, cx + 40, cy - 16, cx + 150, cy + 22);
        Ellipse(dc, cx - 30, cy - 8, cx + 60, cy + 24);
    }
    DeleteObject(cb);
}

static void hills(HDC dc, int off, int base, int h, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    SelectObject(dc, b);
    for (int i = -1; i < 6; i++) {
        int hx = i * 260 + off;
        Ellipse(dc, hx - 160, base - h, hx + 160, base + h / 3);
    }
    DeleteObject(b);
}

static void draw_ground(HDC dc) {
    /* far hills, near hills */
    hills(dc, -(g_world / 4) % 260, GROUND_Y - 30, 90, g_night ? RGB(16,22,44) : RGB(96,140,120));
    hills(dc, -(g_world / 2) % 260, GROUND_Y - 6, 60, g_night ? RGB(22,30,58) : RGB(70,120,96));
    /* ground slab */
    HBRUSH g = CreateSolidBrush(g_night ? RGB(28,32,44) : RGB(52,58,66));
    RECT gr = { 0, GROUND_Y, WND_W, WND_H };
    FillRect(dc, &gr, g);
    DeleteObject(g);
    /* ballast strip */
    HBRUSH ball = CreateSolidBrush(g_night ? RGB(38,42,54) : RGB(88,88,92));
    RECT br = { 0, GROUND_Y + 26, WND_W, GROUND_Y + 58 };
    FillRect(dc, &br, ball);
    DeleteObject(ball);
    /* sleepers */
    HBRUSH sl = CreateSolidBrush(g_night ? RGB(50,44,40) : RGB(112,84,58));
    SelectObject(dc, sl);
    int so = -(g_world % 46);
    for (int x = so; x < WND_W + 46; x += 46)
        Rectangle(dc, x, GROUND_Y + 32, x + 30, GROUND_Y + 52);
    DeleteObject(sl);
    /* rails */
    HPEN rp = CreatePen(PS_SOLID, 3, g_night ? RGB(150,160,180) : RGB(200,205,212));
    SelectObject(dc, rp);
    MoveToEx(dc, 0, GROUND_Y + 30, NULL); LineTo(dc, WND_W, GROUND_Y + 30);
    MoveToEx(dc, 0, GROUND_Y + 56, NULL); LineTo(dc, WND_W, GROUND_Y + 56);
    DeleteObject(rp);
}

static void shadow(HDC dc, int l, int t, int r, int b) {
    HBRUSH s = CreateSolidBrush(g_night ? RGB(14,16,24) : RGB(40,44,52));
    RECT rc = { l + 4, t + 5, r + 4, b + 5 };
    FillRect(dc, &rc, s);
    DeleteObject(s);
}

static void rr_fill(HDC dc, int l, int t, int r, int btm, int ew, int eh, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    SelectObject(dc, b);
    RoundRect(dc, l, t, r, btm, ew, eh);
    DeleteObject(b);
}

static void wheel(HDC dc, int cx, int cy, int r) {
    HBRUSH tire = CreateSolidBrush(RGB(30,32,38));
    SelectObject(dc, tire);
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    DeleteObject(tire);
    HBRUSH hub = CreateSolidBrush(g_night ? RGB(150,160,175) : RGB(205,210,220));
    SelectObject(dc, hub);
    Ellipse(dc, cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2);
    DeleteObject(hub);
    /* rotating spoke = motion cue */
    HPEN sp = CreatePen(PS_SOLID, 2, RGB(70,76,88));
    SelectObject(dc, sp);
    int a = (g_world / 4) & 15;
    int dx = (a < 4 || a > 11) ? r / 3 : (a < 8 ? 0 : -(r / 3));
    int dy = (a >= 4 && a < 12) ? r / 3 : (a < 4 ? 0 : -(r / 3));
    MoveToEx(dc, cx - dx, cy - dy, NULL); LineTo(dc, cx + dx, cy + dy);
    DeleteObject(sp);
}

static void draw_wagon(HDC dc, int idx, int x, int y) {
    Wagon* w = &g_wag[idx];
    const int W = 100, H = 58;
    shadow(dc, x, y, x + W, y + H);
    /* chassis */
    rr_fill(dc, x - 6, y + H - 10, x + W + 6, y + H + 6, 6, 6, RGB(70,74,82));
    rr_fill(dc, x, y, x + W, y + H, 10, 10, w->col);
    /* highlight sheen = modern flat */
    rr_fill(dc, x + 5, y + 4, x + W - 5, y + 12, 6, 6,
            RGB(((w->col & 0xFF) + 60) > 255 ? 255 : (w->col & 0xFF) + 60,
                (((w->col >> 8) & 0xFF) + 60) > 255 ? 255 : ((w->col >> 8) & 0xFF) + 60,
                (((w->col >> 16) & 0xFF) + 60) > 255 ? 255 : ((w->col >> 16) & 0xFF) + 60));
    if (w->type == 0) {          /* container: center doors */
        HPEN d = CreatePen(PS_SOLID, 2, RGB(255,255,255));
        SelectObject(dc, d);
        MoveToEx(dc, x + 38, y + 8, NULL);  LineTo(dc, x + 38, y + H - 8);
        MoveToEx(dc, x + 62, y + 8, NULL);  LineTo(dc, x + 62, y + H - 8);
        MoveToEx(dc, x + 14, y + 20, NULL); LineTo(dc, x + 28, y + 20);
        MoveToEx(dc, x + 74, y + 20, NULL); LineTo(dc, x + 88, y + 20);
        DeleteObject(d);
    } else if (w->type == 1) {   /* tanker: barrel on the flat */
        rr_fill(dc, x, y + 12, x + W, y + H, 10, 10, RGB(70,74,82)); /* flat bed */
        rr_fill(dc, x + 8, y, x + W - 8, y + 34, 40, 34, w->col);
        rr_fill(dc, x + 14, y + 4, x + W - 14, y + 9, 20, 8, RGB(255,255,255));
    } else if (w->type == 2) {   /* hopper: open top + load */
        rr_fill(dc, x, y, x + W, y + H, 10, 10, w->col);
        HBRUSH cargo = CreateSolidBrush(RGB(150,110,70));
        SelectObject(dc, cargo);
        RECT cr = { x + 8, y + 4, x + W - 8, y + 18 };
        FillRect(dc, &cr, cargo);
        DeleteObject(cargo);
    } else {                     /* flat with crates */
        rr_fill(dc, x, y + 30, x + W, y + H, 8, 8, w->col);
        rr_fill(dc, x + 8, y + 2, x + 48, y + 34, 6, 6, RGB(200,160,90));
        rr_fill(dc, x + 56, y + 8, x + 96, y + 34, 6, 6, RGB(170,140,90));
    }
    /* cargo level mark */
    if (w->cargo < 100) {
        HBRUSH lv = CreateSolidBrush(RGB(30,32,38));
        RECT lr = { x + 8, y + H - 18, x + 8 + (W - 16) * w->cargo / 100, y + H - 14 };
        FillRect(dc, &lr, lv);
        DeleteObject(lv);
    }
    /* wheels */
    wheel(dc, x + 24, y + H + 8, 12);
    wheel(dc, x + W - 24, y + H + 8, 12);
    /* coupling */
    if (idx < N_WAGONS - 1) {
        HPEN cp = CreatePen(PS_SOLID, 4, RGB(55,60,70));
        SelectObject(dc, cp);
        MoveToEx(dc, x + W + 2, y + H - 2, NULL); LineTo(dc, x + W + 22, y + H - 2);
        DeleteObject(cp);
    }
    /* selection: accent outline (4 filled rects = loader-friendly frame) */
    if (idx == g_sel) {
        RECT e1 = { x - 3, y - 3, x + W + 3, y };
        RECT e2 = { x - 3, y + H, x + W + 3, y + H + 3 };
        RECT e3 = { x - 3, y, x, y + H };
        RECT e4 = { x + W, y, x + W + 3, y + H };
        HBRUSH ab = CreateSolidBrush(RGB(0,180,255));
        FillRect(dc, &e1, ab); FillRect(dc, &e2, ab);
        FillRect(dc, &e3, ab); FillRect(dc, &e4, ab);
        DeleteObject(ab);
    }
    /* wagon number */
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255,255,255));
    wsprintfA(g_msg, "%02d", idx + 1);
    TextOutA(dc, x + 4, y + H + 14, g_msg, lstrlenA(g_msg));
}

static void draw_loco(HDC dc, int x, int y) {
    const int W = 170, H = 88;
    shadow(dc, x, y, x + W, y + H);
    /* chassis + body */
    rr_fill(dc, x - 8, y + H - 12, x + W + 130, y + H + 8, 8, 8, RGB(60,64,72));
    rr_fill(dc, x, y, x + W, y + H, 12, 12, RGB(225,60,60));
    rr_fill(dc, x + 6, y + 5, x + W - 6, y + 14, 8, 8, RGB(255,120,110));
    /* nose (rounded front) */
    rr_fill(dc, x + W - 4, y + 22, x + W + 46, y + H, 18, 18, RGB(200,50,50));
    /* cab window */
    rr_fill(dc, x + 12, y + 18, x + 52, y + 44, 8, 8,
            g_night ? RGB(255,235,150) : RGB(170,220,250));
    /* stripe */
    HBRUSH st = CreateSolidBrush(RGB(250,250,252));
    RECT sr = { x, y + H - 26, x + W + 40, y + H - 20 };
    FillRect(dc, &sr, st);
    DeleteObject(st);
    /* stack */
    rr_fill(dc, x + 66, y - 26, x + 92, y + 6, 6, 6, RGB(50,54,62));
    /* headlight */
    HBRUSH hl = CreateSolidBrush(g_night ? RGB(255,244,180) : RGB(255,230,140));
    SelectObject(dc, hl);
    Ellipse(dc, x + W + 30, y + H - 34, x + W + 48, y + H - 16);
    DeleteObject(hl);
    if (g_night) {  /* beam */
        HBRUSH bm = CreateSolidBrush(RGB(60,58,40));
        SelectObject(dc, bm);
        POINT beam[3];
        beam[0].x = x + W + 44; beam[0].y = y + H - 25;
        beam[1].x = x + W + 250; beam[1].y = y + H - 60;
        beam[2].x = x + W + 250; beam[2].y = y + H + 10;
        Polygon(dc, beam, 3);
        DeleteObject(bm);
    }
    /* wheels: big drivers + bogie */
    wheel(dc, x + 30, y + H + 8, 16);
    wheel(dc, x + 78, y + H + 8, 16);
    wheel(dc, x + W + 24, y + H + 6, 12);
    /* coupling to first wagon */
    HPEN cp = CreatePen(PS_SOLID, 4, RGB(55,60,70));
    SelectObject(dc, cp);
    MoveToEx(dc, x + W + 44, y + H - 2, NULL); LineTo(dc, x + W + 60, y + H - 2);
    DeleteObject(cp);
    /* name plate */
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255,255,255));
    TextOutA(dc, x + 60, y + H - 18, "MW-10", 5);
}

static void draw_smoke(HDC dc) {
    for (int i = 0; i < g_npuffs; i++) {
        int shade = 210 - g_puffs[i].age * 3;
        if (shade < 120) shade = 120;
        HBRUSH b = CreateSolidBrush(RGB(shade, shade, shade + (g_night ? -60 : 0)));
        SelectObject(dc, b);
        int r = g_puffs[i].r;
        Ellipse(dc, g_puffs[i].x - r, g_puffs[i].y - r,
                    g_puffs[i].x + r, g_puffs[i].y + r);
        DeleteObject(b);
    }
}

static void draw_hud(HDC dc) {
    /* HUD panel */
    HBRUSH p = CreateSolidBrush(g_night ? RGB(18,20,30) : RGB(255,255,255));
    RECT pr = { 8, 8, 420, 46 };
    FillRect(dc, &pr, p);
    DeleteObject(p);
    HPEN pl = CreatePen(PS_SOLID, 1, RGB(190,195,205));
    SelectObject(dc, pl);
    Rectangle(dc, 8, 8, 420, 46);
    DeleteObject(pl);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_night ? RGB(220,225,235) : RGB(40,44,52));
    static const char* tn[4] = { "Container", "Tanker", "Hopper", "Flat" };
    wsprintfA(g_msg, "MiniWin Trains  |  %d km/h  |  wagons: %d", g_speed * 15, N_WAGONS);
    /* HUD panel sized for the wide client */
    TextOutA(dc, 16, 14, g_msg, lstrlenA(g_msg));
    wsprintfA(g_msg, "selected: %02d (%s)  [TAB] next", g_sel + 1, tn[g_wag[g_sel].type]);
    TextOutA(dc, 16, 29, g_msg, lstrlenA(g_msg));
    if (g_honk > 0) {
        SetTextColor(dc, RGB(255,180,40));
        wsprintfA(g_msg, "HONK!");
        TextOutA(dc, g_honk_x, 90, g_msg, 5);
    }
    /* keys hint bar */
    HBRUSH hb = CreateSolidBrush(g_night ? RGB(14,16,24) : RGB(248,248,250));
    RECT hr = { 0, WND_H - 24, WND_W, WND_H };
    FillRect(dc, &hr, hb);
    DeleteObject(hb);
    SetTextColor(dc, RGB(110,115,125));
    TextOutA(dc, 10, WND_H - 18,
             "UP/DOWN speed   TAB wagon   SPACE horn   N day-night   ESC quit",
             64);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER:
        step_sim();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_UP    && g_speed < 8) g_speed++;
        else if (w == VK_DOWN && g_speed > 0) g_speed--;
        else if (w == VK_TAB) { g_sel = (g_sel + 1) % N_WAGONS; InvalidateRect(h, NULL, FALSE); }
        else if (w == VK_SPACE) {
            g_honk = 40; g_honk_x = 640;
            for (int i = 0; i < 6; i++) spawn_puff(LOCO_X + 79 + i * 9, GROUND_Y - 150 - i * 6, 1);
        }
        else if (w == 'N') { g_night = !g_night; }
        else if (w == VK_ESCAPE) { PostQuitMessage(0); }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        draw_sky(dc);
        draw_ground(dc);
        /* train: loco at fixed screen anchor, wagons trailing to the right */
        int lx = LOCO_X;
        int ly = GROUND_Y - 88 - 8;
        draw_smoke(dc);
        draw_loco(dc, lx, ly);
        for (int i = 0; i < N_WAGONS; i++)
            draw_wagon(dc, i, lx + 170 + 44 + i * WAG_PITCH, ly + 30);
        draw_hud(dc);
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
    wc.lpszClassName = "MWTrains";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "MWTrains",
        "MiniWin Trains - 10 Wagons",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        10, 10, WND_W, WND_H, NULL, NULL, hi, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 30, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
