/* APPCOMPAT t05_file.c — low-level file I/O (MiniWin app-compat sweep)
 * Verifies: _open/_write/_read/_lseeki64/_close/_unlink, flag mapping, seek.
 * NOTE: avoids _O_APPEND on purpose (loader maps 0x0080=_O_NOINHERIT to
 * O_APPEND — recorded as finding APP-F-01 during this sweep).
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <string.h>

#define O_WRONLY_CRT 0x0001
#define O_RDWR_CRT   0x0002
#define O_CREAT_CRT  0x0100
#define O_TRUNC_CRT  0x0200

/* CRT low-level file API comes from <io.h> (dllimport declarations). */

int main(void) {
    int fails = 0;
    const char* path = "t05_data.bin";

    /* 1: create + write 256-byte pattern (O_RDWR so we can seek+read back) */
    unsigned char wbuf[256];
    for (int i = 0; i < 256; i++) wbuf[i] = (unsigned char)i;
    int fd = _open(path, O_RDWR_CRT | O_CREAT_CRT | O_TRUNC_CRT, 0644);
    if (fd < 0) { puts("FAIL: _open create"); return 1; }
    int w = _write(fd, wbuf, 256);
    if (w != 256) { printf("FAIL: _write returned %d\n", w); fails++; }

    /* 2: seek to 100, read 56, expect bytes 100..155 */
    long long pos = _lseeki64(fd, 100, 0 /* SEEK_SET */);
    if (pos != 100) { printf("FAIL: lseek set -> %lld\n", pos); fails++; }
    unsigned char rbuf[64];
    int r = _read(fd, rbuf, 56);
    if (r != 56) { printf("FAIL: _read returned %d\n", r); fails++; }
    else for (int i = 0; i < 56; i++)
        if (rbuf[i] != (unsigned char)(100 + i)) { puts("FAIL: read pattern"); fails++; break; }

    /* 3: seek from end */
    long long epos = _lseeki64(fd, -16, 2 /* SEEK_END */);
    if (epos != 240) { printf("FAIL: lseek end -> %lld\n", epos); fails++; }
    r = _read(fd, rbuf, 100); /* only 16 remain */
    if (r != 16) { printf("FAIL: read near EOF -> %d\n", r); fails++; }

    _close(fd);

    /* 4: reopen read-only, verify full content */
    fd = _open(path, 0 /* O_RDONLY */);
    if (fd < 0) { puts("FAIL: _open ro"); fails++; }
    else {
        unsigned char vbuf[256];
        r = _read(fd, vbuf, 300); /* request more than size */
        if (r != 256) { printf("FAIL: ro read -> %d\n", r); fails++; }
        else for (int i = 0; i < 256; i++)
            if (vbuf[i] != (unsigned char)i) { puts("FAIL: ro pattern"); fails++; break; }
        _close(fd);
    }

    /* 5: unlink */
    if (_unlink(path) != 0) { puts("FAIL: _unlink"); fails++; }

    if (fails == 0) { puts("t05: PASS"); return 0; }
    printf("t05: FAIL (%d)\n", fails);
    return 1;
}
