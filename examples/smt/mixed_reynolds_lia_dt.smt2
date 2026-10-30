; Andrew Reynolds, VTSA 2017, theory combination slides 98-130.
; Expected: unsat after sharing equalities between arithmetic and datatype theory.
(set-logic ALL)
(declare-datatypes ((IntList 0))
  (((cons (head Int) (tail IntList)) (nil))))
(declare-const x IntList)
(declare-const y Int)
(assert (or (= (+ (head x) 3) y)
            (= x (cons (+ y 1) nil))))
(assert (> (head x) (+ y 1)))
(check-sat)
