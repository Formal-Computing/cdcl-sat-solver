# CDCL SAT Solver and Step-by-Step Visualizer

Formal Computing lab — C++20 CDCL solver with **MOMS branching**, **two-watched
literal propagation**, **First-UIP learning**, and **non-chronological backjumping**.
A Python-standard-library web server provides an interactive visualizer driven
by the **actual C++ solver's recorded events**. Python does not re-solve SAT.

## Quick start (Paradis / WSL)

From the project root:

    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

**Full verbose iteration** (build, all tests, and an explained trace for
every CNF in examples/):

    bash scripts/run_all_verbose.sh

**Open the Python GUI** (no pip dependencies):

    python3 gui/app.py

Open the printed local address in a browser (default:
http://127.0.0.1:8765). On WSL, if a browser does not launch automatically,
open this address manually in Windows. Stop the server with Ctrl+C.

Alternate port:

    python3 gui/app.py --port 8766 --no-browser

The GUI supports example selection, opening your own DIMACS file, editing CNF,
running the real C++ executable, and **Previous / Next / Play / Reset** step
controls with a slider and keyboard arrows. It visualizes CNF clause statuses,
decision levels, the trail, propagation reasons, and the implication graph.
Conflicts and learned clauses are displayed at their corresponding steps.

The GUI is intended for *small educational examples*. It binds only to
127.0.0.1, limits input size to 256 KiB, and imposes a 20-second solve limit.
The C++ executable must be built first.

## C++ command line

    ./build/sat_solver examples/sat.cnf
    ./build/sat_solver --verbose examples/cdcl_backjump.cnf
    ./build/sat_solver --trace examples/cdcl_backjump_to_one.cnf
    ./build/sat_solver --dpll examples/branch_unsat.cnf
    ./build/sat_solver --propagate examples/bcp_chain.cnf
    ./build/sat_solver --parse examples/sat.cnf
    ./build/sat_solver --events-json examples/branch_backtrack.cnf

The default solver uses CDCL + MOMS.

**--verbose** explains every MOMS selection, decision, BCP consequence and
reason clause, conflict, First-UIP learned clause, backjump, and final outcome.
It also prints the assignment trail at each recorded step.

**--trace** is the concise First-UIP learning and backjumping output.

**--events-json** emits structured C++ events for the Python GUI backend,
including snapshots of the trail and clause references.

DIMACS SAT process exit codes: **10 = SAT**, **20 = UNSAT**, **1 = error**.
These intentionally nonzero success codes can interrupt shell && chains.

## Why BCP can be inconclusive

BCP only applies forced unit assignments; its fixed point has three cases:

- **CONFLICT**: a clause is false. At the root this proves UNSAT.
- **SATISFIABLE**: every clause is already true.
- **UNRESOLVED**: some clauses are open and no unit implication remains.
  DPLL/CDCL must make a decision.

The --propagate mode now distinguishes all three cases. For
examples/bcp_chain.cnf, BCP alone satisfies all clauses and reports SAT.
For a CNF with no unit clauses it may instead report UNRESOLVED.

## MOMS heuristic

MOMS = **Maximum Occurrences in clauses of Minimum Size**.

1. Evaluate clauses under the current partial assignment.
2. Keep unresolved, non-tautological clauses. Residual size is the number
   of *distinct unassigned literals* in each clause.
3. Find the minimum residual size.
4. Count occurrences of each unassigned variable among those clauses,
   counting each variable only once per clause (either polarity).
5. Pick the variable occurring most often, breaking ties by lowest index.
   Try positive polarity first.

Both DPLL and CDCL use MOMS. Unit propagation runs before branching.

## Architecture

- src/DimacsParser.cpp — parsing and validation.
- src/Solver.cpp — assignment trail, DPLL, undo.
- src/Watched.cpp — two-watched-literal propagation.
- src/Moms.cpp — variable-ordering heuristic.
- src/Cdcl.cpp — First-UIP learning and backjumping.
- src/Trace.cpp — narrated events and JSON.
- src/main.cpp — command-line modes.
- gui/app.py — Python local HTTP server invoking the C++ binary.
- gui/index.html — clause view, implication graph and event player.
- scripts/run_all_verbose.sh — full diagnostic iteration.

## Tests

    ctest --test-dir build --output-on-failure

Includes DIMACS parsing, BCP, DPLL, CDCL, watched literals, MOMS/events,
and a Python-to-C++ GUI integration test when Python is available.
Randomized CNFs are checked against independent exhaustive-search or
full-clause-scanning reference implementations.

## Next optional improvements

- Phase saving and heuristic comparisons (MOMS vs VSIDS).
- Restarts and learned-clause deletion.
- Large DIMACS benchmarks and further GUI refinements.

## SATLIB benchmark suite

A curated collection of **21 original SATLIB instances** is included in
benchmarks/satlib/cnf/, with expected answers, SHA-256 hashes, and original
archive links in benchmarks/satlib/manifest.json.

Run controlled SATLIB benchmarks (3-second timeout per problem):

    python3 scripts/benchmark_satlib.py --timeout 3 --csv benchmarks/satlib/results.csv

The runner distinguishes SAT, UNSAT, TIMEOUT and ERROR; it verifies every
returned SAT model and preserves SATLIB's expected classifications.
For difficult problems, use a larger timeout or --filter hole8 / --filter hole9.

Some SATLIB files use a trailing '%' then '0'. The DIMACS parser now accepts
this historical end marker without modifying the downloaded CNF.
See benchmarks/satlib/README.md for details and source attribution.
