/* APPCOMPAT t14_file_read.c — CreateFileW(READ) read-back probe (7za extract debug)
 * Replicates 7-Zip's archive-open path: CreateFileW GENERIC_READ +
 * share RW, GetFileSizeEx, SetFilePointer, ReadFile of the 7z signature. */
#include <stdio.h>
#include <windows.h>

int main(int argc, char** argv) {
    const char* name8 = (argc > 1) ? argv[1] : "test.7z";
    wchar_t wname[512];
    int n = MultiByteToWideChar(65001, 0, name8, -1, wname, 512);
    if (n <= 0) { printf("t14: FAIL MB2WC\n"); return 1; }

    HANDLE h = CreateFileW(wname, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("t14: FAIL CreateFileW err=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { printf("t14: FAIL GetFileSizeEx err=%lu\n", (unsigned long)GetLastError()); return 1; }
    unsigned char sig[6];
    DWORD got = 0;
    if (!ReadFile(h, sig, 6, &got, NULL)) { printf("t14: FAIL ReadFile err=%lu\n", (unsigned long)GetLastError()); return 1; }
    printf("t14: size=%lld got=%u sig=%02X %02X %02X %02X %02X %02X\n",
           (long long)sz.QuadPart, got, sig[0], sig[1], sig[2], sig[3], sig[4], sig[5]);
    CloseHandle(h);
    /* 7z signature: 37 7A BC AF 27 1C */
    if (sz.QuadPart > 0 && got == 6 && sig[0] == 0x37 && sig[1] == 0x7A) {
        puts("t14: PASS"); return 0;
    }
    puts("t14: FAIL signature"); return 1;
}
