#include "Solver.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace sat {

void Solver::logEvent(std::string type, std::string detail, Literal literal,
                      std::optional<std::size_t> clause, int targetLevel, Clause learned) {
    if (!tracing_) return;
    events_.push_back({std::move(type), std::move(detail), decisionLevel_,
                       literal, clause, targetLevel, std::move(learned), trail_});
}

bool Solver::allClausesSatisfied() const {
    for (const Clause& clause : formula_.clauses) {
        bool satisfied = false;
        for (Literal literal : clause) {
            if (literalValue(literal) == true) {
                satisfied = true;
                break;
            }
        }
        if (!satisfied) return false;
    }
    return true;
}

namespace {

void jsonString(std::ostream& out, const std::string& s) {
    out << '"';
    for (unsigned char ch : s) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setfill('0')
                    << std::setw(4) << static_cast<int>(ch)
                    << std::dec << std::setfill(' ');
            } else out << ch;
        }
    }
    out << '"';
}

void jsonClause(std::ostream& out, const Clause& clause) {
    out << '[';
    for (std::size_t i = 0; i < clause.size(); ++i) {
        if (i) out << ',';
        out << clause[i];
    }
    out << ']';
}

void jsonClauses(std::ostream& out, const CnfFormula& f) {
    out << '[';
    for (std::size_t i = 0; i < f.clauses.size(); ++i) {
        if (i) out << ',';
        jsonClause(out, f.clauses[i]);
    }
    out << ']';
}

} // namespace

void Solver::writeEventJson(std::ostream& out, const CnfFormula& original,
                            SolveStatus result) const {
    out << "{\"version\":1,\"algorithm\":\"CDCL-MOMS\",\"variables\":"
        << original.variableCount << ",\"clauses\":";
    jsonClauses(out, original);
    out << ",\"events\":[";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        const SolverEvent& e = events_[i];
        out << "{\"type\":";
        jsonString(out, e.type);
        out << ",\"detail\":";
        jsonString(out, e.detail);
        out << ",\"level\":" << e.level << ",\"literal\":" << e.literal
            << ",\"clause\":";
        if (e.clauseIndex) out << *e.clauseIndex;
        else out << "null";
        out << ",\"target\":" << e.targetLevel << ",\"learned\":";
        jsonClause(out, e.learned);
        out << ",\"trail\":[";
        for (std::size_t j = 0; j < e.trail.size(); ++j) {
            if (j) out << ',';
            const TrailEntry& t = e.trail[j];
            out << "{\"literal\":" << t.literal << ",\"level\":"
                << t.decisionLevel << ",\"reason\":";
            if (t.reasonClause) out << *t.reasonClause;
            else out << "null";
            out << '}';
        }
        out << "]}";
    }
    out << "],\"status\":";
    jsonString(out, result == SolveStatus::Satisfiable ? "SAT" : "UNSAT");
    out << ",\"model\":";
    jsonClause(out, model_);
    out << ",\"stats\":{\"decisions\":" << stats_.decisions
        << ",\"conflicts\":" << stats_.conflicts
        << ",\"backtracks\":" << stats_.backtracks
        << ",\"propagations\":" << stats_.unitPropagations
        << ",\"learned\":" << stats_.learnedClauses
        << ",\"backjumps\":" << stats_.nonChronologicalBackjumps
        << "}}\n";
}

void Solver::writeVerbose(std::ostream& out) const {
    out << "=== CDCL EXPLAINED TRACE (MOMS + two watched literals) ===\n";
    for (std::size_t i = 0; i < events_.size(); ++i) {
        const SolverEvent& e = events_[i];
        out << "\n[" << (i + 1) << "/" << events_.size() << "] "
            << e.type << "  |  decision level " << e.level << '\n'
            << "  WHY: " << e.detail << '\n';
        if (e.clauseIndex) {
            out << "  Clause #" << (*e.clauseIndex + 1) << ": ";
            const Clause* clause = nullptr;
            if (*e.clauseIndex < formula_.clauses.size()) {
                clause = &formula_.clauses[*e.clauseIndex];
            }
            if (clause) {
                for (Literal literal : *clause) out << literal << ' ';
                out << "0\n";
            }
        }
        if (!e.learned.empty()) {
            out << "  First-UIP learned: ";
            for (Literal literal : e.learned) out << literal << ' ';
            out << "0; jump to level " << e.targetLevel << '\n';
        }
        out << "  Trail: ";
        if (e.trail.empty()) out << "(empty)";
        for (const auto& entry : e.trail) {
            out << (entry.literal > 0 ? '+' : '-')
                << "x" << (entry.literal > 0 ? entry.literal : -entry.literal)
                << "@L" << entry.decisionLevel;
            if (entry.reasonClause) out << "{C" << (*entry.reasonClause + 1) << "}";
            else out << "{decision}";
            out << ' ';
        }
        out << '\n';
    }
}

} // namespace sat
