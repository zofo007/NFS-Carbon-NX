from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "reference-nfsmw-nx/sdk/src/ui/switch_perf.cpp"


@unittest.skipUnless(SOURCE.is_file(), "SDK checkout unavailable")
class SwitchHotspotTests(unittest.TestCase):
    def test_sampling_is_opt_in_and_identity_is_handle_bound(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn('"perfil_hotspots.flag"', source)
        self.assertIn('"perfil_pilas.flag"', source)
        self.assertIn("StackMode g_stack_mode = StackMode::kOff;", source)
        self.assertIn("g_stack_mode = SelectMode(sample_stacks, hotspot_flag);", source)
        self.assertIn("sample_stacks = g_stack_mode != StackMode::kOff;", source)
        self.assertIn("ShouldSample(g_stack_mode, h, s.hotspot_handle.load", source)
        self.assertIn("s.hotspot_handle.store(0,", source)
        self.assertIn("s.hotspot_handle.store(IsHotspotThread(tmp) ? handle : 0", source)
        self.assertIn("__real_svcSleepThread(SampleDelayNs(g_stack_mode, sample_sequence));", source)

    def test_pause_has_unconditional_resume_before_reporting_and_overhead_accounting(self):
        source = SOURCE.read_text(encoding="utf-8")
        begin = source.index("const u64 capture_start = armGetSystemTick();")
        end = source.index("// Once per second, FPS and resolution", begin)
        capture = source[begin:end]
        paused = capture[capture.index("++g_capture_stats.paused;"):capture.index("svcSetThreadActivity(h, ThreadActivity_Runnable)")]
        self.assertIn("svcGetThreadContext3", paused)
        self.assertNotIn("return;", paused)
        for forbidden in ("std::", "printf", "fopen", "malloc", "new ", "REXLOG"):
            self.assertNotIn(forbidden, paused)
        self.assertIn("++g_capture_stats.resume_failed;", capture)
        self.assertEqual(capture.count("g_capture_stats.wall_ticks += capture_ticks;"), 2)
        self.assertIn("stack capture %s:", source)
        self.assertIn("attempt wall total/max", source)
