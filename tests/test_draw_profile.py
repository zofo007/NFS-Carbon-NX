import importlib.util
import contextlib
import io
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("draw_profile_analysis", ROOT / "scripts/analyze_draw_profile.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def report(time="15:10:00.000", samples=2, requests=128, stages=None):
    values = stages or ", ".join(f"{name} {i + 0.25}" for i, name in enumerate(analysis.STAGES))
    return f"[2026-10-14 {time}] [info] [gpu] [t123] [carbon-gpu] cpu draw sampled 1/64: {samples}/{requests} requests | estimated ms/frame {values}"


class DrawProfileTests(unittest.TestCase):
    def test_cli_summary_and_legacy_log_rejection(self):
        output = io.StringIO()
        with patch("sys.argv", ["analyze_draw_profile.py", "synthetic.log"]), patch.object(Path, "read_text", return_value=report()), contextlib.redirect_stdout(output):
            analysis.main()
        self.assertIn("Reports: 1; excluded long-gap reports: 0", output.getvalue())
        self.assertIn("record: 7.25", output.getvalue())
        with patch("sys.argv", ["analyze_draw_profile.py", "legacy.log"]), patch.object(Path, "read_text", return_value="old build"), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            analysis.main()
        self.assertEqual(error.exception.code, 2)

    def test_parse_and_long_gap_classification(self):
        rows = analysis.parse_profiles("unrelated line\n" + "\n".join((report(), report("15:10:05.000"), report("15:32:00.000"), report("15:32:05.000"))))
        self.assertEqual([row["long_gap"] for row in rows], [False, False, True, False])
        self.assertEqual(rows[0]["samples"], 2)
        self.assertEqual(rows[0]["stages"]["textures"], 3.25)
        self.assertEqual(analysis.parse_profiles("old build without these fields"), [])

    def test_reject_malformed_contract(self):
        for text in (report(samples=0), report(samples=129), report(requests=0),
                     report().replace("record 7.25", "record nan"),
                     report().replace("record 7.25", "record -1"),
                     report().replace("record 7.25", "record inf"),
                     report().replace("record 7.25", "textures 7.25"),
                     report() + "\n" + report("15:09:00.000")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                analysis.parse_profiles(text)

    @unittest.skipUnless((ROOT / "carbon-source/renderer/src/renderer.cpp").is_file(), "Carbon source checkout unavailable")
    def test_renderer_checkpoints_and_report_binding(self):
        source = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        draw = source[source.index("void Renderer::Draw("):source.index("// Presentation", source.index("void Renderer::Draw("))]
        for stage in ("kPipeline", "kPrepare", "kTextures", "kIndices", "kVertices", "kConstants", "kRecord"):
            self.assertIn(f"draw_stages.Next(DrawStage::{stage});", draw)
        self.assertEqual(draw.count("draw_stages.Next(DrawStage::kIndices);"), 2)
        self.assertIn("#if defined(__SWITCH__)\n  const bool profile_draw = REXCVAR_GET(carbon_gpu_stats);", draw)
        self.assertIn("constexpr bool profile_draw = false;", draw)
        self.assertIn("frame_total += elapsed;", draw)
        self.assertIn("double(stages.requests) / double(stages.samples)", source)
        self.assertIn("stats_ = Stats();", source)
        for name in analysis.STAGES:
            self.assertIn(f"{name} {{:.2f}}", source)

    @unittest.skipUnless((ROOT / "carbon-source/renderer/src/renderer.cpp").is_file(), "Carbon source checkout unavailable")
    def test_ready_pipeline_fast_path_and_lifetime(self):
        source = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        lookup = source[source.index("VkPipeline Renderer::GetPipeline("):source.index("void Renderer::PipelineWorker()")]
        self.assertLess(lookup.index("last_ready_pipeline_.Find(hash)"), lookup.index("lock(pipeline_mutex_)"))
        self.assertIn("last_ready_pipeline_.Store(hash, it->second);", lookup)
        self.assertIn("last_ready_pipeline_.Store(hash, pipeline);", lookup)
        self.assertIn("void Renderer::Shutdown() {\n  recorder_.Stop();\n  last_ready_pipeline_ = {};", source)
        self.assertIn("void Renderer::OnCommandThreadStart() {\n  last_ready_pipeline_ = {};", source)
        worker = source[source.index("void Renderer::PipelineWorker()"):source.index("void Renderer::SavePipelineCacheIfDue(")]
        self.assertNotIn("last_ready_pipeline_", worker)
        self.assertIn("pipeline fast cache: {} hits, {} locked lookups", source)

    @unittest.skipUnless((ROOT / "carbon-source/renderer/src/renderer.cpp").is_file(), "Carbon source checkout unavailable")
    def test_descriptor_batch_binding_and_pool_lifetime(self):
        source = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        draw = source[source.index("void Renderer::Draw("):source.index("// Presentation", source.index("void Renderer::Draw("))]
        self.assertLess(draw.index("if (cmd_.layout != layout)"), draw.index("const bool vs_changed"))
        self.assertIn("if (ps_changed && !vs_set && !key.vs_textures)", draw)
        self.assertIn("AllocateDescriptorSet(GetTextureSetLayout(0))", draw)
        self.assertIn("DescriptorBindCount(vs_set, vs_changed, ps_changed)", draw)
        self.assertIn("layout, 0, bind_count, sets,\n                               constant_blocks_ ? 3 + 2 * kConstantBlocks : 3, dyn_offsets",
                      draw)
        self.assertIn("if (ps_changed && bind_count < 3)", draw)
        self.assertIn("if (bind_count > 1) cmd_.set1 = vs_set;", draw)
        self.assertIn("if (bind_count > 2) cmd_.set2 = ps_set;", draw)
        # Texture sets: per-frame unless carbon_gpu_texture_sets_across_frames; then dropped (pool retired
        # through this frame's deferred work) whenever anything was scheduled for destruction.
        self.assertIn("  if (!texture_sets_across_frames_) {\n    texture_set_cache_.clear();\n"
                      "  } else if (texture_sets_stale_) {\n    RetireTextureSets();\n  }\n"
                      "  empty_texture_set_ = VK_NULL_HANDLE;", source)
        header = (ROOT / "carbon-source/renderer/src/renderer.h").read_text(encoding="utf-8")
        self.assertIn("    texture_sets_stale_ = true;\n    frame().deferred_destroy.push_back(std::move(fn));", header)
        retire = source[source.index("void Renderer::RetireTextureSets()"):source.index("VkDescriptorSet Renderer::AllocateTextureSet(")]
        self.assertIn("texture_set_cache_.clear();", retire)
        self.assertIn("frame().deferred_destroy.push_back([this, pool]()", retire)
        self.assertNotIn("DeferDestroy(", retire)
        allocate = source[source.index("VkDescriptorSet Renderer::AllocateTextureSet("):source.index("VkDescriptorSet Renderer::AllocateDescriptorSet(")]
        self.assertIn("texture_sets_across_frames_ = false;", allocate)  # pool failure: never cache per-frame sets
        self.assertIn("RetireTextureSets();  // full", allocate)
        textures = (ROOT / "carbon-source/renderer/src/textures.cpp").read_text(encoding="utf-8")
        self.assertIn("AllocateTextureSet(GetTextureSetLayout(ts.texture_descriptor_count))", textures)
        self.assertNotIn("AllocateDescriptorSet(GetTextureSetLayout(ts.texture_descriptor_count))", textures)
        # Every image/view destruction still goes through DeferDestroy (which invalidates the sets).
        for name in ("textures.cpp", "render_targets.cpp", "renderer.cpp"):
            text = (ROOT / "carbon-source/renderer/src" / name).read_text(encoding="utf-8")
            for i in [m for m in range(len(text)) if text.startswith("vkDestroyImageView", m)]:
                lead = text.rfind("DeferDestroy(", 0, i)
                assert lead != -1 and i - lead < 400, f"{name}: image view destroyed outside DeferDestroy"
        self.assertIn("descriptor bind batching: {} calls, {} calls avoided", source)
