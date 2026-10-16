/* APPCOMPAT t08_big_cpp_stl.cpp — BIG TEST #2 (MiniWin app-compat sweep)
 * C++17: STL containers (vector/string/map), algorithms, custom exception
 * hierarchy, try/catch chains, RAII destructor counting across unwinding,
 * new/delete, dynamic_cast. Output via printf (iostream is probed in t09).
 *
 * Exit 0 = ALL CHECKS PASS, 1 = FAILURE.
 */
#include <cstdio>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <numeric>
#include <cstring>

/* ---- custom exception hierarchy ---- */
struct AppError : std::exception {
    std::string msg;
    explicit AppError(const char* m) : msg(m) {}
    ~AppError() throw() override {}
    const char* what() const throw() override { return msg.c_str(); }
};
struct NotFound : AppError {
    explicit NotFound(const char* m) : AppError(m) {}
};
struct ValidationError : AppError {
    explicit ValidationError(const char* m) : AppError(m) {}
};

/* ---- RAII guard that counts ctor/dtor ---- */
static int g_live = 0;
static int g_ctor = 0;
static int g_dtor = 0;
struct Guard {
    int id;
    explicit Guard(int i) : id(i) { g_live++; g_ctor++; }
    ~Guard() { g_live--; g_dtor++; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

/* ---- domain ---- */
struct Product {
    int id;
    std::string name;
    std::string category;
    int qty;
    long long price_cents;
    long long value() const { return (long long)qty * price_cents; }
};

static int g_throws = 0;

static Product& find_by_id(std::vector<Product>& v, int id) {
    for (auto& p : v)
        if (p.id == id) return p;
    g_throws++;
    throw NotFound("product not found");
}

static void validate_qty(int q) {
    if (q < 0) { g_throws++; throw ValidationError("negative quantity"); }
    if (q > 100000) { g_throws++; throw ValidationError("quantity too large"); }
}

/* function that partially builds state then throws -> exercises unwinding */
static int unwind_depth_test(int depth) {
    Guard g(depth);
    if (depth == 0) { g_throws++; throw AppError("bottom reached"); }
    return unwind_depth_test(depth - 1) + 1;
}

int main() {
    int checks = 0, failed = 0;
    auto CHECK = [&](bool ok, const char* msg) {
        checks++;
        if (!ok) { std::printf("FAIL: %s\n", msg); failed++; }
    };

    /* ---- build inventory ---- */
    std::vector<Product> inv;
    const char* cats[4] = {"furniture", "lighting", "electronics", "hardware"};
    const char* adjs[5] = {"red", "blue", "dark", "bright", "heavy"};
    const char* nouns[5] = {"chair", "lamp", "cable", "panel", "motor"};
    for (int i = 0; i < 30; i++) {
        Product p;
        p.id = i + 1;
        p.name = std::string(adjs[i % 5]) + " " + nouns[(i * 2) % 5];
        p.category = cats[i % 4];
        p.qty = 1 + (i * 7) % 40;
        p.price_cents = 100 + (long long)(i * 917) % 50000;
        inv.push_back(p);
    }
    CHECK(inv.size() == 30, "vector size 30");

    /* ---- string ops ---- */
    std::string s = "MiniWin";
    s += "-loader";
    CHECK(s == "MiniWin-loader", "string concat");
    CHECK(s.substr(0, 7) == "MiniWin", "string substr");
    CHECK(s.find("Win") == 4, "string find");
    CHECK(s.find("zzz") == std::string::npos, "string find npos");
    CHECK(s.size() == 13, "string size");
    CHECK(s.compare(0, 7, "MiniWin") == 0, "string compare");

    /* ---- map category counts ---- */
    std::map<std::string, int> counts;
    for (auto& p : inv) counts[p.category]++;
    CHECK(counts.size() == 4, "map has 4 categories");
    CHECK(counts["furniture"] == 8, "furniture count 8");
    CHECK(counts["lighting"] == 8, "lighting count 8");
    CHECK(counts["electronics"] == 7, "electronics count 7");
    CHECK(counts["hardware"] == 7, "hardware count 7");

    /* ---- sort by value desc + verify ---- */
    std::sort(inv.begin(), inv.end(),
              [](const Product& a, const Product& b) {
                  if (a.value() != b.value()) return a.value() > b.value();
                  return a.id < b.id;
              });
    bool monotonic = true;
    for (size_t i = 1; i < inv.size(); i++)
        if (inv[i-1].value() < inv[i].value()) { monotonic = false; break; }
    CHECK(monotonic, "sort by value desc");

    long long total = std::accumulate(inv.begin(), inv.end(), 0LL,
        [](long long acc, const Product& p) { return acc + p.value(); });
    long long manual = 0;
    for (auto& p : inv) manual += p.value();
    CHECK(total == manual, "accumulate == manual sum");
    std::printf("total inventory value = %lld cents\n", total);

    /* ---- exceptions: specific then base ---- */
    try {
        Product& p3 = find_by_id(inv, 3);
        p3.qty = 7;
        CHECK(find_by_id(inv, 3).qty == 7, "mutation via reference");
    } catch (...) { CHECK(false, "find id=3 must not throw"); }

    try {
        find_by_id(inv, 9999);
        CHECK(false, "id=9999 must throw NotFound");
    } catch (const NotFound& e) {
        CHECK(true, "caught NotFound");
        std::printf("caught NotFound: %s\n", e.what());
    } catch (const AppError& e) {
        (void)e;
        CHECK(false, "should match NotFound, not base");
    }

    try {
        validate_qty(-5);
        CHECK(false, "negative qty must throw ValidationError");
    } catch (const AppError& e) {
        CHECK(true, "caught ValidationError as base AppError");
        std::printf("caught AppError: %s\n", e.what());
    }

    try {
        unwind_depth_test(6);
        CHECK(false, "unwind test must throw");
    } catch (const AppError&) {
        CHECK(g_throws >= 3, "throw counter");
        /* Guards 6..0 destroyed during unwinding */
        CHECK(g_ctor == 7, "7 guards constructed");
        CHECK(g_dtor == 7, "7 guards destroyed during unwind");
        CHECK(g_live == 0, "no live guards after unwind");
        std::printf("unwind depth test OK (ctor=%d dtor=%d live=%d)\n",
                    g_ctor, g_dtor, g_live);
    }

    /* rethrow chain */
    int rethrow_ok = 0;
    try {
        try {
            g_throws++;
            throw ValidationError("inner");
        } catch (AppError&) {
            rethrow_ok |= 1;
            throw; /* rethrow */
        }
    } catch (const std::exception& e) {
        rethrow_ok |= 2;
        CHECK(std::strcmp(e.what(), "inner") == 0, "rethrow preserves object");
    }
    CHECK(rethrow_ok == 3, "rethrow chain both handlers ran");

    /* ---- new/delete + dynamic_cast ---- */
    struct Base { virtual ~Base() {} virtual int tag() const { return 0; } };
    struct Derived : Base { int tag() const override { return 42; } };
    struct Other : Base { int tag() const override { return 1; } };

    Base* b = new Derived();
    Derived* d = dynamic_cast<Derived*>(b);
    CHECK(d != NULL && d->tag() == 42, "dynamic_cast down");
    Other* o = dynamic_cast<Other*>(b);
    CHECK(o == NULL, "dynamic_cast cross null");
    delete b;

    int* arr = new int[64];
    for (int i = 0; i < 64; i++) arr[i] = i * 3;
    long long asum = 0;
    for (int i = 0; i < 64; i++) asum += arr[i];
    CHECK(asum == 3 * 2016LL, "new[] content");
    delete[] arr;

    /* ---- vector<string> sort ---- */
    std::vector<std::string> words;
    words.push_back("pear"); words.push_back("apple"); words.push_back("orange");
    words.push_back("banana"); words.push_back("kiwi");
    std::sort(words.begin(), words.end());
    CHECK(words[0] == "apple" && words[4] == "pear", "string vector sort");

    std::printf("checks=%d failed=%d throws=%d\n", checks, failed, g_throws);
    if (failed == 0 && checks >= 20) { std::puts("t08: PASS"); return 0; }
    std::puts("t08: FAIL");
    return 1;
}
