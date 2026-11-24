import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from cpu_info import V2, V3, V4, cpu_level, parse_cpuinfo


class CpuInfoTest(unittest.TestCase):
    def test_levels(self):
        # EN: Sandy Bridge has AVX, not AVX2. PT-BR: Sandy Bridge tem AVX, não AVX2.
        self.assertEqual(cpu_level({'lm'}), 1)
        self.assertEqual(cpu_level(V2 | {'avx','xsave'}), 2)
        self.assertEqual(cpu_level(V3), 3)
        self.assertEqual(cpu_level(V4), 4)
        self.assertEqual(cpu_level(V4 - {'avx2'}), 2)

    def test_intersection_and_affinity(self):
        cpu = 'processor: 0\nflags: ' + ' '.join(V3) + '\n\nprocessor: 1\nflags: ' + ' '.join(V2)
        self.assertEqual(cpu_level(parse_cpuinfo(cpu)[1]), 2)
        self.assertEqual(cpu_level(parse_cpuinfo(cpu, {0})[1]), 3)

    def test_missing_flags(self):
        with self.assertRaises(ValueError):
            parse_cpuinfo('processor: 0\n')


if __name__ == '__main__':
    unittest.main()
