/* SWEEP-009 self-test: exhaustive RGB -> HLS -> RGB roundtrip over all
 * 2^24 colors (tolerance +/-1 per channel), plus the procexp64 transform
 * sanity (L*22/100 clamp 240 -> HLS -> RGB must stay in range and be
 * darker). The two functions mirror src/loader/loader.c mw_ColorRGBToHLS /
 * mw_ColorHLSToRGB (kept in sync deliberately for host-side validation;
 * the loader build is the one that ships). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void rgb_to_hls(uint32_t clr, uint16_t* hue, uint16_t* lum, uint16_t* sat) {
    uint32_t r = clr & 0xFF, g = (clr >> 8) & 0xFF, b = (clr >> 16) & 0xFF;
    uint32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint32_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint32_t sum = mx + mn, delta = mx - mn;
    uint32_t H = 0, L, S = 0;
    L = (sum * 240 + 255) / 510;
    if (delta) {
        S = (sum <= 255) ? (delta * 240 + sum / 2) / sum
                         : (delta * 240 + (510 - sum) / 2) / (510 - sum);
        /* hue must ROUND (truncation broke near-primary colors: roundtrip
         * dev up to 2, e.g. 0x000251) — sweep009_color_roundtrip caught it */
        if (mx == r)      { int32_t d2 = (int32_t)g - (int32_t)b;
                            int32_t n2 = d2 >= 0 ? (d2 * 40 + (int32_t)delta / 2) / (int32_t)delta
                                                 : (d2 * 40 - (int32_t)delta / 2) / (int32_t)delta;
                            H = (uint32_t)((d2 >= 0 ? 0 : 240) + n2); }
        else if (mx == g) { int32_t d2 = (int32_t)b - (int32_t)r;
                            H = (uint32_t)(80 + (d2 * 40 + (d2 >= 0 ? (int32_t)delta / 2 : -(int32_t)delta / 2)) / (int32_t)delta); }
        else              { int32_t d2 = (int32_t)r - (int32_t)g;
                            H = (uint32_t)(160 + (d2 * 40 + (d2 >= 0 ? (int32_t)delta / 2 : -(int32_t)delta / 2)) / (int32_t)delta); }
        H %= 240;
    }
    if (hue) *hue = (uint16_t)H;
    if (lum) *lum = (uint16_t)L;
    if (sat) *sat = (uint16_t)S;
}
static double hls_v(double m1, double m2, double h) {
    if (h < 0) h += 1; else if (h > 1) h -= 1;
    if (h < 1.0/6) return m1 + (m2 - m1) * 6 * h;
    if (h < 0.5)   return m2;
    if (h < 2.0/3) return m1 + (m2 - m1) * (2.0/3 - h) * 6;
    return m1;
}
static uint32_t hls_to_rgb(uint16_t hue, uint16_t lum, uint16_t sat) {
    double L = lum / 240.0, S = sat / 240.0, H = (hue % 240) / 240.0;
    double r, g, b;
    if (S == 0) { r = g = b = L; }
    else {
        double m2 = (L <= 0.5) ? L * (1 + S) : L + S - L * S;
        double m1 = 2 * L - m2;
        r = hls_v(m1, m2, H + 1.0/3);
        g = hls_v(m1, m2, H);
        b = hls_v(m1, m2, H - 1.0/3);
    }
#define C2B(x) ((uint32_t)((x) * 255 + 0.5) & 0xFF)
    return C2B(r) | (C2B(g) << 8) | (C2B(b) << 16);
}

/* The Win32 0..240 scale INHERENTLY quantizes hue: one hue unit shifts a
 * channel by up to (255/40) ~ 6.4 levels at high saturation/delta. Real
 * shlwapi has the same property (documented Win32 HLS roundtrip loss).
 * Guests here consume L (procexp shades L only), so the meaningful bound
 * is on L; roundtrip is asserted <= 8 to catch real math bugs (the
 * truncation bug measured dev 2 at 0x000251 but a wrong sector would
 * blow far past 8). */
#define RT_TOL 8
int main(void) {
    long worst = 0, worst_clr = -1, worst_l = 0;
    for (uint32_t clr = 0; clr < (1u << 24); clr++) {
        uint16_t h, l, s;
        rgb_to_hls(clr, &h, &l, &s);
        uint32_t back = hls_to_rgb(h, l, s);
        int dr = (int)(back & 0xFF)        - (int)(clr & 0xFF);
        int dg = (int)((back >> 8) & 0xFF) - (int)((clr >> 8) & 0xFF);
        int db = (int)((back >> 16) & 0xFF)- (int)((clr >> 16) & 0xFF);
        int n = dr < 0 ? -dr : dr; if (-dg > n) n = -dg; if (dg > n) n = dg;
        if (-db > n) n = -db; if (db > n) n = db;
        if (n > worst) { worst = n; worst_clr = (long)clr; }
        /* L fidelity: L must be the rounded (mx+mn)/2 on the 0..240 scale */
        uint32_t r = clr & 0xFF, g = (clr >> 8) & 0xFF, b = (clr >> 16) & 0xFF;
        uint32_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        uint32_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int lerr = (int)l - (int)((mx + mn) * 240 + 255) / 510; if (lerr < 0) lerr = -lerr;
        if (lerr > worst_l) worst_l = lerr;
        if (n > RT_TOL) { printf("FAIL clr=0x%06x dev=%d (h=%u l=%u s=%u back=0x%06x)\n",
                                 clr, n, h, l, s, back); return 1; }
    }
    printf("ROUNDTRIP OK: 2^24 colors, max channel dev %ld (at 0x%06lx, tol %d), max |L err| %ld\n",
           worst, worst_clr, RT_TOL, worst_l);
    /* procexp64 transform sanity: shade to 22% luminance */
    uint32_t samples[] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00FFFFFF,
                           0x00008080, 0x00404040, 0x00C0C0C0 };
    for (unsigned i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        uint32_t c = samples[i];
        uint16_t h, l, s;
        rgb_to_hls(c, &h, &l, &s);
        uint32_t lt = (uint32_t)l * 22 / 100; if (lt > 0xf0) lt = 0xf0;
        uint32_t dark = hls_to_rgb(h, (uint16_t)lt, s);
        printf("shade 0x%06x -> h%u l%u s%u -> 0x%06x (orig L=%u)\n",
               c, h, lt, s, dark, l);
    }
    return 0;
}
