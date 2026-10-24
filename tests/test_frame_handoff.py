from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CARBON = ROOT / "carbon-source"


@unittest.skipUnless(CARBON.is_dir(), "Carbon source checkout unavailable")
class FrameHandoffTests(unittest.TestCase):
    def test_hook_matches_the_two_sleep0_handoff_loops(self):
        hook = (CARBON / "src/frame_handoff_hook.cpp").read_text(encoding="utf-8")
        self.assertIn("kMainWaitReturn = 0x824E977C", hook)
        self.assertIn("kRenderWaitReturn = 0x824E99E4", hook)
        self.assertIn("#define HANDOFF_SLEEP_IMPL __imp__sub_82943548", hook)
        self.assertIn("HANDOFF_SLEEP_IMPL(ctx, base);", hook)  # every other Sleep keeps the original
        self.assertIn('REXCVAR_DEFINE_INT32(carbon_handoff_priority, 0x3A,', hook)  # MW relay priority by default
        self.assertIn("src/frame_handoff_hook.cpp", (CARBON / "CMakeLists.txt").read_text(encoding="utf-8"))
        generated = CARBON / "generated/default"
        if not generated.is_dir():
            self.skipTest("Generated local guest sources unavailable")
        text = "".join(p.read_text(encoding="utf-8") for p in generated.glob("nfscarbon_recomp.*.cpp"))
        # Each site: Sleep(0), then the flag at 0x82C651C4 (lis -32058 + 20932) is re-read in the loop.
        for ret in ("824E977C", "824E99E4"):
            site = re.search(r"ctx\.r3\.s64 = 0;\n\t// bl 0x82943548\n\tctx\.lr = 0x%s;\n\tsub_82943548\(ctx, base\);\n"
                             r"\t// lwz r11,20932\(r(27|31)\)" % ret, text)
            self.assertIsNotNone(site, ret)


if __name__ == "__main__":
    unittest.main()
