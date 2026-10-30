#pragma once

#include "Clause.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sat {

// Each assignment remembers its decision level and the clause that implied it.
// A decision has no reasonClause; a propagated assignment has one.
struct TrailEntry {
    Literal literal;
    int decisionLevel;
    std::optional<std::size_t> reasonClause;
};

enum class SolveStatus { Satisfiable, Unsatisfiable };

struct SolveStats {
    std::size_t decisions = 0;
    std::size_t conflicts = 0;
    std::size_t backtracks = 0;
    std::size_t unitPropagations = 0;
    std::size_t learnedClauses = 0;
    std::size_t nonChronologicalBackjumps = 0;
};

struct LearningEvent {
    Clause clause;
    int conflictLevel;
    int backjumpLevel;
};

struct BranchChoice {
    int variable = 0;
    std::size_t minResidualSize = 0;
    std::size_t occurrences = 0;
};

struct SolverEvent {
    std::string type;
    std::string detail;
    int level = 0;
    Literal literal = 0;
    std::optional<std::size_t> clauseIndex;
    int targetLevel = -1;
    Clause learned;
    std::vector<TrailEntry> trail;
};

class Solver {
public:
    explicit Solver(CnfFormula formula);

    // Repeatedly apply unit propagation until no new literals are implied.
    // On contradiction, return the zero-based index of the falsified clause.
    // nullopt means only that BCP reached a consistent fixed point, NOT SAT.
    [[nodiscard]] std::optional<std::size_t> propagate();

    // Create a new decision level and assign the chosen literal to true.
    // The variable must currently be unassigned.
    void decide(Literal literal);

    // Remove assignments at levels greater than targetLevel.
    void backtrack(int targetLevel);

    // Preserve original recursive DPLL algorithm for comparison.
    [[nodiscard]] SolveStatus solve();

    // First-UIP CDCL: learn asserting clauses and jump to the highest
    // remaining decision level (possibly skipping several levels).
    // Learned clauses are retained inside formula() for inspection.
    [[nodiscard]] SolveStatus solveCdcl();

    void setTracing(bool enabled) noexcept { tracing_ = enabled; }
    // Stream complete state snapshots as they happen; do not retain an unbounded
    // second copy in memory for the browser.
    void setLiveTrace(std::ostream& output) noexcept {
        tracing_ = true;
        liveTrace_ = &output;
    }
    void writeLiveHeader(std::ostream& output, const CnfFormula& original) const;
    void writeLiveFinal(std::ostream& output, SolveStatus result) const;
    [[nodiscard]] const std::vector<SolverEvent>& events() const noexcept { return events_; }
    [[nodiscard]] BranchChoice previewMoms() const { return chooseBranchVariable(); }
    [[nodiscard]] bool allClausesSatisfied() const;
    void writeEventJson(std::ostream& output, const CnfFormula& original,
                        SolveStatus result) const;
    void writeVerbose(std::ostream& output) const;


    // A total model for the latest SAT result; empty for UNSAT.
    [[nodiscard]] const std::vector<Literal>& model() const noexcept { return model_; }
    [[nodiscard]] const SolveStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const std::vector<LearningEvent>& learningEvents() const noexcept {
        return learningEvents_;
    }

    // nullopt: unassigned; otherwise the variable's Boolean value.
    [[nodiscard]] std::optional<bool> value(int variable) const;
    [[nodiscard]] int decisionLevel() const noexcept { return decisionLevel_; }
    [[nodiscard]] const std::vector<TrailEntry>& trail() const noexcept { return trail_; }
    [[nodiscard]] const CnfFormula& formula() const noexcept { return formula_; }

private:
    CnfFormula formula_;
    // Index 0 is unused; 0=unassigned, +1=true, -1=false.
    std::vector<std::int8_t> assignments_;
    std::vector<TrailEntry> trail_;
    int decisionLevel_ = 0;
    std::vector<Literal> model_;
    SolveStats stats_;
    std::vector<LearningEvent> learningEvents_;
    bool tracing_ = false;
    std::ostream* liveTrace_ = nullptr;
    std::vector<SolverEvent> events_;
    void logEvent(std::string type, std::string detail,
                  Literal literal = 0,
                  std::optional<std::size_t> clause = std::nullopt,
                  int targetLevel = -1, Clause learned = {});

    // Two-watched-literal BCP. Watched clauses are normalized only for
    // propagation; formula_ retains stable original/learned clause indexes.
    struct WatchEntry {
        std::size_t clauseIndex;
        std::size_t slot; // 0 or 1
    };
    std::vector<Clause> watchedClauses_;
    std::vector<std::array<std::size_t, 2>> watchedPositions_;
    std::vector<std::vector<WatchEntry>> watchLists_;
    std::vector<std::size_t> unitClauses_;
    std::size_t attachedClauses_ = 0;
    std::size_t propagationHead_ = 0;
    // A contradiction at decision level 0 is permanent across solve() calls.
    std::optional<std::size_t> rootConflict_;

    [[nodiscard]] std::size_t watchIndex(Literal literal) const;
    [[nodiscard]] std::optional<std::size_t> attachNewClauses();

    void requireValidLiteral(Literal literal) const;
    [[nodiscard]] std::optional<bool> literalValue(Literal literal) const;
    void assign(Literal literal, std::optional<std::size_t> reasonClause);
    // Return zero when every clause is satisfied; otherwise select an
    // unassigned variable in a still-unsatisfied clause.
    [[nodiscard]] BranchChoice chooseBranchVariable() const;
    [[nodiscard]] bool searchDpll();

    // Conflict clause must be falsified at a positive decision level.
    // Result: first-UIP asserting clause (index 0 is UIP literal), and
    // the highest decision level among its remaining literals.
    [[nodiscard]] std::pair<Clause, int> analyzeConflict(std::size_t conflict) const;
};

} // namespace sat
