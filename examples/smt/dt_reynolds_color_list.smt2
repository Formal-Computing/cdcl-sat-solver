; Andrew Reynolds, VTSA 2017, inductive datatypes slides 82-97.
; Expected: unsat by constructor injectivity and red != green.
(set-logic QF_DT)
(declare-datatypes ((ClrList 0) (Clr 0))
  (((cons (head Clr) (tail ClrList)) (nil))
   ((red) (green) (blue))))
(declare-const x Clr)
(declare-const y Clr)
(declare-const z ClrList)
(assert (= (cons x nil) (cons y z)))
(assert (or (= x red) (not (= x y))))
(assert (= y green))
(check-sat)
