#!/usr/bin/env python3
"""Reproducible, independent QF_LIA differential and model-soundness test.

Run with a Python environment containing z3-solver. Z3 is an oracle ONLY:
the C++ solver subprocess is the actual implementation under test.
"""
from __future__ import annotations
import argparse
import json
import random
import subprocess
import tempfile
from pathlib import Path
import z3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--solver", type=Path, default=Path(__file__).resolve().parents[1] / "build/sat_solver")
    parser.add_argument("--cases", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261030)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    operators = ("<", ">", "<=", ">=", "=")
    def term():
        a,b,c = (rng.randint(-4,4),rng.randint(-4,4),rng.randint(-8,8))
        return f"(+ (* {a} x) (* {b} y) {c})"
    def atom():
        return f"({rng.choice(operators)} {term()} {rng.randint(-6,6)})"
    def formula():
        parts = [atom() for _ in range(rng.randint(1,5))]
        if rng.random()<0.6:
            parts.append("p" if rng.random()<0.5 else "(not p)")
        expression=parts[0]
        for part in parts[1:]:
            choice=rng.choice(["and","or","=>","xor","ite"])
            if choice=="ite":
                expression=f"(ite p {expression} {part})"
            else:
                expression=f"({choice} {expression} {part})"
        return expression

    mismatches=[]
    unknown=0
    with tempfile.TemporaryDirectory(prefix="native_qflia_") as directory:
        path=Path(directory)/"case.smt2"
        for i in range(args.cases):
            expr=formula()
            source=("(set-logic QF_LIA)\n"
                    '(set-info :source "QF_LIA differential test")\n'
                    "(declare-const x Int)\n(declare-const y Int)\n"
                    "(declare-const p Bool)\n"
                    f"(assert {expr})\n(check-sat)\n(get-model)\n")
            path.write_text(source,encoding="utf-8")
            oracle=z3.Solver()
            oracle.from_string(source)
            expected=str(oracle.check()).upper()
            try:
                run=subprocess.run([str(args.solver),"--smt-json",str(path)],
                                   capture_output=True,text=True,timeout=8)
                actual=json.loads(run.stdout)
            except (subprocess.TimeoutExpired,ValueError) as error:
                mismatches.append((i,expected,"PROCESS ERROR",str(error),source))
                continue
            verdict=actual["status"]
            if verdict=="UNKNOWN":
                unknown+=1
                continue
            if verdict!=expected:
                mismatches.append((i,expected,verdict,actual.get("reason"),source))
                continue
            if verdict=="SAT":
                assignments=dict(actual["model"])
                try:
                    substitutions=[
                        (z3.Int("x"),z3.IntVal(assignments["x"])),
                        (z3.Int("y"),z3.IntVal(assignments["y"])),
                        (z3.Bool("p"),z3.BoolVal(assignments["p"]=="true"))]
                    for asserted in oracle.assertions():
                        if not z3.is_true(z3.simplify(z3.substitute(asserted,*substitutions))):
                            mismatches.append((i,expected,"INVALID MODEL",assignments,source))
                            break
                except (KeyError,ValueError,z3.Z3Exception) as error:
                    mismatches.append((i,expected,"MODEL ERROR",str(error),source))
    print(f"Seed {args.seed}: {args.cases} cases, {unknown} unknown, {len(mismatches)} failures")
    for case in mismatches[:10]:
        print(*case,sep="\n",end="\n---\n")
    return 1 if mismatches else 0

if __name__=="__main__":
    raise SystemExit(main())
