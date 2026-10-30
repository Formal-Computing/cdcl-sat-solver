#include "DimacsParser.hpp"
#include "Solver.hpp"
#include "QfLia.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    const bool implicitSmt = argc == 2 && std::string_view(argv[1]).ends_with(".smt2");
    if (implicitSmt || (argc == 3 && (std::string_view(argv[1]) == "--smt" ||
                      std::string_view(argv[1]) == "--smt-json"))) {
        const lia::Result result = lia::solveFile(implicitSmt ? argv[1] : argv[2]);
        if (std::string_view(argv[1]) == "--smt-json") {
            std::cout << lia::toJson(result) << '\n';
        } else {
            std::cout << "c Native C++ CDCL(T) QF_LIA: Boolean iterations="
                      << result.booleanIterations << " theory checks="
                      << result.theoryChecks << " integer branches="
                      << result.theoryBranches << '\n';
            if (!result.reason.empty()) std::cout << "c " << result.reason << '\n';
            std::cout << "s " << lia::statusName(result.status) << '\n';
            if (result.status == lia::Status::Sat) {
                for (const auto& [name, value] : result.model)
                    std::cout << "v " << name << " = " << value << '\n';
            }
        }
        return result.status == lia::Status::Sat ? 10 :
               result.status == lia::Status::Unsat ? 20 : 0;
    }
    enum class Mode { Cdcl, Dpll, Parse, Propagate, Trace, Verbose, Json, JsonStream };
    Mode mode = Mode::Cdcl;
    const char* path = nullptr;
    const auto usage = [] {
        std::cerr << "Usage: sat_solver "
                     "[--dpll|--parse|--propagate|--trace|--verbose|--events-json|--events-ndjson] "
                     "<input.cnf>\n"
                     "       sat_solver [--smt|--smt-json] <input.smt2>\n";
    };

    if (argc == 2 && argv[1][0] != '-') {
        path = argv[1];
    } else if (argc == 3) {
        const std::string_view option(argv[1]);
        if (option == "--parse") mode = Mode::Parse;
        else if (option == "--propagate") mode = Mode::Propagate;
        else if (option == "--dpll") mode = Mode::Dpll;
        else if (option == "--trace") mode = Mode::Trace;
        else if (option == "--verbose") mode = Mode::Verbose;
        else if (option == "--events-json") mode = Mode::Json;
        else if (option == "--events-ndjson") mode = Mode::JsonStream;
        else {
            usage();
            return 2;
        }
        path = argv[2];
    } else {
        usage();
        return 2;
    }

    try {
        const sat::CnfFormula formula = sat::DimacsParser::parseFile(path);

        if (mode == Mode::Parse || mode == Mode::Propagate) {
            std::cout << "DIMACS CNF parsed successfully\n"
                      << "Variables: " << formula.variableCount << '\n'
                      << "Clauses: " << formula.clauses.size() << '\n';
        }
        if (mode == Mode::Parse) {
            for (std::size_t i = 0; i < formula.clauses.size(); ++i) {
                std::cout << "Clause " << (i + 1) << ':';
                for (const sat::Literal literal : formula.clauses[i]) std::cout << ' ' << literal;
                std::cout << " 0\n";
            }
            return 0;
        }

        sat::Solver solver(formula);
        if (mode == Mode::Propagate) {
            const auto conflict = solver.propagate();
            for (const auto& step : solver.trail()) {
                std::cout << "Assign x" << (step.literal > 0 ? step.literal : -step.literal)
                          << " = " << (step.literal > 0 ? "true" : "false")
                          << " at level " << step.decisionLevel;
                if (step.reasonClause)
                    std::cout << " (reason: clause " << (*step.reasonClause + 1) << ')';
                std::cout << '\n';
            }
            if (conflict)
                std::cout << "BCP: CONFLICT in clause " << (*conflict + 1)
                          << " (UNSAT at root level)\n";
            else if (solver.allClausesSatisfied())
                std::cout << "BCP: SATISFIABLE (every clause is satisfied)\n";
            else
                std::cout << "BCP: UNRESOLVED fixed point (decisions required)\n";
            return 0;
        }

        const bool dpll = mode == Mode::Dpll;
        if (mode == Mode::JsonStream) {
            solver.setLiveTrace(std::cout);
            solver.writeLiveHeader(std::cout, formula);
        } else {
            solver.setTracing(mode == Mode::Verbose || mode == Mode::Json);
        }
        const sat::SolveStatus result = dpll ? solver.solve() : solver.solveCdcl();
        if (mode == Mode::JsonStream) {
            solver.writeLiveFinal(std::cout, result);
            return result == sat::SolveStatus::Satisfiable ? 10 : 20;
        }
        if (mode == Mode::Json) {
            solver.writeEventJson(std::cout, formula, result);
            return result == sat::SolveStatus::Satisfiable ? 10 : 20;
        }
        if (mode == Mode::Verbose) solver.writeVerbose(std::cout);
        if (mode == Mode::Trace) {
            for (const auto& event : solver.learningEvents()) {
                std::cout << "c First-UIP conflict_level=" << event.conflictLevel
                          << " backjump_level=" << event.backjumpLevel << " learned:";
                for (const sat::Literal literal : event.clause) std::cout << ' ' << literal;
                std::cout << " 0\n";
            }
        }

        const auto& stats = solver.stats();
        std::cout << "c " << (dpll ? "DPLL" : "CDCL-MOMS")
                  << " decisions=" << stats.decisions
                  << " conflicts=" << stats.conflicts
                  << " backtracks=" << stats.backtracks
                  << " propagations=" << stats.unitPropagations;
        if (!dpll)
            std::cout << " learned=" << stats.learnedClauses
                      << " nonchronological_backjumps=" << stats.nonChronologicalBackjumps;
        std::cout << '\n';
        if (result == sat::SolveStatus::Unsatisfiable) {
            std::cout << "s UNSATISFIABLE\n";
            return 20;
        }
        std::cout << "s SATISFIABLE\nv";
        for (const sat::Literal literal : solver.model()) std::cout << ' ' << literal;
        std::cout << " 0\n";
        return 10;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
