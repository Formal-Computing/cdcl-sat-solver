#include "Solver.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;

void check(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error("FAIL: " + message);
    }
    ++checks;
}

bool evaluatesTrue(const sat::CnfFormula& cnf,
                   const std::vector<sat::Literal>& model) {
    if (model.size() != static_cast<std::size_t>(cnf.variableCount)) {
        return false;
    }
    std::vector<int> values(model.size() + 1, 0);
    for (std::size_t i = 0; i < model.size(); ++i) {
        const int variable = static_cast<int>(i) + 1;
        if (model[i] != variable && model[i] != -variable) {
            return false;
        }
        values[static_cast<std::size_t>(variable)] = model[i] > 0 ? 1 : -1;
    }
    for (const sat::Clause& clause : cnf.clauses) {
        bool satisfied = false;
        for (const sat::Literal literal : clause) {
            const auto assigned = values[static_cast<std::size_t>(
                literal > 0 ? literal : -literal)];
            if ((literal > 0 && assigned == 1) ||
                (literal < 0 && assigned == -1)) {
                satisfied = true;
                break;
            }
        }
        if (!satisfied) {
            return false;
        }
    }
    return true;
}

bool bruteForceSat(const sat::CnfFormula& cnf) {
    const std::uint64_t possibilities = std::uint64_t{1} << cnf.variableCount;
    for (std::uint64_t mask = 0; mask < possibilities; ++mask) {
        std::vector<sat::Literal> candidate;
        for (int variable = 1; variable <= cnf.variableCount; ++variable) {
            const bool isTrue = (mask & (std::uint64_t{1} << (variable - 1))) != 0;
            candidate.push_back(isTrue ? variable : -variable);
        }
        if (evaluatesTrue(cnf, candidate)) {
            return true;
        }
    }
    return false;
}

void checkSolver(const sat::CnfFormula& cnf, const bool expected,
                 const std::string& description) {
    sat::Solver solver(cnf);
    const auto status = solver.solve();
    const bool actual = status == sat::SolveStatus::Satisfiable;
    check(actual == expected, description + ": SAT/UNSAT result");
    if (actual) {
        check(evaluatesTrue(cnf, solver.model()), description + ": valid total model");
    } else {
        check(solver.model().empty(), description + ": no model for UNSAT");
        check(solver.decisionLevel() == 0, description + ": restored root level");
    }
    // solve() searches from the root, so reset a successful search trail first.
    solver.backtrack(0);
    check(solver.solve() == status, description + ": repeated solve after reset");
}

void testBasicCases() {
    checkSolver({0, {}}, true, "empty conjunction");
    checkSolver({0, {sat::Clause{}}}, false, "empty clause");
    checkSolver({1, {}}, true, "unused variable");
    checkSolver({3, {{1}, {-1, 2}, {-2, 3}}}, true, "unit chain");
    checkSolver({1, {{1}, {-1}}}, false, "contradictory units");
    checkSolver({3, {{1, 2}, {-1, 3}, {-2, 3}}}, true,
                "satisfiable without root units");
    checkSolver({2, {{1, -1}, {2, -2}}}, true, "tautological clauses");
    checkSolver({2, {{1, 1}, {-1, 2}}}, true, "repeated literals");
    checkSolver({5, {{-5}}}, true, "unused variables completed to a model");
}

void testNonUnitUnsat() {
    const sat::CnfFormula cnf{2, {{1, 2}, {1, -2}, {-1, 2}, {-1, -2}}};
    sat::Solver solver(cnf);
    check(!solver.propagate(), "2-SAT contradiction has no root-level unit conflict");
    check(solver.solve() == sat::SolveStatus::Unsatisfiable,
          "DPLL proves UNSAT after exploring both branches");
    check(solver.stats().decisions >= 2, "DPLL actually made decisions");
    check(solver.stats().conflicts >= 2, "both branches produce conflicts");
    check(solver.stats().backtracks >= 2, "both branches backtracked");
    check(solver.decisionLevel() == 0 && solver.model().empty(),
          "UNSAT restored root-level search state");
}

void testFalseBranchAndModel() {
    // Choosing x1=true causes x2 and !x2. The x1=false branch succeeds.
    const sat::CnfFormula cnf{2, {{-1, 2}, {-1, -2}, {1, 2}}};
    sat::Solver solver(cnf);
    check(solver.solve() == sat::SolveStatus::Satisfiable,
          "failed true branch followed by successful false branch");
    check(solver.stats().conflicts >= 1 && solver.stats().backtracks >= 1,
          "solver had to backtrack");
    check(solver.stats().decisions >= 2,
          "solver attempted both polarities of first decision");
    check(solver.value(1) == false, "solver chose correct false assignment");
    check(evaluatesTrue(cnf, solver.model()), "model from backtracked search satisfies CNF");

    // Low-level decide() is explicitly not a set of solving assumptions.
    sat::Solver partial({1, {{1}}});
    partial.decide(-1);
    try {
        (void)partial.solve();
        throw std::runtime_error("FAIL: solving with external decision was accepted");
    } catch (const std::logic_error&) {
        ++checks;
    }
    partial.backtrack(0);
    check(partial.solve() == sat::SolveStatus::Satisfiable,
          "solve after external decision is cleared");
}

void testRandomAgainstExhaustiveSearch() {
    std::mt19937 generator(20261016);
    std::uniform_int_distribution<int> variableCount(0, 5);
    std::uniform_int_distribution<int> clauseCount(0, 14);
    std::uniform_int_distribution<int> length(0, 5);
    std::uniform_int_distribution<int> polarity(0, 1);

    // Deterministic cross-check: 400 formulas, max 5 variables. Brute
    // force enumerates every assignment independently of DPLL.
    for (int trial = 0; trial < 400; ++trial) {
        sat::CnfFormula cnf;
        cnf.variableCount = variableCount(generator);
        for (int c = 0, count = clauseCount(generator); c < count; ++c) {
            sat::Clause clause;
            const int n = cnf.variableCount == 0 ? 0 : length(generator);
            for (int j = 0; j < n; ++j) {
                std::uniform_int_distribution<int> variable(1, cnf.variableCount);
                const int v = variable(generator);
                clause.push_back(polarity(generator) ? v : -v);
            }
            cnf.clauses.push_back(std::move(clause));
        }

        const bool expected = bruteForceSat(cnf);
        sat::Solver solver(cnf);
        const bool actual = solver.solve() == sat::SolveStatus::Satisfiable;
        check(expected == actual, "exhaustive cross-check #" + std::to_string(trial));
        if (actual) {
            check(evaluatesTrue(cnf, solver.model()),
                  "exhaustive model check #" + std::to_string(trial));
        }
    }
}

} // namespace

int main() {
    try {
        testBasicCases();
        testNonUnitUnsat();
        testFalseBranchAndModel();
        testRandomAgainstExhaustiveSearch();
        std::cout << "DPLL: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
