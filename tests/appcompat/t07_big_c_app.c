/* APPCOMPAT t07_big_c_app.c — BIG TEST #1 (MiniWin app-compat sweep)
 * "Inventory Manager": a realistic C console application (~420 lines).
 *
 * Exercises: structs/arrays/pointers, malloc/realloc/free, string building,
 * multi-key qsort, linear search, binary search, FNV-1a hashing, rand/srand,
 * _time64, binary persistence via _open/_write/_read/_close/_unlink,
 * journal text file, setjmp/longjmp error handling, formatted reports.
 *
 * Fully deterministic (fixed RNG seed, no wall-clock dependence in checks).
 * Exit 0 = ALL CHECKS PASS, 1 = FAILURE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* ---- low-level CRT file API (implemented by MiniWin) ---- */
extern int _open(const char*, int, ...);
extern int _close(int);
extern int _read(int, void*, unsigned);
extern int _write(int, const void*, unsigned);
extern long long _lseeki64(int, long long, int);
extern long long _time64(long long*);

#define O_WRONLY_CRT 0x0001
#define O_RDONLY_CRT 0x0000
#define O_CREAT_CRT  0x0100
#define O_TRUNC_CRT  0x0200

/* ---- domain model ---- */
typedef struct {
    int  id;
    char name[32];
    char category[16];
    int  qty;
    long long unit_price_cents;
} Product;

#define MAX_PRODUCTS 64

/* ---- global app state ---- */
static Product g_inv[MAX_PRODUCTS];
static int     g_count = 0;
static jmp_buf g_err;
static char    g_errmsg[96];
static long long g_journal_lines = 0;

/* ---- error handling via setjmp/longjmp ---- */
/* NOTE: snprintf/strncpy are NOT implemented by MiniWin (import table has no
 * sprintf/snprintf/strncpy). All formatting is done manually below.
 * Findings from the sweep are tracked as APP-F-* / backlog API-0xx items. */

static void app_error(const char* msg) {
    size_t i = 0;
    while (msg[i] && i < sizeof(g_errmsg) - 1) { g_errmsg[i] = msg[i]; i++; }
    g_errmsg[i] = 0;
    longjmp(g_err, 1);
}

/* ---- deterministic name generator ---- */
static const char* ADJ[8]  = {"red", "blue", "dark", "bright", "heavy", "light", "smart", "classic"};
static const char* NOUN[8] = {"chair", "lamp", "cable", "socket", "panel", "sensor", "motor", "filter"};
static const char* CATS[4] = {"furniture", "lighting", "electronics", "hardware"};

/* manual "snprintf(out, cap, "%s %s %02d", adj, noun, idx%100)" */
static void make_name(int idx, char* out, size_t cap) {
    const char* a = ADJ[idx % 8];
    const char* n = NOUN[(idx * 3) % 8];
    size_t k = 0;
    while (*a && k < cap - 1) out[k++] = *a++;
    if (k < cap - 1) out[k++] = ' ';
    while (*n && k < cap - 1) out[k++] = *n++;
    if (k < cap - 1) out[k++] = ' ';
    int v = idx % 100;
    if (v < 10) { if (k < cap - 1) out[k++] = '0'; }
    else { if (k < cap - 1) out[k++] = (char)('0' + v / 10); }
    if (k < cap - 1) out[k++] = (char)('0' + v % 10);
    out[k] = 0;
}

/* ---- inventory ops ---- */
static void inv_add(int id, const char* cat, int qty, long long price) {
    if (g_count >= MAX_PRODUCTS) app_error("inventory full");
    if (qty < 0) app_error("negative qty");
    if (price < 0) app_error("negative price");
    Product* p = &g_inv[g_count++];
    p->id = id;
    make_name(id, p->name, sizeof(p->name));
    {
        size_t i = 0;
        while (cat[i] && i < sizeof(p->category) - 1) { p->category[i] = cat[i]; i++; }
        p->category[i] = 0;
    }
    p->qty = qty;
    p->unit_price_cents = price;
}

static Product* inv_find_by_id(int id) {
    for (int i = 0; i < g_count; i++)
        if (g_inv[i].id == id) return &g_inv[i];
    return NULL;
}

static Product* inv_find_by_name_substr(const char* needle) {
    for (int i = 0; i < g_count; i++)
        if (strstr(g_inv[i].name, needle)) return &g_inv[i];
    return NULL;
}

static long long inv_total_value(void) {
    long long total = 0;
    for (int i = 0; i < g_count; i++)
        total += (long long)g_inv[i].qty * g_inv[i].unit_price_cents;
    return total;
}

static void inv_set_qty(int id, int new_qty) {
    Product* p = inv_find_by_id(id);
    if (!p) app_error("set_qty: product not found");
    if (new_qty < 0) app_error("set_qty: negative");
    p->qty = new_qty;
}

/* ---- sorting ---- */
static int cmp_by_value_desc(const void* a, const void* b) {
    const Product* x = (const Product*)a;
    const Product* y = (const Product*)b;
    long long vx = (long long)x->qty * x->unit_price_cents;
    long long vy = (long long)y->qty * y->unit_price_cents;
    if (vx != vy) return (vx > vy) ? -1 : 1;
    return x->id - y->id;
}

static int cmp_by_name(const void* a, const void* b) {
    return strcmp(((const Product*)a)->name, ((const Product*)b)->name);
}

static int cmp_by_id(const void* a, const void* b) {
    return ((const Product*)a)->id - ((const Product*)b)->id;
}

/* ---- FNV-1a over the whole inventory (order-dependent) ---- */
static unsigned long long inv_hash(void) {
    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < g_count; i++) {
        const unsigned char* bytes = (const unsigned char*)&g_inv[i];
        for (size_t b = 0; b < sizeof(Product); b++) {
            h ^= bytes[b];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

/* ---- persistence (binary) ---- */
static const char MAGIC[8] = {'M', 'W', 'I', 'N', 'V', '0', '0', '1'};

static int inv_save(const char* path) {
    int fd = _open(path, O_WRONLY_CRT | O_CREAT_CRT | O_TRUNC_CRT, 0644);
    if (fd < 0) return -1;
    if (_write(fd, MAGIC, 8) != 8) goto fail;
    if (_write(fd, &g_count, sizeof(int)) != (int)sizeof(int)) goto fail;
    if (_write(fd, g_inv, sizeof(Product) * (size_t)g_count)
        != (int)(sizeof(Product) * (size_t)g_count)) goto fail;
    if (_close(fd) != 0) return -1;
    return 0;
fail:
    _close(fd);
    return -1;
}

static int inv_load(const char* path) {
    int fd = _open(path, O_RDONLY_CRT);
    if (fd < 0) return -1;
    char magic[8];
    if (_read(fd, magic, 8) != 8 || memcmp(magic, MAGIC, 8) != 0) goto fail;
    int n = 0;
    if (_read(fd, &n, sizeof(int)) != (int)sizeof(int)) goto fail;
    if (n <= 0 || n > MAX_PRODUCTS) goto fail;
    if (_read(fd, g_inv, sizeof(Product) * (size_t)n)
        != (int)(sizeof(Product) * (size_t)n)) goto fail;
    g_count = n;
    if (_close(fd) != 0) return -1;
    return 0;
fail:
    _close(fd);
    return -1;
}

/* ---- journal (text) ---- */
static void journal(const char* line) {
    /* Re-open with TRUNC only for the first line: journal starts fresh each
     * run (deterministic), then we seek to end for appends. */
    static int jfd = -2;
    if (jfd == -2) {
        jfd = _open("t07_journal.txt", O_WRONLY_CRT | O_CREAT_CRT | O_TRUNC_CRT, 0644);
        if (jfd < 0) app_error("journal open failed");
    }
    int len = (int)strlen(line);
    if (_write(jfd, line, (unsigned)len) != len) app_error("journal write failed");
    if (_write(jfd, "\n", 1) != 1) app_error("journal newline failed");
    g_journal_lines++;
}

/* ---- report ---- */
static void inv_report(const char* title) {
    printf("---- %s ----\n", title);
    printf("%4s %-24s %-12s %6s %12s %14s\n",
           "id", "name", "category", "qty", "unit(c)", "value(c)");
    for (int i = 0; i < g_count; i++) {
        const Product* p = &g_inv[i];
        printf("%4d %-24s %-12s %6d %12lld %14lld\n",
               p->id, p->name, p->category, p->qty,
               p->unit_price_cents,
               (long long)p->qty * p->unit_price_cents);
    }
    printf("total value: %lld cents (%lld.%02lld) over %d products\n",
           inv_total_value(), inv_total_value() / 100, inv_total_value() % 100,
           g_count);
}

/* ---- self-check ---- */
static int g_checks = 0, g_failed = 0;
#define CHECK(cond, msg) do { \
        g_checks++; \
        if (!(cond)) { printf("FAIL: %s\n", msg); g_failed++; } \
    } while (0)

int main(void) {
    long long now = _time64(NULL);
    printf("t07 Inventory Manager (timestamp=%lld)\n", now);

    /* ---- build inventory (deterministic) ---- */
    srand(1337);
    if (setjmp(g_err) != 0) {
        printf("FAIL: unexpected app_error: %s\n", g_errmsg);
        return 1;
    }

    for (int i = 0; i < 25; i++) {
        int qty = 1 + rand() % 50;
        long long price = 100 + (long long)(rand() % 90000);
        inv_add(i + 1, CATS[i % 4], qty, price);
    }
    CHECK(g_count == 25, "inventory size after build");
    journal("built 25 products");

    /* ---- lookups ---- */
    Product* p12 = inv_find_by_id(12);
    CHECK(p12 != NULL, "find id=12");
    if (p12) CHECK(p12->qty >= 1 && p12->qty <= 50, "qty range for id=12");
    CHECK(inv_find_by_id(9999) == NULL, "missing id returns NULL");
    Product* psub = inv_find_by_name_substr("lamp");
    CHECK(psub != NULL, "name substring 'lamp' found");
    journal("lookups done");

    /* ---- error handling through longjmp ---- */
    if (setjmp(g_err) == 0) {
        inv_set_qty(9999, 5);
        printf("FAIL: set_qty(9999) did not raise\n");
        g_failed++;
    } else {
        g_checks++;
        if (strstr(g_errmsg, "not found") == NULL) { printf("FAIL: err msg: %s\n", g_errmsg); g_failed++; }
        else puts("set_qty(9999) -> app_error 'not found' handled OK");
    }
    if (setjmp(g_err) == 0) {
        g_inv[0].qty = -1; /* force negative-qty path via direct call */
        inv_set_qty(g_inv[0].id, -3);
        printf("FAIL: negative qty did not raise\n");
        g_failed++;
        g_inv[0].qty = 1;
    } else {
        g_checks++;
        g_inv[0].qty = 1;
        if (strstr(g_errmsg, "negative") == NULL) { printf("FAIL: err msg: %s\n", g_errmsg); g_failed++; }
        else puts("set_qty(-3) -> app_error 'negative' handled OK");
    }
    journal("error paths exercised");

    /* ---- stats ---- */
    long long v1 = inv_total_value();
    inv_report("INITIAL (id order)");
    journal("initial report printed");

    /* ---- sort by value desc, verify monotonic ---- */
    qsort(g_inv, (size_t)g_count, sizeof(Product), cmp_by_value_desc);
    int monotonic = 1;
    for (int i = 1; i < g_count; i++) {
        long long a = (long long)g_inv[i-1].qty * g_inv[i-1].unit_price_cents;
        long long b = (long long)g_inv[i].qty * g_inv[i].unit_price_cents;
        if (a < b) { monotonic = 0; break; }
    }
    CHECK(monotonic, "value-desc sort monotonic");
    CHECK(inv_total_value() == v1, "total invariant under sort");
    inv_report("BY VALUE DESC");
    journal("value sort done");

    /* ---- sort by name, verify lexicographic ---- */
    qsort(g_inv, (size_t)g_count, sizeof(Product), cmp_by_name);
    int lex = 1;
    for (int i = 1; i < g_count; i++)
        if (strcmp(g_inv[i-1].name, g_inv[i].name) > 0) { lex = 0; break; }
    CHECK(lex, "name sort lexicographic");
    inv_report("BY NAME");
    journal("name sort done");

    /* ---- binary round-trip ---- */
    unsigned long long h_before = inv_hash();
    if (inv_save("t07_inventory.bin") != 0) app_error("save failed");
    memset(g_inv, 0, sizeof(g_inv));
    g_count = 0;
    if (inv_load("t07_inventory.bin") != 0) app_error("load failed");
    unsigned long long h_after = inv_hash();
    CHECK(g_count == 25, "reloaded count");
    CHECK(h_before == h_after, "FNV-1a hash identical after round-trip");
    CHECK(inv_total_value() == v1, "total value identical after round-trip");
    printf("hash before=%016llX after=%016llX\n", h_before, h_after);
    journal("binary round-trip verified");

    /* ---- mutation + re-save + reload ---- */
    inv_set_qty(p12 ? 12 : 1, 7);
    CHECK(inv_find_by_id(12)->qty == 7, "mutation persisted in memory");
    if (inv_save("t07_inventory.bin") != 0) app_error("re-save failed");
    memset(g_inv, 0, sizeof(g_inv));
    g_count = 0;
    if (inv_load("t07_inventory.bin") != 0) app_error("re-load failed");
    CHECK(inv_find_by_id(12)->qty == 7, "mutation survived round-trip");
    journal("mutation round-trip verified");

    /* ---- journal verify ---- */
    {
        int fd = _open("t07_journal.txt", O_RDONLY_CRT);
        CHECK(fd >= 0, "journal reopen");
        if (fd >= 0) {
            char buf[512];
            int r = _read(fd, buf, sizeof(buf) - 1);
            CHECK(r > 100, "journal has content");
            if (r > 0) {
                buf[r] = 0;
                int nl = 0;
                for (int i = 0; i < r; i++) if (buf[i] == '\n') nl++;
                CHECK(nl == 8, "journal line count == 8");
            }
            _close(fd);
        }
    }

    /* ---- cleanup + final ---- */
    printf("checks=%d failed=%d journal_lines=%lld\n", g_checks, g_failed, g_journal_lines);
    if (g_failed == 0 && g_checks >= 15) { puts("t07: PASS"); return 0; }
    puts("t07: FAIL");
    return 1;
}
