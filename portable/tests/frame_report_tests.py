#!/usr/bin/env python3
"""Check frame-selection and pacing evidence with independent CSV fixtures."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPORT = Path(__file__).resolve().parents[1] / "tools" / "frame_report.py"


class FrameReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.write("frames.csv", ["frame", "frame_ms", "supported_draws", "rejected_draws"],
                   [[1, 1, 10, 0], [2, 16, 100, 0], [3, 20, 100, 0], [4, 40, 100, 0]])
        # Partial submissions use the same guest frame; only selected complete
        # frames should contribute, and both spans for frame 3 must be summed.
        self.write("gpu.csv", ["frame", "gpu_ms"],
                   [[1, 200], [2, 1], [3, 2], [3, 3], [4, 400], [999, 999]])

    def write(self, name, columns, rows):
        with (self.directory / name).open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(columns)
            writer.writerows(rows)

    def run_report(self, *args):
        return subprocess.run([sys.executable, str(REPORT), str(self.directory), *args],
                              capture_output=True, text=True, check=False)

    def test_range_joins_gpu_by_selected_complete_frame(self):
        result = self.run_report("--after-frame", "1", "--before-frame", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Selected frame range=2..3", result.stdout)
        self.assertIn("End-to-end frame: mean=18.000 ms", result.stdout)
        self.assertIn("GPU timestamp span (asynchronous): mean=3.000 ms", result.stdout)
        self.assertIn("GPU timestamps matched=2/2", result.stdout)
        self.assertIn("over budget=1/2 (50.0%)", result.stdout)
        self.assertIn("over budget+1 ms=1/2 (50.0%)", result.stdout)
        self.assertIn("1% low presentation rate=50.00", result.stdout)
        self.assertIn("Mean presentation rate=55.56", result.stdout)
        self.assertIn("Legacy CSV: no presentation status", result.stdout)

    def test_jitter_tolerance_is_separate_from_exact_budget(self):
        self.write("frames.csv", ["frame", "frame_ms", "supported_draws", "rejected_draws"],
                   [[1, 16.8, 10, 0], [2, 17, 10, 0], [3, 18, 10, 0], [4, 35, 10, 0]])
        result = self.run_report()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("over budget=4/4 (100.0%)", result.stdout)
        self.assertIn("over budget+1 ms=2/4 (50.0%)", result.stdout)
        self.assertIn("over two budgets=1", result.stdout)
        self.assertIn("longest consecutive budget+1 ms overruns=2", result.stdout)

    def test_capture_breakdown_and_missing_gpu_samples(self):
        self.write("frames.csv", ["frame", "frame_ms", "supported_draws", "rejected_draws",
                                  "texture_check_ms", "assemble_ms"],
                   [[2, 16, 10, 0, 1, 3], [5, 20, 10, 0, 2, 5]])
        result = self.run_report()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("texture_check_ms: mean=1.500 ms", result.stdout)
        self.assertIn("assemble_ms: mean=4.000 ms", result.stdout)
        self.assertIn("GPU timestamps matched=1/2", result.stdout)

    def test_no_selected_frames_does_not_print_rate(self):
        result = self.run_report("--after-frame", "20")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No submitted game frames", result.stderr)
        self.assertNotIn("presentation rate", result.stdout)

    def test_only_successful_present_counter_intervals_determine_fps(self):
        self.write("frames.csv", ["frame", "frame_ms", "present_requested", "presented",
                                  "presented_frame_ms", "supported_draws", "rejected_draws",
                                  "fence_ms", "acquire_ms"],
                   [[1, 0.1, 1, 1, 0, 10, 0, 1, 2],   # initializes counter
                    [2, 0.1, 1, 0, 0, 1000, 99, 900, 900],  # failed acquire
                    [3, 0.1, 0, 0, 0, 1000, 99, 900, 900],  # offscreen draw
                    [4, 0.1, 0, 0, 0, 0, 0, 900, 900],  # simulation tick
                    [5, 1, 1, 1, 40, 10, 0, 3, 4],
                    [6, 1, 1, 1, 60, 10, 0, 5, 6]])
        result = self.run_report()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Successful presentations=3/6", result.stdout)
        self.assertIn("excluded unsuccessful/offscreen rows=3", result.stdout)
        self.assertIn("measured intervals=2; uninitialized intervals omitted=1", result.stdout)
        self.assertIn("End-to-end frame: mean=50.000 ms", result.stdout)
        self.assertIn("Mean presentation rate=20.00", result.stdout)
        self.assertIn("Frames=3 supported Draws=30 rejected Draws=0", result.stdout)
        self.assertIn("fence_ms: mean=3.000 ms", result.stdout)
        self.assertIn("acquire_ms: mean=4.000 ms", result.stdout)

    def test_older_status_csv_accumulates_time_in_failed_attempts(self):
        self.write("frames.csv", ["frame", "frame_ms", "presented", "supported_draws", "rejected_draws"],
                   [[1, 10, 1, 10, 0], [2, 30, 0, 10, 0], [3, 10, 1, 10, 0]])
        result = self.run_report()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("End-to-end frame: mean=25.000 ms", result.stdout)
        self.assertIn("Mean presentation rate=40.00", result.stdout)
        self.assertIn("Interval source=accumulated logged intervals", result.stdout)

    def test_failed_presentations_and_initial_zero_cannot_claim_fps(self):
        columns = ["frame", "frame_ms", "presented", "presented_frame_ms", "supported_draws", "rejected_draws"]
        for rows, message in [([[1, 1, 0, 1, 10, 0]], "No successful presented"),
                              ([[1, 1, 1, 0, 10, 0]], "No measured positive presentation intervals")]:
            with self.subTest(rows=rows):
                self.write("frames.csv", columns, rows)
                result = self.run_report()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertNotIn("Mean presentation rate", result.stdout)

    def test_reference_uses_capture_frame_range(self):
        self.write("pgr1_perf.csv", ["capture_frame", "reference_draw_ms", "reference_clear_ms", "decode_ms", "ipc_ms"],
                   [[60, 100, 20, 3, 1], [120, 200, 40, 6, 2]])
        result = self.run_report("--reference", "--after-frame", "60")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Sampled CPU reference captures: 1", result.stdout)
        self.assertIn("reference_draw_ms: mean=200.000 ms", result.stdout)

    def test_zero_intervals_cannot_produce_infinite_fps(self):
        self.write("frames.csv", ["frame", "frame_ms", "supported_draws", "rejected_draws"],
                   [[1, 0, 10, 0]])
        result = self.run_report()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timing data is incomplete", result.stderr)


if __name__ == "__main__":
    unittest.main()
