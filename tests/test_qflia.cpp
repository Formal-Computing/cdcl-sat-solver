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

    // Euclidean integer division and modulus: remainder always nonnegative.
    expected("(assert (= (div 5 2) 2))",Status::Sat);
    expected("(assert (= (mod 5 2) 1))",Status::Sat);
    expected("(assert (= (div (- 5) 2) (- 3)))",Status::Sat);
    expected("(assert (= (mod (- 5) 2) 1))",Status::Sat);
    expected("(assert (= (div 5 (- 2)) (- 2)))",Status::Sat);
    expected("(assert (= (mod 5 (- 2)) 1))",Status::Sat);
    expected("(assert (= (div (- 5) (- 2)) 3))",Status::Sat);
    expected("(assert (= (mod (- 5) (- 2)) 1))",Status::Sat);
    expected("(assert (not (= (div (- 5) 2) (- 3))))",Status::Unsat);
    expected("(assert (not (= (mod (- 5) 2) 1)))",Status::Unsat);
    expected("(assert (not (= (mod x 1) 0)))",Status::Unsat);
    expected("(assert (not (= (div x 1) x)))",Status::Unsat);
    expected("(assert (not (= (+ (* 4 (div x 4)) (mod x 4)) x)))",Status::Unsat);
    expected("(assert (= x (- 11)))(assert (= (div x 4) (- 3)))"
             "(assert (= (mod x 4) 1))",Status::Sat);
    expected("(assert (= x 14))(assert (= (mod x (- 3)) 2))"
             "(assert (= (div x (- 3)) (- 4)))",Status::Sat);
    expected("(assert (= (mod (+ (* 3 x) 1) 3) 2))",Status::Unsat);
    expected("(assert (= (div x y) 1))",Status::Unknown);
    expected("(assert (= (mod x y) 0))",Status::Unknown);
    expected("(assert (= (div x 0) 1))",Status::Unknown);
    expected("(assert (= (mod x 0) 0))",Status::Unknown);

    // Simultaneous, capture-safe let and local lexical shadowing.
    expected("(assert (= (let ((a (+ x 1)) (b 2)) (+ a b)) (+ x 3)))",
             Status::Sat);
    expected("(assert (not (= (let ((a (+ x 1)) (b 2)) (+ a b)) (+ x 3))))",
             Status::Unsat);
    expected("(assert (= x 2))(assert (= (let ((x 9) (y x)) y) 2))",
             Status::Sat);
    expected("(assert (= x 2))(assert (= (let ((x 9) (y x)) y) 9))",
             Status::Unsat);
    expected("(assert (= (let ((x 2)) (let ((x (+ x 1))) x)) 3))",Status::Sat);
    expected("(assert (let ((b (> x 1))) (and b (> x 1))))",Status::Sat);
    expected("(assert (let ((p true) (q false)) (and p (not q))))",Status::Sat);
    expected("(assert (let ((p true)) (not p)))",Status::Unsat);
    expected("(assert (= (let ((x 9)) (div x 4)) 2))",Status::Sat);
    expected("(assert (= (let ((q (div x 3))) (+ q q))"
             " (* 2 (div x 3))))",Status::Sat);
    expected("(assert (let ((p (>= x 1))) (ite p true false)))"
             "(assert (< x 1))",Status::Unsat);
    expected("(assert (let ((a 1) (a 2)) (= a 1)))",Status::Unknown);

    // Integer ite ties a fresh arithmetic witness to the selected branch.
    expected("(assert (= (ite (> x 0) 3 7) 3))(assert (> x 0))",Status::Sat);
    expected("(assert (= (ite (> x 0) 3 7) 3))(assert (<= x 0))",Status::Unsat);
    expected("(assert (= (ite (> x 0) (+ x 1) (- x 1)) 4))"
             "(assert (= x 3))",Status::Sat);
    expected("(assert (= (ite (> x 0) (+ x 1) (- x 1)) 4))"
             "(assert (= x (- 3)))",Status::Unsat);
    expected("(assert (= (ite (> x 0) (div x 2) (mod x 2)) (- 3)))"
             "(assert (= x (- 5)))",Status::Unsat);
    expected("(assert (= (ite (> x 0) (div x 2) (mod x 2)) 1))"
             "(assert (= x (- 5)))",Status::Sat);
    expected("(assert (= (ite (> x 0) (ite (> y 0) 1 2) 3) 2))"
             "(assert (> x 0))(assert (<= y 0))",Status::Sat);
    expected("(assert (not (= (ite true x y) x)))",Status::Unsat);
    expected("(assert (not (= (ite false x y) y)))",Status::Unsat);
    expected("(assert (= (ite true 1 false) 1))",Status::Unknown);

    std::cout << "Native QF_LIA unit suite: passed\n";
}
