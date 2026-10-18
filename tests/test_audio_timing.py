from pathlib import Path
import re
import unittest

SDK = Path(__file__).resolve().parents[1] / "reference-nfsmw-nx/sdk/src"


@unittest.skipUnless(SDK.is_dir(), "Horizon SDK source checkout unavailable")
class AudioTimingTests(unittest.TestCase):
    def test_timing_producers_and_report_counter_contract(self):
        worker = (SDK / "audio/audio_system.cpp").read_text(encoding="utf-8")
        driver = (SDK / "audio/switch/switch_audio_system.cpp").read_text(encoding="utf-8")
        report = (SDK / "ui/switch_perf.cpp").read_text(encoding="utf-8")
        counters = {
            int(counter): operation
            for operation, counter in re.findall(r"RexSwitchPerf(Add|Max)\((\d+),", worker + driver)
            if int(counter) >= 33
        }
        self.assertEqual(counters, {33: "Add", 34: "Max", 35: "Add", 36: "Max"})
        capacity = re.search(r"kCounterCount = (\d+)", report)
        self.assertIsNotNone(capacity)
        self.assertGreater(int(capacity[1]), max(counters))
        for counter in (33, 35):
            self.assertIn(f"delta({counter})", report)
        for counter in (34, 36):
            self.assertIn(f"g_counters[{counter}].exchange(0, std::memory_order_relaxed)", report)
        self.assertIn("audio timing: pump deadline resets", report)
        self.assertIn("nx::SwitchAudioDriver::kChannelSamples / 48000", worker)
        self.assertIn("block_ns / 1000", worker)
        self.assertIn("armTicksToNs(now - deadline) / 1000", driver)
