/* ISS1-t10 — float_math (Issue #1 Phase-1 target, recreated fixture).
 * Floating-point semantics: single/double arithmetic, conversion, sqrt/sin,
 * printf %f/%e formatting. Deterministic; exit 0 iff all checks pass.
 */
#include <stdio.h>
#include <math.h>

static int nearly_equal(double a, double b, double eps) { return fabs(a - b) <= eps; }

int main(void) {
    int score = 0;
    volatile float  f32 = 0.1f;
    volatile double f64 = 0.1;
    if (nearly_equal((double)f32, 0.10000000149011612, 1e-12)) score += 1;  /* exact f32 value of 0.1f */
    if (nearly_equal(f64 * 10.0, 1.0, 1e-15)) score += 2;

    double pi = acos(-1.0);
    if (nearly_equal(pi, 3.14159265358979323846, 1e-15)) score += 4;
    if (nearly_equal(sin(pi / 2.0), 1.0, 1e-15) && nearly_equal(cos(pi), -1.0, 1e-15)) score += 8;

    if (nearly_equal(sqrt(2.0), 1.41421356237309504880, 1e-15)) score += 16;

    /* float->int rounding semantics (truncation toward zero) */
    if ((int)(2.7) == 2 && (int)(-2.7) == -2) score += 32;

    /* FP accumulation check: 0.1 added 10 times != exactly 1.0 in binary FP */
    double acc = 0.0; for (int i = 0; i < 10; ++i) acc += 0.1;
    if (acc != 1.0 && nearly_equal(acc, 1.0, 1e-9)) score += 64;

    /* long double basic */
    volatile long double ld = 1.0L / 3.0L;
    if ((double)ld > 0.3333333 && (double)ld < 0.3333334) score += 128;

    printf("RESULT score=%d expected=255\n", score);
    printf("RESULT sqrt2=%.9f pi=%.7f acc=%.17f\n", sqrt(2.0), pi, acc);
    return score == 255 ? 0 : 1;
}
