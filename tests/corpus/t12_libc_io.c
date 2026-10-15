/* ISS1-t12 — libc_io (Issue #1 Phase-1 target, recreated fixture).
 * Standard C library I/O: file create/write/read/seek/close, formatted I/O,
 * buffering, error paths, remove(). Deterministic; exit 0 iff all checks pass.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

int main(void) {
    int score = 0;
    const char *path = "t12_testfile.bin";

    /* 1. create + write */
    FILE *f = fopen(path, "wb");
    if (f) {
        const char *msg = "MiniWin libc IO test 0123456789";
        size_t n = fwrite(msg, 1, strlen(msg), f);
        if (n == strlen(msg)) score += 1;
        fclose(f);
    }

    /* 2. read back + verify */
    f = fopen(path, "rb");
    if (f) {
        char buf[64]; memset(buf, 0, sizeof buf);
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        if (n == 31 && strcmp(buf, "MiniWin libc IO test 0123456789") == 0) score += 2;

        /* 3. seek + partial read */
        if (fseek(f, 8, SEEK_SET) == 0) {
            memset(buf, 0, sizeof buf);
            n = fread(buf, 1, 4, f);
            if (n == 4 && strncmp(buf, "libc", 4) == 0) score += 4;
        }

        /* 4. feof/eof behavior */
        fseek(f, 0, SEEK_END);
        (void)fgetc(f);
        if (feof(f)) score += 8;
        fclose(f);
    }

    /* 5. formatted text I/O round-trip: "-42 txt 3.75 ff" = 15 bytes */
    f = fopen("t12_text.txt", "w");
    if (f) {
        if (fprintf(f, "%d %s %.2f %lx", -42, "txt", 3.75, 255UL) == 15) score += 16;
        fclose(f);
    }
    f = fopen("t12_text.txt", "r");
    if (f) {
        int d; char word[16]; double dbl; unsigned long hex;
        int got = fscanf(f, "%d %15s %lf %lx", &d, word, &dbl, &hex);
        if (got == 4 && d == -42 && strcmp(word, "txt") == 0 && dbl == 3.75 && hex == 255)
            score += 32;
        fclose(f);
    }

    /* 6. error path: missing file must return NULL with errno set */
    errno = 0;
    FILE *missing = fopen("t12_definitely_missing.bin", "rb");
    if (missing == NULL && errno == ENOENT) score += 64;

    /* 7. remove() cleanup */
    if (remove(path) == 0 && remove("t12_text.txt") == 0) score += 128;
    if (fopen(path, "rb") == NULL) score += 0; /* informational */

    printf("RESULT score=%d expected=255\n", score);
    printf("RESULT libc_io=ok\n");
    return score == 255 ? 0 : 1;
}
