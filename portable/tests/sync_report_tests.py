#!/usr/bin/env python3
"""EN/PT-BR: Independent synchronization evidence / Evidência independente de sincronização."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPORT = Path(__file__).resolve().parents[1] / "tools" / "sync_report.py"
COLUMNS = ("frame", "guest_vblank", "presented", "reason", "submissions", "nonpresent_submissions",
           "visibility_queries", "visibility_query_pool_creates", "visibility_query_pool_reuses",
           "readback_requests", "readback_bytes", "visibility_report_wait_ms", "readback_wait_ms",
           "resource_wait_ms", "fence_wait_ms", "readback_resolve_ms", "shader_ms")


class SyncReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.frames = [[1, 10, 1, 0], [2, 20, 0, 0], [3, 30, 1, 40], [4, 40, 1, 20]]
        self.source = [
            [1, 10, 1, "present", 1, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            [2, 20, 0, "flush", 1, 1, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
            [2, 21, 0, "readback", 1, 1, 0, 0, 0, 1, 4096, 0, 0, 0, 0, .5, 0],
            [2, 22, 0, "present", 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
            [3, 29, 0, "visibility_report", 1, 1, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0],
            [3, 30, 1, "present", 1, 0, 2, 0, 1, 0, 0, 3, 0, 0, 0, 0, 0],
            [4, 40, 1, "present", 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
            [5, 45, 0, "resource_busy", 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        ]
        self.sync = [
            [1, 10, 1, "present", 1, 0, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            [3, 30, 1, "present", 5, 4, 6, 0, 2, 1, 4096, 3, 7, 0, 0, .5, 0],
            [4, 40, 1, "present", 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0],
        ]
        # EN: Deliberately defer and reorder samples, retaining duplicate source IDs.
        # PT-BR: Amostras adiadas/fora de ordem, mantendo IDs de origem repetidos.
        self.gpu = [[1, 1], [2, 1], [3, 4], [2, 2], [4, .5], [3, 5], [2, 3], [5, 99]]
        self.write("frames.csv", ("frame", "guest_vblank", "presented", "presented_frame_ms"), self.frames)
        self.write("sync-submissions.csv", COLUMNS, self.source)
        self.write("sync.csv", COLUMNS, self.sync)
        self.write("gpu.csv", ("frame", "gpu_ms"), self.gpu)

    def write(self, name, columns, rows):
        with (self.directory / name).open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(columns)
            writer.writerows(rows)

    def run_report(self, *args):
        return subprocess.run([sys.executable, "-B", str(REPORT), str(self.directory), *args],
                              text=True, capture_output=True, check=False)

    def result(self, *args):
        completed = self.run_report(*args, "--json")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return json.loads(completed.stdout)

    def assert_invalid(self, *args):
        completed = self.run_report(*args)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Evidência de sincronização inválida", completed.stderr)
        self.assertNotIn("FPS=", completed.stdout)

    def test_all_submissions_and_real_successful_interval_denominator(self):
        result = self.result()
        self.assertEqual(result["successful_presentations"], 3)
        self.assertEqual(result["totals"]["submissions"], 7)
        self.assertAlmostEqual(result["submissions_per_present"], 7 / 3)
        self.assertEqual(result["totals"]["nonpresent_submissions"], 4)
        self.assertEqual(result["totals"]["visibility_queries"], 9)
        self.assertEqual(result["totals"]["visibility_query_pool_creates"], 1)
        self.assertEqual(result["totals"]["visibility_query_pool_reuses"], 3)
        self.assertEqual(result["totals"]["readback_requests"], 1)
        self.assertEqual(result["totals"]["readback_bytes"], 4096)
        self.assertEqual(result["source"]["selected_rows"], 7)
        self.assertEqual(result["source"]["reason_totals"]["present"]["rows"], 4)
        self.assertEqual(result["source"]["reason_totals"]["readback"]["readback_bytes"], 4096)
        self.assertEqual(result["source"]["trailing_reasons"], {"resource_busy": 1})
        self.assertEqual(result["source"]["trailing_unpresented_rows"], 1)

    def test_wait_percentiles_are_per_successful_interval_including_zero(self):
        result = self.result()
        waits = result["interval_distributions"]["readback_wait_ms"]
        self.assertEqual(waits["count"], 3)
        self.assertAlmostEqual(waits["mean"], 7 / 3)
        self.assertEqual(waits["p50"], 0)
        self.assertEqual(waits["p95"], 7)
        self.assertEqual(waits["p99"], 7)
        self.assertEqual(result["presentation_ms"]["count"], 2)
        self.assertEqual(result["fps"], 1000 / 30)
        self.assertEqual(result["one_percent_low_fps"], 25)

    def test_gpu_sums_duplicates_and_deferred_samples_by_source(self):
        result = self.result()
        gpu = result["gpu"]
        self.assertEqual(gpu["source_frame_sums_ms"], {"1": 1, "2": 6, "3": 9, "4": .5})
        self.assertEqual(gpu["source_samples"], 7)
        self.assertEqual(gpu["expected_samples_by_source_frame"], {"1": 1, "2": 3, "3": 2, "4": 1})
        self.assertEqual(gpu["complete_interval_sums_ms"], {"1": 1, "3": 15, "4": .5})
        self.assertEqual(gpu["complete_intervals"], 3)
        self.assertEqual(gpu["source_ms"]["mean"], 16.5 / 4)

    def test_vblank_filter_keeps_failed_attempts_and_earlier_prefixes(self):
        result = self.result("--after-vblank", "25", "--before-vblank", "35")
        self.assertEqual(result["selection"]["frames"], [3])
        self.assertEqual(result["source"]["selected_rows"], 5)
        self.assertEqual(result["totals"]["submissions"], 5)
        self.assertEqual(result["gpu"]["source_frame_sums_ms"], {"2": 6, "3": 9})
        self.assertEqual(result["gpu"]["complete_interval_sums_ms"], {"3": 15})

    def test_frame_and_vblank_bounds_are_exclusive_and_intersect(self):
        result = self.result("--after-frame", "1", "--before-frame", "4",
                             "--after-vblank", "20", "--before-vblank", "40")
        self.assertEqual(result["selection"]["frames"], [3])
        self.assert_invalid("--after-vblank", "30", "--before-vblank", "40")
        self.assert_invalid("--after-frame", "4")

    def test_partial_gpu_same_frame_is_not_falsely_complete(self):
        self.write("gpu.csv", ("frame", "gpu_ms"), [row for row in self.gpu if row != [2, 2]])
        result = self.result("--after-frame", "1", "--before-frame", "4")
        self.assertEqual(result["gpu"]["source_frame_sums_ms"], {"2": 4, "3": 9})
        self.assertEqual(result["gpu"]["matched_source_frames"], 2)
        self.assertEqual(result["gpu"]["complete_source_frames"], 1)
        self.assertEqual(result["gpu"]["complete_intervals"], 0)
        self.assertIsNone(result["gpu"]["complete_interval_ms"])

    def test_absent_gpu_is_unavailable(self):
        (self.directory / "gpu.csv").unlink()
        self.assertEqual(self.result()["gpu"], {"available": False})

    def test_legacy_absent_sync_does_not_invent_zero_counts(self):
        (self.directory / "sync.csv").unlink()
        result = self.result()
        self.assertFalse(result["available"])
        self.assertNotIn("totals", result)
        self.assertNotIn("submissions_per_present", result)
        output = self.run_report().stdout
        self.assertIn("indisponíveis", output)
        self.assertNotIn("submissions: total=0", output)

    def test_missing_source_cannot_prove_complete_gpu_interval(self):
        (self.directory / "sync-submissions.csv").unlink()
        result = self.result()
        self.assertFalse(result["source"]["available"])
        self.assertIsNone(result["source"]["reason_totals"])
        self.assertEqual(result["gpu"]["complete_intervals"], 0)

    def test_missing_optional_counter_is_unavailable_not_zero(self):
        result = self.result()
        self.assertIsNone(result["totals"]["fence_wait_calls"])

    def test_cli_bilingual_caveats_and_reason_breakdown(self):
        completed = self.run_report()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("Reason / Motivo visibility_report: rows=1", completed.stdout)
        self.assertIn("not additive exclusive costs", completed.stdout)
        self.assertIn("não são custos exclusivos aditivos", completed.stdout)
        self.assertIn("not individual calls", completed.stdout)
        self.assertIn("do not prove identical scenes", completed.stdout)

    def test_invalid_bounds(self):
        for bounds in (("--after-frame", "-1"), ("--before-vblank", "-1"),
                       ("--after-vblank", "5", "--before-vblank", "5")):
            with self.subTest(bounds=bounds):
                self.assert_invalid(*bounds)

    def test_invalid_metrics_rejected_even_outside_selection(self):
        for filename, columns, rows, column in (
                ("sync.csv", COLUMNS, self.sync, "visibility_report_wait_ms"),
                ("sync-submissions.csv", COLUMNS, self.source, "readback_wait_ms"),
                ("gpu.csv", ("frame", "gpu_ms"), self.gpu, "gpu_ms")):
            for invalid in ("nan", "inf", "-0.001", "not-a-number"):
                with self.subTest(filename=filename, invalid=invalid):
                    changed = [list(row) for row in rows]
                    changed[0][columns.index(column)] = invalid
                    self.write(filename, columns, changed)
                    self.assert_invalid("--after-frame", "1")
                    self.write(filename, columns, rows)

    def test_bad_integer_status_and_reason(self):
        for column, invalid in (("visibility_queries", -1), ("submissions", 1 << 64),
                                ("presented", 2), ("reason", "bad reason")):
            with self.subTest(column=column):
                changed = [list(row) for row in self.sync]
                changed[0][COLUMNS.index(column)] = invalid
                self.write("sync.csv", COLUMNS, changed)
                self.assert_invalid()

    def test_truncated_extra_and_duplicate_columns(self):
        for data in ("frame,guest_vblank,presented,reason\n1,10,1\n",
                     "frame,guest_vblank,presented,reason\n1,10,1,present,extra\n",
                     "frame,frame,guest_vblank,presented,reason\n1,1,10,1,present\n"):
            with self.subTest(data=data):
                (self.directory / "sync.csv").write_text(data)
                self.assert_invalid()

    def test_counter_inconsistency_and_missing_endpoint(self):
        changed = [list(row) for row in self.source]
        changed[1][COLUMNS.index("visibility_queries")] += 1
        self.write("sync-submissions.csv", COLUMNS, changed)
        self.assert_invalid()
        self.write("sync-submissions.csv", COLUMNS, self.source[:-2])
        self.assert_invalid()

    def test_duplicate_endpoint_and_mismatched_scene_stamp(self):
        self.write("sync.csv", COLUMNS, self.sync + self.sync[-1:])
        self.assert_invalid()
        changed = [list(row) for row in self.sync]
        changed[1][1] = 31
        self.write("sync.csv", COLUMNS, changed)
        self.assert_invalid()

    def test_missing_vblank_cannot_select_scene(self):
        self.write("frames.csv", ("frame", "presented", "presented_frame_ms"),
                   [[row[0], row[2], row[3]] for row in self.frames])
        self.assert_invalid("--after-vblank", "15")

    def test_tick_filter_is_exclusive_and_retains_all_prefixes(self):
        self.write("frames.csv", ("frame", "guest_vblank", "presented", "presented_frame_ms", "simulation_tick"),
                   [row + [tick] for row, tick in zip(self.frames, (2, 3, 6, 9))])
        result = self.result("--after-tick", "3", "--before-tick", "9")
        self.assertEqual(result["selection"]["frames"], [3])
        self.assertEqual(result["selection"]["simulation_ticks"], [6])
        self.assertEqual(result["source"]["selected_rows"], 5)
        self.assertEqual(result["gpu"]["complete_interval_sums_ms"], {"3": 15})
        self.assert_invalid("--after-tick", "6", "--before-tick", "9")

    def test_legacy_missing_tick_rejects_tick_selection(self):
        self.assert_invalid("--after-tick", "1")


if __name__ == "__main__":
    unittest.main()
