/* mw_reg_test — SWEEP-023 SW-991/992/995 registry contract acceptance PE.
 *
 * Proves the in-memory registry hive round-trips:
 *   T1 SW-991 REG_MULTI_SZ: set {"alpha","beta","gamma"} -> query -> byte-exact
 *   T2 SW-992 REG_DWORD overwrite: write 0x11111111 then 0x22222222 ->
 *      query must return 4 bytes == 0x22222222 (the SWEEP-023 overwrite fix;
 *      the old append-only hive read back the FIRST write -> this test failed)
 *   T3 REG_SZ round-trip + RegQueryInfoKeyW visibility
 *   T4 SW-995 SHGetValueW reads the same value end-to-end without creating
 *
 * House style (same as miniwin_calc): no stdio.h — the zig/mingw static-CRT
 * snprintf builds a fake FILE that map_crt_stream refuses; wsprintfA is the
 * proven stream-free path. Exit code 0 = all tests passed, 1 = any failure.
 * Deterministic: no rand()/time(). */
#include <windows.h>
#include <shlwapi.h>
#include <stdint.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

static void check(int cond, const char* what) {
    if (cond) g_pass++;
    else g_fail++;
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)a; (void)b; (void)c; (void)d;

    HKEY hk = NULL;
    DWORD disp = 0;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MWRegTest",
                              0, NULL, 0, KEY_ALL_ACCESS, NULL, &hk, &disp);
    check(rc == 0, "create key");
    check(disp == 1 /*CREATED_NEW_KEY*/ || disp == 2, "disposition typed");

    /* T1: REG_MULTI_SZ round-trip (SW-991) */
    static const wchar_t multi[] = L"alpha\0beta\0gamma\0";
    const DWORD multi_bytes = (DWORD)sizeof(multi); /* includes final NUL */
    rc = RegSetValueExW(hk, L"multi", 0, REG_MULTI_SZ,
                        (const BYTE*)multi, multi_bytes);
    check(rc == 0, "set multi_sz");
    wchar_t back[64];
    DWORD got = sizeof(back), got_type = 0;
    rc = RegQueryValueExW(hk, L"multi", NULL, &got_type, (BYTE*)back, &got);
    check(rc == 0, "query multi_sz");
    check(got_type == REG_MULTI_SZ, "multi_sz type");
    check(got == multi_bytes, "multi_sz byte-exact size");
    check(got == multi_bytes && memcmp(back, multi, multi_bytes) == 0,
          "multi_sz byte-exact data");

    /* T2: REG_DWORD overwrite contract (SW-992) */
    DWORD v1 = 0x11111111, v2 = 0x22222222;
    rc = RegSetValueExW(hk, L"dw", 0, REG_DWORD, (const BYTE*)&v1, 4);
    check(rc == 0, "set dword v1");
    rc = RegSetValueExW(hk, L"dw", 0, REG_DWORD, (const BYTE*)&v2, 4);
    check(rc == 0, "set dword v2 (overwrite)");
    DWORD dval = 0; got = sizeof(dval); got_type = 0;
    rc = RegQueryValueExW(hk, L"dw", NULL, &got_type, (BYTE*)&dval, &got);
    check(rc == 0, "query dword");
    check(got_type == REG_DWORD, "dword type");
    check(got == 4, "dword 4-byte size contract");
    check(dval == 0x22222222, "dword reads the OVERWRITE (0x22222222)");

    /* T3: REG_SZ round-trip + info visibility */
    rc = RegSetValueExW(hk, L"str", 0, REG_SZ, (const BYTE*)L"hello", 12);
    check(rc == 0, "set sz");
    wchar_t sback[16]; got = sizeof(sback); got_type = 0;
    rc = RegQueryValueExW(hk, L"str", NULL, &got_type, (BYTE*)sback, &got);
    check(rc == 0 && got_type == REG_SZ && lstrcmpW(sback, L"hello") == 0,
          "sz round-trip");
    DWORD nvals = 0;
    rc = RegQueryInfoKeyW(hk, NULL, NULL, NULL, NULL, NULL, NULL, &nvals,
                          NULL, NULL, NULL, NULL);
    check(rc == 0 && nvals == 3, "info sees 3 values after overwrites");

    /* T4: SHGetValueW end-to-end (SW-995) — read without creating */
    wchar_t shback[16]; DWORD shcb = sizeof(shback); DWORD sh_type = 0;
    DWORD src = SHGetValueW(HKEY_CURRENT_USER, L"Software\\MWRegTest", L"str",
                            &sh_type, (void*)shback, &shcb);
    check(src == 0, "SHGetValueW success");
    check(sh_type == REG_SZ && lstrcmpW(shback, L"hello") == 0, "SHGetValueW data");
    DWORD miss = SHGetValueW(HKEY_CURRENT_USER, L"Software\\MWRegTest",
                             L"no_such_value", NULL, NULL, NULL);
    check(miss == 2 /*ERROR_FILE_NOT_FOUND*/, "SHGetValueW honest miss");

    RegCloseKey(hk);

    /* verdict via ExitProcess (stream-free); report string via MessageBoxA
     * so the pump's dialog path records it too */
    char rep[64];
    wsprintfA(rep, "REG-TEST pass=%d fail=%d", g_pass, g_fail);
    MessageBoxA(NULL, rep, "mw_reg_test", MB_OK);
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
