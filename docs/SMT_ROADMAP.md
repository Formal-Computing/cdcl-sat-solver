# CDCL to CDCL(T): next engineering milestones

Status as of 23 October 2026: native C++ CDCL(T) for an explicitly limited
QF_LIA fragment is implemented and regression-tested. Full QF_LIA grammar,
stronger integer search, full LRA, datatype engines, and combination
remain open. Do not confuse limited native QF_LIA with complete SMT-LIB
QF_LIA standard conformance.

## Principles

- Preserve the C++20 CDCL/MOMS/two-watched-literal implementation and
  First-UIP conflict analysis as the Boolean engine.
- Use SMT-LIB 2.6 or current SMT-LIB conventions for a validated typed AST;
  distinguish Int and Real sorts, constructors, selectors, equalities and
  Boolean connectives. Never silently coerce theory atoms into arbitrary
  Boolean variables and report SAT.
- Every T-conflict or T-propagation must have a checkable explanation that
  can be translated into a Boolean lemma / reason clause.
- Do not claim complete QF_LIA or QF_DT until the solver handles its full
  documented fragment, terminating correctly with exact arithmetic.

## Milestone 0: event stream and GUI (implemented)

- C++ --events-ndjson emits header, decision/propagate/conflict/learn/
  backjump/terminal events as they occur, and does not retain a second trace.
- Python /api/stream forwards NDJSON without a solver deadline.
- Browser follows newly arrived frames; earlier states remain scrub-able.
- Zoom, fit, scroll and drag-pan controls; no 5,000-event cutoff.
- Important remaining scaling concern: each frame includes a full trail and
  the browser retains all frames. Implement optional disk-backed trace
  indexing and a bounded window for very large instances; **do not**
  turn visualization budgets into SAT solver timeouts.

## Milestone 1: typed SMT front end

- Parse QF_LRA, QF_LIA, QF_DT and relevant fragments of mixed terms.
- Symbol table, sort checking, AST DAG hash-consing, canonical atoms.
- Tseitin abstraction into CNF and atom-to-theory-literal bijection.
- Tests: malformed syntax, unbound variables, sort mismatch, strict
  inequalities, negated equalities, and shared theory terms.

## Milestone 2: theory API and CDCL coordination

A first useful incremental interface:

    assert_literal(atom_id, polarity, decision_level)
    propagate() -> list[theory_literal + explanation]
    check() -> SAT(model) | CONFLICT(explanation) | UNKNOWN(reason)
    backtrack(target_level)
    explain(propagation_id) -> original-assumption core

The core must append conflict explanations as **lemmas** and invoke 1-UIP
as usual. Check theory consistency before returning SAT; for a lazy first
prototype check after Boolean candidate assignments, then add eager
incremental propagation. Preserve model soundness and stable event IDs.

## Milestone 3: exact linear arithmetic

- Start with QF_LRA linear rational constraints; use exact rational
  arithmetic and an incremental feasibility algorithm (e.g. SMT simplex).
- QF_LIA requires **integrality**, not just relaxed real feasibility.
  Develop integer branch-and-bound/cuts or a sound complete decision
  procedure for the explicitly supported fragment. Detect infeasible
  lattice regions even when the LRA relaxation is feasible.
- Export conflicts as explanations / lemmas to the CDCL engine.
- Geometry UI: half-planes + intersection, Simplex pivots for LRA;
  lattice feasible points and integrality conflicts for 2D LIA examples.
- Tests: 2*x=1 (LIA UNSAT, LRA SAT), 1 <= 3x-3y <= 2 (LIA UNSAT),
  boundary cases, negatives, unbounded domains, strict comparisons.

## Milestone 4: algebraic/inductive datatypes

- Constructor disjointness, injectivity, exhaustiveness and selector
  behavior under constructor guards; acyclicity for finite terms.
- Correct equality reasoning, usually via congruence closure/EUF.
- Test empty/nonempty list, color enum, cons(x,nil)=cons(y,z),
  x=cons(c,x), and constructor exclusivity.
- Visualize term DAG, merged equivalence classes, selector edges,
  constructor splits and conflicts, linked to Boolean events.

## Milestone 5: mixed LIA + DT

- Purify mixed terms; exchange equalities/disequalities over shared terms.
- Theory combination is not simply checking each theory independently:
  models must agree on shared equalities, and finite datatype domains
  and non-convex theories need careful treatment.
- Trace both theory solvers' deductions with explicit origin and reasons.
- Start with Reynolds' IntList/head/cons mixed example.
- Tests: soundness of combination and model validity, not only statuses.

## Quality gate before claiming a new theory

1. Correct SAT/UNSAT/UNKNOWN semantics, including complete models.
2. No false SAT from an inconsistent conjunction of theory literals.
3. Every learned T-lemma entailed by the original theory constraints.
4. Full randomized differential testing against independent Z3/cvc5
   (reference oracle only; our solver must execute its own procedures).
5. Small examples with manually inspected event traces, larger stress
   cases without implicit time caps, and explicit memory warnings.
6. C++/Python tests, browser screenshot + smoke testing on Paradis WSL.

## Primary references

- Andrew Reynolds, DPLL(T) for SMT, VTSA 2017:
  https://conferences.mpi-inf.mpg.de/rg1/vtsa17/slides/reynolds-vtsa-part1.pdf
  (DPLL(T) around pp. 34-80; datatypes pp. 82-98;
   combination pp. 99-131; arithmetic from p. 135).
- UW CSEP 590B, CDCL lecture:
  https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l02/02-theory/
- UW CSEP 590B, theory solvers:
  https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l04/02-theory/
