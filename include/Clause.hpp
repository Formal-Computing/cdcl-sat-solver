#pragma once

#include <vector>

namespace sat {

// DIMACS literals are signed integers: n means x_n, -n means NOT x_n.
// Literal 0 is only a clause terminator in the input format.
using Literal = int;
using Clause = std::vector<Literal>;

struct CnfFormula {
    int variableCount = 0;
    std::vector<Clause> clauses;
};

} // namespace sat
