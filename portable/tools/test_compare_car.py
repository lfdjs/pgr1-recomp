import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
HERE = Path(__file__).resolve().parent
class CompareTest(unittest.TestCase):
    def test_region_and_heatmap(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            header=b'P6\n2 1\n255\n'
            (p/'compare-reference.ppm').write_bytes(header+bytes([1,2,3,4,5,6]))
            (p/'compare-gpu.ppm').write_bytes(header+bytes([1,2,3,14,5,6]))
            r=subprocess.run([sys.executable,str(HERE/'compare_car.py'),directory,'--region','1','0','1','1','--heatmap',str(p/'heatmap.ppm')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('3.33333/255',r.stdout)
            self.assertIn('1/1',r.stdout)
            self.assertEqual((p/'heatmap.ppm').read_bytes(),header+bytes([0,0,0,10,0,0]))
if __name__=='__main__': unittest.main()
