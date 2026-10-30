#pragma once
#include <string>
#include <utility>
#include <vector>

namespace lia {

enum class Status { Sat, Unsat, Unknown };

struct Result {
    Status status = Status::Unknown;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> model;
    std::vector<std::string> atoms;
    std::vector<int> conflictAtoms;
    int booleanIterations = 0;
    int theoryChecks = 0;
    int theoryBranches = 0;
};

Result solveFile(const std::string& path);
std::string toJson(const Result& result);
std::string statusName(Status status);
}
