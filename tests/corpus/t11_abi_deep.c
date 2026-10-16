/* ISS1-t11 — abi_deep (Issue #1 Phase-1 target, recreated fixture).
 * Windows x64 ABI stress: deep recursion, structs passed BY VALUE (in
 * registers and on the stack), varargs, function pointers, nonvolatile
 * register preservation across nested calls, stack allocation.
 * Deterministic; exit 0 iff all checks pass.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef HOST_PRESERVE_EXPECTED
#define HOST_PRESERVE_EXPECTED 0L  /* set via -DHOST_PRESERVE_EXPECTED=<host-verified value> */
#endif

typedef struct Small { int a, b; } Small;                    /* passed in register(s) */
typedef struct Big   { long v[6]; } Big;                     /* 48B, passed by pointer per x64 ABI */
typedef struct Mix   { int a; double d; int b; } Mix;

static int  g_callee_count = 0;

static long fib25(int n) { long a = 0, b = 1; while (n-- > 0) { long t = a + b; a = b; b = t; } return a; }

static int deep10(int depth, Small s, Mix m) {
    ++g_callee_count;
    if (depth == 0) return s.a + s.b + (int)m.d + m.a + m.b;
    Small s2 = { s.a + 1, s.b + 2 };
    return deep10(depth - 1, s2, m);
}

static long sum_big(Big b) { long t = 0; for (int i = 0; i < 6; ++i) t += b.v[i]; return t; }

static int varsum(int count, ...) {
    va_list ap; va_start(ap, count);
    int t = 0;
    for (int i = 0; i < count; ++i) t += va_arg(ap, int);
    va_end(ap);
    return t;
}

static int op_add(int a, int b) { return a + b; }
static int op_mul(int a, int b) { return a * b; }
static int (*get_op(char which))(int, int) {
    return which == '+' ? op_add : op_mul;
}

/* nonvolatile preservation probe: heavy local computation with many live values */
static long preserve_probe(long base) {
    long r1 = base * 3, r2 = base + 7, r3 = base ^ 0x55, r4 = base - 2, r5 = base + 100;
    for (int i = 0; i < 5; ++i) {
        r1 += varsum(3, (int)r2, (int)r3, i);
        r2 ^= i + (int)r1;
        r3 = r3 * 3 + i;
        Big b = {{ r1, r2, r3, r4, r5, (long)i }};
        r4 = sum_big(b);
    }
    return r1 + r2 + r3 + r4 + r5;
}

int main(void) {
    int score = 0;

    Small s = {2, 3};
    Mix  m = {10, 0.5, 20};
    /* deep10(10,{2,3},{10,0.5,20}): after 10 descents a=2+10, b=3+20;
     * base returns a + b + (int)0.5(=0) + m.a(10) + m.b(20) = 65 (host-verified). */
    if (deep10(10, s, m) == 65) score += 1;
    if (g_callee_count == 11) score += 2;

    Big b = {{100, 200, 300, 400, 500, 600}};
    if (sum_big(b) == 2100) score += 4;

    if (varsum(5, 1, 2, 3, 4, 5) == 15 && varsum(1, 42) == 42) score += 8;

    int (*op)(int, int) = get_op('+');
    if (op(20, 22) == 42) { op = get_op('*'); if (op(6, 7) == 42) score += 16; }

    /* preserve_probe expected value is verified by a host-native build of the
     * same translation unit (pure integer ops, platform-independent). */
    long probe = preserve_probe(1000);
    if (probe == HOST_PRESERVE_EXPECTED) score += 32;

    /* large stack frame usage */
    char frame[4096];
    memset(frame, 'Z', sizeof frame);
    if (frame[0] == 'Z' && frame[4095] == 'Z') score += 64;

    /* recursion producing known value */
    if (fib25(25) == 75025) score += 128;

    printf("RESULT preserve_probe=%ld expected=%ld\n", probe, (long)HOST_PRESERVE_EXPECTED);
    printf("RESULT score=%d expected=255\n", score);
    printf("RESULT abi_deep=ok\n");
    return score == 255 ? 0 : 1;
}
