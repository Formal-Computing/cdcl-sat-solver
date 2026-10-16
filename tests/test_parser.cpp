#include "DimacsParser.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int testsPassed = 0;

void check(bool condition, const std::string& description) {
    if (!condition) {
        throw std::runtime_error("FAIL: " + description);
    }
    ++testsPassed;
}

sat::CnfFormula parse(const std::string& text) {
    std::istringstream stream(text);
    return sat::DimacsParser::parse(stream);
}

void expectFailure(const std::string& input, const std::string& expectedMessage) {
    try {
        (void)parse(input);
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find(expectedMessage) != std::string::npos,
              "error should contain '" + expectedMessage + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("FAIL: invalid DIMACS was accepted; expected: " + expectedMessage);
}

void testValidInputs() {
    const auto basic = parse(
        "c first comment\n"
        "p cnf 3 2\n"
        "1 -3 0\n"
        "2 3 -1 0\n");
    check(basic.variableCount == 3, "variable count");
    check(basic.clauses == std::vector<sat::Clause>{{1, -3}, {2, 3, -1}},
          "basic clauses");

    const auto multiline = parse(
        "  c leading-whitespace comment\n"
        " p cnf 4 3 \n"
        "1 -2\n"
        "c comment within a clause\n"
        "3 0   -4 0  2\n"
        " 1 0\n");
    check(multiline.clauses == std::vector<sat::Clause>{{1, -2, 3}, {-4}, {2, 1}},
          "multiline and multiple clauses per line");

    const auto emptyClause = parse("p cnf 0 1\n0\n");
    check(emptyClause.clauses.size() == 1 && emptyClause.clauses[0].empty(),
          "empty clause is preserved");

    const auto noClauses = parse("p cnf 0 0\n");
    check(noClauses.variableCount == 0 && noClauses.clauses.empty(),
          "empty CNF formula");

    const auto bom = parse("\xEF\xBB\xBF" "c UTF-8\np cnf 1 1\n-1 0\n");
    check(bom.clauses == std::vector<sat::Clause>{{-1}}, "UTF-8 BOM");

    const auto whitespace = parse("\n \n c a comment\np cnf 2 1\n\t2\t-1\t0 \n");
    check(whitespace.clauses == std::vector<sat::Clause>{{2, -1}},
          "whitespace and blank lines");

    const auto satlibTrailer = parse("p cnf 2 2\n1 -2 0\n2 0\n%\n0\n");
    check(satlibTrailer.clauses == std::vector<sat::Clause>{{1, -2}, {2}},
          "SATLIB historical '%' and trailing zero accepted");

    const auto inlineTrailer = parse("p cnf 1 1\n1 0 %\n0\n");
    check(inlineTrailer.clauses == std::vector<sat::Clause>{{1}},
          "SATLIB marker can follow the final clause");
}

void testInvalidInputs() {
    expectFailure("", "missing 'p cnf' header");
    expectFailure("c comments only\n", "missing 'p cnf' header");
    expectFailure("1 0\np cnf 1 1\n", "before 'p cnf' header");
    expectFailure("p cnf 1 0\np cnf 1 0\n", "duplicate problem header");
    expectFailure("p sat 1 1\n", "expected 'p cnf");
    expectFailure("p cnf 1\n", "expected 'p cnf");
    expectFailure("p cnf 1 2 extra\n", "expected 'p cnf");
    expectFailure("p cnf -1 0\n", "out of range");
    expectFailure("p cnf 2147483648 0\n", "out of range");
    expectFailure("p cnf 2 1\n3 0\n", "exceeds declared variable count");
    expectFailure("p cnf 2 1\n-3 0\n", "exceeds declared variable count");
    expectFailure("p cnf 2 1\n1 banana 0\n", "invalid integer");
    expectFailure("p cnf 2 1\n1\n", "missing terminating 0");
    expectFailure("p cnf 2 2\n1 0\n", "expected 2 clauses, found 1");
    expectFailure("p cnf 2 1\n1 0\n2 0\n", "literal after declared number of clauses");
    expectFailure("p cnf 2 1\n1 0 2\n", "literal after declared number of clauses");
    expectFailure("p cnf 0 0\n0\n", "more clauses than declared");
    expectFailure("p cnf 1 1\n99999999999999999999999999 0\n", "invalid integer");
    expectFailure("p cnf 1 2\n1 0\n%\n0\n", "'%' end marker before complete CNF");
    expectFailure("p cnf 1 1\n1 0\n%\n17\n", "unexpected input after '%'");
    expectFailure("p cnf 1 1\n1 0\n%\n0\n0\n", "unexpected input after '%'");
    expectFailure("p cnf 1 1\n1 0 % unexpected\n", "unexpected input after '%'");
}

} // namespace

int main() {
    try {
        testValidInputs();
        testInvalidInputs();
        std::cout << "DIMACS parser: " << testsPassed << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
