#include "Solver.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int count = 0;
void check(bool ok, const std::string& label) {
    if (!ok) throw std::runtime_error("FAIL: " + label);
    ++count;
}
}

int main() {
    try {
        sat::Solver selection({5, {{1, 2}, {2, 3}, {2, 4}, {1, 5}}});
        const auto first = selection.previewMoms();
        check(first.variable == 2, "MOMS variable has maximal occurrences");
        check(first.minResidualSize == 2, "shortest unresolved clause has length 2");
        check(first.occurrences == 3, "x2 occurs in three shortest clauses");

        sat::Solver ties({4, {{1, 2}, {3, 4}}});
        check(ties.previewMoms().variable == 1, "lowest variable breaks MOMS tie");
        check(ties.previewMoms().occurrences == 1, "tied count is one");

        sat::Solver shortSize({5, {{1, 2, 3}, {2, 3}, {2, 4}, {5, 4}}});
        check(shortSize.previewMoms().variable == 2, "longer clause does not contribute");
        check(shortSize.previewMoms().occurrences == 2, "MOMS only counts shortest clauses");

        sat::Solver finished({2, {{1}}});
        check(!finished.propagate(), "root propagation consistent");
        check(finished.allClausesSatisfied(), "BCP recognizes SAT with unused variable");
        check(finished.previewMoms().variable == 0, "no branch when all clauses satisfied");

        sat::Solver pending({2, {{1, 2}}});
        check(!pending.propagate(), "no unit propagation");
        check(!pending.allClausesSatisfied(), "fixed point is unresolved, not SAT");

        sat::Solver sample({2, {{-1, 2}, {-1, -2}, {1, 2}}});
        sample.setTracing(true);
        check(sample.solveCdcl() == sat::SolveStatus::Satisfiable, "CDCL sample SAT");
        const auto& events = sample.events();
        check(events.front().type == "start", "trace starts with start event");
        check(events.back().type == "sat", "trace ends with SAT event");
        bool sawMoms=false, sawDecision=false, sawBCP=false,
             sawConflict=false, sawLearn=false, sawBackjump=false;
        for (const auto& e : events) {
            sawMoms |= e.type=="moms";
            sawDecision |= e.type=="decision";
            sawBCP |= e.type=="propagate";
            sawConflict |= e.type=="conflict";
            sawLearn |= e.type=="learn";
            sawBackjump |= e.type=="backjump";
            check(e.level >= 0, "nonnegative event level");
            for(const auto& t:e.trail)
                check(t.decisionLevel <= e.level, "event trail has legal level");
        }
        check(sawMoms && sawDecision && sawBCP, "branching and BCP recorded");
        check(sawConflict && sawLearn && sawBackjump, "learning and undo recorded");
        check(sample.stats().learnedClauses > 0, "actual clause learning happened");

        std::cout << "MOMS + event trace: " << count << " checks passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
