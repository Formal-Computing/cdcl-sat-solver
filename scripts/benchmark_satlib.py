#!/usr/bin/env python3
"""Run the curated, unchanged SATLIB DIMACS problems against the C++ solver.

SATLIB archives: https://www.cs.ubc.ca/~hoos/SATLIB/benchm.html
SAT/UNSAT expectations are specified by the original SATLIB families.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "benchmarks" / "satlib"
MANIFEST = LIB / "manifest.json"
BINARY = ROOT / "build" / "sat_solver"


def model_satisfies(filename: Path, signed_model: list[int], num_vars: int) -> bool:
    if len(signed_model) != num_vars:
        return False
    values: dict[int, bool] = {}
    for i, literal in enumerate(signed_model, 1):
        if abs(literal) != i:
            return False
        values[i] = literal > 0
    clauses = []
    clause = []
    for line in filename.read_text(encoding="ascii").splitlines():
        stripped = line.strip()
        if stripped.startswith("%"):
            break  # SATLIB end marker; sometimes followed by a non-clause '0'.
        if not stripped or stripped.startswith(("c", "p")):
            continue
        for token in stripped.split():
            literal = int(token)
            if literal == 0:
                clauses.append(clause)
                clause = []
            else:
                clause.append(literal)
    if clause:
        return False
    return all(
        any(values[abs(literal)] == (literal > 0) for literal in c)
        for c in clauses
    )


def evaluate(item: dict, timeout: float, engine: str) -> dict:
    path = LIB / "cnf" / item["file"]
    if not path.is_file():
        raise FileNotFoundError(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
        raise RuntimeError(f"SATLIB benchmark modified: {path}")

    command = [str(BINARY)]
    if engine == "dpll":
        command.append("--dpll")
    command.append(str(path))
    started = time.perf_counter()
    try:
        process = subprocess.run(command, capture_output=True,
                                 text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return {**item, "result": "TIMEOUT", "seconds": round(time.perf_counter()-started, 4),
                "model_valid": "", "stats": "", "notes": ""}
    seconds = round(time.perf_counter()-started, 4)
    outcome = {10: "SAT", 20: "UNSAT"}.get(process.returncode, "ERROR")
    if process.returncode in (10, 20):
        tagged = "s SATISFIABLE" if outcome == "SAT" else "s UNSATISFIABLE"
        if tagged not in process.stdout:
            outcome = "ERROR"
    valid = ""
    if outcome == "SAT":
        model_lines = [line for line in process.stdout.splitlines() if line.startswith("v ")]
        if len(model_lines) == 1:
            try:
                signed = [int(x) for x in model_lines[0][2:].split()]
                valid = model_satisfies(path, signed[:-1], item["variables"]) if signed and signed[-1] == 0 else False
            except (ValueError, KeyError):
                valid = False
        else:
            valid = False
    stats = next((line for line in process.stdout.splitlines() if line.startswith("c CDCL") or line.startswith("c DPLL")), "")
    notes = "" if outcome != "ERROR" else process.stderr.strip()[:150]
    return {**item, "result": outcome, "seconds": seconds, "model_valid": valid,
            "stats": stats, "notes": notes}


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark the C++ CDCL-MOMS solver on original SATLIB instances")
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="Timeout in seconds per formula (default 3)")
    parser.add_argument("--engine", choices=["cdcl", "dpll"], default="cdcl")
    parser.add_argument("--filter", default="", help="Only filenames containing this substring")
    parser.add_argument("--csv", type=Path, help="Write detailed CSV result table")
    parser.add_argument("--list", action="store_true", help="Show selected instances without running")
    args = parser.parse_args()
    if args.timeout <= 0: parser.error("--timeout must be positive")
    if not BINARY.exists() and not args.list:
        parser.error("Build the C++ solver first: cmake --build build")
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))["benchmarks"]
    entries = [e for e in entries if args.filter in e["file"]]
    if not entries:
        parser.error("No matching benchmark files")
    print(f"{'Benchmark':<29} {'Vars':>5} {'Clauses':>7} {'Expected':>8} {'Result':>9} {'Seconds':>9} {'Model':>7}")
    print("-" * 83)
    rows = []
    for item in entries:
        if args.list:
            print(f"{item['file']:<29} {item['variables']:>5} {item['clauses']:>7} {item['expected']:>8}")
            continue
        row = evaluate(item, args.timeout, args.engine)
        rows.append(row)
        print(f"{row['file']:<29} {row['variables']:>5} {row['clauses']:>7} {row['expected']:>8} "
              f"{row['result']:>9} {row['seconds']:>9.3f} {str(row['model_valid']):>7}", flush=True)
    if args.list:
        return 0
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        columns = ["file", "variables", "clauses", "difficulty", "expected", "result",
                   "seconds", "model_valid", "stats", "notes", "source_url", "sha256"]
        with args.csv.open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
        print(f"CSV report: {args.csv}")
    wrong = [row for row in rows if
             row["result"] not in ("TIMEOUT", row["expected"]) or row["model_valid"] is False]
    passed = sum(row["result"] == row["expected"] and row["model_valid"] is not False for row in rows)
    timed_out = sum(row["result"] == "TIMEOUT" for row in rows)
    print(f"\nCorrect: {passed}, timeout: {timed_out}, incorrect/errors: {len(wrong)} "
          f"(timeout {args.timeout:g}s each)")
    if wrong:
        for row in wrong:
            print(f"FAILED: {row['file']}: {row['result']} expected {row['expected']} "
                  f"model_valid={row['model_valid']} {row['notes']}", file=sys.stderr)
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
