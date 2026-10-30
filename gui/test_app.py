"""Headless tests for the browser GUI's Python-to-C++ boundary."""
import json
import subprocess
import threading
from urllib.request import Request, urlopen
import unittest
from unittest import mock

from app import create_server, solve_text, check_smt_text, check_native_qflia_text, z3_python


class GuiTests(unittest.TestCase):
    def test_sat_and_steps(self):
        data = solve_text("p cnf 2 2\n1 2 0\n-1 2 0\n")
        self.assertEqual(data["status"], "SAT")
        self.assertEqual(data["algorithm"], "CDCL-MOMS")
        self.assertEqual(data["events"][0]["type"], "start")
        self.assertEqual(data["events"][-1]["type"], "sat")
        self.assertEqual(len(data["model"]), 2)

    def test_unsat(self):
        data = solve_text("p cnf 1 2\n1 0\n-1 0\n")
        self.assertEqual(data["status"], "UNSAT")
        self.assertEqual(data["events"][-1]["type"], "unsat")
        self.assertEqual(data["model"], [])

    def test_learned_trace(self):
        data = solve_text("p cnf 2 3\n-1 2 0\n-1 -2 0\n1 2 0\n")
        kinds = [e["type"] for e in data["events"]]
        self.assertIn("learn", kinds)
        self.assertIn("backjump", kinds)
        self.assertTrue(data["stats"]["learned"] >= 1)

    def test_unlimited_runtime_and_measured_elapsed(self):
        # Simulate a 35-second C++ run without waiting 35 seconds. The
        # subprocess must receive NO timeout argument.
        fake_result = subprocess.CompletedProcess(
            args=[], returncode=10,
            stdout=json.dumps({"status": "SAT", "events": [], "model": []}),
            stderr="",
        )
        with mock.patch("app.subprocess.run", return_value=fake_result) as run:
            with mock.patch("app.time.perf_counter", side_effect=[10.0, 45.25]):
                result = solve_text("p cnf 0 0\\n")
        self.assertEqual(result["elapsed_seconds"], 35.25)
        self.assertNotIn("timeout", run.call_args.kwargs)

    def test_live_http_stream_delivers_real_solver_events(self):
        server = create_server(0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            request = Request(
                f"http://127.0.0.1:{server.server_port}/api/stream",
                data=json.dumps({"cnf": "p cnf 2 3\n-1 2 0\n-1 -2 0\n1 2 0\n"}).encode(),
                headers={"Content-Type": "application/json"}, method="POST",
            )
            with urlopen(request) as response:
                self.assertEqual(response.headers["Content-Type"].split(";")[0],
                                 "application/x-ndjson")
                frames = [json.loads(line) for line in response if line.strip()]
            kinds = [f["kind"] for f in frames]
            self.assertEqual(kinds[0], "header")
            self.assertEqual(kinds[-1], "timing")
            self.assertIn("event", kinds)
            self.assertEqual(frames[-2]["kind"], "complete")
            self.assertEqual(frames[-2]["status"], "SAT")
            self.assertIn("learn", [f["event"]["type"] for f in frames
                                     if f["kind"] == "event"])
            self.assertGreaterEqual(frames[-1]["elapsed_seconds"], 0)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def test_legacy_json_has_no_5000_event_replay_ceiling(self):
        fake = subprocess.CompletedProcess(
            args=[], returncode=10,
            stdout=json.dumps({"status": "SAT", "events": [{}] * 5001,
                               "model": []}), stderr="",
        )
        with mock.patch("app.subprocess.run", return_value=fake):
            data = solve_text("p cnf 0 0\n")
        self.assertEqual(len(data["events"]), 5001)

    def test_native_qflia_three_literal_core(self):
        source = ("(set-logic QF_LIA)(declare-const x Int)(declare-const y Int)"
                  "(assert (>= x 0))(assert (>= y 0))"
                  "(assert (<= (+ x y) (- 1)))(check-sat)")
        result = check_native_qflia_text(source)
        self.assertEqual(result["backend"], "Native C++ CDCL(T) QF_LIA")
        self.assertEqual(result["status"], "UNSAT")
        self.assertEqual(result["conflict_atoms"], [0, 1, 2])
        self.assertGreaterEqual(result["theory_checks"], 1)

    def test_native_qflia_sat_model(self):
        source = ("(set-logic QF_LIA)(declare-const x Int)(declare-const y Int)"
                  "(assert (>= x 0))(assert (>= y 0))"
                  "(assert (= (+ x y) 3))(check-sat)")
        result = check_native_qflia_text(source)
        self.assertEqual(result["status"], "SAT")
        x = int(result["model"]["x"])
        y = int(result["model"]["y"])
        self.assertGreaterEqual(x, 0)
        self.assertGreaterEqual(y, 0)
        self.assertEqual(x+y, 3)

    def test_native_qflia_ten_constraint_sat_example(self):
        from pathlib import Path
        source_path = (Path(__file__).resolve().parent.parent / "examples" /
                       "smt" / "qf_lia_ten_constraints_sat.smt2")
        source = source_path.read_text(encoding="utf-8")
        self.assertEqual(source.count("(assert "), 10)
        result = check_native_qflia_text(source)
        self.assertEqual(result["backend"], "Native C++ CDCL(T) QF_LIA")
        self.assertEqual(result["status"], "SAT")
        x, y, z = (int(result["model"][key]) for key in ("x", "y", "z"))
        self.assertGreaterEqual(x, 0)
        self.assertGreaterEqual(y, 2)
        self.assertGreaterEqual(z, 1)
        self.assertEqual(x+y+z, 9)
        self.assertLessEqual(x, 4)
        self.assertLessEqual(y, 4)
        self.assertLessEqual(z, 3)
        self.assertGreaterEqual(2*x+y, 7)
        self.assertLess(x, y)
        self.assertIn(z, (2, 3))

    def test_native_qflia_rejects_unsupported_logic_safely(self):
        source = "(set-logic QF_LRA)(declare-const x Real)(assert (> x 1))(check-sat)"
        result = check_native_qflia_text(source)
        self.assertEqual(result["status"], "UNKNOWN")
        self.assertIn("QF_LIA", result["reason"])

    def test_native_http_endpoint(self):
        server = create_server(0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            src = ("(set-logic QF_LIA)(declare-const x Int)"
                   "(assert (>= x 1))(assert (<= x 0))(check-sat)")
            request = Request(
                f"http://127.0.0.1:{server.server_port}/api/native-smt",
                data=json.dumps({"source":src}).encode(),
                headers={"Content-Type": "application/json"}, method="POST",
            )
            with urlopen(request) as response:
                self.assertEqual(response.status, 200)
                result = json.load(response)
            self.assertEqual(result["status"], "UNSAT")
            self.assertEqual(result["backend"], "Native C++ CDCL(T) QF_LIA")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def test_smtcomp_2020_selected_catalog_is_metadata_only(self):
        from benchmark_catalog import list_catalog, fetch_benchmark
        result = list_catalog("2020", logic="QF_LIA", limit=12)
        self.assertEqual(result["total"], 89924)
        self.assertGreater(result["count"], 100)
        self.assertEqual(len(result["entries"]), 12)
        self.assertTrue(all(x["logic"] == "QF_LIA" for x in result["entries"]))
        unavailable = next(x for x in result["entries"] if not x["ready"])
        with self.assertRaises(LookupError):
            fetch_benchmark(unavailable["id"])
        with self.assertRaises(ValueError):
            fetch_benchmark("https://arbitrary-server.invalid")
        self.assertEqual(list_catalog("mirror")["total"], 200)

    def test_smtcomp_catalog_query_and_pagination(self):
        from benchmark_catalog import list_catalog
        ready = list_catalog("2020", logic="QF_LIA", ready_only=True)
        self.assertEqual(ready["count"], 4)
        self.assertTrue(all(x["ready"] for x in ready["entries"]))
        limited = list_catalog("mirror", logic="QF_LIA", q="slack", limit=3)
        self.assertTrue(all("slack" in x["path"].lower() for x in limited["entries"]))
        self.assertLessEqual(len(limited["entries"]), 3)
        with self.assertRaises(ValueError):
            list_catalog("2020", limit=100000)

    @unittest.skipUnless(z3_python() is not None, "Optional Z3 reference backend unavailable")
    def test_smt_reference_checks_arithmetic_and_datatypes(self):
        from pathlib import Path
        root = Path(__file__).resolve().parent.parent
        for name, expected in (
            ("lia_lattice_gap_uw.smt2", "UNSAT"),
            ("lra_halfplanes_reynolds.smt2", "SAT"),
            ("dt_reynolds_color_list.smt2", "UNSAT"),
            ("mixed_reynolds_lia_dt.smt2", "UNSAT"),
        ):
            with self.subTest(name=name):
                response = check_smt_text((root / "examples/smt" / name).read_text())
                self.assertEqual(response["status"], expected)
                self.assertEqual(response["backend"], "Z3 SMT")
        with self.assertRaisesRegex(ValueError, "exactly one check-sat"):
            check_smt_text("(set-logic QF_LIA)(check-sat)(check-sat)")

    def test_rejects_bad_input(self):
        with self.assertRaisesRegex(ValueError, "DIMACS"):
            solve_text("p cnf 1 1\n2 0\n")
        with self.assertRaisesRegex(ValueError, "supply"):
            solve_text("")

    def test_rejects_oversized(self):
        with self.assertRaisesRegex(ValueError, "256 KiB"):
            solve_text("p cnf 0 0\n" + "c" * (256 * 1024 + 1))


if __name__ == "__main__":
    unittest.main()
