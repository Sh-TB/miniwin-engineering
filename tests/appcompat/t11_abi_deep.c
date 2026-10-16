/* t11_abi_deep.c — MiniWin appcompat: ABI stress — structs by value, recursion,
 * function pointers, mixed integer arg counts (register + stack args).
 * Deterministic PASS/FAIL self-checks, exit 0 = PASS. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", #cond, __LINE__); fails++; } \
} while (0)

struct P32 { int x, y; };
struct P64 { long long a, b; };
struct Big { long long a, b, c, d; };  /* > 16 bytes: memory class on SysV */

static int sum32(struct P32 p) { return p.x + p.y; }
static long long sum64(struct P64 p) { return p.a + p.b; }
static long long sumBig(struct Big b) { return b.a + b.b + b.c + b.d; }

/* mixed integer/pointer args: exercises register assignment + shadow space */
static long long mix7(int a, int b, int c, long long d, const char *s,
                      void *p, unsigned e) {
    return a + b + c + d + (long long)strlen(s) + (p != 0) + e;
}

/* deep recursion */
static long long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
static long long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

/* function pointers through tables */
typedef long long (*binop)(long long, long long);
static long long add_ll(long long a, long long b) { return a + b; }
static long long mul_ll(long long a, long long b) { return a * b; }
static long long apply_n(binop op, long long v, int n) {
    long long r = v;
    while (n-- > 0) r = op(r, v);
    return r;
}

/* variadic mix */
static long long sum_varargs(int n, ...) {
    /* use stdarg properly */
    return 0; /* replaced below */
}
#include <stdarg.h>
static long long sum_va(int n, ...) {
    va_list ap; va_start(ap, n);
    long long s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(void) {
    printf("t11 start\n");
    struct P32 p = {3, 4};
    CHECK(sum32(p) == 7);
    struct P64 q = {1000000000LL, 2345678LL};
    CHECK(sum64(q) == 1002345678LL);
    struct Big b = {1, 2, 3, 4};
    CHECK(sumBig(b) == 10);

    CHECK(mix7(1, 2, 3, 4LL, "abc", (void *)1, 5) == 1 + 2 + 3 + 4 + 3 + 1 + 5);
    CHECK(mix7(0, 0, 0, 0, "", 0, 0) == 0);

    CHECK(fact(10) == 3628800LL);
    CHECK(fact(20) == 2432902008176640000LL);
    CHECK(fib(25) == 75025LL);

    binop ops[2] = {add_ll, mul_ll};
    CHECK(apply_n(ops[0], 2, 10) == 22);   /* 2+2 ten times */
    CHECK(ops[1](6, 7) == 42);
    CHECK(apply_n(ops[1], 2, 3) == 16);    /* ((2*2)*2)*2 */

    CHECK(sum_va(5, 1, 2, 3, 4, 5) == 15);
    CHECK(sum_va(1, 100) == 100);
    CHECK(sum_va(0) == 0);

    /* qsort through the loader's glibc bridge */
    {
        int arr[10] = {9, 3, 7, 1, 8, 2, 6, 4, 5, 0};
        qsort(arr, 10, sizeof(int), cmp_int);
        int ok = 1;
        for (int i = 0; i < 10; i++) if (arr[i] != i) ok = 0;
        CHECK(ok);
    }

    /* string funcs */
    {
        char buf[32];
        strcpy(buf, "hello");
        strcat(buf, "-world");
        CHECK(strcmp(buf, "hello-world") == 0);
        CHECK(strlen(buf) == 11);
        CHECK(strchr(buf, 'w') != NULL);
        CHECK(strncmp(buf, "hello-worX", 8) == 0);
    }

    if (fails == 0) { printf("t11: PASS\n"); return 0; }
    printf("t11: %d FAILURES\n", fails);
    return 1;
}
