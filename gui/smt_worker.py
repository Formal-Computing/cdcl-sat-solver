"""SMT-LIB solving engine powered by Z3, with theory-aware explanation data.

The native C++ SAT engine remains independent. This is a real SMT solver
backend supporting Z3's theories (including LIA/LRA and algebraic datatypes),
not a fake Boolean abstraction of an SMT-LIB formula.
"""
from __future__ import annotations

import json
import sys
import time
import uuid


def theory_of(expr, z3):
    sort = expr.sort()
    kind = sort.kind()
    op = expr.decl().kind() if z3.is_app(expr) else None
    theory_kinds = {z3.Z3_DATATYPE_SORT: "datatype", z3.Z3_INT_SORT: "arithmetic",
                    z3.Z3_REAL_SORT: "arithmetic", z3.Z3_BV_SORT: "bitvector",
                    z3.Z3_ARRAY_SORT: "array"}
    if kind in theory_kinds:
        return theory_kinds[kind]
    if z3.is_app(expr):
        # Equality and relational predicates have Boolean result sorts.
        children = expr.children()
        if any(c.sort().kind() == z3.Z3_DATATYPE_SORT for c in children):
            return "datatype"
        if any(c.sort().kind() in (z3.Z3_INT_SORT, z3.Z3_REAL_SORT) for c in children):
            return "arithmetic"
        if any(c.sort().kind() == z3.Z3_BV_SORT for c in children):
            return "bitvector"
        if any(c.sort().kind() == z3.Z3_ARRAY_SORT for c in children):
            return "array"
    if z3.is_bool(expr):
        return "boolean"
    return "other"


def expression_graph(assertions, model, core_indices, z3):
    """Bounded *explanation view* of the parsed Z3 term DAG.

    This graph is a factual representation of input formulas and, when SAT,
    model evaluations. It is not claimed to be Z3's private search trace.
    """
    nodes = []
    edges = []
    by_ast_id = {}
    budgets = {"max_nodes": 240, "max_edges": 360, "max_assertions": 80}
    seen_theories = set()
    for assertion in assertions[:budgets["max_assertions"]]:
        stack = [assertion]
        while stack and len(nodes) < budgets["max_nodes"]:
            term = stack.pop()
            if not hasattr(term, "get_id"):
                continue
            ident = term.get_id()
            if ident in by_ast_id:
                continue
            node_id = f"t{len(nodes)}"
            by_ast_id[ident] = node_id
            category = theory_of(term, z3)
            seen_theories.add(category)
            label = str(term)
            node = {"id": node_id, "label": label[:100],
                    "detail": label[:900], "sort": str(term.sort()),
                    "theory": category}
            if model is not None and z3.is_app(term):
                try:
                    evaluated = str(model.eval(term, model_completion=False))
                    node["value"] = evaluated[:200]
                except z3.Z3Exception:
                    pass
            nodes.append(node)
            stack.extend(reversed(term.children()) if z3.is_app(term) else [])
    for assertion in assertions[:budgets["max_assertions"]]:
        stack = [assertion]
        visited = set()
        while stack and len(edges) < budgets["max_edges"]:
            term = stack.pop()
            ident = term.get_id()
            if ident in visited:
                continue
            visited.add(ident)
            parent = by_ast_id.get(ident)
            if parent and z3.is_app(term):
                for child in term.children():
                    kid = by_ast_id.get(child.get_id())
                    if kid:
                        edges.append({"from": parent, "to": kid})
                        stack.append(child)
    roots = []
    for i, assertion in enumerate(assertions[:budgets["max_assertions"]]):
        roots.append({"id": "a" + str(i), "index": i, "label": "A" + str(i + 1),
                      "formula": str(assertion)[:2000],
                      "term": by_ast_id.get(assertion.get_id()),
                      "theory": theory_of(assertion, z3),
                      "in_core": i in core_indices})
    return {"assertions": roots, "nodes": nodes, "edges": edges,
            "theories": sorted(seen_theories),
            "total_assertions": len(assertions),
            "truncated": (len(assertions) > budgets["max_assertions"]
                          or len(nodes) >= budgets["max_nodes"]
                          or len(edges) >= budgets["max_edges"]),
            "note": ("Z3-parsed expression DAG with optional model evaluation. "
                     "This is not an internal theory-propagation or CDCL search trace.")}


def main():
    try:
        import z3
    except ImportError:
        print(json.dumps({"error": "Z3 SMT engine is not installed"}))
        return 2

    if len(sys.argv) < 2:
        print(json.dumps({"error": "Expected an SMT-LIB file path"}))
        return 2
    started = time.perf_counter()
    try:
        with open(sys.argv[1], encoding="utf-8") as file:
            source = file.read()
        parsed_solver = z3.Solver()
        parsed_solver.from_string(source)
        assertions = list(parsed_solver.assertions())
        # Capture actual Z3 unsat cores for small instructive formulas.
        # For large competition inputs, disabling per-assertion trackers
        # avoids an artificial algorithmic overhead.
        track_core = len(assertions) <= 96 and len(source) <= 80_000
        labels = {}
        solver = parsed_solver
        if track_core and assertions:
            solver = z3.Solver()
            solver.set(unsat_core=True)
            prefix = "cdcl_smt_core_" + uuid.uuid4().hex + "_"
            for i, assertion in enumerate(assertions):
                label = z3.Bool(prefix + str(i))
                labels[str(label)] = i
                solver.assert_and_track(assertion, label)

        result = solver.check()
        status = "SAT" if result == z3.sat else "UNSAT" if result == z3.unsat else "UNKNOWN"
        model = solver.model() if result == z3.sat else None
        core_indices = sorted({labels[str(item)] for item in solver.unsat_core()
                               if str(item) in labels}) if result == z3.unsat and labels else []
        entries = []
        if model is not None:
            decls = list(model.decls())
            for declaration in decls[:250]:
                value = model[declaration]
                entries.append({"name": str(declaration),
                                "sort": str(declaration.range()),
                                "value": str(value)[:600],
                                "arity": declaration.arity()})
        theories = set()
        for assertion in assertions[:256]:
            stack = [assertion]
            seen = set()
            while stack and len(seen) < 1000:
                term = stack.pop()
                if term.get_id() in seen:
                    continue
                seen.add(term.get_id())
                theories.add(theory_of(term, z3))
                if z3.is_app(term):
                    stack.extend(term.children())
        response = {"status": status, "backend": "Z3 SMT",
                    "engine": "Z3", "version": z3.get_version_string(),
                    "capabilities": {"linear_integer_arithmetic": True,
                                     "linear_real_arithmetic": True,
                                     "algebraic_datatypes": True,
                                     "combined_theories": True},
                    "assertions": len(assertions),
                    "theories": sorted(theories),
                    "elapsed_seconds": round(time.perf_counter() - started, 6),
                    "model": entries,
                    "model_truncated": model is not None and len(model.decls()) > len(entries),
                    "unsat_core_indices": core_indices,
                    "core_available": bool(labels),
                    "graph": expression_graph(assertions, model, set(core_indices), z3)}
        if result == z3.unknown:
            response["reason"] = solver.reason_unknown()
        print(json.dumps(response, ensure_ascii=False))
        return 0
    except (z3.Z3Exception, UnicodeDecodeError, OSError, ValueError) as error:
        print(json.dumps({"error": str(error)[:1500]}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
