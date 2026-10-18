import importlib.util
import tempfile
import unittest
import warnings
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_register_sharing", ROOT / "scripts/check_register_sharing.py")
sharing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sharing)

SAMPLE = """DEFINE_REX_FUNC(sub_82000000) {
\t// std r31,-8(r1)
\t// mr r31,r3
\t// lwz r3,0(r31)
}
DEFINE_REX_FUNC(sub_82000100) {
\t// lwz r3,100(r31)
}
DEFINE_REX_FUNC(sub_82000200) {
\t// vor128 v14,v69,v69
}
DEFINE_REX_FUNC(sub_82000300) {
\t// lwz r3,100(r31)
}
DEFINE_REX_FUNC(sub_82000400) {
\t// lwz r3,100(r31)
\tctx.r3.u64 = REX_LOAD_U32(ctx.r31.u32 + 100);
}
"""


class RegisterSharingTests(unittest.TestCase):
    def test_scan_closes_generated_files(self):
        with tempfile.TemporaryDirectory() as tmp, warnings.catch_warnings(record=True) as observed:
            warnings.simplefilter('always', ResourceWarning)
            Path(tmp, 'nfscarbon_recomp.0.cpp').write_text(SAMPLE)
            manifest = Path(tmp, 'm.toml')
            manifest.write_text('')
            sharing.scan(tmp, manifest)
            self.assertEqual([w for w in observed if w.category is ResourceWarning], [])

    def test_scan_flags_only_unshared_reads_before_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "nfscarbon_recomp.0.cpp").write_text(SAMPLE)
            manifest = Path(tmp, "m.toml")
            manifest.write_text("0x82000300 = { share_registers = true }\n")
            hits = sharing.scan(tmp, manifest)
        # Prologue save then write: fine. Marked in the manifest or routed through ctx: fine.
        self.assertEqual(set(hits), {"sub_82000100", "sub_82000200"})
        self.assertEqual(set(hits["sub_82000200"]), {("v", 69)})

    def test_generated_carbon_code_has_no_unshared_reads(self):
        generated = ROOT / "carbon-source/generated/default"
        if not generated.is_dir():
            self.skipTest("Generated local guest sources unavailable")
        hits = sharing.scan(generated, ROOT / "carbon-source/nfscarbon_manifest.toml")
        self.assertEqual(set(hits) - sharing.KNOWN_TEXT_ORDER, set())


if __name__ == "__main__":
    unittest.main()
