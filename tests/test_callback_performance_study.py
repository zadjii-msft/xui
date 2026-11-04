import copy
import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("callback_study", Path(__file__).with_name("callback-performance-study.py"))
study = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(study)


class StudyProtocolTests(unittest.TestCase):
    def setUp(self):
        self.group = dict(name="abi", phases=["first", "second"])
        self.rows = [
            dict(phase=name, iterations=32, measured=True, wall_ms=100,
                 kernel_before100ns=10, kernel_after100ns=20,
                 user_before100ns=30, user_after100ns=40, callbacks=32)
            for name in ("first", "second")
        ] + [dict(retained_handles_before=6, retained_handles_after=6)]
        self.counts = dict(first=32, second=32)
        self.reference = study.validate(self.group, self.rows, self.counts)

    def test_exact_work(self):
        study.validate(self.group, self.rows, self.counts, self.reference)

    def test_reject_changed_work(self):
        for field in ("iterations", "callbacks"):
            with self.subTest(field=field):
                rows = copy.deepcopy(self.rows)
                rows[0][field] += 1
                with self.assertRaises(RuntimeError):
                    study.validate(self.group, rows, self.counts, self.reference)

    def test_reject_missing_duplicate_reordered_phases(self):
        for rows in (self.rows[1:], [self.rows[0], *self.rows], [self.rows[1], self.rows[0], self.rows[2]]):
            with self.assertRaises(RuntimeError):
                study.validate(self.group, rows, self.counts, self.reference)

    def test_reject_bad_cpu(self):
        for field, value in (("kernel_after100ns", -1), ("user_before100ns", True), ("kernel_after100ns", 20.0)):
            with self.subTest(field=field, value=value):
                rows = copy.deepcopy(self.rows)
                rows[0][field] = value
                with self.assertRaises(RuntimeError):
                    study.validate(self.group, rows, self.counts, self.reference)

    def test_reject_bad_timing(self):
        for value in (float("nan"), 0, 90000):
            with self.subTest(value=value):
                rows = copy.deepcopy(self.rows)
                rows[0]["wall_ms"] = value
                with self.assertRaises(RuntimeError):
                    study.validate(self.group, rows, self.counts, self.reference)

    def test_reject_verification_records(self):
        rows = copy.deepcopy(self.rows)
        rows[0]["measured"] = False
        with self.assertRaises(RuntimeError):
            study.validate(self.group, rows, self.counts, self.reference)

    def test_reject_handle_growth(self):
        rows = copy.deepcopy(self.rows)
        rows[-1]["retained_handles_after"] = 7
        with self.assertRaises(RuntimeError):
            study.validate(self.group, rows, self.counts, self.reference)

    def test_distribution_retains_tails(self):
        values = [1, 2, 4, 8, 16, 32, 128]
        result = study.distribution(values)
        self.assertEqual(result["samples"], values)
        self.assertEqual(result["p90_nearest_rank"], 128)
        self.assertEqual(result["median"], 8)
        self.assertIsNone(study.pct(1, 0))
        self.assertEqual(study.pct(2, 1), 100)


if __name__ == "__main__":
    unittest.main()
