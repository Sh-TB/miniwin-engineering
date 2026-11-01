/* t16_child_rect — W7 (SW-1571) child-rect contract regression PE.
 *
 * Locks the child-geometry contracts WinRAR's layout loop leans on:
 *   - a child created 0x0 (the WinRAR pattern: geometry arrives later via
 *     the parent's WM_SIZE layout) registers with a clamped 1x1 rect, not
 *     the main rect (SW-1288 family)
 *   - MoveWindow(child,...) updates the child's OWN rect (SW-1287c/1288)
 *   - GetWindowRect(child) answers the child rect, parent-relative
 *   - SetWindowPos(child, size) updates it too (SW-1288 FIX)
 *   - WS_CHILD without a parent -> NULL + ERROR_TLW_WITH_WSCHILD (1406,
 *     SW-1326)
 *   - a second MoveWindow with the same dims still stores (idempotent)
 * House style: no stdio.h, wsprintfA output, exit 0 = all pass.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

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
    wc.lpszClassName = "t16frame";
    RegisterClassA(&wc);

    HWND main = CreateWindowExA(0, "t16frame", "t16 child rect",
                                WS_OVERLAPPEDWINDOW, 0, 0, 800, 600,
                                NULL, NULL, a, NULL);
    check(main != NULL, "main frame created");
    ShowWindow(main, SW_SHOW);

    /* WinRAR pattern: layout children are created 0x0 and sized later */
    HWND lv = CreateWindowExA(0, "SysListView32", NULL,
                              WS_CHILD | WS_VISIBLE | 0x44200249,
                              0, 0, 0, 0, main, (HMENU)11, a, NULL);
    check(lv != NULL, "listview child created 0x0");

    RECT rc;
    check(GetWindowRect(lv, &rc), "GetWindowRect(lv) answers");
    check(rc.right - rc.left >= 1 && rc.bottom - rc.top >= 1,
          "0x0 create clamps to >= 1x1 (not the main rect)");
    check(rc.right - rc.left < 100, "clamped width is small (not 800)");

    /* the parent's layout pass positions it (WinRAR: MoveWindow(listview)) */
    check(MoveWindow(lv, 0, 56, 764, 480, TRUE), "MoveWindow(lv,0,56,764,480)");
    check(GetWindowRect(lv, &rc) && rc.left == 0 && rc.top == 56 &&
          rc.right == 764 && rc.bottom == 536,
          "child rect answers the moved rect exactly");

    /* SetWindowPos resize updates the record too */
    check(SetWindowPos(lv, NULL, 0, 56, 700, 400, SWP_NOZORDER),
          "SetWindowPos(lv 700x400)");
    check(GetWindowRect(lv, &rc) && rc.right == 700 && rc.bottom == 456,
          "SetWindowPos rect stored");

    /* a toolbar child in the same frame keeps its own rect (no cross-talk) */
    HWND tb = CreateWindowExA(0, "ToolbarWindow32", NULL,
                              WS_CHILD | WS_VISIBLE,
                              0, 0, 0, 0, main, (HMENU)12, a, NULL);
    check(tb != NULL, "toolbar child created");
    check(MoveWindow(tb, 0, 0, 608, 37, TRUE), "MoveWindow(tb 608x37)");
    check(GetWindowRect(tb, &rc) && rc.right == 608 && rc.bottom == 37,
          "toolbar rect independent");
    check(GetWindowRect(lv, &rc) && rc.right == 700,
          "listview rect untouched by toolbar move");

    /* WS_CHILD without a parent is invalid (SW-1326) */
    SetLastError(0xDEAD);
    HWND orphan = CreateWindowExA(0, "BUTTON", "x", WS_CHILD | WS_VISIBLE,
                                  0, 0, 10, 10, NULL, NULL, a, NULL);
    check(orphan == NULL && GetLastError() == 1406,
          "WS_CHILD without parent -> NULL + 1406");

    /* same-dims MoveWindow is accepted (real Windows: no-op relayout) */
    check(MoveWindow(tb, 0, 0, 608, 37, TRUE), "same-dims MoveWindow accepted");

    if (g_log_n < (int)sizeof(g_log) - 80)
        g_log_n += wsprintfA(g_log + g_log_n,
                             "---- t16_child_rect: %d passed, %d failed ----\r\n",
                             g_pass, g_fail);
    MessageBoxA(main, g_log, "t16 result", MB_OK);
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
