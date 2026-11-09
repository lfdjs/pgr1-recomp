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
