#include "Solver.hpp"

#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace sat {

// Maximum Occurrences in clauses of Minimum Size (MOMS).
// Min size refers to the number of distinct *unassigned* literals in
// a non-satisfied, non-tautological clause under the current assignment.
// Each variable is counted at most once per eligible clause.
BranchChoice Solver::chooseBranchVariable() const {
    std::size_t shortest = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> occurrences(
        static_cast<std::size_t>(formula_.variableCount) + 1, 0);
    bool unresolved = false;

    for (const Clause& clause : formula_.clauses) {
        std::unordered_set<Literal> literals;
        bool satisfied = false;
        bool tautology = false;
        for (Literal lit : clause) {
            if (literalValue(lit) == true) {
                satisfied = true;
                break;
            }
            if (literals.contains(-lit)) {
                tautology = true;
                break;
            }
            if (literalValue(lit) == std::nullopt) {
                literals.insert(lit);
            }
        }
        if (satisfied || tautology) continue;
        unresolved = true;
        if (literals.empty()) {
            throw std::logic_error("BCP missed a conflicting clause");
        }
        const std::size_t length = literals.size();
        if (length < shortest) {
            shortest = length;
            std::fill(occurrences.begin(), occurrences.end(), 0);
        }
        if (length == shortest) {
            std::unordered_set<int> uniqueVariables;
            for (Literal lit : literals) {
                const int variable = lit > 0 ? lit : -lit;
                uniqueVariables.insert(variable);
            }
            for (int variable : uniqueVariables) {
                ++occurrences[static_cast<std::size_t>(variable)];
            }
        }
    }
    if (!unresolved) return {};
    BranchChoice result;
    result.minResidualSize = shortest;
    for (int variable = 1; variable <= formula_.variableCount; ++variable) {
        const auto count = occurrences[static_cast<std::size_t>(variable)];
        if (count > result.occurrences) {
            result.variable = variable;
            result.occurrences = count;
        }
    }
    if (result.variable == 0) {
        throw std::logic_error("MOMS could not choose an unresolved variable");
    }
    return result;
}

} // namespace sat
