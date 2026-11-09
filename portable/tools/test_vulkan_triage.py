import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent

class TriageTest(unittest.TestCase):
    def test_csv_analysis_identifies_long_queue_wait_and_short_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            with (p / 'frames.csv').open('w', newline='') as file:
                cols = 'frame frame_ms cpu_ms capture_ms max_draw_ms decode_ms texture_check_ms assemble_ms queue_wait_ms execute_ms present_ms'.split()
                out = csv.DictWriter(file, fieldnames=cols); out.writeheader()
                out.writerow(dict(zip(cols, [1, 5000, 20, 2, 0.1, 0, 1.4, 0.1, 4990, 0.5, 5000])))
                out.writerow(dict(zip(cols, [2, 20, 20, 2, 0.1, 0, 1.4, 0.1, 1, 0.5, 15])))
            result = subprocess.run([sys.executable, str(HERE / 'vulkan_triage.py'), str(p)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('5.020s', result.stdout)
            self.assertIn('WARNING', result.stdout)
            self.assertIn('queue wait >200ms', result.stdout)
    def test_rejects_invalid_csv(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'frames.csv').write_text('frame,frame_ms\n1,0\n')
            result = subprocess.run([sys.executable, str(HERE / 'vulkan_triage.py'), tmp], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing CSV columns', result.stderr)

if __name__ == '__main__': unittest.main()
