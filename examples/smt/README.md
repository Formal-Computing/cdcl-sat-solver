# Reference examples for the future DPLL(T) implementation

These are SMT-LIB 2 examples for theory-engine development; the current
C++ SAT solver only accepts DIMACS CNF and cannot solve these files yet.
The GUI provides a read-only preview instead of incorrectly claiming SAT or
UNSAT using Boolean abstraction alone.

| Example | Logic | Expected |
|---|---|---|
| lia_lattice_gap_uw.smt2 | QF_LIA | UNSAT |
| lia_reynolds_dpll.smt2 | QF_LIA | UNSAT |
| lra_halfplanes_reynolds.smt2 | QF_LRA | SAT |
| dt_reynolds_color_list.smt2 | QF_DT | UNSAT |
| mixed_reynolds_lia_dt.smt2 | LIA + algebraic datatypes | UNSAT |

Sources:
- https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l02/02-theory/
- https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l04/02-theory/
- https://conferences.mpi-inf.mpg.de/rg1/vtsa17/slides/reynolds-vtsa-part1.pdf

SAT demo: examples/uw_cdcl_lecture.cnf uses the six-clause formula
from UW CSEP 590B Lecture 2 and is runnable in our CDCL GUI.

Once LIA/DT plugins are implemented, use an independent reference SMT solver
such as Z3 or cvc5 for differential testing, not as a substitute for claiming
native theory support.
