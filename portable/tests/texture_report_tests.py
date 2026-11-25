#!/usr/bin/env python3
import importlib.util
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "texture_report.py"
spec = importlib.util.spec_from_file_location("texture_report", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
HEADER = "frame,texture_static_binds,texture_memcmp_checks,texture_memcmp_bytes,texture_same_draw_reuses,texture_decodes,texture_check_ms,decode_ms,capture_ms,frame_ms\n"


class TextureReportTests(unittest.TestCase):
    def parse(self, body, after=0, before=None):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            file.write_text(HEADER + body)
            return module.read_frames(file, after, before)

    def test_values_and_ranges(self):
        rows, errs = self.parse("1,5,4,8192,1,0,1,0,5,16\n2,5,5,10000,0,1,2,1,6,20\n", 1)
        self.assertEqual([r["frame"] for r in rows], [2])
        self.assertFalse(errs)

    def test_csv_truncation(self):
        rows, errs = self.parse("1,5,4,8192,1,0,1,0,5,16\n2,5,4,8192,1,0\n")
        self.assertEqual(len(rows), 1)
        self.assertIn("incomplete", errs[0])

    def test_invalid_ratio_and_negative(self):
        rows, errs = self.parse("1,5,4,8192,6,0,1,0,5,16\n2,-5,4,8192,0,0,1,0,5,16\n3,5,4,8192,1,0,1,0,5,16\n")
        self.assertEqual(len(rows), 1)
        self.assertEqual(len(errs), 2)

    def test_percentile(self):
        self.assertEqual(module.percentile([1, 2, 3, 4, 5], 0.95), 5)
        self.assertEqual(module.percentile([], 0.99), 0)

    def test_optional_mip_counters_and_legacy_format(self):
        # EN/PT-BR: Old frames.csv works; new mip fields are parsed when present.
        legacy, errors = self.parse("1,5,4,8192,1,0,1,0,5,16\n")
        self.assertFalse(errors)
        self.assertNotIn("texture_mip_levels_reused", legacy[0])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "frames.csv"
            path.write_text(HEADER.rstrip("\n") +
                ",texture_mip_levels_decoded,texture_mip_levels_reused\n" +
                "1,5,4,8192,1,1,1,0,5,16,1,3\n")
            current, issues = module.read_frames(path)
            self.assertFalse(issues)
            self.assertEqual(current[0]["texture_mip_levels_reused"], 3)

    def test_optional_cache_counters_and_peak(self):
        # EN: Read the new columns and preserve legacy support.
        # PT-BR: Leia os novos campos e preserve CSVs anteriores.
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "frames.csv"
            path.write_text(HEADER.rstrip("\n") + "," + ",".join(module.CACHE_COUNTS) +
                "\n1,5,4,8192,1,4,1,0,5,16,2,3,1,0,512\n")
            rows, errors = module.read_frames(path)
            self.assertFalse(errors)
            self.assertEqual(rows[0]["texture_cache_evictions"], 2)
            self.assertEqual(rows[0]["texture_cache_entries_peak"], 512)

    def tracker_csv(self, changes=None):
        counts = {key: 0 for key in module.TRACKER_COUNTS}
        counts.update(texture_tracker_queries=10, texture_tracker_fast_hits=4,
                      texture_tracker_covered_queries=6, texture_tracker_exact_fallbacks=6,
                      texture_tracker_uncovered_ranges=4, texture_tracker_invalidations=1,
                      texture_tracker_false_positives=1, texture_tracker_false_negatives=2,
                      texture_tracker_monitored_writes=3, texture_tracker_dirty_pages=5,
                      texture_tracker_tracked_bytes=1048576)
        values = {**counts, "texture_tracker_ms": 0.2, "texture_tracker_write_ms": 0.3}
        values.update(changes or {})
        fields = (*module.TRACKER_COUNTS, *module.TRACKER_TIMES)
        return (HEADER.rstrip("\n") + "," + ",".join(fields) + "\n" +
                "1,5,4,8192,1,0,1,0,5,16," + ",".join(str(values[key]) for key in fields) + "\n")

    def parse_tracker(self, changes=None):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            file.write_text(self.tracker_csv(changes))
            return module.read_frames(file)

    def test_tracker_optional_schema_and_report(self):
        rows, errors = self.parse_tracker()
        self.assertFalse(errors)
        self.assertEqual(rows[0]["texture_tracker_false_negatives"], 2)
        self.assertEqual(rows[0]["texture_tracker_write_ms"], 0.3)
        # EN: Residency is a peak; cost is per frame and errors remain advisory.
        # PT-BR: Ocupação é pico; custo é por frame e erros seguem consultivos.
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            module.report([rows[0], {**rows[0], "frame": 2}])
        text = output.getvalue()
        self.assertIn("(60.00%)", text)
        self.assertIn("(50.00% of exact+proven", text)
        self.assertIn("falsos negativos=4", text)
        self.assertIn("bytes registrados=1,048,576", text)
        self.assertIn("mean/média 0.500 ms/frame; total 1.000 ms", text)
        self.assertIn("zero errors does not prove writer coverage", text)
        self.assertIn("do not add them again", text)

    def test_tracker_invalid_values(self):
        for changes in (
                {"texture_tracker_fast_hits": -1},
                {"texture_tracker_covered_queries": 11},
                {"texture_tracker_fast_hits": 11},
                {"texture_tracker_ms": -1},
                {"texture_tracker_ms": "nan"},
                {"texture_tracker_write_ms": "inf"},
                {"texture_tracker_write_ms": "-inf"},
                {"texture_tracker_queries": ""},
                {"texture_tracker_ms": ""}):
            with self.subTest(changes=changes):
                rows, errors = self.parse_tracker(changes)
                self.assertFalse(rows)
                self.assertEqual(len(errors), 1)

    def test_tracker_truncated_optional_fields(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            text = self.tracker_csv()
            file.write_text(text[:text.rfind(",")] + "\n")
            rows, errors = module.read_frames(file)
            self.assertFalse(rows)
            self.assertEqual(len(errors), 1)

    def test_legacy_report_has_no_tracker_claim(self):
        rows, errors = self.parse("1,5,4,8192,1,0,1,0,5,16\n")
        self.assertFalse(errors)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            module.report(rows)
        self.assertNotIn("Tracker /", output.getvalue())
        self.assertNotIn("texture_tracker_ms", output.getvalue())

    def test_journal_shadow_schema(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "frames.csv"
            header = HEADER.rstrip("\n") + "," + ",".join((*module.JOURNAL_COUNTS,*module.JOURNAL_TIMES)) + "\n"
            path.write_text(header + "1,5,4,8192,1,0,1,0,5,16,3,1,0,0,1,0,0.1\n")
            rows, errors = module.read_frames(path)
            self.assertFalse(errors)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                module.report(rows)
            self.assertIn("75.000%", output.getvalue())
            self.assertIn("NOT skipped", output.getvalue())
            path.write_text(header + "1,5,4,8192,1,0,1,0,5,16,3,1,0,4,1,0,0.1\n")
            rows, errors = module.read_frames(path)
            self.assertFalse(rows)
            self.assertEqual(len(errors),1)

    def test_old_schema_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            file.write_text("frame,frame_ms\n1,16\n")
            with self.assertRaisesRegex(ValueError, "missing fields"):
                module.read_frames(file)


if __name__ == "__main__":
    unittest.main()
