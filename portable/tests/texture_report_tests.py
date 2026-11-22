#!/usr/bin/env python3
import importlib.util
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

    def test_old_schema_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / "frames.csv"
            file.write_text("frame,frame_ms\n1,16\n")
            with self.assertRaisesRegex(ValueError, "missing fields"):
                module.read_frames(file)


if __name__ == "__main__":
    unittest.main()
