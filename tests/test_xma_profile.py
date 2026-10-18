from pathlib import Path
import unittest

SDK = Path(__file__).resolve().parents[1] / "reference-nfsmw-nx/sdk"


@unittest.skipUnless((SDK / "src/audio/xma_context.cpp").is_file(), "SDK checkout unavailable")
class XmaProfileTests(unittest.TestCase):
    def test_sampling_boundaries_preserve_locks_and_decoding(self):
        source = (SDK / "src/audio/xma_context.cpp").read_text(encoding="utf-8")
        for name, stage in (("Enable", "kEnable"), ("Disable", "kDisable")):
            body = source[source.index(f"void XmaContext::{name}()"):]
            body = body[:body.index("\n}")]
            self.assertLess(body.index(f"XmaProfileStage::{stage}"), body.index("std::lock_guard<std::mutex> lock(lock_);"))
        work = source[source.index("bool XmaContext::Work()"):source.index("void XmaContext::Enable()")]
        self.assertLess(work.index("if (!is_allocated() || !is_enabled())"), work.index("XmaProfileStage::kWorkLock"))
        self.assertLess(work.index("std::lock_guard<std::mutex> lock(lock_);"), work.index("profile.Next(XmaProfileStage::kWorkBody)"))
        self.assertIn("StoreContextMerged(data, initial_data, context_ptr);", work)
        self.assertIn("XmaProfileStage::kCodec", source[source.index("bool XmaContext::DecodePacket("):source.index("void XmaContext::Decode(")])
        self.assertIn("XmaProfileStage::kConvert", source[source.index("void XmaContext::ConvertFrame("):])
        decoder = (SDK / "src/audio/xma_decoder.cpp").read_text(encoding="utf-8")
        self.assertIn("context.Disable();", decoder)
        self.assertIn("context.Block(false);", decoder)
        self.assertNotIn("xma-profile", decoder)  # No new IO on the audio/MMIO thread.

    def test_optional_clock_and_profiler_report_contract(self):
        source = (SDK / "src/audio/xma_context.cpp").read_text(encoding="utf-8")
        self.assertIn("#if REX_PLATFORM_SWITCH\nconstexpr bool kXmaProfileDefault = true;", source)
        self.assertIn("#else\nconstexpr bool kXmaProfileDefault = false;", source)
        self.assertIn("REXCVAR_DEFINE_BOOL(audio_xma_profile", source)
        self.assertIn('extern "C" {\n#include <switch/arm/counter.h>', source)
        self.assertIn("InfoType_ThreadTickCountDeprecated", source)
        self.assertIn("threadGetCurHandle(), UINT64_MAX", source)
        self.assertIn("valid ? armTicksToNs(cpu_ticks) : 0, valid", source)
        report = (SDK / "src/ui/switch_perf.cpp").read_text(encoding="utf-8")
        for name in ("enable", "disable", "work.lock", "work.body(inclusive)", "codec", "convert"):
            self.assertIn(f'"{name}"', report)
        self.assertIn("xma-profile %s:", report)
        self.assertIn("valid CPU samples", report)
        self.assertIn("rex::audio::g_xma_profile[i].Take()", report)
