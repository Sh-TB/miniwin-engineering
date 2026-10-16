/* APPCOMPAT t03_mem.c — heap management (MiniWin app-compat sweep)
 * Verifies: malloc/calloc/realloc/free, pattern integrity, many allocations.
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    int fails = 0;

    /* 1: malloc + pattern */
    unsigned char* p = (unsigned char*)malloc(4096);
    if (!p) { puts("FAIL: malloc 4096"); return 1; }
    for (int i = 0; i < 4096; i++) p[i] = (unsigned char)(i * 7 + 3);
    for (int i = 0; i < 4096; i++)
        if (p[i] != (unsigned char)(i * 7 + 3)) { puts("FAIL: pattern after malloc"); fails++; break; }
    free(p);

    /* 2: calloc zeroing */
    unsigned char* c = (unsigned char*)calloc(100, 64); /* 6400 bytes */
    if (!c) { puts("FAIL: calloc"); fails++; }
    else {
        int zeroed = 1;
        for (int i = 0; i < 6400; i++) if (c[i]) { zeroed = 0; break; }
        if (!zeroed) { puts("FAIL: calloc not zeroed"); fails++; }
        free(c);
    }

    /* 3: realloc growth chain 1k->4k->16k->64k */
    char* r = (char*)malloc(1024);
    if (!r) { puts("FAIL: malloc 1k"); return 1; }
    memset(r, 0xAB, 1024);
    size_t sizes[3] = {4096, 16384, 65536};
    for (int i = 0; i < 3; i++) {
        char* nr = (char*)realloc(r, sizes[i]);
        if (!nr) { printf("FAIL: realloc %zu\n", sizes[i]); fails++; free(r); r = NULL; break; }
        r = nr;
        int intact = 1;
        for (size_t j = 0; j < 1024; j++) if ((unsigned char)r[j] != 0xAB) { intact = 0; break; }
        if (!intact) { puts("FAIL: realloc did not preserve data"); fails++; }
    }
    if (r) { free(r); }

    /* 4: many small allocations (stress loop) */
    void* slots[64];
    for (int i = 0; i < 64; i++) {
        slots[i] = malloc((size_t)(16 + i * 32));
        if (!slots[i]) { printf("FAIL: alloc %d\n", i); fails++; }
    }
    for (int i = 0; i < 64; i++) if (slots[i]) free(slots[i]);

    /* 5: large single allocation (1 MiB) */
    void* big = malloc(1024 * 1024);
    if (!big) { puts("FAIL: malloc 1MiB"); fails++; }
    else {
        memset(big, 0x5A, 1024 * 1024);
        unsigned char* b = (unsigned char*)big;
        if (b[0] != 0x5A || b[1023] != 0x5A || b[1048575] != 0x5A) { puts("FAIL: 1MiB pattern"); fails++; }
        free(big);
    }

    if (fails == 0) { puts("t03: PASS"); return 0; }
    printf("t03: FAIL (%d)\n", fails);
    return 1;
}
