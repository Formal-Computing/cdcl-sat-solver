#pragma once

#include "Clause.hpp"

#include <filesystem>
#include <istream>

namespace sat {

class DimacsParser {
public:
    // Parse a DIMACS CNF stream. Invalid input throws std::runtime_error.
    static CnfFormula parse(std::istream& input);

    // Open and parse a DIMACS CNF file.
    static CnfFormula parseFile(const std::filesystem::path& path);
};

} // namespace sat
