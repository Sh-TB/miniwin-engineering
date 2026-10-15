/* t10_float_math.c — MiniWin appcompat: floating point + varargs FP classification.
 * Stresses: printf %f/%e/%g (float args travel in the MS xmm slot area and must be
 * repacked into a SysV va_list by the loader's fmt-aware printf shim), double math,
 * math.h basics. Deterministic PASS/FAIL self-checks, exit 0 = PASS. */
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", #cond, __LINE__); fails++; } \
} while (0)

static double scale(double x, double f) { return x * f; }

static float avg3(float a, float b, float c) { return (a + b + c) / 3.0f; }

struct Rect { double w, h; };
static double area(struct Rect r) { return r.w * r.h; }

static double mixed(int n, double d, const char *s, float f) {
    return n + d + f + (double)strlen(s);
}

int main(void) {
    /* integer printing sanity */
    printf("t10 start\n");
    CHECK(fabs(scale(2.5, 4.0) - 10.0) < 1e-9);

    /* float args in varargs: printf repacking path */
    double d = scale(1.5, 2.0);
    printf("d=%.2f\n", d);
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%.3f", 3.14159);
        CHECK(strcmp(buf, "3.142") == 0);
    }
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%5.1f", 2.25);
        CHECK(strcmp(buf, "  2.2") == 0); /* round-half-even at 2.25 -> 2.2 */
    }
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%e", 12345.678);
        CHECK(strncmp(buf, "1.234568e+04", 12) == 0);
    }
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%g", 0.0001);
        CHECK(strcmp(buf, "0.0001") == 0);
    }

    CHECK(fabs(avg3(1.0f, 2.0f, 3.0f) - 2.0f) < 1e-6);

    struct Rect r; r.w = 2.5; r.h = 4.0;
    CHECK(fabs(area(r) - 10.0) < 1e-9);

    CHECK(fabs(mixed(1, 2.5, "ab", 0.5f) - 6.0) < 1e-9);

    /* math.h */
    CHECK(fabs(sqrt(2.0) - 1.4142135623730951) < 1e-12);
    CHECK(fabs(pow(2.0, 10.0) - 1024.0) < 1e-9);
    CHECK(fabs(floor(-1.5) - (-2.0)) < 1e-9);
    CHECK(fabs(fmod(7.0, 3.0) - 1.0) < 1e-9);
    CHECK(fabs(sin(0.0)) < 1e-12);
    CHECK(fabs(cos(0.0) - 1.0) < 1e-12);

    /* double formatting round trip via sscanf */
    {
        double back = 0;
        int n = sscanf("12.75", "%lf", &back);
        CHECK(n == 1);
        CHECK(fabs(back - 12.75) < 1e-12);
    }

    if (fails == 0) { printf("t10: PASS\n"); return 0; }
    printf("t10: %d FAILURES\n", fails);
    return 1;
}
