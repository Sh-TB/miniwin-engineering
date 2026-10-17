/* BUG-062 probe B: msvcrt FILE* fread/fwrite chunk-walk.
 * Separates fread (input) from fwrite (output) paths. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "s393216.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { printf("PROBE-FAIL: fopen %s\n", path); return 2; }
    static unsigned char buf[0x10000];
    unsigned long total = 0; int it = 0;
    for (;;) {
        size_t got = fread(buf, 1, sizeof(buf), f);
        if (got == 0) { printf("FREAD: EOF at 0x%lX (feof=%d)\n", total, feof(f)); break; }
        total += got;
        if ((it % 2) == 0 || got != sizeof(buf)) {
            printf("FREAD: it=%d off=0x%lX got=%lu\n", it, total - got, (unsigned long)got);
            fflush(stdout);
        }
        it++;
        if (total > 0x1000000) break;
    }
    printf("FREAD-DONE total=0x%lX\n", total);
    fclose(f);
    fflush(stdout);

    /* write path: copy what we just read into probe_out.bin via fwrite */
    FILE* g = fopen("probe_out.bin", "wb");
    if (!g) { printf("PROBE-FAIL: fopen out\n"); return 2; }
    unsigned long wtotal = 0;
    for (unsigned long i = 0; i < total; i += sizeof(buf)) {
        size_t n = (total - i < sizeof(buf)) ? (size_t)(total - i) : sizeof(buf);
        size_t w = fwrite(buf + i, 1, n, g);
        if (w != n) { printf("FWRITE: short at 0x%lX\n", i); break; }
        wtotal += w;
        if ((i / sizeof(buf)) % 2 == 0) { printf("FWRITE: off=0x%lX\n", wtotal); fflush(stdout); }
    }
    fclose(g);
    printf("FWRITE-DONE total=0x%lX\n", wtotal);
    fflush(stdout);
    return (total == wtotal) ? 0 : 3;
}
