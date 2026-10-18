from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless((ROOT / "carbon-source/renderer/src/renderer.cpp").is_file(), "Carbon checkout unavailable")
class UploadMemoryTests(unittest.TestCase):
    def test_pacing_accounting_reuses_existing_timer_without_changing_wait(self):
        header = (ROOT / "carbon-source/renderer/src/renderer.h").read_text(encoding="utf-8")
        note = header[header.index("void NoteCpWait("):header.index("void OnPrimaryBufferEnd()")]
        self.assertIn("stats_.cp_wait_s += seconds;", note)
        self.assertIn("++stats_.cp_wait_polls;", note)
        self.assertIn("interval / 0x100", note)
        self.assertNotIn("now()", note)
        cp = (ROOT / "carbon-source/renderer/src/command_processor.cpp").read_text(encoding="utf-8")
        self.assertIn(" - wait_start).count(), wait);", cp)
        self.assertIn("std::chrono::milliseconds(std::max<uint32_t>(1, wait / 0x100))", cp)
        self.assertIn("if (CompareWaitValue(wait_info, value, ref, mask))", cp)

    def test_cached_switch_policy_and_chunk_high_water(self):
        text = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        acquire = text[text.index("bool Renderer::AcquireChunk()"):text.index("void Renderer::ReserveUpload(")]
        self.assertIn("#if defined(__SWITCH__)", acquire)
        switch, host = acquire.split("#else", 1)
        self.assertIn("aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT", switch)
        self.assertIn("aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT", host)
        self.assertIn("upload_chunks_[index].used = 0;", acquire)
        self.assertIn("vmaGetAllocationMemoryProperties", acquire)
        upload = text[text.index("Renderer::UploadAllocation Renderer::Upload("):text.index("VkDescriptorSet Renderer::AllocateDescriptorSet(")]
        self.assertIn("chunk_offset_ = offset + size;\n  c.used = chunk_offset_;", upload)

    def test_flush_all_frame_chunks_after_writes_before_submit(self):
        text = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        end = text[text.index("void Renderer::EndFrame("):text.index("bool Renderer::AcquireChunk()")]
        flush = end.index("vmaFlushAllocation(allocator_, chunk.allocation, 0, chunk.used)")
        self.assertLess(end.index("present_fn(f.cb);"), flush)
        self.assertLess(flush, end.index("vkQueueSubmit"))
        self.assertIn("for (uint32_t index : f.chunks)", end)
        self.assertIn('rex::FatalError("Carbon upload memory flush failed")', end)
        self.assertIn("upload flush: {:.3f} ms/frame, {:.2f} MiB/frame", text)
        begin = text[text.index("bool Renderer::BeginFrame()"):text.index("void Renderer::EndFrame(")]
        self.assertLess(begin.index("vkWaitForFences"), begin.index("free_chunks_.push_back(c)"))
