/* t15_toolbar_contract — W7 (SW-1571) toolbar protocol regression PE.
 *
 * Locks the engine's TOOLBAR child message model at the measured Wine-10.1
 * oracle parity (artifacts/l0-matrix/winrar-bridge/L0-20261101-080928-WinRAR
 * relay, WinRAR 7.12 x64 toolbar 0xd00b6):
 *   TB_BUTTONSTRUCTSIZE(0x41E, sizeof(TBBUTTON)=32)
 *   TB_SETBUTTONSIZE(0x41F, MAKELONG(w,h)) -> TRUE
 *   TB_ADDBUTTONSW(0x444, 1, &TBBUTTON) per button -> TRUE, count grows
 *   TB_BUTTONCOUNT(0x418) -> buttons added
 *   TB_GETBUTTON(0x417, idx, &TBBUTTON) -> fields round-trip
 *   TB_ADDSTRINGW-pool via 0x44D returns the running base index
 *   TB_GETITEMRECT(0x41D, idx, &rect) -> cumulative x layout
 * Guest is a plain x64 PE: RegisterClassExA + CreateWindowExA main frame,
 * CreateWindowExA("TOOLBAR") child (engine sysclass route), then the full
 * protocol. House style: no stdio.h, wsprintfA output, exit 0 = all pass.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int32_t  iBitmap;
    int32_t  idCommand;
    uint8_t  fsState;
    uint8_t  fsStyle;
    uint8_t  bReserved[6];
    int64_t  dwData;
    int64_t  iString;
} TBBUTTON; /* 32 bytes on x64 */

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
    (void)a; (void)b; (void)c; (void)d;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = a;
    wc.lpszClassName = "t15frame";
    RegisterClassA(&wc);

    HWND main = CreateWindowExA(0, "t15frame", "t15 toolbar contract",
                                WS_OVERLAPPEDWINDOW, 40, 40, 640, 480,
                                NULL, NULL, a, NULL);
    check(main != NULL, "main frame created");
    ShowWindow(main, SW_SHOW);

    /* engine sysclass route: "TOOLBAR" prefix matches the toolbar model */
    HWND tb = CreateWindowExA(0, "ToolbarWindow32", NULL,
                              WS_CHILD | WS_VISIBLE | 0x56000d64,
                              0, 0, 0, 0, main, (HMENU)12, a, NULL);
    check(tb != NULL, "toolbar child created (ToolbarWindow32 route)");
    check(GetParent(tb) == main, "toolbar parent is the frame");

    SendMessageA(tb, 0x41E /*TB_BUTTONSTRUCTSIZE*/, 32, 0);
    LRESULT bs = SendMessageW(tb, 0x41F /*TB_SETBUTTONSIZE*/, 0,
                              MAKELONG(48, 36));
    check(bs == 1, "TB_SETBUTTONSIZE(48,36) -> TRUE (Wine oracle: 1)");

    static const struct { int bmp; int cmd; const char* lbl; } btns[3] = {
        { 0, 0x190, "Add" }, { 1, 0x191, "Extract" }, { 2, 0x193, "View" },
    };
    static uint16_t pool[3][24];
    for (int i = 0; i < 3; i++) {
        int k = 0;
        for (; btns[i].lbl[k]; k++) pool[i][k] = (uint16_t)btns[i].lbl[k];
        pool[i][k] = 0;
    }
    int base0 = -1;
    for (int i = 0; i < 3; i++) {
        TBBUTTON b;
        memset(&b, 0, sizeof(b));
        b.iBitmap = btns[i].bmp;
        b.idCommand = btns[i].cmd;
        b.fsState = 4 /*TBSTATE_ENABLED*/;
        b.iString = (int64_t)(intptr_t)pool[i];
        int before = (int)SendMessageW(tb, 0x418 /*TB_BUTTONCOUNT*/, 0, 0);
        LRESULT r = SendMessageW(tb, 0x444 /*TB_ADDBUTTONSW*/, 1, (LPARAM)&b);
        int after = (int)SendMessageW(tb, 0x418, 0, 0);
        check(r == 1, "TB_ADDBUTTONSW -> TRUE");
        check(after == before + 1, "TB_BUTTONCOUNT grows by one per add");
        /* 0x44D on the engine pools the label and returns the base index
         * (Wine oracle: incrementing base per button: 0,1,2...) */
        LRESULT si = SendMessageW(tb, 0x44D /*TB_GETSTRINGW-pool*/, 0,
                                  (LPARAM)pool[i]);
        if (i == 0) base0 = (int)si;
        check((int)si == base0 + i, "string pool base index increments");
    }
    check(SendMessageW(tb, 0x418, 0, 0) == 3, "BUTTONCOUNT == 3 after adds");

    /* TB_GETBUTTON readback: fields round-trip (engine stores bmp/cmd/style) */
    for (int i = 0; i < 3; i++) {
        TBBUTTON rb;
        memset(&rb, 0xAA, sizeof(rb));
        LRESULT r = SendMessageW(tb, 0x417 /*TB_GETBUTTON*/, i, (LPARAM)&rb);
        check(r == 1, "TB_GETBUTTON -> TRUE");
        check(rb.idCommand == btns[i].cmd, "GETBUTTON idCommand round-trip");
        check(rb.iBitmap == btns[i].bmp, "GETBUTTON iBitmap round-trip");
        check(rb.fsState == 4, "GETBUTTON fsState round-trip");
    }

    /* TB_GETITEMRECT cumulative layout: sep-free row -> x = i*bw */
    for (int i = 0; i < 3; i++) {
        RECT rc;
        memset(&rc, 0, sizeof(rc));
        SendMessageW(tb, 0x41D /*TB_GETITEMRECT*/, i, (LPARAM)&rc);
        check(rc.left == i * 48, "GETITEMRECT cumulative x layout");
        check(rc.right == (i + 1) * 48, "GETITEMRECT right edge");
    }

    /* separator button occupies a 6px slot, not a full button */
    {
        TBBUTTON sep;
        memset(&sep, 0, sizeof(sep));
        sep.fsStyle = 0x01 /*BTNS_SEP*/;
        SendMessageW(tb, 0x444, 1, (LPARAM)&sep);
        RECT rc;
        memset(&rc, 0, sizeof(rc));
        SendMessageW(tb, 0x41D, 3, (LPARAM)&rc);
        check(rc.right - rc.left == 6, "BTNS_SEP slot is 6px wide");
    }

    /* toolbar art registry parity: the renderer's art source exists only
     * when the guest decoded images; without any Gdip decode the model
     * still answers the whole protocol (this test's shape). */

    if (g_log_n < (int)sizeof(g_log) - 80)
        g_log_n += wsprintfA(g_log + g_log_n,
                             "---- t15_toolbar_contract: %d passed, %d failed ----\r\n",
                             g_pass, g_fail);
    MessageBoxA(main, g_log, "t15 result", MB_OK);
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
