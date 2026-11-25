#!/usr/bin/env python3
"""EN/PT-BR: Synthetic evidence for interval comparison / Evidência sintética da comparação."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "compare_capture.py"
SPEC = importlib.util.spec_from_file_location("compare_capture", SCRIPT)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class CompareCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, columns, rows):
        path = self.root / name
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(columns)
            writer.writerows(rows)
        return path

    def modern(self):
        return self.write("modern.csv", ["frame", "frame_ms", "presented", "presented_frame_ms",
                                         "capture_ms", "supported_draws", "texture_static_binds",
                                         "texture_memcmp_checks", "texture_tracker_queries",
                                         "texture_tracker_covered_queries", "texture_tracker_fast_hits"],
                          [[1, 1, 1, 0, 500, 500, 500, 500, 500, 0, 0],
                           [2, 1, 0, 0, 900, 900, 900, 900, 900, 0, 0],
                           [3, 1, 1, 10, 2, 10, 20, 8, 10, 2, 2],
                           [4, 1, 1, 30, 6, 30, 40, 2, 10, 8, 8]])

    def test_only_successful_positive_intervals_and_weighted_cost(self):
        summary = module.summarize(module.read_frames(self.modern()))
        self.assertEqual(summary["selected_logged_rows"], 4)
        self.assertEqual(summary["successful_presentations"], 3)
        self.assertEqual(summary["measured_intervals"], 2)
        self.assertEqual(summary["frame_ms"], {"mean": 20, "p50": 10, "p95": 30, "p99": 30})
        self.assertEqual(summary["mean_fps"], 50)
        self.assertAlmostEqual(summary["one_percent_low_fps"], 1000 / 30)
        self.assertEqual(summary["above_budget_frames"], 1)
        self.assertEqual(summary["metrics_ms"]["capture_ms"]["mean"], 4)
        self.assertEqual(summary["weighted_ms_per_supported_draw"]["capture_ms"], 0.2)
        self.assertEqual(summary["counts"]["supported_draws"]["total"], 40)
        self.assertEqual(summary["counts"]["texture_static_binds"]["per_frame"], 30)
        self.assertEqual(summary["tracker_rates"]["covered_queries"], 0.5)
        self.assertEqual(summary["tracker_rates"]["comparisons_avoided_by_proof"], 0.5)

    def test_independent_exclusive_ranges_and_json_cli(self):
        left = self.modern()
        right = self.write("right.csv", ["frame", "frame_ms"], [[101, 5], [102, 20], [103, 25]])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = module.main([str(left), str(right), "--left-after", "2", "--left-before", "4",
                                  "--right-after", "101", "--right-before", "103", "--json",
                                  "--scene-note", "same camera, unverified"])
        self.assertEqual(status, 0)
        data = json.loads(output.getvalue())
        self.assertIn("NOT established", data["notice"])
        self.assertEqual(data["left"]["measured_frame_first"], 3)
        self.assertEqual(data["right"]["measured_frame_first"], 102)
        self.assertEqual(data["delta_right_minus_left"]["frame_ms.mean"]["percent"], 100)
        self.assertEqual(data["scene_note_unverified"], "same camera, unverified")

    def test_legacy_status_accumulates_failures_before_selection(self):
        path = self.write("legacy-status.csv", ["frame", "frame_ms", "presented"],
                          [[1, 10, 1], [2, 30, 0], [3, 10, 1], [4, 20, 1]])
        summary = module.summarize(module.read_frames(path), after=2, before=4)
        self.assertEqual(summary["measured_intervals"], 1)
        self.assertEqual(summary["frame_ms"]["mean"], 40)
        self.assertIn("accumulated", summary["interval_source"])
        self.assertTrue(any("first successful" in warning for warning in summary["warnings"]))

    def test_legacy_minimal_schema_and_missing_optional_metrics(self):
        path = self.write("legacy.csv", ["frame", "frame_ms"], [[1, 16], [2, 20]])
        summary = module.summarize(module.read_frames(path))
        self.assertEqual(summary["frame_ms"]["mean"], 18)
        self.assertFalse(summary["metrics_ms"])
        self.assertFalse(summary["weighted_ms_per_supported_draw"])
        self.assertTrue(any("assume" in warning for warning in summary["warnings"]))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            module.print_summary("legacy", summary)
        self.assertIn("P50=16.000", output.getvalue())

    def test_one_percent_low_is_inverse_mean_of_slowest(self):
        rows = [{"frame": i + 1, "frame_ms": 10} for i in range(198)]
        rows.extend([{"frame": 199, "frame_ms": 50}, {"frame": 200, "frame_ms": 150}])
        summary = module.summarize(rows)
        self.assertEqual(summary["one_percent_low_samples"], 2)
        self.assertEqual(summary["one_percent_low_fps"], 10)

    def test_no_valid_measured_intervals_or_empty_range(self):
        for rows in ([{"frame": 1, "presented": 0, "presented_frame_ms": 20}],
                     [{"frame": 1, "presented": 1, "presented_frame_ms": 0}],
                     [{"frame": 1, "frame_ms": 0}], []):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                module.summarize(rows)
        with self.assertRaisesRegex(ValueError, "no frames"):
            module.summarize(module.read_frames(self.modern()), after=100)

    def test_schema_and_numeric_validation(self):
        cases = [(["frame", "capture_ms"], [[1, 1]]),
                 (["frame", "frame_ms", "frame_ms"], [[1, 1, 2]]),
                 (["frame", "frame_ms"], [[1]]),
                 (["frame", "frame_ms"], [[1, 2, 3]]),
                 (["frame", "frame_ms"], [[1, "nan"]]),
                 (["frame", "frame_ms"], [[1, "inf"]]),
                 (["frame", "frame_ms"], [[1, -1]]),
                 (["frame", "frame_ms", "supported_draws"], [[1, 1, -1]]),
                 (["frame", "frame_ms", "capture_ms"], [[1, 1, ""]]),
                 (["frame", "frame_ms", "presented"], [[1, 1, 2]]),
                 (["frame", "frame_ms", "supported_draws"], [[1, 1, 1 << 65]]),
                 (["frame", "frame_ms", "texture_tracker_queries", "texture_tracker_fast_hits"], [[1, 1, 1, 2]])]
        for index, (columns, rows) in enumerate(cases):
            with self.subTest(columns=columns, rows=rows), self.assertRaises(ValueError):
                module.read_frames(self.write(f"invalid{index}.csv", columns, rows))

    def test_invalid_budget_range_and_zero_delta_baseline(self):
        rows = [{"frame": 1, "frame_ms": 10, "capture_ms": 0}]
        for target in (0, -1, float("nan"), float("inf")):
            with self.subTest(target=target), self.assertRaises(ValueError):
                module.summarize(rows, target_fps=target)
        with self.assertRaises(ValueError):
            module.summarize(rows, after=4, before=4)
        left = module.summarize(rows)
        right = module.summarize([{**rows[0], "capture_ms": 1}])
        self.assertIsNone(module.differences(left, right)["capture_ms.mean"]["percent"])

    def test_unsafe_diagnostic_and_rejected_draws_remain_visible(self):
        summary = module.summarize([{"frame": 1, "frame_ms": 10,
                                     "texture_diagnostic_skips": 3, "rejected_draws": 1}])
        self.assertTrue(any("UNSAFE" in warning for warning in summary["warnings"]))
        self.assertTrue(any("Rejected draws" in warning for warning in summary["warnings"]))

    def test_gauges_are_peaks_and_zero_draws_have_no_cost_ratio(self):
        rows = [{"frame": 1, "frame_ms": 10, "capture_ms": 1, "supported_draws": 0,
                 "texture_tracker_tracked_bytes": 200},
                {"frame": 2, "frame_ms": 10, "capture_ms": 1, "supported_draws": 0,
                 "texture_tracker_tracked_bytes": 100}]
        summary = module.summarize(rows)
        self.assertEqual(summary["peak_gauges"]["texture_tracker_tracked_bytes"], 200)
        self.assertFalse(summary["weighted_ms_per_supported_draw"])


if __name__ == "__main__":
    unittest.main()
