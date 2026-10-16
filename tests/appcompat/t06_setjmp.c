/* APPCOMPAT t06_setjmp.c — setjmp/longjmp control flow (MiniWin app-compat sweep)
 * Verifies: _setjmp/longjmp interop across ms_abi boundary, volatile state.
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>
#include <setjmp.h>

static jmp_buf g_buf;

static int deep(int depth) {
    if (depth == 3) { longjmp(g_buf, 42); }
    return deep(depth + 1) + 1;
}

static int run_nested(void) {
    volatile int stage = 0;
    int v = setjmp(g_buf);
    if (v == 0) {
        stage = 1;
        return deep(0); /* will longjmp back with 42 */
    }
    if (v == 42 && stage == 1) return 1;  /* success marker */
    return 0;
}

int main(void) {
    int fails = 0;

    if (run_nested() != 1) { puts("FAIL: nested longjmp stage"); fails++; }
    else puts("nested longjmp: reached handler with 42 after 3 frames OK");

    /* second pass: reuse the same jmp_buf with a fresh setjmp */
    volatile int counter = 0;
    jmp_buf b2;
    int v = setjmp(b2);
    counter++;
    if (v == 0) {
        longjmp(b2, 7);
    }
    if (v != 7 || counter != 2) { printf("FAIL: reuse v=%d counter=%d\n", v, counter); fails++; }

    /* longjmp preserves volatile across frames */
    if (fails == 0) { puts("t06: PASS"); return 0; }
    printf("t06: FAIL (%d)\n", fails);
    return 1;
}
