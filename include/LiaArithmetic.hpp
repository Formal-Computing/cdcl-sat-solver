#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace lia {
using Int = __int128_t;
struct Inequality {
    std::vector<Int> coefficients; // a_0*x_0 + ... <= rhs
    Int rhs = 0;
};
struct ArithmeticResult {
    enum class State { Sat, Unsat, Unknown } state = State::Unknown;
    std::vector<Int> model;
    std::string reason;
    int branches = 0;
};
Int checkedAdd(Int a, Int b);
Int checkedMultiply(Int a, Int b);
Int checkedNegate(Int a);
Int floorDivide(Int a, Int positive);
std::string printInt(Int value);
Int parseInt(const std::string& token);
ArithmeticResult checkIntegers(const std::vector<Inequality>& constraints, int nvars);
}
