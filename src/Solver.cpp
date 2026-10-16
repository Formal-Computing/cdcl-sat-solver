#include "Solver.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace sat {

Solver::Solver(CnfFormula formula)
    : formula_(std::move(formula)),
      assignments_(formula_.variableCount > 0
                       ? static_cast<std::size_t>(formula_.variableCount) + 1
                       : 1, 0),
      watchLists_((static_cast<std::size_t>(
                       formula_.variableCount > 0 ? formula_.variableCount : 0) + 1) * 2) {
    if (formula_.variableCount < 0) {
        throw std::invalid_argument("CNF variable count cannot be negative");
    }
    for (const Clause& clause : formula_.clauses) {
        for (const Literal literal : clause) {
            requireValidLiteral(literal);
        }
    }
}

void Solver::requireValidLiteral(const Literal literal) const {
    // Compare as long long to avoid overflow on INT_MIN.
    const long long n = static_cast<long long>(literal);
    if (n == 0 || n > formula_.variableCount || -n > formula_.variableCount) {
        throw std::invalid_argument("literal is zero or outside declared variable range");
    }
}

std::optional<bool> Solver::value(const int variable) const {
    if (variable <= 0 || variable > formula_.variableCount) {
        throw std::out_of_range("variable outside declared range");
    }
    const auto assignment = assignments_[static_cast<std::size_t>(variable)];
    if (assignment == 0) {
        return std::nullopt;
    }
    return assignment == 1;
}

std::optional<bool> Solver::literalValue(const Literal literal) const {
    // All stored clauses and decisions have already been validated.
    const int variable = literal > 0 ? literal : -literal;
    const auto assignment = value(variable);
    if (!assignment) {
        return std::nullopt;
    }
    return literal > 0 ? *assignment : !*assignment;
}

void Solver::assign(const Literal literal,
                    const std::optional<std::size_t> reasonClause) {
    requireValidLiteral(literal);
    const int variable = literal > 0 ? literal : -literal;
    if (assignments_[static_cast<std::size_t>(variable)] != 0) {
        throw std::logic_error("attempted to assign a variable twice");
    }
    assignments_[static_cast<std::size_t>(variable)] = literal > 0 ? 1 : -1;
    trail_.push_back({literal, decisionLevel_, reasonClause});
    if (tracing_) {
        std::string detail;
        if (reasonClause) {
            detail = "BCP forces " + std::to_string(literal) +
                ": all other literals in clause " + std::to_string(*reasonClause + 1) +
                " are false.";
        } else {
            detail = "Decision: set " + std::to_string(literal) +
                " to true at level " + std::to_string(decisionLevel_) + ".";
        }
        logEvent(reasonClause ? "propagate" : "decision",
                 std::move(detail), literal, reasonClause);
    }
}

void Solver::decide(const Literal literal) {
    requireValidLiteral(literal);
    const int variable = literal > 0 ? literal : -literal;
    if (assignments_[static_cast<std::size_t>(variable)] != 0) {
        throw std::invalid_argument("decision variable is already assigned");
    }
    ++decisionLevel_;
    assign(literal, std::nullopt);
}

void Solver::backtrack(const int targetLevel) {
    if (targetLevel < 0 || targetLevel > decisionLevel_) {
        throw std::out_of_range("invalid backtrack decision level");
    }
    const int previousLevel = decisionLevel_;
    while (!trail_.empty() && trail_.back().decisionLevel > targetLevel) {
        const Literal literal = trail_.back().literal;
        const int variable = literal > 0 ? literal : -literal;
        assignments_[static_cast<std::size_t>(variable)] = 0;
        trail_.pop_back();
    }
    decisionLevel_ = targetLevel;
    if (tracing_ && targetLevel != previousLevel) {
        logEvent("backjump", "Undo assignments above level " +
                 std::to_string(targetLevel) + " (from level " +
                 std::to_string(previousLevel) + ").", 0, std::nullopt, targetLevel);
    }
    // Watches are stable under undo; only the pending trail queue moves back.
    propagationHead_ = std::min(propagationHead_, trail_.size());
}


bool Solver::searchDpll() {
    const std::size_t trailBefore = trail_.size();
    const auto conflict = propagate();
    stats_.unitPropagations += trail_.size() - trailBefore;

    if (conflict) {
        ++stats_.conflicts;
        return false;
    }

    const BranchChoice choice = chooseBranchVariable();
    const int variable = choice.variable;
    if (variable == 0) {
        return true;
    }

    logEvent("moms", "MOMS picks x" + std::to_string(variable) +
             " (" + std::to_string(choice.occurrences) +
             " occurrences in shortest unresolved clauses of length " +
             std::to_string(choice.minResidualSize) + ").", variable);
    const int parentLevel = decisionLevel_;

    // Try x=true first, then x=false. Every failed branch is undone.
    for (const Literal decision : {variable, -variable}) {
        decide(decision);
        ++stats_.decisions;
        if (searchDpll()) {
            return true;
        }
        backtrack(parentLevel);
        ++stats_.backtracks;
    }
    return false;
}

SolveStatus Solver::solve() {
    // Decisions made through the low-level API are not a set of assumptions.
    if (decisionLevel_ != 0) {
        throw std::logic_error("solve() requires decision level zero");
    }

    model_.clear();
    stats_ = {};
    if (!searchDpll()) {
        return SolveStatus::Unsatisfiable;
    }

    // The satisfying trail may be partial. Fill irrelevant variables with
    // false to produce a total, readily checkable DIMACS model.
    model_.reserve(static_cast<std::size_t>(formula_.variableCount));
    for (int variable = 1; variable <= formula_.variableCount; ++variable) {
        model_.push_back(value(variable).value_or(false) ? variable : -variable);
    }
    return SolveStatus::Satisfiable;
}

} // namespace sat
