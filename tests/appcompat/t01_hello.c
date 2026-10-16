/* APPCOMPAT t01_hello.c — basic stdio smoke test (MiniWin app-compat sweep)
 * Verifies: puts, printf with common format specifiers, fflush, exit code.
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>

int main(void) {
    int fails = 0;

    puts("t01: HELLO-FROM-MINIWIN");
    printf("plain: no specifier\n");
    printf("int=%d u=%u neg=%d\n", 42, 4000000000u, -7);
    printf("hex=0x%08X oct=%o\n", 0xDEADBEEF, 0644);
    printf("str=%s char=%c pct=%%\n", "miniwin", 'Z');
    printf("long=%ld llong=%lld\n", 123456789L, -1234567890123LL);
    printf("width=[%5d] pad=[%-6s] zero=[%05d]\n", 99, "ab", 42);
    printf("float=%.2f sci=%.3e g=%g\n", 3.14159, 12345.6789, 0.0001);
    printf("ptr=%p\n", (void*)main);

    /* Basic sanity of returned values from printf ("count-me\n" = 9 bytes) */
    int n = printf("count-me\n");
    if (n != 9) { printf("FAIL: printf returned %d, expected 9\n", n); fails++; }

    if (fflush(stdout) != 0) { printf("FAIL: fflush\n"); fails++; }

    if (fails == 0) { puts("t01: PASS"); return 0; }
    printf("t01: FAIL (%d)\n", fails);
    return 1;
}
