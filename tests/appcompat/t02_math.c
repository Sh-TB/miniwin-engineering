/* APPCOMPAT t02_math.c — integer math / loops / bitwise (MiniWin app-compat sweep)
 * Verifies: 64-bit arithmetic, loops, primes, GCD, bit tricks, integer printf.
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>

static unsigned long long sum_primes_below(int limit) {
    unsigned long long s = 0;
    for (int n = 2; n < limit; n++) {
        int is_prime = 1;
        for (int d = 2; (long long)d * d <= n; d++) {
            if (n % d == 0) { is_prime = 0; break; }
        }
        if (is_prime) s += n;
    }
    return s;
}

static unsigned long long gcd(unsigned long long a, unsigned long long b) {
    while (b) { unsigned long long t = a % b; a = b; b = t; }
    return a;
}

static int popcount64(unsigned long long x) {
    int c = 0;
    while (x) { x &= x - 1; c++; }
    return c;
}

static unsigned long long fib(int n) {
    unsigned long long a = 0, b = 1;
    for (int i = 0; i < n; i++) { unsigned long long t = a + b; a = b; b = t; }
    return a;
}

int main(void) {
    int fails = 0;

    unsigned long long sp = sum_primes_below(1000);
    if (sp != 76127ULL) { printf("FAIL: sum primes<1000 = %llu, want 76127\n", sp); fails++; }
    else printf("sum primes<1000 = %llu OK\n", sp);

    if (gcd(1071, 462) != 21) { printf("FAIL: gcd=%llu\n", gcd(1071,462)); fails++; }
    if (gcd(0, 5) != 5 || gcd(5, 0) != 5) { puts("FAIL: gcd edge"); fails++; }

    if (popcount64(0xFFFFFFFFFFFFFFFFULL) != 64) { puts("FAIL: popcount all-ones"); fails++; }
    if (popcount64(0x8000000000000001ULL) != 2) { puts("FAIL: popcount edges"); fails++; }

    if (fib(50) != 12586269025ULL) { printf("FAIL: fib50=%llu\n", fib(50)); fails++; }
    else printf("fib(50) = %llu OK\n", fib(50));

    /* 64-bit signed multiply / divide */
    long long big = 3037000499LL;
    if (big * big != 9223372030926249001LL) { puts("FAIL: 64-bit mul"); fails++; }
    long long q = (-9223372036854775807LL - 1) / 2; /* INT64_MIN / 2 */
    if (q != -4611686018427387904LL) { puts("FAIL: int64 min div"); fails++; }

    /* shifts */
    unsigned long long sh = 1ULL << 63;
    if (sh != 9223372036854775808ULL) { puts("FAIL: shift63"); fails++; }
    if ((sh >> 63) != 1) { puts("FAIL: shift back"); fails++; }

    /* hex / decimal printing of computed values */
    printf("check: %llu 0x%llX %lld\n", sp, sh, q);
    printf("expect: 76127 0x8000000000000000 -4611686018427387904\n");

    if (fails == 0) { puts("t02: PASS"); return 0; }
    printf("t02: FAIL (%d)\n", fails);
    return 1;
}
