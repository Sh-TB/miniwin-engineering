/* ISS1-t08-probe — minimal C++ exception value probe (diagnostic fixture).
 * Classifies how throw values reach catch handlers under MiniWin:
 *   p1: catch int by value        -> prints caught value
 *   p2: catch std::string by ref  -> prints length + content
 *   p3: catch exception& (what()) -> prints message
 * Exit 0 iff all three delivered correct values.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>

int main() {
    int ok = 0;
    /* p1 */
    { int got = 0;
      try { throw 42; } catch (int e) { got = e; }
      printf("PROBE p1_int caught=%d expected=42 %s\n", got, got == 42 ? "OK" : "BAD"); if (got == 42) ++ok; }
    /* p2 */
    { std::string got;
      try { throw std::string("str-exc"); } catch (std::string& s) { got = s; }
      printf("PROBE p2_str len=%d content=%s %s\n", (int)got.size(), got.c_str(),
             (got == "str-exc") ? "OK" : "BAD"); if (got == "str-exc") ++ok; }
    /* p3 */
    { const char* got = "<none>";
      try { throw std::runtime_error("deep-boom"); } catch (std::exception& e) { got = e.what(); }
      printf("PROBE p3_what msg=%s %s\n", got, strcmp(got, "deep-boom") == 0 ? "OK" : "BAD");
      if (strcmp(got, "deep-boom") == 0) ++ok; }
    printf("PROBE ok=%d/3\n", ok);
    return ok == 3 ? 0 : 1;
}
