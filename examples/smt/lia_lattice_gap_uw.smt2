; University of Washington CSEP 590B lecture 4: integer lattice gap.
; Expected: unsat, because 3(x-y) cannot equal 1 or 2 for integers.
(set-logic QF_LIA)
(declare-const x Int)
(declare-const y Int)
(assert (<= 0 x))
(assert (<= x 4))
(assert (<= 0 y))
(assert (<= y 4))
(assert (<= 1 (- (* 3 x) (* 3 y))))
(assert (<= (- (* 3 x) (* 3 y)) 2))
(check-sat)
