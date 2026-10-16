"""Headless tests for the browser GUI's Python-to-C++ boundary."""
import unittest

from app import solve_text


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
