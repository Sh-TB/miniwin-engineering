/* ISS1-t09 — recon_c_smoke (Issue #1 Phase-1 target).
 * NOTE: the historical "t09" identifier appears in prior reports WITHOUT a
 * surviving source or spec (MISSING upstream). Per Issue #1 §11.3 we do NOT
 * claim continuity with the historical t09; this is a NEW, clearly-labelled
 * C smoke fixture covering the same engineering family (CRT init, printf
 * formatting, control flow, pointers). Deterministic; exit 0 iff all pass.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Point { int x, y; };

static int  add3(int a, int b, int c) { return a + b + c; }
static long fib(int n) { long a = 0, b = 1; while (n-- > 0) { long t = a + b; a = b; b = t; } return a; }
static int  str_checks(void) {
    char buf[64];
    if (snprintf(buf, sizeof buf, "%s-%d-%c", "win", 64, 'x') != 8) return 0;
    return strcmp(buf, "win-64-x") == 0;
}

int main(void) {
    int score = 0;
    if (add3(20, 21, 1) == 42) score += 1;
    if (fib(30) == 832040L) score += 2;
    if (str_checks()) score += 4;

    struct Point p = {3, 4};
    struct Point *q = &p;
    if (q->x * q->x + q->y * q->y == 25) score += 8;

    int arr[10];
    for (int i = 0; i < 10; ++i) arr[i] = i * i;
    int sum = 0; for (int i = 0; i < 10; ++i) sum += arr[i];   /* 285 */
    if (sum == 285) score += 16;

    char *heap = (char*)malloc(256);
    if (heap) { memset(heap, 'A', 255); heap[255] = 0; if (heap[128] == 'A') score += 32; free(heap); }

    void *mem = calloc(16, 16);
    if (mem && ((char*)mem)[0] == 0 && ((char*)mem)[255] == 0) score += 64;
    free(mem);

    printf("RESULT score=%d expected=127\n", score);
    printf("RESULT c_smoke=ok\n");
    return score == 127 ? 0 : 1;
}
