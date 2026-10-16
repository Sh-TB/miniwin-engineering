/* APPCOMPAT t13_wide_abi.c — guest wide-string ABI smoke test (BUG-050)
 * Verifies the 2-byte UTF-16LE guest wchar_t contract end-to-end:
 *   1. wcs family (2-byte strides): len/cpy/cat/chr/rchr/str/cmp/ncmp
 *   2. GetModuleFileNameW returns a UTF-16 path containing ".exe"
 *   3. GetEnvironmentStringsW block parses as UTF-16 (find "PATH=")
 *   4. CreateFileW + WriteFile + CloseHandle + GetFileAttributesW + DeleteFileW
 *   5. MultiByteToWideChar/WideCharToMultiByte cp=65001 round trip (non-ASCII)
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int fails = 0;
static void ck(int cond, const char* what) {
    if (!cond) { printf("FAIL: %s\n", what); fails++; }
}

int main(void) {
    /* 1. wide string family — literal L"" is 2-byte UTF-16 in the guest */
    wchar_t buf[64];
    wcscpy(buf, L"mini");
    wcscat(buf, L"win");
    ck(wcslen(buf) == 7, "wcslen 7");
    ck(wcscmp(buf, L"miniwin") == 0, "wcscmp miniwin");
    ck(wcsncmp(buf, L"minimax", 4) == 0, "wcsncmp prefix min");
    ck(wcschr(buf, L'i') == buf + 1, "wcschr first i at +1");
    ck(wcsrchr(buf, L'i') == buf + 5, "wcsrchr last i at +5");
    ck(wcsstr(buf, L"niw") == buf + 2, "wcsstr niw at +2");
    ck(wcsstr(buf, L"xyz") == NULL, "wcsstr miss");
    ck(wcschr(buf, L'z') == NULL, "wcschr miss");

    /* 2. GetModuleFileNameW — UTF-16 path with .exe suffix */
    wchar_t exe[512];
    DWORD n = GetModuleFileNameW(NULL, exe, 512);
    ck(n > 0 && n < 512, "GetModuleFileNameW len");
    ck(wcsstr(exe, L".exe") != NULL, "module name has .exe");

    /* 3. environment block parses as UTF-16 */
    const wchar_t* env = (const wchar_t*)GetEnvironmentStringsW();
    ck(env != NULL, "GetEnvironmentStringsW");
    int saw_path = 0, saw_upper = 0;
    for (const wchar_t* p = env; *p; ) {
        if (wcsncmp(p, L"PATH=", 5) == 0) saw_path = 1;
        if (wcsncmp(p, L"MINWIN", 6) == 0) saw_upper = 1;
        while (*p) p++;
        p++;
    }
    ck(saw_path || saw_upper, "env block has PATH= or MINWIN*");

    /* 4. W file API round trip */
    const wchar_t* fname = L"t13_w16test.bin";
    HANDLE h = CreateFileW(fname, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    ck(h != INVALID_HANDLE_VALUE, "CreateFileW create");
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        const char* payload = "wide-abi-ok";
        BOOL ok = WriteFile(h, payload, 11, &wrote, NULL);
        ck(ok && wrote == 11, "WriteFile 11 bytes");
        CloseHandle(h);
    }
    DWORD attr = GetFileAttributesW(fname);
    ck(attr != INVALID_FILE_ATTRIBUTES, "GetFileAttributesW sees file");
    ck(DeleteFileW(fname), "DeleteFileW removes file");
    ck(GetFileAttributesW(fname) == INVALID_FILE_ATTRIBUTES, "file gone");

    /* 5. conversion round trip with non-ASCII (U+00E9 = e-acute) */
    char mbs_in[16] = { 'a', (char)0xC3, (char)0xA9, 'z', 0 }; /* "aéz" UTF-8 */
    wchar_t wbuf[16];
    int wn = MultiByteToWideChar(65001, 0, mbs_in, -1, wbuf, 16);
    ck(wn == 4, "MB2WC units a,e-acute,z,NUL");
    ck(wbuf[0] == L'a' && wbuf[1] == 0x00E9 && wbuf[2] == L'z' && wbuf[3] == 0,
       "MB2WC values");
    char mbs_out[16];
    int bn = WideCharToMultiByte(65001, 0, wbuf, -1, mbs_out, 16, NULL, NULL);
    ck(bn == 5, "WC2MB bytes a,C3,A9,z,NUL");
    ck(memcmp(mbs_in, mbs_out, 5) == 0, "round trip identical");

    if (fails == 0) { puts("t13: PASS"); return 0; }
    printf("t13: FAIL (%d)\n", fails);
    return 1;
}
