import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import analyze_hotspots as h

NM = """0000000000001000 T __imp__sub_82110000
0000000000002000 t helper
0000000000003000 T sub_82111111
"""
LOG = """==== 10.0 s | modo: normal
-- hilo 1 "Render XThread": CPU 80.0% | prioridad 0x3A, nucleo preferido -1, mascara 0x7
    50.0%  pc imagen+0x1010
    25.0%  pc imagen+0x3004
    40.0%  pila imagen+0x1010 <- imagen+0x3004
-- hilo 2 "Other": CPU 5.0% | prioridad 0x3A, nucleo preferido -1, mascara 0x7
   sin muestras
"""


class HotspotTests(unittest.TestCase):
    def test_names_and_shares(self):
        leaf, incl, intervals, cpu = h.analyze(LOG, h.load_symbols(NM))
        self.assertEqual(leaf["Render XThread"], {"sub_82110000": 50.0, "sub_82111111": 25.0})
        self.assertEqual(incl["Render XThread"], {"sub_82110000": 40.0, "sub_82111111": 40.0})
        self.assertEqual(intervals["Other"], 1)
        self.assertFalse(leaf["Other"])


if __name__ == "__main__":
    unittest.main()