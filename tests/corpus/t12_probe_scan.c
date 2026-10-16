/* ISS1-t12-probe — fscanf behavior probe */
#include <stdio.h>
int strcmp_(const char* a, const char* b);
int main(void) {
    FILE *f = fopen("t12_probe.txt", "w");
    if (!f) { printf("PROBE fopen_w failed\n"); return 2; }
    fprintf(f, "%d %s %.2f %lx", -42, "txt", 3.75, 255UL);
    fclose(f);

    f = fopen("t12_probe.txt", "r");
    if (!f) { printf("PROBE fopen_r failed\n"); return 2; }
    int d = 0; char word[16] = {0}; double dbl = 0; unsigned long hex = 0;
    int got = fscanf(f, "%d %15s %lf %lx", &d, word, &dbl, &hex);
    printf("PROBE fscanf got=%d d=%d word=[%s] dbl=%.2f hex=%lu\n", got, d, word, dbl, hex);
    fclose(f);
    remove("t12_probe.txt");
    return (got == 4 && d == -42 && !strcmp_(word, "txt") && dbl == 3.75 && hex == 255) ? 0 : 1;
}
/* avoid pulling strcmp via header confusion in probe: tiny local impl */
int strcmp_(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
