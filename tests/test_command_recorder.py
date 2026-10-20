from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "carbon-source/renderer/src"
HAVE_SOURCE = (SRC / "renderer.cpp").is_file()


def read(name):
    return (SRC / name).read_text(encoding="utf-8")


def function(source, signature, end_marker="\n}\n"):
    start = source.index(signature)
    return source[start:source.index(end_marker, start) + len(end_marker)]


@unittest.skipUnless(HAVE_SOURCE, "Carbon source checkout unavailable")
class CommandRecorderBindingTests(unittest.TestCase):
    """The frame command buffer is only recorded through the recorder (same order either mode)."""

    def test_no_direct_frame_buffer_recording(self):
        direct = re.compile(r"(?:dfn|functions\(\))\.vkCmd\w+\(|pfn_copy_image_\(|pfn_write_timestamp_\(")
        allowed = {
            # TransitionImage's direct branch: upload_cb and init-time buffers only.
            ("renderer.cpp", "dfn.vkCmdPipelineBarrier("),
            # One-time null texture clear before the command thread exists.
            ("textures.cpp", "dfn.vkCmdClearColorImage("),
            # Texture uploads go to upload_cb, recorded on the CP from its own pool.
            ("textures.cpp", "dfn.vkCmdCopyBufferToImage("),
        }
        found = set()
        for name in ("renderer.cpp", "render_targets.cpp", "textures.cpp"):
            for match in direct.finditer(read(name)):
                found.add((name, match.group(0)))
        self.assertEqual(found, allowed)
        textures = read("textures.cpp")
        upload = function(textures, "bool Renderer::UploadTexture(")
        self.assertIn("VkCommandBuffer cb = frame().upload_cb;", upload)
        null_clear = function(textures, "void Renderer::CreateNullTextures(")
        self.assertIn("Frame& f = frames_[0];", null_clear)

    def test_transition_routing_and_lifetimes(self):
        source = read("renderer.cpp")
        transition = function(source, "void Renderer::TransitionImage(")
        self.assertIn("if (cb && cb == recorder_.target()) {", transition)
        self.assertLess(transition.index("recorder_.PipelineBarrier("), transition.index("dfn.vkCmdPipelineBarrier("))
        begin = function(source, "bool Renderer::BeginFrame()")
        order = [begin.index(s) for s in ("dfn.vkResetCommandPool(vk_device_, f.pool, 0);",
                                          "dfn.vkResetCommandPool(vk_device_, f.upload_pool, 0);",
                                          "dfn.vkBeginCommandBuffer(f.cb, &bi);",
                                          "recorder_.BeginFrame(f.cb);",
                                          "recorder_.ResetQueryPool(")]
        self.assertEqual(order, sorted(order))
        end = function(source, "void Renderer::EndFrame(")
        order = [end.index(s) for s in ("present_fn(f.cb);", "vmaFlushAllocation(", "GpuStamp(kTagEnd);",
                                        "stats_.record_drain_s += recorder_.EndFrame();",
                                        "dfn.vkEndCommandBuffer(f.cb);", "vkQueueSubmit(")]
        self.assertEqual(order, sorted(order))
        self.assertIn("dfn.vkCreateCommandPool(vk_device_, &pci, nullptr, &f.upload_pool)", source)
        self.assertIn("ai.commandPool = f.upload_pool;\n    if (dfn.vkAllocateCommandBuffers(vk_device_, &ai, &f.upload_cb)", source)
        stop = function(source, "void Renderer::OnCommandThreadStop()")
        self.assertLess(stop.index("EndFrame(false, nullptr);"), stop.index("recorder_.Stop();"))
        self.assertIn("void Renderer::Shutdown() {\n  recorder_.Stop();", source)
        self.assertIn("assert(cb == recorder_.target());", function(source, "void Renderer::RecordPresent("))

    def test_cvar_default_and_report(self):
        source = read("renderer.cpp")
        self.assertIn("#if defined(__SWITCH__)\n#define CARBON_RECORD_THREAD_DEFAULT true\n#else\n"
                      "#define CARBON_RECORD_THREAD_DEFAULT false", source)
        self.assertIn("REXCVAR_DEFINE_BOOL(carbon_gpu_record_thread, CARBON_RECORD_THREAD_DEFAULT,", source)
        start = function(source, "void Renderer::OnCommandThreadStart()")
        self.assertIn("recorder_.Start(d, threaded,", start)
        # Default libnx priority (0x3B): the CP's 0x2C starved guest audio on hardware.
        self.assertNotIn("svcSetThreadPriority", source)
        for name in ("PipelineBarrier", "BeginRendering", "EndRendering", "ResetQueryPool", "BindPipeline",
                     "BindDescriptorSets", "SetViewport", "SetScissor", "SetDepthBias", "SetBlendConstants",
                     "SetStencilReference", "SetStencilCompareMask", "SetStencilWriteMask", "BindIndexBuffer",
                     "Draw", "DrawIndexed", "PushConstants", "ClearColorImage", "ClearAttachments",
                     "CopyImageToBuffer"):
            self.assertIn(f"d.{name} = dfn.vkCmd{name};", start)
        self.assertIn("d.CopyImage = pfn_copy_image_;", start)
        self.assertIn("d.WriteTimestamp = pfn_write_timestamp_;", start)
        self.assertIn("record thread {}: {:.0f} records/frame, worker busy {:.2f} ms/frame", source)
        header = read("command_recorder.h")
        # Every dispatch entry the recorder declares is filled in by the renderer.
        declared = set(re.findall(r"PFN_vkCmd\w+ (\w+) = nullptr;", header))
        assigned = set(re.findall(r"  d\.(\w+) = ", start))
        self.assertEqual(declared, assigned)


if __name__ == "__main__":
    unittest.main()
