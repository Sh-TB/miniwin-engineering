/* APPCOMPAT t09_big_cpp_iostream.cpp — iostream probe (MiniWin app-compat sweep)
 * Smaller companion to t08: exercises std::cout formatting paths.
 * This may FAIL at import resolution if libc++ needs CRT APIs MiniWin does
 * not implement — that outcome is itself a measured finding.
 * Exit 0 = PASS, 1 = FAIL, 125 = setup error. */
#include <iostream>
#include <iomanip>
#include <string>

int main() {
    std::cout << "t09: iostream probe" << std::endl;
    std::cout << "dec=" << 12345 << " hex=" << std::hex << 0xDEADBEEF
              << std::dec << " neg=" << -42 << std::endl;
    std::string s = "std::string via cout";
    std::cout << s << std::endl;
    std::cout << "width=[" << std::setw(8) << 99 << "]"
              << " fill=[" << std::setfill('0') << std::setw(5) << 7 << "]"
              << std::endl;
    std::cout << "fixed=" << std::fixed << std::setprecision(3) << 3.14159
              << std::endl;
    std::cout << "t09: PASS" << std::endl;
    return 0;
}
