; Native CDCL(T) demonstration: SAT with ten asserted constraints.
; Three integer variables, a linear equation, strict inequality,
; weighted linear inequality, and a disjunction.
; An independently checkable satisfying assignment is x=2, y=4, z=3.
(set-logic QF_LIA)
(set-info :status sat)

(declare-const x Int)
(declare-const y Int)
(declare-const z Int)

; L1-L3: integer lower bounds
(assert (>= x 0))                     ; L1
(assert (>= y 2))                     ; L2
(assert (>= z 1))                     ; L3

; L4: the values sum to nine
(assert (= (+ x y z) 9))               ; L4

; L5-L7: upper bounds
(assert (<= x 4))                     ; L5
(assert (<= y 4))                     ; L6
(assert (<= z 3))                     ; L7

; L8: weighted linear inequality
(assert (>= (+ (* 2 x) y) 7))          ; L8

; L9: strict ordering
(assert (< x y))                      ; L9

; L10: a Boolean choice between two arithmetic equalities
(assert (or (= z 2) (= z 3)))         ; L10

(check-sat)
(get-model)
