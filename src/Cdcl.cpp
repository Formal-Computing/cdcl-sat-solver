#include "Solver.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sat {

std::pair<Clause, int> Solver::analyzeConflict(const std::size_t conflict) const {
    if (decisionLevel_ == 0 || conflict >= formula_.clauses.size()) {
        throw std::logic_error("First-UIP requires a valid non-root conflict");
    }

    // Use the existing trail as the single source of truth for reasons and
    // assignment levels. This avoids duplicating mutable solver state.
    const std::size_t count = static_cast<std::size_t>(formula_.variableCount) + 1;
    std::vector<int> level(count, -1);
    std::vector<std::optional<std::size_t>> reason(count);
    for (const TrailEntry& entry : trail_) {
        const int variable = entry.literal > 0 ? entry.literal : -entry.literal;
        level[static_cast<std::size_t>(variable)] = entry.decisionLevel;
        reason[static_cast<std::size_t>(variable)] = entry.reasonClause;
    }

    std::vector<bool> seen(count, false);
    Clause learned{0}; // Filled with the negated First-UIP literal.
    std::size_t cursor = trail_.size();
    int pendingCurrentLevel = 0;
    int resolvedVariable = 0;
    std::size_t reasonIndex = conflict;
    Literal pivot = 0;

    for (;;) {
        // Resolve the current conflict/reason clause with the previous UIP
        // candidate. The pivot variable is omitted from its own reason.
        for (const Literal literal : formula_.clauses[reasonIndex]) {
            const int variable = literal > 0 ? literal : -literal;
            if (variable == resolvedVariable ||
                seen[static_cast<std::size_t>(variable)]) {
                continue;
            }
            if (level[static_cast<std::size_t>(variable)] < 0) {
                throw std::logic_error("conflict analysis found unassigned literal");
            }
            seen[static_cast<std::size_t>(variable)] = true;
            if (level[static_cast<std::size_t>(variable)] == decisionLevel_) {
                ++pendingCurrentLevel;
            } else {
                // Falsified earlier-level literal retained in learned clause.
                learned.push_back(literal);
            }
        }

        // Find most recently assigned variable still present at this level.
        do {
            if (cursor == 0) {
                throw std::logic_error("First-UIP implication trail exhausted");
            }
            pivot = trail_[--cursor].literal;
        } while (!seen[static_cast<std::size_t>(
            pivot > 0 ? pivot : -pivot)]);

        const int variable = pivot > 0 ? pivot : -pivot;
        seen[static_cast<std::size_t>(variable)] = false;
        --pendingCurrentLevel;

        if (pendingCurrentLevel == 0) {
            learned[0] = -pivot;
            break;
        }

        if (!reason[static_cast<std::size_t>(variable)]) {
            throw std::logic_error("reached a decision before First-UIP");
        }
        reasonIndex = *reason[static_cast<std::size_t>(variable)];
        resolvedVariable = variable;
    }

    // Only one literal from the conflicting decision level remains.
    // Jump to the maximum of all other levels; the learned clause will
    // immediately force its First-UIP literal at that target level.
    int backjumpLevel = 0;
    for (std::size_t i = 1; i < learned.size(); ++i) {
        const int variable = learned[i] > 0 ? learned[i] : -learned[i];
        backjumpLevel = std::max(backjumpLevel, level[static_cast<std::size_t>(variable)]);
    }
    if (backjumpLevel >= decisionLevel_) {
        throw std::logic_error("learned First-UIP clause is not asserting");
    }
    return {std::move(learned), backjumpLevel};
}

SolveStatus Solver::solveCdcl() {
    if (decisionLevel_ != 0) {
        throw std::logic_error("solveCdcl() requires decision level zero");
    }

    model_.clear();
    stats_ = {};
    learningEvents_.clear();
    events_.clear();
    logEvent("start", "Start CDCL: propagate root-level unit clauses, then choose MOMS decisions.");

    for (;;) {
        const std::size_t trailBefore = trail_.size();
        const auto conflict = propagate();
        stats_.unitPropagations += trail_.size() - trailBefore;

        if (conflict) {
            ++stats_.conflicts;
            if (decisionLevel_ == 0) {
                // A root-level contradiction proves UNSAT.
                logEvent("unsat", "Conflict at decision level zero proves UNSAT.");
                return SolveStatus::Unsatisfiable;
            }

            auto [learned, jumpLevel] = analyzeConflict(*conflict);
            const int priorLevel = decisionLevel_;
            logEvent("learn",
                     "First-UIP resolves implication reasons into an asserting clause; "
                     "the remaining literals determine backjump level " + std::to_string(jumpLevel) + ".",
                     learned.front(), formula_.clauses.size(), jumpLevel, learned);
            backtrack(jumpLevel);
            ++stats_.backtracks;
            if (jumpLevel < priorLevel - 1) {
                ++stats_.nonChronologicalBackjumps;
            }

            // Reason indexes are stable since clauses are only appended.
            const std::size_t learnedIndex = formula_.clauses.size();
            formula_.clauses.push_back(std::move(learned));
            ++stats_.learnedClauses;

            const Clause& asserting = formula_.clauses[learnedIndex];
            // An asserting clause has exactly one unset literal here.
            if (literalValue(asserting.front()).has_value()) {
                throw std::logic_error("learned UIP literal was not unset after backjump");
            }
            for (std::size_t i = 1; i < asserting.size(); ++i) {
                if (literalValue(asserting[i]) != false) {
                    throw std::logic_error("learned clause is not unit after backjump");
                }
            }
            assign(asserting.front(), learnedIndex);
            learningEvents_.push_back({asserting, priorLevel, jumpLevel});
            ++stats_.unitPropagations;
            continue;
        }

        const BranchChoice choice = chooseBranchVariable();
        const int variable = choice.variable;
        if (variable == 0) {
            // Every clause is satisfied. Totalize the model for DIMACS output.
            model_.reserve(static_cast<std::size_t>(formula_.variableCount));
            for (int v = 1; v <= formula_.variableCount; ++v) {
                model_.push_back(value(v).value_or(false) ? v : -v);
            }
            logEvent("sat", "All clauses are satisfied. Remaining unconstrained variables may be set arbitrarily.");
            return SolveStatus::Satisfiable;
        }

        logEvent("moms", "MOMS: among unresolved clauses of minimum size " +
                  std::to_string(choice.minResidualSize) + ", x" +
                  std::to_string(variable) + " occurs " +
                  std::to_string(choice.occurrences) +
                  " times (ties: lowest variable number).", variable);
        decide(variable);
        ++stats_.decisions;
    }
}

} // namespace sat
