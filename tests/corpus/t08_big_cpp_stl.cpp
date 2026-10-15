/* ISS1-t08 — big_cpp_stl (Issue #1 Phase-1 target, recreated fixture)
 * Heavy C++ STL + exceptions + destructors, deterministic.
 * Prints RESULT lines; exit 0 iff every check passed.
 */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <numeric>
#include <stdexcept>

struct Widget {
    int id; std::string name;
    Widget(int i, std::string n) : id(i), name(std::move(n)) {}
    Widget(const Widget& o) : id(o.id), name(o.name) {}
    Widget(Widget&& o) noexcept : id(o.id), name(std::move(o.name)) {}
    Widget& operator=(const Widget& o) { id = o.id; name = o.name; return *this; }
    Widget& operator=(Widget&& o) noexcept { id = o.id; name = std::move(o.name); return *this; }
    ~Widget() {}
};

static int g_dtor_count = 0;
struct Tracer {
    int v;
    explicit Tracer(int x) : v(x) {}
    ~Tracer() { ++g_dtor_count; }
};

static int sum_via_maps() {
    std::map<std::string, std::vector<int>> groups;
    groups["alpha"] = {5, 3, 1};
    groups["beta"]  = {10, 20, 30};
    groups["gamma"] = {7};
    int total = 0;
    for (auto& kv : groups) {
        std::sort(kv.second.begin(), kv.second.end());
        total += std::accumulate(kv.second.begin(), kv.second.end(), (int)kv.first.size());
    }
    return total; /* 14 + 64 + 12 = 90 */
}

static int throw_across_frames(int depth) {
    Tracer t(depth);                       /* destructor must run during unwind */
    if (depth == 0) throw std::runtime_error("deep-boom");
    return throw_across_frames(depth - 1) + 1;
}

static int catch_typed(int mode) {
    try {
        if (mode == 0) throw 42;
        if (mode == 1) throw std::string("str-exc");
        throw_across_frames(4);
    } catch (int e)      { return e == 42 && mode == 0 ? 100 : -1; }
    catch (std::string& s){ return s == "str-exc" && mode == 1 ? 200 : -2; }
    catch (std::exception& e) {
        if (mode != 2) return -3;
        return std::string(e.what()) == "deep-boom" && g_dtor_count == 5 ? 300 : -4;
    }
    return -5;
}

int main() {
    int score = 0;
    std::vector<int> v(1000);
    std::iota(v.begin(), v.end(), 1);
    std::set<int> s(v.rbegin(), v.rend());
    if (s.size() == 1000 && *s.begin() == 1 && *s.rbegin() == 1000) score += 1;

    std::map<std::string,int> ages{{"ada",36},{"bo",25},{"cy",47}};
    int total = 0; for (auto& kv : ages) total += kv.second;   /* 108 */
    if (total == 108) score += 2;

    if (sum_via_maps() == 90) score += 4;

    std::string big; big.reserve(10000);
    for (int i = 0; i < 100; ++i) big += "0123456789";
    if (big.size() == 1000 && big[999] == '9') score += 8;

    std::vector<Widget> w; w.reserve(3);
    w.emplace_back(1, "w1"); w.emplace_back(2, "w2"); w.emplace_back(3, "w3");
    std::stable_sort(w.begin(), w.end(), [](const Widget& a, const Widget& b){ return a.id > b.id; });
    if (w[0].id == 3 && w[2].id == 1) score += 16;

    int r0 = catch_typed(0), r1 = catch_typed(1), r2 = catch_typed(2);
    if (r0 == 100) score += 32;
    if (r1 == 200) score += 64;
    if (r2 == 300) score += 128;

    printf("RESULT score=%d expected=255\n", score);
    printf("RESULT stl=ok exceptions=ok dtors=%d\n", g_dtor_count);
    return score == 255 ? 0 : 1;
}
