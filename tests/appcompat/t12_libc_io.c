/* t12_libc_io.c — MiniWin appcompat: broader libc — heap churn, file IO, time,
 * locale-lite formatting, atexit ordering.
 * Deterministic PASS/FAIL self-checks, exit 0 = PASS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", #cond, __LINE__); fails++; } \
} while (0)

static int order[8], order_n = 0;
static void note(int v) { if (order_n < 8) order[order_n++] = v; }
static void h1(void) { note(1);
    /* runs LAST (LIFO): the full order must be 3,2,1 */
    if (order[0] == 3 && order[1] == 2 && order[2] == 1)
        printf("t12 exit: PASS\n");
    else
        printf("t12 exit: FAIL order=%d,%d,%d\n",
               order[0], order[1], order[2]);
}
static void h2(void) { note(2); }
static void h3(void) { note(3); }

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int main(void) {
    printf("t12 start\n");

    /* heap churn */
    {
        char *ptrs[32];
        for (int i = 0; i < 32; i++) {
            size_t sz = 8 * (i + 1);
            ptrs[i] = (char *)malloc(sz);
            CHECK(ptrs[i] != NULL);
            memset(ptrs[i], i & 0xff, sz);
        }
        for (int i = 0; i < 32; i += 2) free(ptrs[i]);
        for (int i = 1; i < 32; i += 2) {
            CHECK(ptrs[i][0] == (char)(i & 0xff));
            free(ptrs[i]);
        }
        void *big = malloc(1 << 20);
        CHECK(big != NULL);
        free(big);
        int *zi = calloc(100, sizeof(int));
        CHECK(zi != NULL);
        int zok = 1;
        for (int i = 0; i < 100; i++) if (zi[i]) zok = 0;
        CHECK(zok);
        zi = realloc(zi, 400 * sizeof(int));
        CHECK(zi != NULL);
        zi[399] = 7;
        CHECK(zi[399] == 7);
        free(zi);
    }

    /* file IO */
    {
        const char *path = "t12_tmp.dat";
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        const char *msg = "line1\nline2\nline3\n";
        size_t w = fwrite(msg, 1, strlen(msg), f);
        CHECK(w == strlen(msg));
        fclose(f);
        f = fopen(path, "rb");
        CHECK(f != NULL);
        char buf[64];
        size_t r = fread(buf, 1, sizeof buf, f);
        CHECK(r == strlen(msg));
        CHECK(memcmp(buf, msg, r) == 0);
        CHECK(fgetc(f) == EOF);
        rewind(f);
        char *line = fgets(buf, sizeof buf, f);
        CHECK(line != NULL);
        CHECK(strcmp(buf, "line1\n") == 0);
        fclose(f);
        remove(path);
    }

    /* qsort with string pointers */
    {
        const char *words[] = {"pear", "apple", "orange", "banana"};
        qsort(words, 4, sizeof(const char *), cmp_str);
        CHECK(strcmp(words[0], "apple") == 0);
        CHECK(strcmp(words[1], "banana") == 0);
        CHECK(strcmp(words[2], "orange") == 0);
        CHECK(strcmp(words[3], "pear") == 0);
    }

    /* time: clock() advances, time() sane-ish */
    {
        clock_t c0 = clock();
        volatile double s = 0;
        for (int i = 0; i < 100000; i++) s += i * 1.5;
        clock_t c1 = clock();
        CHECK(c1 >= c0);
        time_t t = time(NULL);
        CHECK(t > 1600000000); /* after 2020 */
        struct tm *tmv = gmtime(&t);
        CHECK(tmv != NULL);
        CHECK(tmv->tm_year >= 120);
        char buf[32];
        size_t n = strftime(buf, sizeof buf, "%Y", tmv);
        CHECK(n == 4);
        CHECK(atoi(buf) >= 2020);
    }

    /* atexit LIFO ordering (check happens in h1, which runs last) */
    {
        atexit(h1);
        atexit(h2);
        atexit(h3);
    }

    if (fails == 0) { printf("t12: PASS\n"); return 0; }
    printf("t12: %d FAILURES\n", fails);
    return 1;
}
