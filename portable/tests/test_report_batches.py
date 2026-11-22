#!/usr/bin/env python3
"""Unit tests for portable/tools/report_batches.py; no GPU needed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "report_batches.py"
spec = importlib.util.spec_from_file_location("report_batches", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

HEADER = "frame,accepted,render_passes,batched_groups,batched_draws,passes_saved\n"


class BatchReportTests(unittest.TestCase):
    def parse(self, rows, after=0, before=None):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "vk_batches.csv"
            path.write_text(HEADER + rows, encoding="utf-8")
            return module.load_csv(path, after, before)

    def test_correct_counts(self):
        good, errors = self.parse("1,10,5,2,6,4\n2,20,9,3,8,5\n")
        self.assertEqual([x["frame"] for x in good], [1, 2])
        self.assertEqual(errors, [])

    def test_truncated_record_reports_line(self):
        good, errors = self.parse("1,10,5,2,6,4\n2,100,20,3,12,\n3,7,3,1,2,1\n")
        self.assertEqual(len(good), 2)
        self.assertIn("line 3", errors[0])

    def test_extra_columns_and_invalid_counts(self):
        good, errors = self.parse("1,10,5,2,6,4,extra\n2,10,5,2,6,99\n3,10,5,2,6,4\n")
        self.assertEqual(len(good), 1)
        self.assertEqual(len(errors), 2)

    def test_frame_bounds(self):
        good, errors = self.parse("1,5,3,1,2,1\n2,5,3,1,2,1\n3,5,3,1,2,1\n", 1, 3)
        self.assertEqual([x["frame"] for x in good], [2])
        self.assertEqual(errors, [])

    def test_missing_header_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "vk_batches.csv"
            path.write_text("frame,accepted\n1,10\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing columns"):
                module.load_csv(path)


if __name__ == "__main__":
    unittest.main()
