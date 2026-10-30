#include "Solver.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace sat {

std::size_t Solver::watchIndex(const Literal literal) const {
    const auto variable = static_cast<std::size_t>(literal > 0 ? literal : -literal);
    return 2 * variable + (literal < 0 ? 1U : 0U);
}

// Attach only new clauses. This includes CDCL learned clauses appended to
// formula_.clauses; original clause/reason indexes never change.
std::optional<std::size_t> Solver::attachNewClauses() {
    std::optional<std::size_t> conflict;

    while (attachedClauses_ < formula_.clauses.size()) {
        const std::size_t index = attachedClauses_++;
        const Clause& original = formula_.clauses[index];

        // Normalize *only the watching representation*. Preserve original
        // clauses for explanations, First-UIP resolution and CLI inspection.
        std::unordered_set<Literal> seen;
        Clause normalized;
        bool tautology = false;
        for (Literal literal : original) {
            if (seen.contains(-literal)) {
                tautology = true;
                break;
            }
            if (seen.insert(literal).second) {
                normalized.push_back(literal);
            }
        }
        watchedClauses_.push_back(tautology ? Clause{} : std::move(normalized));
        watchedPositions_.push_back({0, 0});
        if (tautology) {
            continue; // This clause is always true.
        }

        const Clause& clause = watchedClauses_.back();
        if (clause.empty()) {
            if (!conflict) {
                conflict = index; // Empty clause is permanently UNSAT.
            }
            continue;
        }

        // Prefer non-false watched literals when attaching under an
        // existing partial assignment (e.g. after learning a clause).
        std::vector<std::size_t> live;
        live.reserve(2);
        bool satisfied = false;
        for (std::size_t i = 0; i < clause.size(); ++i) {
            const auto state = literalValue(clause[i]);
            if (state == true) {
                satisfied = true;
            }
            if (state != false && live.size() < 2) {
                live.push_back(i);
            }
        }

        if (clause.size() == 1) {
            unitClauses_.push_back(index);
        } else {
            std::size_t first = live.empty() ? 0 : live.front();
            std::size_t second = 0;
            if (live.size() > 1) {
                second = live[1];
            } else {
                // The other watch can be false; one unset watch makes
                // a unit clause, or one true watch makes it satisfied.
                second = first == 0 ? 1 : 0;
            }
            watchedPositions_[index] = {first, second};
            watchLists_[watchIndex(clause[first])].push_back({index, 0});
            watchLists_[watchIndex(clause[second])].push_back({index, 1});
        }

        if (!satisfied) {
            if (live.empty()) {
                if (!conflict) {
                    conflict = index;
                }
            } else if (live.size() == 1 && !conflict) {
                // Clause is unit under the current assignment.
                const Literal unit = clause[live.front()];
                if (!literalValue(unit).has_value()) {
                    assign(unit, index);
                }
            }
        }
    }
    return conflict;
}

std::optional<std::size_t> Solver::propagate() {
    if (rootConflict_) {
        logEvent("conflict", "Root-level contradiction remains in clause " +
                 std::to_string(*rootConflict_ + 1) + ".", 0, rootConflict_);
        return rootConflict_;
    }
    if (auto conflict = attachNewClauses()) {
        if (decisionLevel_ == 0) rootConflict_ = conflict;
        logEvent("conflict", "Clause " + std::to_string(*conflict + 1) +
                 " is falsified while attaching clauses.", 0, conflict);
        return conflict;
    }

    // Unit clauses are persistent: low-level callers may backtrack from
    // a conflicting decision made *before* the first propagation.
    // Rechecking units is O(number of unit clauses), not O(all clauses).
    for (std::size_t clauseIndex : unitClauses_) {
        const Literal unit = watchedClauses_[clauseIndex][0];
        const auto state = literalValue(unit);
        if (state == false) {
            if (decisionLevel_ == 0) rootConflict_ = clauseIndex;
            logEvent("conflict", "Unit clause " + std::to_string(clauseIndex + 1) +
                     " demands the opposite assignment.", 0, clauseIndex);
            return clauseIndex;
        }
        if (!state) {
            assign(unit, clauseIndex);
        }
    }

    // Process only newly assigned trail literals. When L is set true,
    // clauses watching -L are the only clauses that can become unit/false.
    while (propagationHead_ < trail_.size()) {
        const Literal falsified = -trail_[propagationHead_++].literal;
        auto& watching = watchLists_[watchIndex(falsified)];
        std::size_t i = 0;

        while (i < watching.size()) {
            const WatchEntry entry = watching[i];
            const Clause& clause = watchedClauses_[entry.clauseIndex];
            auto& pair = watchedPositions_[entry.clauseIndex];
            const std::size_t watched = pair[entry.slot];
            const std::size_t other = pair[1 - entry.slot];

            if (clause[watched] != falsified) {
                throw std::logic_error("invalid watched-literal list state");
            }

            const auto otherValue = literalValue(clause[other]);
            if (otherValue == true) {
                ++i;
                continue; // Clause already satisfied.
            }

            // Find a replacement watch that is not falsified.
            bool moved = false;
            for (std::size_t j = 0; j < clause.size(); ++j) {
                if (j == watched || j == other || literalValue(clause[j]) == false) {
                    continue;
                }
                pair[entry.slot] = j;
                watchLists_[watchIndex(clause[j])].push_back(entry);
                watching[i] = watching.back();
                watching.pop_back();
                moved = true;
                break;
            }
            if (moved) {
                continue; // Process the swapped-in entry at index i.
            }

            if (otherValue == false) {
                if (decisionLevel_ == 0) rootConflict_ = entry.clauseIndex;
                logEvent("conflict", "Both watched literals of clause " +
                         std::to_string(entry.clauseIndex + 1) +
                         " are false; this branch is inconsistent.", 0, entry.clauseIndex);
                return entry.clauseIndex; // Both watched literals false.
            }
            // The other watch is the only non-false literal: propagate it.
            assign(clause[other], entry.clauseIndex);
            ++i;
        }
    }
    return std::nullopt;
}

} // namespace sat
