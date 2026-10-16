/* APPCOMPAT t04_string.c — string/ctype/qsort (MiniWin app-compat sweep)
 * Verifies: strcpy, strcmp, strchr, strrchr, strstr, memcmp, memmove, strtol,
 *           qsort with comparators, ctype classification.
 * Deterministic. Exit 0 = PASS, 1 = FAIL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

static int cmp_str(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

int main(void) {
    int fails = 0;

    /* strcpy / strlen / strcmp */
    char buf[64];
    strcpy(buf, "miniwin-loader");
    if (strlen(buf) != 14) { puts("FAIL: strlen"); fails++; }
    if (strcmp(buf, "miniwin-loader") != 0) { puts("FAIL: strcmp eq"); fails++; }
    if (strcmp(buf, "miniwin") <= 0) { puts("FAIL: strcmp gt"); fails++; }
    if (strncmp(buf, "miniwin", 7) != 0) { puts("FAIL: strncmp"); fails++; }

    /* strchr / strrchr */
    const char* s = "a/b/c/d.txt";
    if (strchr(s, '/') != s + 1) { puts("FAIL: strchr first"); fails++; }
    if (strrchr(s, '/') != s + 5) { puts("FAIL: strrchr"); fails++; }
    if (strchr(s, 'z') != NULL) { puts("FAIL: strchr absent"); fails++; }

    /* strstr */
    const char* hay = "the quick brown fox";
    if (strstr(hay, "brown") != hay + 10) { puts("FAIL: strstr"); fails++; }
    if (strstr(hay, "cat") != NULL) { puts("FAIL: strstr absent"); fails++; }

    /* memmove overlapping */
    char mv[16];
    strcpy(mv, "abcdef");
    memmove(mv + 2, mv, 4); /* -> "ababcd" */
    if (strcmp(mv, "ababcd") != 0) { printf("FAIL: memmove ov -> %s\n", mv); fails++; }

    /* memcmp / memchr */
    if (memcmp("abc", "abc", 3) != 0) { puts("FAIL: memcmp eq"); fails++; }
    if (memcmp("abc", "abd", 3) >= 0) { puts("FAIL: memcmp lt"); fails++; }
    if (memchr("hello", 'l', 5) == NULL) { puts("FAIL: memchr"); fails++; }
    if (memchr("hello", 'z', 5) != NULL) { puts("FAIL: memchr absent"); fails++; }

    /* strtol / strtoul */
    if (strtol("  -123xyz", NULL, 10) != -123) { puts("FAIL: strtol dec"); fails++; }
    if (strtol("0x1F", NULL, 16) != 31) { puts("FAIL: strtol hex"); fails++; }
    if (strtoul("4294967295", NULL, 10) != 4294967295UL) { puts("FAIL: strtoul"); fails++; }

    /* qsort ints */
    int arr[10] = {9, 3, 7, 1, 8, 2, 6, 4, 0, 5};
    qsort(arr, 10, sizeof(int), cmp_int);
    for (int i = 0; i < 10; i++)
        if (arr[i] != i) { puts("FAIL: qsort ints"); fails++; break; }

    /* qsort strings */
    const char* words[5] = {"pear", "apple", "orange", "banana", "kiwi"};
    qsort(words, 5, sizeof(const char*), cmp_str);
    if (strcmp(words[0], "apple") != 0 || strcmp(words[4], "pear") != 0) {
        puts("FAIL: qsort strings"); fails++;
    }

    /* ctype */
    if (!isupper('A') || tolower('A') != 'a') { puts("FAIL: isupper/tolower"); fails++; }
    if (!isspace(' ') || !isdigit('7')) { puts("FAIL: isspace/isdigit"); fails++; }
    if (!isxdigit('F') || isxdigit('G')) { puts("FAIL: isxdigit"); fails++; }

    if (fails == 0) { puts("t04: PASS"); return 0; }
    printf("t04: FAIL (%d)\n", fails);
    return 1;
}
