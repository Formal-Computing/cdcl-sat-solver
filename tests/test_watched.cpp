#include "Solver.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::size_t checks = 0;

void check(bool value, const std::string& explanation) {
    if (!value) {
        throw std::runtime_error("FAIL: " + explanation);
    }
    ++checks;
}

struct ReferenceResult {
    bool conflict = false;
    std::vector<std::int8_t> values;
};

// Independent full-clause-scanning unit propagation oracle. It reconstructs
// a fixed point solely from the original CNF and the solver's decisions;
// no watch lists or learned clauses are used.
ReferenceResult reference(const sat::CnfFormula& cnf,
                          const std::vector<sat::TrailEntry>& trail) {
    ReferenceResult result;
    result.values.assign(static_cast<std::size_t>(cnf.variableCount) + 1, 0);
    for (const auto& entry : trail) {
        if (entry.reasonClause) continue;
        const int v = entry.literal > 0 ? entry.literal : -entry.literal;
        const std::int8_t value = entry.literal > 0 ? 1 : -1;
        if (result.values[static_cast<std::size_t>(v)] == -value) {
            result.conflict = true;
            return result;
        }
        result.values[static_cast<std::size_t>(v)] = value;
    }

    bool changed;
    do {
        changed = false;
        for (const auto& clause : cnf.clauses) {
            bool trueLiteral = false;
            int remaining = 0;
            bool multiple = false;
            for (const int lit : clause) {
                const int v = lit > 0 ? lit : -lit;
                const auto value = result.values[static_cast<std::size_t>(v)];
                if ((lit > 0 && value == 1) || (lit < 0 && value == -1)) {
                    trueLiteral = true;
                    break;
                }
                if (value == 0) {
                    if (remaining == 0) remaining = lit;
                    else if (remaining != lit) multiple = true;
                }
            }
            if (trueLiteral) continue;
            if (remaining == 0) {
                result.conflict = true;
                return result;
            }
            if (!multiple) {
                const int v = remaining > 0 ? remaining : -remaining;
                if (result.values[static_cast<std::size_t>(v)] == 0) {
                    result.values[static_cast<std::size_t>(v)] = remaining > 0 ? 1 : -1;
                    changed = true;
                }
            }
        }
    } while (changed);
    return result;
}

bool checkPropagation(sat::Solver& solver, const sat::CnfFormula& original,
                      const std::string& scenario) {
    const ReferenceResult expected = reference(original, solver.trail());
    const auto actual = solver.propagate();
    check(actual.has_value() == expected.conflict, scenario + ": conflict comparison");
    if (!expected.conflict) {
        for (int v = 1; v <= original.variableCount; ++v) {
            const auto result = solver.value(v);
            const int actualValue = !result ? 0 : (*result ? 1 : -1);
            check(actualValue == expected.values[static_cast<std::size_t>(v)],
                  scenario + ": value of variable " + std::to_string(v));
        }
    }
    return expected.conflict;
}

void testWatchRelocation() {
    const sat::CnfFormula cnf{4, {{1, 2, 3, 4}}};
    sat::Solver solver(cnf);

    solver.decide(-1);
    check(!solver.propagate(), "watch replacement after x1=false");
    solver.decide(-2);
    check(!solver.propagate(), "watch replacement after x2=false");
    solver.decide(-3);
    check(!solver.propagate(), "unit from last two watches");
    check(solver.value(4) == true, "fourth literal became unit");
    check(solver.trail().back().reasonClause == 0, "reason is original clause");
    check(solver.trail().back().decisionLevel == 3, "propagation occurs at decision level 3");

    solver.backtrack(2);
    check(!solver.value(3) && !solver.value(4), "undo late unit and decision");
    solver.decide(-4);
    check(!solver.propagate(), "reused moved watches after backtrack");
    check(solver.value(3) == true, "third literal forced by moved watches");
    check(solver.trail().back().reasonClause == 0, "reason preserved across move");
    solver.backtrack(0);
    check(solver.trail().empty(), "all decisions and consequences undone");
    check(!solver.propagate(), "backtrack fixed point has no stale watches");
    check(!solver.value(1) && !solver.value(2) && !solver.value(3) && !solver.value(4),
          "no stale assignments after full backtrack");
}

void testNormalizationAndRecovery() {
    sat::Solver duplicates({2, {{1, 1, -2}, {-1}}});
    check(!duplicates.propagate(), "duplicate literal normalized for watches");
    check(duplicates.value(1) == false && duplicates.value(2) == false,
          "duplicate literals still force -x2");
    sat::Solver tautology({2, {{1, -1, 2}, {-2}}});
    check(!tautology.propagate() && !tautology.value(1),
          "tautology cannot force x1");

    sat::Solver beforeFirstPropagation({2, {{1}, {-1, 2}}});
    beforeFirstPropagation.decide(-1);
    check(beforeFirstPropagation.propagate() == 0,
          "unit clause detects wrong decision before first propagation");
    beforeFirstPropagation.backtrack(0);
    check(!beforeFirstPropagation.propagate(), "unit restored after backtrack");
    check(beforeFirstPropagation.value(1) == true &&
              beforeFirstPropagation.value(2) == true,
          "root implication chain restored");

    sat::Solver empty({0, {sat::Clause{}}});
    check(empty.propagate() == 0 && empty.propagate() == 0,
          "permanent root-level empty clause conflict stays visible");
    sat::Solver contradictory({1, {{1}, {-1}}});
    check(contradictory.propagate().has_value() &&
              contradictory.propagate().has_value(),
          "root contradictory units remain UNSAT on repeated calls");
}

void testRandomIncrementalAgainstReference() {
    std::mt19937 rng(130731);
    std::uniform_int_distribution<int> nvars(0, 8), nclauses(0, 20);
    std::uniform_int_distribution<int> clauseSize(0, 7), sign(0, 1);
    int totalCheckpoints = 0;

    for (int trial = 0; trial < 1200; ++trial) {
        sat::CnfFormula cnf;
        cnf.variableCount = nvars(rng);
        const int clauses = nclauses(rng);
        for (int c = 0; c < clauses; ++c) {
            sat::Clause clause;
            const int len = cnf.variableCount == 0 ? 0 : clauseSize(rng);
            for (int k = 0; k < len; ++k) {
                std::uniform_int_distribution<int> var(1, cnf.variableCount);
                int lit = var(rng);
                if (sign(rng)) lit = -lit;
                clause.push_back(lit);
            }
            cnf.clauses.push_back(std::move(clause));
        }
        sat::Solver solver(cnf);

        for (int step = 0; step < 16; ++step) {
            const auto scenario = "trial " + std::to_string(trial) +
                                  " step " + std::to_string(step);
            const bool conflict = checkPropagation(solver, cnf, scenario);
            ++totalCheckpoints;
            if (conflict) {
                if (solver.decisionLevel() == 0) {
                    // No decision can repair a root contradiction.
                    break;
                }
                std::uniform_int_distribution<int> target(0, solver.decisionLevel() - 1);
                solver.backtrack(target(rng));
                continue;
            }

            if (solver.decisionLevel() > 0 && (rng() % 4) == 0) {
                std::uniform_int_distribution<int> target(0, solver.decisionLevel() - 1);
                solver.backtrack(target(rng));
                continue;
            }
            std::vector<int> unset;
            for (int v = 1; v <= cnf.variableCount; ++v) {
                if (!solver.value(v)) unset.push_back(v);
            }
            if (unset.empty()) {
                if (solver.decisionLevel() == 0) break;
                solver.backtrack(0);
                continue;
            }
            std::uniform_int_distribution<std::size_t> pick(0, unset.size() - 1);
            const int variable = unset[pick(rng)];
            solver.decide(sign(rng) ? variable : -variable);
        }
    }
    check(totalCheckpoints > 1200, "incremental oracle checked multiple states");
    std::cout << "Incremental BCP reference checkpoints: " << totalCheckpoints << '\n';
}

} // namespace

int main() {
    try {
        testWatchRelocation();
        testNormalizationAndRecovery();
        testRandomIncrementalAgainstReference();
        std::cout << "Watched literals: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
