from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "carbon-source/renderer/src"


@unittest.skipUnless((ROOT / "renderer.cpp").is_file(), "Carbon checkout unavailable")
class VertexArenaTests(unittest.TestCase):
    def test_single_storage_preserves_page_checks_and_copy_lifetime(self):
        header = (ROOT / "renderer.h").read_text(encoding="utf-8")
        self.assertIn("using ArenaEntry = VertexArenaEntry;", header)
        source = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        fetch = source[source.index("bool Renderer::ArenaFetch("):source.index("bool Renderer::SetupVertexData(")]
        self.assertIn("e.pages.resize(pages);", fetch)
        for old in ("page_hash", "page_checked", "page_interval"):
            self.assertNotIn(old, fetch)
        for check in (
            "if (page.checked == frame_number_) continue;",
            "if (page.hash && frame_number_ < page.checked + page.interval) continue;",
            "else if (h != page.hash)",
            "page.interval = std::min<uint8_t>(page.interval * 2, kArenaMaxInterval);",
            "page.checked = frame_number_;",
            "if (count >= 3)",
            "arena_reset_pending_ = true;",
        ):
            self.assertIn(check, fetch)
        self.assertLess(fetch.index("if (!page.hash)"), fetch.index("std::memcpy(arena_mapped_"))
        self.assertLess(fetch.index("vmaFlushAllocation"), fetch.index("page.hash = h;"))
        self.assertIn("ArenaEntry e(&arena_metadata_memory_);", fetch)
        self.assertLess(fetch.index("arena_entries_->erase(key);"), fetch.index("if (count >= 3)"))

    def test_reset_waits_remain_before_clear_and_phase_timing_is_optional(self):
        source = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        start = source.index("if (arena_reset_pending_) {")
        reset = source[start:source.index("for (auto& fn : f.deferred_destroy)", start)]
        self.assertIn("if (&other != &f && other.submitted)", reset)
        self.assertIn("vkWaitForFences(vk_device_, 1, &other.fence, VK_TRUE, UINT64_MAX);", reset)
        self.assertLess(reset.index("vkWaitForFences"), reset.index("arena_entries_.reset();"))
        self.assertLess(reset.index("arena_entries_.reset();"), reset.index("arena_metadata_memory_.release();"))
        self.assertLess(reset.index("arena_metadata_memory_.release();"), reset.index("arena_entries_.emplace(&arena_metadata_memory_);"))
        self.assertLess(reset.index("arena_entries_.emplace(&arena_metadata_memory_);"), reset.index("arena_used_ = 0;"))
        self.assertIn("const bool profile_reset = REXCVAR_GET(carbon_gpu_stats);", reset)
        self.assertEqual(reset.count("profile_reset ? NowSeconds() : 0"), 3)
        self.assertLess(reset.index("if (profile_reset)"), reset.index("const double reset_end = NowSeconds();"))
        self.assertIn("wait {:.3f}, log {:.3f}, metadata clear {:.3f}, total {:.3f}", reset)
