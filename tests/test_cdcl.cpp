#include "Solver.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int checks = 0;

void check(bool predicate, const std::string& explanation) {
    if (!predicate) {
        throw std::runtime_error("FAIL: " + explanation);
    }
    ++checks;
}

bool literalSatisfied(sat::Literal lit, std::uint64_t mask) {
    const int variable = lit > 0 ? lit : -lit;
    const bool value = ((mask >> (variable - 1)) & 1ULL) != 0;
    return lit > 0 ? value : !value;
}

bool clauseSatisfied(const sat::Clause& clause, std::uint64_t mask) {
    for (sat::Literal literal : clause) {
        if (literalSatisfied(literal, mask)) {
            return true;
        }
    }
    return false;
}

bool formulaSatisfied(const sat::CnfFormula& f, std::uint64_t mask) {
    for (const auto& clause : f.clauses) {
        if (!clauseSatisfied(clause, mask)) {
            return false;
        }
    }
    return true;
}

bool bruteForceSat(const sat::CnfFormula& f) {
    for (std::uint64_t mask = 0; mask < (1ULL << f.variableCount); ++mask) {
        if (formulaSatisfied(f, mask)) {
            return true;
        }
    }
    return false;
}

bool validTotalModel(const sat::CnfFormula& original,
                     const std::vector<sat::Literal>& model) {
    if (model.size() != static_cast<std::size_t>(original.variableCount)) {
        return false;
    }
    std::uint64_t mask = 0;
    for (int variable = 1; variable <= original.variableCount; ++variable) {
        const sat::Literal assigned = model[static_cast<std::size_t>(variable - 1)];
        if (assigned != variable && assigned != -variable) {
            return false;
        }
        if (assigned > 0) {
            mask |= (1ULL << (variable - 1));
        }
    }
    return formulaSatisfied(original, mask);
}

// Exhaustively validate that every learned clause is entailed by the
// original formula: no valuation may satisfy original CNF and falsify it.
bool learnedClausesEntailed(const sat::CnfFormula& original,
                           const sat::CnfFormula& augmented) {
    for (std::uint64_t mask = 0; mask < (1ULL << original.variableCount); ++mask) {
        if (!formulaSatisfied(original, mask)) {
            continue;
        }
        if (!formulaSatisfied(augmented, mask)) {
            return false;
        }
    }
    return true;
}

void testSimpleCases() {
    const std::vector<std::pair<sat::CnfFormula, bool>> cases = {
        {{0, {}}, true},
        {{0, {sat::Clause{}}}, false},
        {{1, {}}, true},
        {{1, {{1}}}, true},
        {{1, {{1}, {-1}}}, false},
        {{2, {{1, 2}, {-1, 2}}}, true},
        {{2, {{1, 2}, {1, -2}, {-1, 2}, {-1, -2}}}, false},
        {{2, {{1, -1}, {2, -2}}}, true},
        {{2, {{1, 1}, {-1, 2}, {-1, -2}}}, false},
        {{5, {{-5}}}, true}
    };

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& [cnf, expected] = cases[i];
        sat::Solver solver(cnf);
        const auto status = solver.solveCdcl();
        check((status == sat::SolveStatus::Satisfiable) == expected,
              "basic SAT verdict #" + std::to_string(i));
        if (expected) {
            check(validTotalModel(cnf, solver.model()),
                  "basic SAT model #" + std::to_string(i));
        } else {
            check(solver.model().empty(), "UNSAT model cleared #" + std::to_string(i));
            check(solver.decisionLevel() == 0, "UNSAT ends at root level");
        }
        check(learnedClausesEntailed(cnf, solver.formula()),
              "basic learned clause entailment #" + std::to_string(i));
        solver.backtrack(0);
        check(solver.solveCdcl() == status, "repeat solving #" + std::to_string(i));
    }
}

void testFirstUipAndBackjump() {
    // One decision level: learn -x1 from contradictory implications.
    const sat::CnfFormula unsat{2,
        {{1, 2}, {1, -2}, {-1, 2}, {-1, -2}}};
    sat::Solver solver(unsat);
    check(solver.solveCdcl() == sat::SolveStatus::Unsatisfiable,
          "CDCL proves non-unit UNSAT");
    check(solver.stats().learnedClauses > 0, "learned clauses were added");
    check(solver.stats().conflicts >= 2, "root conflict follows learning");
    check(learnedClausesEntailed(unsat, solver.formula()),
          "non-unit UNSAT learned clauses entailed");

    // Three levels with conflict independent of two earlier decisions:
    // learn (-x3), jumping from level 3 all the way to root.
    const sat::CnfFormula jumpToRoot{18,
        {{1, 5}, {2, 6}, {3, 7}, {-3, 4}, {-3, -4}, {1, 8}, {1, 9}, {1, 10}, {1, 11}, {1, 12}, {1, 13}, {2, 14}, {2, 15}, {2, 16}, {2, 17}, {2, 18}}};
    sat::Solver rootJump(jumpToRoot);
    check(rootJump.solveCdcl() == sat::SolveStatus::Satisfiable,
          "non-chronological backjump to root solves SAT");
    check(rootJump.stats().nonChronologicalBackjumps >= 1,
          "backjump skipped at least one decision level");
    check(rootJump.stats().learnedClauses >= 1, "learned asserting unit clause");
    check(rootJump.learningEvents().size() == rootJump.stats().learnedClauses,
          "one trace event per learned clause");
    check(rootJump.learningEvents().front().conflictLevel == 3 &&
              rootJump.learningEvents().front().backjumpLevel == 0,
          "trace records actual non-chronological level 3 to 0 jump");
    check(rootJump.formula().clauses[jumpToRoot.clauses.size()] == sat::Clause{-3},
          "First-UIP clause is (-x3)");
    check(rootJump.value(3) == false, "learned unit forces x3=false");
    check(validTotalModel(jumpToRoot, rootJump.model()), "root-jump model valid");
    check(learnedClausesEntailed(jumpToRoot, rootJump.formula()),
          "root-jump learned clause is entailed");

    // Conflict involves decision 1 and decision 3, not decision 2:
    // learn (-x3 OR -x1) and jump directly to level 1.
    const sat::CnfFormula jumpToOne{18,
        {{1, 5}, {2, 6}, {3, 7}, {-1, -3, 4}, {-1, -3, -4}, {1, 8}, {1, 9}, {1, 10}, {1, 11}, {1, 12}, {1, 13}, {2, 14}, {2, 15}, {2, 16}, {2, 17}, {2, 18}}};
    sat::Solver oneJump(jumpToOne);
    check(oneJump.solveCdcl() == sat::SolveStatus::Satisfiable,
          "backjump to earlier non-root level solves SAT");
    check(oneJump.stats().nonChronologicalBackjumps >= 1,
          "non-root backjump skips a level");
    check(oneJump.stats().learnedClauses >= 1, "non-root asserting clause learned");
    check(oneJump.learningEvents().front().conflictLevel == 3 &&
              oneJump.learningEvents().front().backjumpLevel == 1,
          "trace records jump from level 3 to level 1");
    check(oneJump.formula().clauses[jumpToOne.clauses.size()] ==
              sat::Clause({-3, -1}), "First-UIP learned clause is (-x3 OR -x1)");
    check(learnedClausesEntailed(jumpToOne, oneJump.formula()),
          "learned non-unit clause entailed");
    check(validTotalModel(jumpToOne, oneJump.model()), "non-root jump model valid");
}

void testDecisionLevelPrecondition() {
    sat::Solver solver({1, {{1}}});
    solver.decide(-1);
    bool threw = false;
    try {
        (void)solver.solveCdcl();
    } catch (const std::logic_error&) {
        threw = true;
    }
    check(threw, "CDCL refuses preexisting non-root decisions");
    solver.backtrack(0);
    check(solver.solveCdcl() == sat::SolveStatus::Satisfiable,
          "CDCL solves after restoring root");
}

void testRandomExhaustive() {
    std::mt19937 rng(20261016);
    std::uniform_int_distribution<int> variables(0, 6);
    std::uniform_int_distribution<int> clauses(0, 20);
    std::uniform_int_distribution<int> clauseSize(0, 6);
    std::uniform_int_distribution<int> sign(0, 1);

    // Independent truth-table oracle, including duplicate literals,
    // tautologies, unused variables and zero-length clauses.
    for (int trial = 0; trial < 1000; ++trial) {
        sat::CnfFormula cnf;
        cnf.variableCount = variables(rng);
        const int nClauses = clauses(rng);
        for (int i = 0; i < nClauses; ++i) {
            sat::Clause clause;
            const int size = cnf.variableCount == 0 ? 0 : clauseSize(rng);
            for (int j = 0; j < size; ++j) {
                std::uniform_int_distribution<int> variable(1, cnf.variableCount);
                int literal = variable(rng);
                if (sign(rng)) {
                    literal = -literal;
                }
                clause.push_back(literal);
            }
            cnf.clauses.push_back(std::move(clause));
        }

        sat::Solver cdcl(cnf);
        const bool actual = cdcl.solveCdcl() == sat::SolveStatus::Satisfiable;
        check(actual == bruteForceSat(cnf),
              "CDCL matches exhaustive oracle #" + std::to_string(trial));
        check(learnedClausesEntailed(cnf, cdcl.formula()),
              "CDCL learned clauses are entailed #" + std::to_string(trial));
        if (actual) {
            check(validTotalModel(cnf, cdcl.model()),
                  "CDCL returned valid model #" + std::to_string(trial));
        } else {
            check(cdcl.model().empty(), "UNSAT has no model");
        }
        sat::Solver dpll(cnf);
        check((dpll.solve() == sat::SolveStatus::Satisfiable) == actual,
              "CDCL agrees with DPLL #" + std::to_string(trial));
    }
}

void testNontrivialRandom3Cnf() {
    std::mt19937 rng(103299);
    std::uniform_int_distribution<int> variables(6, 9);
    std::uniform_int_distribution<int> clauses(10, 45);
    std::uniform_int_distribution<int> length(2, 3);
    std::uniform_int_distribution<int> sign(0, 1);
    int formulasWithLearning = 0;

    // Harder than random clauses with frequent empty/unit clauses:
    // every clause has 2-3 literals, forcing search and conflict analysis.
    for (int trial = 0; trial < 600; ++trial) {
        sat::CnfFormula cnf;
        cnf.variableCount = variables(rng);
        const int count = clauses(rng);
        for (int i = 0; i < count; ++i) {
            sat::Clause clause;
            const int size = length(rng);
            for (int j = 0; j < size; ++j) {
                std::uniform_int_distribution<int> variable(1, cnf.variableCount);
                const int v = variable(rng);
                clause.push_back(sign(rng) ? v : -v);
            }
            cnf.clauses.push_back(std::move(clause));
        }
        sat::Solver solver(cnf);
        const bool actual = solver.solveCdcl() == sat::SolveStatus::Satisfiable;
        check(actual == bruteForceSat(cnf),
              "non-unit CNF exhaustive verdict #" + std::to_string(trial));
        check(learnedClausesEntailed(cnf, solver.formula()),
              "non-unit CNF every learned clause entailed #" + std::to_string(trial));
        check(solver.stats().learnedClauses == solver.learningEvents().size(),
              "learned clause/event counts agree");
        if (actual) {
            check(validTotalModel(cnf, solver.model()), "non-unit model verified");
        }
        if (solver.stats().learnedClauses != 0) {
            ++formulasWithLearning;
        }
    }
    check(formulasWithLearning > 30,
          "nontrivial randomized cases exercised conflict learning");
}

} // namespace

int main() {
    try {
        testSimpleCases();
        testFirstUipAndBackjump();
        testDecisionLevelPrecondition();
        testRandomExhaustive();
        testNontrivialRandom3Cnf();
        std::cout << "CDCL: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
