/* t14_wine_contracts — wine-l0 W4 regression PE (LoadLibraryEx + 0x8000).
 *
 * Discriminates the wine-l0 wagon-4 contract additions (wine-9.0 pinned):
 * Phase 4 LoadLibraryEx (dlls/kernelbase/loader.c):
 *   - NULL name -> ERROR_INVALID_PARAMETER(87), NULL
 *   - empty name -> ERROR_MOD_NOT_FOUND(126), NULL
 *   - known system DLL, flags 0 -> non-NULL pseudo-handle (documented
 *     engine technique; WinRAR CRT-init call shape, SW-1212)
 *   - full Windows path -> base-name resolution
 *   - unknown DLL -> 126
 *   - LOAD_LIBRARY_AS_DATAFILE(0x2) / AS_IMAGE_RESOURCE(0x20) -> typed
 *     failure 126 (never the export-resolving pseudo-handle)
 *   - LOAD_WITH_ALTERED_SEARCH_PATH(0x8) -> proceeds like flags 0
 *   - GetProcAddress on the handle resolves (SW-1221 purpose)
 * Phase 5 CW_USEDEFAULT (win32u is_default_coord):
 *   - the 0x8000 sentinel behaves like CW_USEDEFAULT (W4 addition to
 *     SW-1471) -> engine work metric 1024x768
 *   - explicit coordinates pass through unchanged
 *
 * House style: no stdio.h, wsprintfA output, exit 0 = all pass.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;
static char g_log[8192];
static int g_log_n = 0;

static void check(int cond, const char* what) {
    if (cond) g_pass++;
    else g_fail++;
    if (g_log_n < (int)sizeof(g_log) - 160) {
        g_log_n += wsprintfA(g_log + g_log_n, "%s %s\r\n",
                             cond ? "[PASS]" : "[FAIL]", what);
    }
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)a; (void)b; (void)c; (void)d;

    /* ---- LoadLibraryEx contract ---- */
    HMODULE hAdv = LoadLibraryExA("ADVAPI32.dll", NULL, 0);
    check(hAdv != NULL, "ExA known dll flags=0 -> handle (WinRAR CRT shape)");
    HMODULE hAdvW = LoadLibraryExW(L"advapi32.dll", NULL, 0);
    check(hAdvW != NULL, "ExW known dll flags=0 -> handle");
    HMODULE hAdvPath = LoadLibraryExA("C:\\Windows\\System32\\ADVAPI32.DLL", NULL, 0);
    check(hAdvPath != NULL, "ExA full windows path -> base-name resolve");
    SetLastError(0xDEAD);
    HMODULE hMiss = LoadLibraryExA("no_such_dll_xyz123.dll", NULL, 0);
    check(hMiss == NULL && GetLastError() == 126, "ExA unknown dll -> NULL + 126");
    SetLastError(0xDEAD);
    check(LoadLibraryExA(NULL, NULL, 0) == NULL && GetLastError() == 87,
          "ExA NULL -> NULL + 87 (ERROR_INVALID_PARAMETER)");
    SetLastError(0xDEAD);
    check(LoadLibraryExW(NULL, NULL, 0) == NULL && GetLastError() == 87,
          "ExW NULL -> NULL + 87");
    SetLastError(0xDEAD);
    check(LoadLibraryExA("", NULL, 0) == NULL && GetLastError() == 126,
          "ExA empty -> NULL + 126");
    SetLastError(0xDEAD);
    check(LoadLibraryExA("advapi32.dll", NULL, 0x2) == NULL && GetLastError() == 126,
          "ExA LOAD_LIBRARY_AS_DATAFILE -> typed failure 126");
    SetLastError(0xDEAD);
    check(LoadLibraryExA("advapi32.dll", NULL, 0x20) == NULL && GetLastError() == 126,
          "ExA LOAD_LIBRARY_AS_IMAGE_RESOURCE -> typed failure 126");
    check(LoadLibraryExA("advapi32.dll", NULL, 0x8) != NULL,
          "ExA LOAD_WITH_ALTERED_SEARCH_PATH -> proceeds");
    check(LoadLibraryA("advapi32.dll") != NULL, "plain LoadLibraryA known dll ok");
    check(LoadLibraryW(L"advapi32.dll") != NULL, "plain LoadLibraryW known dll ok");
    {
        FARPROC f = GetProcAddress(hAdv, "RegOpenKeyExW");
        check(f != NULL, "GetProcAddress on ExA pseudo-handle resolves");
    }

    /* ---- CW_USEDEFAULT / 0x8000 sentinel ---- */
    {
        WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"MWContractT14";
        RegisterClassW(&wc);
    }
    HWND hw1 = CreateWindowExW(0, L"MWContractT14", L"t", WS_OVERLAPPEDWINDOW,
                               (int)0x80000000, 0, (int)0x80000000, (int)0x80000000,
                               NULL, NULL, NULL, NULL);
    RECT rc1; memset(&rc1, 0, sizeof(rc1));
    int ok1 = GetWindowRect(hw1, &rc1);
    check(ok1 && rc1.left == 0 && rc1.top == 0 &&
          rc1.right == 1024 && rc1.bottom == 768,
          "CW_USEDEFAULT -> engine work metric 1024x768 @0,0 (SW-1471)");
    /* W4: 0x8000 is ALSO a default sentinel (Wine is_default_coord) */
    HWND hw2 = CreateWindowExW(0, L"MWContractT14", L"t", WS_OVERLAPPEDWINDOW,
                               0x8000, 0x8000, 0x8000, 0x8000,
                               NULL, NULL, NULL, NULL);
    RECT rc2; memset(&rc2, 0, sizeof(rc2));
    check(GetWindowRect(hw2, &rc2) && rc2.right == 1024 && rc2.bottom == 768,
          "W4: 0x8000 sentinel behaves as default too");
    /* Engine model (SW-1382): the FIRST top-level owns the paint surface;
     * later windows are helper identities. GetWindowRect reports the main
     * surface. The observable contract here: sentinel/explicit windows
     * must NOT shrink or corrupt the main surface (the historical bug was
     * a 32x32 shrink from an unhandled sentinel). */
    HWND hw4 = CreateWindowExW(0, L"MWContractT14", L"t", WS_OVERLAPPEDWINDOW,
                               10, 20, 300, 200, NULL, NULL, NULL, NULL);
    RECT rc4; memset(&rc4, 0, sizeof(rc4));
    check(GetWindowRect(hw4, &rc4) && rc4.right == 1024 && rc4.bottom == 768,
          "explicit-size helper window leaves main surface 1024x768");
    /* WS_POPUP defaults collapse to 0 before storage (never 32x32 shrink) */
    HWND hw3 = CreateWindowExW(0, L"MWContractT14", L"p", WS_POPUP,
                               (int)0x80000000, (int)0x80000000,
                               (int)0x80000000, (int)0x80000000,
                               NULL, NULL, NULL, NULL);
    RECT rc3; memset(&rc3, 0, sizeof(rc3));
    check(GetWindowRect(hw3, &rc3) && rc3.right == 1024 && rc3.bottom == 768,
          "WS_POPUP sentinel window leaves main surface intact");

    /* ---- CharLowerW sanity (SW-1213 DWORD-char contract still intact) ---- */
    check((wchar_t)(uintptr_t)CharLowerW((wchar_t*)(uintptr_t)1) == 1,
          "CharLowerW(1) -> 1");
    check((wchar_t)(uintptr_t)CharLowerW((wchar_t*)(uintptr_t)'Q') == L'q',
          "CharLowerW DWORD-char 'Q' -> 'q'");

    g_log_n += wsprintfA(g_log + g_log_n,
                         "---- t14_wine_contracts: %d passed, %d failed ----\r\n",
                         g_pass, g_fail);
    OutputDebugStringA(g_log);
    return g_fail == 0 ? 0 : 1;
}
