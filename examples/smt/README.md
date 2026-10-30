# SMT-LIB examples and expected results

The native C++20 CDCL(T) engine solves an explicitly limited QF_LIA fragment:
Boolean combinations of linear integer inequalities, lexical let, integer
and Boolean ite, and integer div/mod by **nonzero constant divisors**.

Run a native input with ./build/sat_solver --smt examples/smt/<name>.smt2.
Unsupported expressions return UNKNOWN, not a guessed SAT/UNSAT classification.
Z3 is an independent reference implementation, not the native engine.

| Example | Logic | Expected native result |
|---|---|---|
| qf_lia_three_literals_unsat.smt2 | QF_LIA | UNSAT |
| qf_lia_ten_constraints_sat.smt2 | QF_LIA | SAT |
| qf_lia_div_mod_let_ite.smt2 | QF_LIA | SAT |
| lia_lattice_gap_uw.smt2 | QF_LIA | UNSAT |
| lia_reynolds_dpll.smt2 | QF_LIA | UNSAT |
| lra_halfplanes_reynolds.smt2 | QF_LRA | UNKNOWN (reference Z3 SAT) |
| dt_reynolds_color_list.smt2 | QF_DT | UNKNOWN (reference Z3 UNSAT) |
| mixed_reynolds_lia_dt.smt2 | mixed | UNKNOWN (reference Z3 UNSAT) |

References:
- https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l02/02-theory/
- https://courses.cs.washington.edu/courses/csep590b/26sp/lecture/l04/02-theory/
- https://conferences.mpi-inf.mpg.de/rg1/vtsa17/slides/reynolds-vtsa-part1.pdf

The CNF example examples/uw_cdcl_lecture.cnf is independently runnable
through the DIMACS mode and web-based CDCL visualizer.
