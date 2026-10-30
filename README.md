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
decision levels, the trail, propagation reasons, and the live implication graph.
You can pan, zoom, fit the graph, follow the newest event, or scrub earlier events.
Conflicts and learned clauses are displayed at their corresponding steps.

The GUI is intended for *small educational examples*. It binds only to
127.0.0.1, limits input size to 256 KiB, and streams solver events to the browser as they happen.
**There is no solver time limit and no fixed 5,000-event ceiling.** While a CNF is being solved, the
interface shows a live elapsed-time clock. After completion, it displays the
precisely measured C++ process wall time (startup, DIMACS parsing, CDCL and
JSON serialization), distinct from browser rendering time. A very large trace
can still exceed memory or practical browser-memory limits. The C++
executable must be built first.

After changing gui/app.py, restart any already running Python GUI server
(Ctrl+C and rerun python3 gui/app.py) to load the updated backend.

## C++ command line

    ./build/sat_solver examples/sat.cnf
    ./build/sat_solver --verbose examples/cdcl_backjump.cnf
    ./build/sat_solver --trace examples/cdcl_backjump_to_one.cnf
    ./build/sat_solver --dpll examples/branch_unsat.cnf
    ./build/sat_solver --propagate examples/bcp_chain.cnf
    ./build/sat_solver --parse examples/sat.cnf
    ./build/sat_solver --events-json examples/branch_backtrack.cnf
    ./build/sat_solver --events-ndjson examples/branch_backtrack.cnf

The default solver uses CDCL + MOMS.

**--verbose** explains every MOMS selection, decision, BCP consequence and
reason clause, conflict, First-UIP learned clause, backjump, and final outcome.
It also prints the assignment trail at each recorded step.

**--trace** is the concise First-UIP learning and backjumping output.

**--events-json** emits structured C++ events for the Python GUI backend,
including snapshots of the trail and clause references.

**--events-ndjson** emits a header, individual flushed solver events,
a final SAT/UNSAT result, and stats. The GUI's /api/stream forwards them
incrementally, so the implication graph updates during solving, not only
at the end. Streaming has no wall-clock deadline. Large traces can still
consume browser memory, so this is not an unlimited-memory guarantee.

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


## Native C++ QF_LIA CDCL(T) implementation (23 October 2026)

The C++20 executable now accepts supported quantifier-free linear integer
SMT-LIB 2 scripts directly, with no Z3 call or required Python dependency.

    ./build/sat_solver --smt examples/smt/qf_lia_three_literals_unsat.smt2
    ./build/sat_solver examples/smt/qf_lia_three_literals_unsat.smt2
    ./build/sat_solver --smt-json examples/smt/qf_lia_three_literals_unsat.smt2

Exit codes: SAT=10, UNSAT=20, UNKNOWN=0. JSON includes the atom map,
integer and declared Boolean model where SAT, a learned theory-conflict example, Boolean CDCL
passes, theory checks and integer branching counts.

Architecture:
- src/QfLia.cpp: S-expression parser, linear Int terms, Boolean/Tseitin
  encoding, SAT abstraction, CDCL candidates and learned theory lemmas.
- src/LiaArithmetic.cpp: exact checked 128-bit integer arithmetic,
  sound integer gcd cuts, Fourier-Motzkin real feasibility,
  exact rational witness reconstruction and integer branch-and-bound.
- Every reported SAT model is checked against the actual integer
  inequalities. Every UNSAT theory lemma is based on an independently
  proven inconsistent set. The CDCL abstraction searches again after
  adding the lemma.
- Theory resource limits produce UNKNOWN, *never UNSAT*.

The parser currently supports Int and Bool declarations, numeral constants,
linear expressions with +, -, multiplication by integer constants,
<, <=, >, >=, =, distinct, Boolean and/or/not/implies/xor, Boolean ite,
annotations, metadata strings, set-logic QF_LIA, assert and
exactly one check-sat. It rejects assertions added after check-sat.
The native engine additionally supports SMT-LIB's simultaneous lexical
let (including nested shadowing), typed integer/Boolean ite, and exact
Euclidean div and mod by **nonzero constant integer expressions**.
For each constant division, the front-end adds internal quotient/remainder
integer variables constrained by n = d*q + r and 0 <= r < abs(d).
Both positive and negative divisors use SMT-LIB's Euclidean remainder.
For integer ite, branch-guarded equalities tie a fresh integer variable
to the selected branch; the unselected branch imposes no restriction.
These constructions are handled by the native CDCL(T) theory solver,
not delegated to Z3. Local let bindings are scoped and simultaneous.

**Still unsupported:** variable divisors (nonlinear multiplication),
division by zero (underspecified), non-linear products, arbitrary
integer operators, quantifiers and other SMT theories. Unsupported inputs
return UNKNOWN. Resource limits include 32 integer variables **including
fresh internal arithmetic variables**, 128 distinct theory atoms, 2,500
projection rows, 1,500 integer-search nodes, checked 128-bit coefficients
and 2,048 CDCL(T) iterations. This remains an explicitly bounded QF_LIA
fragment rather than complete SMT-LIB conformance.

The updated web app at http://127.0.0.1:8777 calls the **native C++ engine
by default** for supported .smt2/QF_LIA files. Its interactive theory
graph shows clickable arithmetic atoms and a sound learned conflict.
This graph summarizes a theory conflict after solving; it does **not**
claim to show the internal live arithmetic projection/search trace.
A separate Z3 button is available solely for independent cross-checking.

Demo: examples/smt/qf_lia_three_literals_unsat.smt2

SAT demo: examples/smt/qf_lia_ten_constraints_sat.smt2

Ten asserted constraints on x, y, z include integer bounds, a linear
equation, weighted inequality, strict order, and an arithmetic disjunction.
The native C++ engine produces an integer model and the GUI lists this file
under its QF_LIA teaching examples. One valid model is x=3, y=4, z=2.

    ./build/sat_solver --smt examples/smt/qf_lia_ten_constraints_sat.smt2


    L1: x >= 0
    L2: y >= 0
    L3: x + y <= -1

All three literals are needed: the native learned theory conflict is
L1, L2, L3, and the final verdict is UNSAT. The native core also
handles examples/smt/lia_lattice_gap_uw.smt2 and
examples/smt/lia_reynolds_dpll.smt2.

Regression tests: CTest target native_qf_lia, Python web-bridge checks,
seeded QF_LIA differential tests against Z3, plus the independent
tests/qflia_extensions_differential.py covering div/mod, nested let and
integer ite (both SAT/UNSAT statuses and validity of returned models).
Run the full test suite and both Z3-oracle scripts using GitHub Actions.

Combined example: examples/smt/qf_lia_div_mod_let_ite.smt2.
Run it with:
    ./build/sat_solver --smt examples/smt/qf_lia_div_mod_let_ite.smt2

Reference Z3 is used only to check results in tests and through the
separate reference GUI action, never as the native QF_LIA algorithm.
All existing CNF SAT/trace functionality is preserved.

## SMT theory roadmap and teaching inputs

See docs/SMT_ROADMAP.md for the broader path toward fuller QF_LIA,
LRA, datatypes, shared equalities and richer streaming theory traces.
The examples/smt directory includes native QF_LIA examples as well
as non-QF_LIA reference inputs that only Z3 currently handles.

## Interactive literal inspector (23 October 2026)

In the implication graph, click or keyboard-focus (Tab, Enter/Space) a
literal node, or click any literal of a CNF clause. The right-hand
Selected literal panel displays:
- its truth assignment and decision level at the current solver event;
- a concrete BCP reason clause and antecedents, if propagated;
- immediate propagated consequences and clause occurrences.
The corresponding nodes, reason edges, and clauses are highlighted.
This is based on the real C++ CDCL trail, not an imagined graph.

## SMT-COMP 2020 benchmark explorer

Official catalog: https://smt-comp.github.io/2020/benchmarks.html
Published single-query selection list:
https://smt-comp.github.io/2020/selected_benchmarks/benchmark_selection_single_query_2020.tar.xz

The browser indexes 89,924 selected paths (70 logics) from a compressed
738 KiB local catalog, gui/catalog/smtcomp2020_selected.json.gz.
It does not download all of the SMT-LIB benchmark bodies.
Choose source, logic, search, and one benchmark, then press Fetch.

The original 2020 source collection is on StarExec; this index has paths,
not the benchmark contents. A verified bounded, individually downloadable
2025 mirror exposes 200 examples, 81 with exactly matching 2020 paths:
https://github.com/rjungemann/smt-lib-benchmarks
This later-release mirror does not establish byte-identical content
with the original 2020 competition. The UI explicitly labels this.
The original mirror corpus attributes SMT-LIB 2025 benchmarks under CC-BY-4.0.
For entries with no verified individual-file URL, the GUI reports this rather
than inventing a download. Local .smt2 upload is always supported.

The server's HTTPS-only curl fetch path derives URLs exclusively from
a committed trusted whitelist, never from arbitrary user-entered URLs.
Only the requested file (max 2 MiB) is downloaded and verified as recognizable
SMT-LIB text; it is cached under gui/.benchmark-cache/. The response reports
a SHA-256 fingerprint and origin. Subsequent fetches reuse the cached file.

For supported QF_LIA SMT-LIB input, click **Solve with native C++
QF_LIA**. This runs our C++ CDCL(T) backend. For independent validation
or currently unsupported theories, **Cross-check with Z3** invokes
the separately installed reference solver. The optional reference
engine is not the implementation of the native QF_LIA solver.

For DIMACS .cnf inputs and the live propositional implication graph,
click **Run C++ solver**. Native SMT explanation graphs currently
render after completion. Both native and reference SMT execution have
no automatic wall-clock limit and expose a Stop button.

Install the optional Z3 engine into an isolated environment (on Paradis this
environment was already created at /home/admin/.venvs/cdcl-smt):

    python3 -m venv ~/.venvs/cdcl-smt
    ~/.venvs/cdcl-smt/bin/python -m pip install z3-solver
    python3 gui/app.py

The app locates this interpreter automatically, or you may set
CDCL_SMT_PYTHON to any Python interpreter with z3-solver installed.
No benchmark corpus bulk downloads or forced solver timeouts are required.

Tests:

    ctest --test-dir build --output-on-failure

Tests cover the 2020 catalog counts, strict trusted IDs, mirror filtering,
reference LIA/LRA/DT results, and real C++ NDJSON event streaming.
