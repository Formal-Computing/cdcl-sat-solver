; Andrew Reynolds, VTSA 2017, DPLL(LIA) example, slides 34-47.
; Expected: unsat (forces x >= 0 and x < 0).
(set-logic QF_LIA)
(declare-const x Int)
(declare-const y Int)
(assert (or (> (+ x 1) 0) (> (+ x y) 0)))
(assert (or (< x 0) (> (+ x y) 4)))
(assert (not (> (+ x y) 0)))
(check-sat)
