#include "DimacsParser.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace sat {
namespace {

[[noreturn]] void parseError(std::size_t line, const std::string& message) {
    throw std::runtime_error("DIMACS line " + std::to_string(line) + ": " + message);
}

long long parseInteger(const std::string_view token, const std::size_t line) {
    long long value = 0;
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) {
        parseError(line, "invalid integer '" + std::string(token) + "'");
    }
    return value;
}

} // namespace

CnfFormula DimacsParser::parse(std::istream& input) {
    CnfFormula formula;
    Clause currentClause;
    bool hasHeader = false;
    bool endMarkerSeen = false;
    bool legacyTrailingZeroSeen = false;
    std::size_t expectedClauses = 0;
    std::size_t lineNumber = 0;
    std::string line;

    while (std::getline(input, line)) {
        ++lineNumber;
        // Accept a UTF-8 BOM if one is present in the first line.
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }

        std::istringstream tokens(line);
        std::string word;
        if (!(tokens >> word)) {
            continue; // blank line
        }
        if (word == "c") {
            continue; // DIMACS comment (may appear between clause lines)
        }

        // Several original SATLIB benchmarks use a historical trailer:
        // a '%' line followed by a single '0' line. This is NOT another
        // clause, so accept only this trailer after all declared clauses.
        if (endMarkerSeen) {
            std::string extra;
            if (word == "0" && !legacyTrailingZeroSeen && !(tokens >> extra)) {
                legacyTrailingZeroSeen = true;
                continue;
            }
            parseError(lineNumber, "unexpected input after '%' end marker");
        }

        if (word == "p") {
            if (hasHeader) {
                parseError(lineNumber, "duplicate problem header");
            }
            std::string format, variablesText, clausesText, extra;
            if (!(tokens >> format >> variablesText >> clausesText) ||
                format != "cnf" || (tokens >> extra)) {
                parseError(lineNumber, "expected 'p cnf <variables> <clauses>'");
            }

            const long long variables = parseInteger(variablesText, lineNumber);
            const long long clauses = parseInteger(clausesText, lineNumber);
            if (variables < 0 || variables > std::numeric_limits<int>::max() ||
                clauses < 0 || clauses > std::numeric_limits<int>::max()) {
                parseError(lineNumber, "variable or clause count out of range");
            }

            formula.variableCount = static_cast<int>(variables);
            expectedClauses = static_cast<std::size_t>(clauses);
            hasHeader = true;
            continue;
        }

        if (!hasHeader) {
            parseError(lineNumber, "clause data before 'p cnf' header");
        }

        // A clause can span lines; several clauses can share one line.
        do {
            if (word == "%") {
                if (!currentClause.empty() ||
                    formula.clauses.size() != expectedClauses) {
                    parseError(lineNumber, "'%' end marker before complete CNF");
                }
                std::string extra;
                if (tokens >> extra) {
                    parseError(lineNumber, "unexpected input after '%' marker");
                }
                endMarkerSeen = true;
                break;
            }
            const long long literal = parseInteger(word, lineNumber);
            if (literal == 0) {
                if (formula.clauses.size() >= expectedClauses) {
                    parseError(lineNumber, "more clauses than declared in header");
                }
                formula.clauses.push_back(std::move(currentClause));
                currentClause.clear(); // empty clause '0' is valid
            } else {
                if (formula.clauses.size() >= expectedClauses) {
                    parseError(lineNumber, "literal after declared number of clauses");
                }
                if (literal > formula.variableCount || literal < -formula.variableCount) {
                    parseError(lineNumber,
                               "literal " + std::to_string(literal) +
                               " exceeds declared variable count");
                }
                currentClause.push_back(static_cast<int>(literal));
            }
        } while (tokens >> word);
    }

    if (input.bad()) {
        throw std::runtime_error("I/O error while reading DIMACS stream");
    }
    if (!hasHeader) {
        parseError(lineNumber == 0 ? 1 : lineNumber, "missing 'p cnf' header");
    }
    if (!currentClause.empty()) {
        parseError(lineNumber, "last clause is missing terminating 0");
    }
    if (formula.clauses.size() != expectedClauses) {
        parseError(lineNumber,
                   "expected " + std::to_string(expectedClauses) +
                   " clauses, found " + std::to_string(formula.clauses.size()));
    }

    return formula;
}

CnfFormula DimacsParser::parseFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open DIMACS file: " + path.string());
    }
    return parse(input);
}

} // namespace sat
