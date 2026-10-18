from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "carbon-source/renderer/src"


@unittest.skipUnless((ROOT / "textures.cpp").is_file(), "Carbon checkout unavailable")
class TextureBindingTests(unittest.TestCase):
    def test_resolve_and_pool_reset_invalidate_before_resource_use(self):
        textures = (ROOT / "textures.cpp").read_text(encoding="utf-8")
        resolve = textures[textures.index("Texture* Renderer::GetOrCreateResolveTexture("):textures.index("Texture* Renderer::GetTexture(")]
        reset = "for (auto& cache : texture_binding_cache_) cache.Reset();"
        self.assertLess(resolve.index(reset), resolve.index("textures_.equal_range"))
        renderer = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        begin = renderer[renderer.index("bool Renderer::BeginFrame()"):renderer.index("void Renderer::EndFrame(")]
        self.assertLess(begin.index(reset), begin.index("vkResetDescriptorPool"))
        self.assertLess(begin.index(reset), begin.index("EvictResources();"))
        targets = (ROOT / "render_targets.cpp").read_text(encoding="utf-8")
        self.assertLess(targets.index("dest = GetOrCreateResolveTexture("), targets.index("dest->has_unsampled = true;"))

    def test_fast_path_keeps_first_use_checks_and_failure_retry(self):
        source = (ROOT / "textures.cpp").read_text(encoding="utf-8")
        bind = source[source.index("void Renderer::BindTextures("):source.index("void Renderer::EvictResources()")]
        self.assertLess(bind.index("binding_cache.Find(regs, ts, c)"), bind.index("GuestTextureFromFetch(fetch, g)"))
        self.assertIn("texture_binding_cache_[set_index - 1]", bind)
        self.assertIn("binding_cache.Reset();", bind)
        self.assertIn("if (!t) cacheable = false;", bind)
        self.assertIn("if (!sampler) cacheable = false;", bind)
        self.assertIn("if (!view) cacheable = false;", bind)
        self.assertEqual(bind.count("if (cacheable) binding_cache.Store("), 2)
        get = source[source.index("Texture* Renderer::GetTexture("):source.index("VkImageView Renderer::GetTextureView(")]
        self.assertIn("t->last_used_frame != frame_number_", get)
        self.assertIn("HashExtent(physical_base_, t->extent)", get)
        self.assertIn("t->last_used_frame = frame_number_;", get)
        renderer = (ROOT / "renderer.cpp").read_text(encoding="utf-8")
        self.assertIn("texture binding reuse: {} hits, {} full binds", renderer)

    def test_unused_textures_kept_under_memory_cap(self):
        source = (ROOT / "textures.cpp").read_text(encoding="utf-8")
        evict = source[source.index("void Renderer::EvictResources()"):]
        self.assertIn("constexpr uint64_t kKeepBytes = uint64_t(1024) << 20;", evict)
        self.assertIn("bool stale = texture_bytes_ > kKeepBytes && frame_number_ > t->last_used_frame + kStaleAge;", evict)
        # Over-budget trimming is unchanged.
        self.assertIn("if (!stale && !(trimming && texture_bytes_ > target)) {", evict)
