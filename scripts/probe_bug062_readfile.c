/* BUG-062 probe: isolated ReadFile chunk-walk against a large file.
 * Prints progress per chunk so the hang offset is visible on stdout. */
#include <windows.h>
#include <stdio.h>

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "s393216.bin";
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("PROBE-FAIL: CreateFileA(%s) failed\n", path);
        return 2;
    }
    DWORD hi = 0;
    DWORD lo = GetFileSize(h, &hi);
    printf("PROBE: file size lo=%lu hi=%lu\n", (unsigned long)lo, (unsigned long)hi);
    fflush(stdout);

    static unsigned char buf[0x10000];
    DWORD total = 0;
    int it = 0;
    for (;;) {
        DWORD got = 0;
        DWORD off = total;
        OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
        ov.Offset = off; ov.OffsetHigh = 0;
        /* plain positional ReadFile via OVERLAPPED-free path: use SetFilePointer+ReadFile */
        SetFilePointer(h, (LONG)off, NULL, FILE_BEGIN);
        BOOL ok = ReadFile(h, buf, sizeof(buf), &got, NULL);
        if (!ok) {
            printf("PROBE: ReadFile at 0x%lX returned FALSE, GLE=%lu\n",
                   (unsigned long)off, (unsigned long)GetLastError());
            break;
        }
        if (got == 0) { printf("PROBE: EOF at 0x%lX\n", (unsigned long)off); break; }
        total += got;
        if ((it % 2) == 0 || got != sizeof(buf)) {
            printf("PROBE: chunk %d off=0x%lX got=%lu total=0x%lX\n",
                   it, (unsigned long)off, (unsigned long)got, (unsigned long)total);
            fflush(stdout);
        }
        it++;
        if (total > 0x1000000) { printf("PROBE: runtoo long\n"); break; }
    }
    printf("PROBE-DONE total=0x%lX\n", (unsigned long)total);
    fflush(stdout);
    CloseHandle(h);
    return 0;
}
