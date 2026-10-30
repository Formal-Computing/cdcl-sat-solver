#include "Solver.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

int checks = 0;

void check(const bool condition, const std::string& description) {
    if (!condition) {
        throw std::runtime_error("FAIL: " + description);
    }
    ++checks;
}

template <typename Exception, typename Function>
void checkThrows(Function function, const std::string& description) {
    try {
        function();
    } catch (const Exception&) {
        ++checks;
        return;
    }
    throw std::runtime_error("FAIL: expected exception for " + description);
}

void testNoUnits() {
    sat::Solver solver({3, {{1, 2}, {-1, 3}}});
    check(!solver.propagate(), "no initial conflict");
    check(solver.trail().empty(), "no forced assignments");
    check(!solver.value(1) && !solver.value(2), "unassigned values");
    check(!solver.propagate(), "repeated BCP reaches same fixed point");

    sat::Solver tautology({1, {{1, -1}}});
    check(!tautology.propagate() && tautology.trail().empty(),
          "tautological clause does not force either value");
}

void testUnitChain() {
    // First clause is initially non-unit; revisiting it is essential.
    sat::Solver solver({3, {{-2, 3}, {-1, 2}, {1}}});
    check(!solver.propagate(), "implication chain is consistent");
    check(solver.trail().size() == 3, "full implication chain reaches fixed point");
    check(solver.value(1) == true && solver.value(2) == true &&
              solver.value(3) == true, "all chain assignments true");
    check(solver.trail()[0].literal == 1 && solver.trail()[0].reasonClause == 2,
          "first implication records reason clause");
    check(solver.trail()[1].literal == 2 && solver.trail()[1].reasonClause == 1,
          "second implication records reason clause");
    check(solver.trail()[2].literal == 3 && solver.trail()[2].reasonClause == 0,
          "third implication records reason clause");
    check(solver.trail()[2].decisionLevel == 0, "root-level implications");
    check(!solver.propagate() && solver.trail().size() == 3,
          "propagation is idempotent");
}

void testNegativeAndDuplicateUnits() {
    sat::Solver solver({2, {{-1}, {2, 2}}});
    check(!solver.propagate(), "negative and duplicate unit clauses consistent");
    check(solver.value(1) == false && solver.value(2) == true,
          "negative literal and repeated unit literal");
}

void testRootConflict() {
    sat::Solver solver({1, {{1}, {-1}}});
    const auto conflict = solver.propagate();
    check(conflict && *conflict == 1, "conflict points to falsified second clause");
    check(solver.trail().size() == 1 && solver.trail()[0].literal == 1,
          "trail holds the first forced assignment");

    sat::Solver emptyClause({0, {sat::Clause{}}});
    check(emptyClause.propagate() == 0, "empty clause immediately conflicts");

    sat::Solver emptyFormula({0, {}});
    check(!emptyFormula.propagate() && emptyFormula.trail().empty(),
          "zero-clause formula has no conflict");
}

void testDecisionAndBacktrack() {
    sat::Solver solver({6, {
        {1}, {-2, 3}, {-3, 4}, {-4, 5}
    }});
    check(!solver.propagate() && solver.value(1) == true,
          "root unit before decision");
    solver.decide(2);
    check(solver.decisionLevel() == 1 && !solver.trail().back().reasonClause,
          "decision creates new level without a reason");
    check(!solver.propagate(), "decision-driven propagation succeeds");
    check(solver.trail().size() == 5, "decision forces three additional variables");
    check(solver.value(3) == true && solver.value(4) == true &&
              solver.value(5) == true, "decision implications");
    check(solver.trail().back().decisionLevel == 1 &&
              solver.trail().back().reasonClause == 3,
          "implication reason and decision level recorded");

    solver.decide(-6);
    check(solver.decisionLevel() == 2 && solver.value(6) == false,
          "second decision can assign false");
    solver.backtrack(1);
    check(solver.decisionLevel() == 1 && !solver.value(6) &&
              solver.value(5) == true, "backtrack one level");
    solver.backtrack(0);
    check(solver.trail().size() == 1 && solver.value(1) == true &&
              !solver.value(2) && !solver.value(3) && !solver.value(4) &&
              !solver.value(5), "backtrack keeps only root implications");
    check(!solver.propagate() && solver.trail().size() == 1,
          "repropagate after backtracking leaves root fixed point");
}

void testDecisionConflict() {
    sat::Solver solver({2, {{-1, -2}, {-1, 2}}});
    check(!solver.propagate() && solver.trail().empty(), "no root units");
    solver.decide(1);
    check(solver.propagate() == 1, "decision produces conflicting implications");
    check(solver.value(2) == false && solver.decisionLevel() == 1,
          "conflicting propagation records the implication");
    solver.backtrack(0);
    check(solver.trail().empty() && !solver.value(1) && !solver.value(2),
          "conflict trail cleared by backtracking");
}

void testInvalidOperations() {
    checkThrows<std::invalid_argument>([] { sat::Solver({1, {{2}}}); },
                                       "literal exceeds declared variable count");
    checkThrows<std::invalid_argument>([] { sat::Solver({1, {{0}}}); },
                                       "zero literal in internal formula");
    checkThrows<std::invalid_argument>([] { sat::Solver({-1, {}}); },
                                       "negative variable count");
    sat::Solver solver({1, {}});
    checkThrows<std::out_of_range>([&] { (void)solver.value(0); },
                                  "zero variable index");
    checkThrows<std::out_of_range>([&] { (void)solver.value(2); },
                                  "variable above range");
    checkThrows<std::invalid_argument>([&] { solver.decide(0); },
                                       "zero decision literal");
    solver.decide(-1);
    checkThrows<std::invalid_argument>([&] { solver.decide(1); },
                                       "deciding assigned variable");
    checkThrows<std::out_of_range>([&] { solver.backtrack(-1); },
                                  "negative backtrack level");
    checkThrows<std::out_of_range>([&] { solver.backtrack(2); },
                                  "backtrack to future level");
    solver.backtrack(0);
    check(!solver.value(1) && solver.trail().empty(),
          "successful backtrack clears decision");
}

} // namespace

int main() {
    try {
        testNoUnits();
        testUnitChain();
        testNegativeAndDuplicateUnits();
        testRootConflict();
        testDecisionAndBacktrack();
        testDecisionConflict();
        testInvalidOperations();
        std::cout << "BCP: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
