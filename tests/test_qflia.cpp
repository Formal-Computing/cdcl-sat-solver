#include "QfLia.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
namespace {
lia::Result solve(const std::string& source) {
    const auto path=std::filesystem::temp_directory_path()/"cdcl_qf_lia_regression.smt2";
    {std::ofstream out(path);out<<source;}
    auto result=lia::solveFile(path.string());
    std::filesystem::remove(path);
    return result;
}
std::string prefix="(set-logic QF_LIA)\n(declare-const x Int)\n(declare-const y Int)\n";
void expected(const std::string& body, lia::Status want) {
    const auto result=solve(prefix+body+"\n(check-sat)\n");
    if(result.status!=want) {
        std::cerr<<"Expected "<<lia::statusName(want)<<", got "<<lia::statusName(result.status)
                 <<": "<<result.reason<<" for "<<body<<"\n";
        std::abort();
    }
}
}
int main() {
    using lia::Status;
    expected("(assert (>= x 0))(assert (>= y 0))(assert (<= (+ x y) (- 1)))",
             Status::Unsat);
    auto core=solve(prefix+
        "(assert (>= x 0))(assert (>= y 0))(assert (<= (+ x y) (- 1)))(check-sat)");
    assert(core.conflictAtoms.size()==3);
    expected("(assert (>= x 0))(assert (>= y 0))(assert (= (+ x y) 3))",
             Status::Sat);
    expected("(assert (= (* 2 x) 1))",Status::Unsat);
    expected("(assert (>= (* 3 (- x y)) 1))"
             "(assert (<= (* 3 (- x y)) 2))", Status::Unsat);
    expected("(assert (< x 0))(assert (>= x 0))",Status::Unsat);
    expected("(assert (not (<= x 0)))(assert (<= x 0))",Status::Unsat);
    expected("(assert (or (> x 3) (< y (- 2))))"
             "(assert (<= x 3))(assert (>= y 0))",Status::Unsat);
    expected("(assert (or (= x 1) (= x 2)))"
             "(assert (distinct x 1))",Status::Sat);
    expected("(assert (=> (>= x 1) (>= y 1)))"
             "(assert (>= x 1))(assert (< y 1))",Status::Unsat);
    expected("(assert (> x 4))(assert (< x 10))",Status::Sat);
    expected("(assert (>= x 0))",Status::Sat);
    expected("(assert (<= x (- 1000000)))",Status::Sat);
    expected("(assert (= (* x y) 5))",Status::Unknown);
    {
        const auto other=solve("(set-logic QF_LRA)(declare-const x Real)"
                               "(assert (= x 1.5))(check-sat)");
        assert(other.status==Status::Unknown);
    }
    // SMT-LIB Boolean declarations and mixed Boolean/arithmetic atoms.
    {
        auto r=solve("(set-logic QF_LIA)(declare-const b Bool)"
                     "(declare-const x Int)(assert b)(assert (>= x 7))(check-sat)");
        assert(r.status==Status::Sat);
        bool sawBool=false;
        for(const auto& [name,value]:r.model)
            if(name=="b") { assert(value=="true"); sawBool=true; }
        assert(sawBool);
    }
    assert(solve("(set-logic QF_LIA)(declare-fun flag () Bool)"
                 "(assert (and flag (not flag)))(check-sat)").status==Status::Unsat);
    assert(solve("(set-logic QF_LIA)(declare-const b Bool)"
                 "(declare-const x Int)(assert (= b (> x 1)))"
                 "(assert (not b))(assert (> x 2))(check-sat)").status==Status::Unsat);
    assert(solve("(set-logic QF_LIA)(declare-const b Bool)"
                 "(assert (distinct b true))(assert b)(check-sat)").status==Status::Unsat);
    assert(solve("(set-logic QF_LIA)(declare-const b Bool)"
                 "(declare-const x Int)(assert (ite b (= x 1) (= x 2)))"
                 "(assert (not b))(assert (distinct x 2))(check-sat)").status==Status::Unsat);
    assert(solve("(set-logic QF_LIA)(set-info :source \"quoted \"\" value; text\")"
                 "(declare-const x Int)(assert (! (= x 3) :named example))"
                 "(check-sat)(get-model)(exit)").status==Status::Sat);
    assert(solve(prefix+"(check-sat)(assert (and (< x 0) (> x 0)))")
           .status==Status::Unknown);
    assert(solve(prefix+"(check-sat)(declare-const z Int)")
           .status==Status::Unknown);
    assert(solve(prefix+"(assert true)(check-sat)(check-sat)")
           .status==Status::Unknown);
    assert(solve(prefix+"(assert true)(get-model)(check-sat)")
           .status==Status::Unknown);
    assert(solve(prefix+"(assert true)(check-sat)(exit)(assert false)")
           .status==Status::Unknown);
    assert(solve("(set-logic QF_LIA)(declare-const x Int)"
                 "(declare-const b Bool)(assert (= x b))(check-sat)")
           .status==Status::Unknown);
    assert(solve("(set-logic QF_LIA)(declare-const b Bool)"
                 "(declare-const b Int)(assert b)(check-sat)")
           .status==Status::Unknown);
    assert(solve("(set-info :notes \"never closes)(check-sat)")
           .status==Status::Unknown);
    std::cout << "Native QF_LIA unit suite: passed\n";
}
