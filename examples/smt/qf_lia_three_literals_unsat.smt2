; Three QF_LIA theory literals: ALL THREE are necessary for UNSAT.
; Independent real integer linear arithmetic constraints with two variables.
(set-logic QF_LIA)
(set-info :status unsat)
(declare-const x Int)
(declare-const y Int)
; L1: x >= 0
(assert (>= x 0))
; L2: y >= 0
(assert (>= y 0))
; L3: x+y <= -1
(assert (<= (+ x y) (- 1)))
(check-sat)
