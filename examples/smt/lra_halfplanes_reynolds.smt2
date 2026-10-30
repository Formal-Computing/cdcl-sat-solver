; Andrew Reynolds, VTSA 2017, simplex feasible-region illustration.
; Expected: sat, e.g. x=3 and y=3.
(set-logic QF_LRA)
(declare-const x Real)
(declare-const y Real)
(assert (>= (- x y) (- 1)))
(assert (<= y 4))
(assert (>= (+ x y) 6))
(check-sat)
(get-model)
