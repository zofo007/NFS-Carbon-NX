from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "carbon-source/renderer/src"


@unittest.skipUnless((ROOT / "renderer.cpp").is_file(), "Carbon checkout unavailable")
class CacheFileTests(unittest.TestCase):
    def test_both_cache_writers_check_close_and_commit_before_success(self):
        source = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        for name, path, end in (("SavePipelineCache", "pipeline_cache_path_", "void Renderer::Shutdown"),
                                ("SaveWarmList", "warm_path_", "void Renderer::RunPipelineWarmup")):
            body = source[source.index(f"bool Renderer::{name}()"):source.index(end)]
            self.assertLess(body.index("out.close();"), body.index("if (!out)"))
            self.assertLess(body.index("out.close();"), body.index(f"CommitCacheFile(temp, {path}, ec);"))
            self.assertIn("if (ec)", body)
            self.assertIn("return false;", body)
            self.assertLess(body.index("if (ec)"), body.index('REXGPU_INFO("[carbon-gpu] saved'))
        warm = source[source.index("bool Renderer::SaveWarmList()"):source.index("void Renderer::RunPipelineWarmup")]
        self.assertEqual(warm.count("warm_dirty_ = true;"), 2)
        retry = source[source.index("void Renderer::SavePipelineCacheIfDue"):source.index("VkPipeline Renderer::BuildPipeline")]
        self.assertIn("if (!cache_saved || !list_saved)", retry)
        self.assertIn("pipeline_cache_dirty_ = true;", retry)
