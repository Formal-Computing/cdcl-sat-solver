# SATLIB — curated benchmark suite

Source: https://www.cs.ubc.ca/~hoos/SATLIB/index-ubc.html
Benchmark index: https://www.cs.ubc.ca/~hoos/SATLIB/benchm.html

Twenty-one original DIMACS CNF problems downloaded from SATLIB, covering
random uniform 3-SAT, artificial AIM benchmarks, DUBOIS, pigeonhole and
SAT-encoded graph colouring. Filenames and CNF contents are preserved.
Source archive URLs, original members, known SAT/UNSAT classifications,
headers and SHA-256 hashes are recorded in manifest.json.

## Run from the project root

List the selected instances:

    python3 scripts/benchmark_satlib.py --list

Run with a timeout per problem and write a CSV report:

    python3 scripts/benchmark_satlib.py --timeout 3 --csv benchmarks/satlib/results.csv

Focus on a particular benchmark or family:

    python3 scripts/benchmark_satlib.py --timeout 30 --filter hole8
    python3 scripts/benchmark_satlib.py --timeout 5 --filter aim-200
    python3 scripts/benchmark_satlib.py --timeout 5 --filter uuf50

Compare against the original DPLL implementation:

    python3 scripts/benchmark_satlib.py --engine dpll --timeout 5 --filter uf20

The runner verifies each unchanged benchmark's SHA-256 hash before execution.
It checks returned SAT/UNSAT statuses against the classifications published
by SATLIB and independently verifies every returned SAT assignment.

A TIMEOUT is reported separately from ERROR and mismatches. TIMEOUT does
not mean UNSAT or that the solver has malfunctioned.

## Initial run on Paradis (3s timeout per instance)

- 21 instances total; 19 completed with expected results.
- Two timed out: hole8.cnf and hole9.cnf.
- No erroneous SAT/UNSAT classifications observed among completed instances.
- The 19 finished results are recorded in results.csv alongside the timeouts.

Times are local machine measurements, not standardized benchmarks.
Run a larger time budget for difficult pigeonhole instances.

## DIMACS compatibility

Several original SATLIB random 3-SAT files end with historical lines:

    %
    0

These are NOT additional clauses. The C++ DIMACS parser now accepts this
legacy trailer only after the declared CNF clauses are complete; malformed
trailing data remains an error. Original benchmark files are unmodified.

## Families and expected answers

- uf20-91, uf50-218 — uniform random satisfiable 3-SAT.
- uuf50-218 — uniform random unsatisfiable 3-SAT.
- AIM — generated SAT and UNSAT instances (yes/no in original filenames).
- DUBOIS — unsatisfiable structured SAT instances.
- PHOLE — unsatisfiable pigeonhole-principle encodings.
- flat30-60 — satisfiable 3-colouring encodings of graphs.

SATLIB is a research benchmark collection created by Holger Hoos and
Thomas Stützle. Keep source attribution when redistributing benchmark files.
