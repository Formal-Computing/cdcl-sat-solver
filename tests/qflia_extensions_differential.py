#!/usr/bin/env python3
"""Differential regressions for native SMT-LIB div, mod, let, arithmetic ite.
Requires Z3 as independent oracle; native backend does all actual solving.
"""
from __future__ import annotations
import argparse
import json
import random
import subprocess
import tempfile
from pathlib import Path
import z3

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--solver",type=Path,default=Path(__file__).resolve().parents[1]/"build/sat_solver")
    ap.add_argument("--cases",type=int,default=150)
    ap.add_argument("--seed",type=int,default=20261030)
    args=ap.parse_args()
    rng=random.Random(args.seed)
    statuses={"sat":"SAT","unsat":"UNSAT","unknown":"UNKNOWN"}
    failures=[];unknowns=0
    with tempfile.TemporaryDirectory() as temp:
        file=Path(temp)/"input.smt2"
        for iteration in range(args.cases):
            a,b,c,d=(rng.randint(-4,4),rng.randint(-4,4),
                       rng.randint(-6,6),rng.choice([-5,-4,-3,-2,-1,1,2,3,4,5]))
            kind=iteration%7
            numerator=f"(+ (* {a} x) (* {b} y) {c})"
            if kind==0:
                value=f"(div {numerator} {d})"
            elif kind==1:
                value=f"(mod {numerator} {d})"
            elif kind==2:
                value=f"(ite p (div {numerator} {d}) (mod {numerator} {d}))"
            elif kind==3:
                value=f"(let ((u (div {numerator} {d})) (v (mod {numerator} {d}))) (+ (* {d} u) v))"
            elif kind==4:
                value=f"(let ((x (+ x {a})) (z y)) (ite p (div x {d}) (mod z {d})))"
            elif kind==5:
                value=f"(let ((k (div (+ x {c}) {d}))) (let ((k (+ k y))) (ite p k (- k))))"
            else:
                value=f"(ite (> x y) (mod {numerator} {d}) (div {numerator} {d}))"
            cmp=rng.choice(["=","<","<=",">",">=","distinct"])
            k=rng.randint(-9,9)
            source=( "(set-logic QF_LIA)\n(declare-const x Int)\n(declare-const y Int)\n"
                     "(declare-const p Bool)\n"
                     "(assert (>= x (- 5)))\n(assert (<= x 5))\n"
                     "(assert (>= y (- 5)))\n(assert (<= y 5))\n"
                     f"(assert ({cmp} {value} {k}))\n"
                     f"(assert {'p' if rng.choice([0,1]) else '(not p)'})\n"
                     "(check-sat)\n(get-model)\n")
            file.write_text(source,encoding="utf-8")
            reference=z3.Solver()
            try:
                reference.from_string(source)
                want=statuses[str(reference.check())]
            except z3.Z3Exception as exc:
                failures.append((iteration,"Z3 parse",str(exc),source))
                continue
            try:
                run=subprocess.run([str(args.solver),"--smt-json",str(file)],
                                   capture_output=True,text=True,timeout=8)
                parsed=json.loads(run.stdout)
                got=parsed["status"]
            except (ValueError,subprocess.TimeoutExpired) as exc:
                failures.append((iteration,"execution",str(exc),source))
                continue
            if got=="UNKNOWN":
                unknowns+=1
                continue
            if got!=want:
                failures.append((iteration,want,got,source))
                continue
            if got=="SAT":
                try:
                    model=parsed["model"]
                    subst=[(z3.Int("x"),z3.IntVal(model["x"])),
                           (z3.Int("y"),z3.IntVal(model["y"])),
                           (z3.Bool("p"),z3.BoolVal(model["p"]=="true"))]
                    for assertion in reference.assertions():
                        check=z3.simplify(z3.substitute(assertion,*subst))
                        if not z3.is_true(check):
                            failures.append((iteration,"invalid native model",str(check),source,model))
                            break
                except (KeyError,z3.Z3Exception,ValueError) as e:
                    failures.append((iteration,"model error",str(e),source))
    for f in failures[:5]: print("FAIL:",*f,sep="\n",end="\n----\n")
    print("Extension differential:",args.cases,"cases,",unknowns,"UNKNOWN,",len(failures),"failures")
    return 1 if failures else 0
if __name__=="__main__": raise SystemExit(main())
