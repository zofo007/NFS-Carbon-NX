#include "command_recorder.h"

#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
using carbon::gpu::CommandRecorder;
using carbon::gpu::RecorderDispatch;

// Every fake vkCmd* folds its command buffer, opcode and argument VALUES (never pointer
// addresses or struct padding) into one running hash. Immediate calls run on the test
// thread, threaded ones on the worker; the hash is read only after a drain/stop.
uint64_t g_hash = 1469598103934665603ull, g_calls = 0;
bool g_slow = false;

void Mix(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) g_hash = (g_hash ^ bytes[i]) * 1099511628211ull;
}
template <typename... T>
void Log(uint32_t op, VkCommandBuffer cb, const T&... values) {
  ++g_calls;
  if (g_slow && g_calls % 4096 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
  Mix(&op, sizeof(op));
  Mix(&cb, sizeof(cb));
  (Mix(&values, sizeof(values)), ...);
}
void LogBarrier(const VkImageMemoryBarrier& b) {
  Log(100, nullptr, b.sType, b.srcAccessMask, b.dstAccessMask, b.oldLayout, b.newLayout,
      b.srcQueueFamilyIndex, b.dstQueueFamilyIndex, b.image, b.subresourceRange);
  assert(!b.pNext);
}
void LogAttachment(const VkRenderingAttachmentInfo& a) {
  Log(101, nullptr, a.sType, a.imageView, a.imageLayout, a.resolveMode, a.resolveImageView,
      a.resolveImageLayout, a.loadOp, a.storeOp, a.clearValue);
  assert(!a.pNext);
}

VKAPI_ATTR void VKAPI_CALL PipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags src,
                                           VkPipelineStageFlags dst, VkDependencyFlags dep,
                                           uint32_t mc, const VkMemoryBarrier*, uint32_t bc,
                                           const VkBufferMemoryBarrier*, uint32_t ic,
                                           const VkImageMemoryBarrier* images) {
  Log(1, cb, src, dst, dep, mc, bc, ic);
  for (uint32_t i = 0; i < ic; ++i) LogBarrier(images[i]);
}
VKAPI_ATTR void VKAPI_CALL BeginRendering(VkCommandBuffer cb, const VkRenderingInfo* info) {
  assert(!info->pNext);
  Log(2, cb, info->sType, info->flags, info->renderArea, info->layerCount, info->viewMask,
      info->colorAttachmentCount, info->pDepthAttachment != nullptr,
      info->pStencilAttachment != nullptr);
  for (uint32_t i = 0; i < info->colorAttachmentCount; ++i) LogAttachment(info->pColorAttachments[i]);
  if (info->pDepthAttachment) LogAttachment(*info->pDepthAttachment);
  if (info->pStencilAttachment) LogAttachment(*info->pStencilAttachment);
}
VKAPI_ATTR void VKAPI_CALL EndRendering(VkCommandBuffer cb) { Log(3, cb); }
VKAPI_ATTR void VKAPI_CALL WriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits stage,
                                          VkQueryPool pool, uint32_t query) {
  Log(4, cb, stage, pool, query);
}
VKAPI_ATTR void VKAPI_CALL ResetQueryPool(VkCommandBuffer cb, VkQueryPool pool, uint32_t first,
                                          uint32_t count) {
  Log(5, cb, pool, first, count);
}
VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer cb, VkPipelineBindPoint point,
                                        VkPipeline pipeline) {
  Log(6, cb, point, pipeline);
}
VKAPI_ATTR void VKAPI_CALL BindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint point,
                                              VkPipelineLayout layout, uint32_t first,
                                              uint32_t count, const VkDescriptorSet* sets,
                                              uint32_t dynamic_count, const uint32_t* dynamic) {
  Log(7, cb, point, layout, first, count, dynamic_count);
  for (uint32_t i = 0; i < count; ++i) Mix(&sets[i], sizeof(sets[i]));
  for (uint32_t i = 0; i < dynamic_count; ++i) Mix(&dynamic[i], sizeof(dynamic[i]));
  assert(dynamic_count || !dynamic);
}
VKAPI_ATTR void VKAPI_CALL SetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count,
                                       const VkViewport* v) {
  assert(count == 1);
  Log(8, cb, first, count, *v);
}
VKAPI_ATTR void VKAPI_CALL SetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count,
                                      const VkRect2D* r) {
  assert(count == 1);
  Log(9, cb, first, count, *r);
}
VKAPI_ATTR void VKAPI_CALL SetDepthBias(VkCommandBuffer cb, float c, float clamp, float slope) {
  Log(10, cb, c, clamp, slope);
}
VKAPI_ATTR void VKAPI_CALL SetBlendConstants(VkCommandBuffer cb, const float constants[4]) {
  Log(11, cb, constants[0], constants[1], constants[2], constants[3]);
}
VKAPI_ATTR void VKAPI_CALL StencilReference(VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t v) {
  Log(12, cb, f, v);
}
VKAPI_ATTR void VKAPI_CALL StencilCompare(VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t v) {
  Log(13, cb, f, v);
}
VKAPI_ATTR void VKAPI_CALL StencilWrite(VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t v) {
  Log(14, cb, f, v);
}
VKAPI_ATTR void VKAPI_CALL BindIndexBuffer(VkCommandBuffer cb, VkBuffer b, VkDeviceSize o,
                                           VkIndexType t) {
  Log(15, cb, b, o, t);
}
VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer cb, uint32_t v, uint32_t i, uint32_t fv,
                                uint32_t fi) {
  Log(16, cb, v, i, fv, fi);
}
VKAPI_ATTR void VKAPI_CALL DrawIndexed(VkCommandBuffer cb, uint32_t n, uint32_t i, uint32_t fi,
                                       int32_t vo, uint32_t first_instance) {
  Log(17, cb, n, i, fi, vo, first_instance);
}
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                                         VkShaderStageFlags stages, uint32_t offset, uint32_t size,
                                         const void* data) {
  Log(18, cb, layout, stages, offset, size);
  Mix(data, size);
}
VKAPI_ATTR void VKAPI_CALL ClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout,
                                           const VkClearColorValue* color, uint32_t n,
                                           const VkImageSubresourceRange* range) {
  assert(n == 1);
  Log(19, cb, image, layout, *color, n, *range);
}
VKAPI_ATTR void VKAPI_CALL ClearAttachments(VkCommandBuffer cb, uint32_t n,
                                            const VkClearAttachment* a, uint32_t rn,
                                            const VkClearRect* r) {
  assert(n == 1 && rn == 1);
  Log(20, cb, a->aspectMask, a->colorAttachment, a->clearValue, *r);
}
VKAPI_ATTR void VKAPI_CALL CopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst,
                                     VkImageLayout dl, uint32_t n, const VkImageCopy* region) {
  assert(n == 1);
  Log(21, cb, src, sl, dst, dl, *region);
}
VKAPI_ATTR void VKAPI_CALL CopyImageToBuffer(VkCommandBuffer cb, VkImage src, VkImageLayout layout,
                                             VkBuffer dst, uint32_t n, const VkBufferImageCopy* r) {
  assert(n == 1);
  Log(22, cb, src, layout, dst, n, r->bufferOffset, r->bufferRowLength, r->bufferImageHeight,
      r->imageSubresource, r->imageOffset, r->imageExtent);
}

RecorderDispatch FakeDispatch() {
  RecorderDispatch d;
  d.PipelineBarrier = PipelineBarrier;
  d.BeginRendering = BeginRendering;
  d.EndRendering = EndRendering;
  d.WriteTimestamp = WriteTimestamp;
  d.ResetQueryPool = ResetQueryPool;
  d.BindPipeline = BindPipeline;
  d.BindDescriptorSets = BindDescriptorSets;
  d.SetViewport = SetViewport;
  d.SetScissor = SetScissor;
  d.SetDepthBias = SetDepthBias;
  d.SetBlendConstants = SetBlendConstants;
  d.SetStencilReference = StencilReference;
  d.SetStencilCompareMask = StencilCompare;
  d.SetStencilWriteMask = StencilWrite;
  d.BindIndexBuffer = BindIndexBuffer;
  d.Draw = Draw;
  d.DrawIndexed = DrawIndexed;
  d.PushConstants = PushConstants;
  d.ClearColorImage = ClearColorImage;
  d.ClearAttachments = ClearAttachments;
  d.CopyImage = CopyImage;
  d.CopyImageToBuffer = CopyImageToBuffer;
  return d;
}

struct Random {
  uint64_t state;
  uint64_t Next() {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state ^ (state >> 29);
  }
  uint32_t U32() { return uint32_t(Next() >> 16); }
  uint32_t Below(uint32_t n) { return uint32_t((Next() >> 33) % n); }
  float F() { return float(int32_t(U32())) / 65536.0f; }
  template <typename H>
  H Handle() { return reinterpret_cast<H>(uintptr_t(Next() | 1)); }
};

// Issues one random command; argument structs are stack temporaries that die right
// after the call, so a recorder that kept pointers instead of values would fail.
void Issue(CommandRecorder& rec, Random& r, uint32_t op) {
  switch (op) {
    case 0: {
      VkImageMemoryBarrier b = {};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = r.U32();
      b.dstAccessMask = r.U32();
      b.oldLayout = VkImageLayout(r.Below(8));
      b.newLayout = VkImageLayout(r.Below(8));
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = r.Handle<VkImage>();
      b.subresourceRange = {r.U32(), r.U32(), r.U32(), r.U32(), r.U32()};
      rec.PipelineBarrier(r.U32(), r.U32(), b);
    } break;
    case 1: {
      VkRenderingAttachmentInfo att[6] = {};
      for (auto& a : att) {
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = r.Below(4) ? r.Handle<VkImageView>() : VK_NULL_HANDLE;
        a.imageLayout = VkImageLayout(r.Below(1000));
        a.loadOp = VkAttachmentLoadOp(r.Below(3));
        a.storeOp = VkAttachmentStoreOp(r.Below(2));
        a.clearValue.color.uint32[0] = r.U32();
        a.clearValue.color.uint32[3] = r.U32();
      }
      VkRenderingInfo info = {};
      info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
      info.renderArea = {{int32_t(r.U32()), int32_t(r.U32())}, {r.U32(), r.U32()}};
      info.layerCount = 1 + r.Below(2);
      info.colorAttachmentCount = r.Below(5);
      info.pColorAttachments = info.colorAttachmentCount ? att : nullptr;
      info.pDepthAttachment = r.Below(2) ? &att[4] : nullptr;
      info.pStencilAttachment = r.Below(2) ? &att[r.Below(2) ? 4 : 5] : nullptr;
      rec.BeginRendering(info);
    } break;
    case 2: rec.EndRendering(); break;
    case 3: rec.WriteTimestamp(VkPipelineStageFlagBits(r.U32()), r.Handle<VkQueryPool>(), r.U32()); break;
    case 4: rec.ResetQueryPool(r.Handle<VkQueryPool>(), r.U32(), r.U32()); break;
    case 5: rec.BindPipeline(VkPipelineBindPoint(r.Below(2)), r.Handle<VkPipeline>()); break;
    case 6: {
      VkDescriptorSet sets[3] = {r.Handle<VkDescriptorSet>(), r.Handle<VkDescriptorSet>(),
                                 r.Handle<VkDescriptorSet>()};
      uint32_t dynamic[3] = {r.U32(), r.U32(), r.U32()};
      const uint32_t count = 1 + r.Below(3), dynamic_count = r.Below(4);
      rec.BindDescriptorSets(VkPipelineBindPoint(r.Below(2)), r.Handle<VkPipelineLayout>(),
                             r.Below(3), count, sets, dynamic_count, dynamic_count ? dynamic : nullptr);
    } break;
    case 7: rec.SetViewport({r.F(), r.F(), r.F(), r.F(), r.F(), r.F()}); break;
    case 8: rec.SetScissor({{int32_t(r.U32()), int32_t(r.U32())}, {r.U32(), r.U32()}}); break;
    case 9: rec.SetDepthBias(r.F(), r.F(), r.F()); break;
    case 10: {
      float blend[4] = {r.F(), r.F(), r.F(), r.F()};
      rec.SetBlendConstants(blend);
    } break;
    case 11: rec.SetStencilReference(r.U32(), r.U32()); break;
    case 12: rec.SetStencilCompareMask(r.U32(), r.U32()); break;
    case 13: rec.SetStencilWriteMask(r.U32(), r.U32()); break;
    case 14: rec.BindIndexBuffer(r.Handle<VkBuffer>(), r.Next(), VkIndexType(r.Below(2))); break;
    case 15: rec.Draw(r.U32(), r.U32(), r.U32(), r.U32()); break;
    case 16: rec.DrawIndexed(r.U32(), r.U32(), r.U32(), int32_t(r.U32()), r.U32()); break;
    case 17: {
      uint8_t data[CommandRecorder::kMaxPushConstantBytes];
      for (auto& byte : data) byte = uint8_t(r.U32());
      const uint32_t size = 4 * (1 + r.Below(32));
      rec.PushConstants(r.Handle<VkPipelineLayout>(), r.U32(), 4 * r.Below(8), size, data);
    } break;
    case 18: {
      VkClearColorValue color;
      for (auto& v : color.uint32) v = r.U32();
      rec.ClearColorImage(r.Handle<VkImage>(), VkImageLayout(r.Below(8)), color,
                          {r.U32(), r.U32(), r.U32(), r.U32(), r.U32()});
    } break;
    case 19: {
      VkClearAttachment a = {r.U32(), r.U32(), {}};
      a.clearValue.color.uint32[1] = r.U32();
      VkClearRect rect = {{{int32_t(r.U32()), 0}, {r.U32(), r.U32()}}, r.U32(), r.U32()};
      rec.ClearAttachments(a, rect);
    } break;
    case 20: {
      VkImageCopy region = {{r.U32(), r.U32(), r.U32(), r.U32()}, {1, 2, 3},
                            {r.U32(), r.U32(), r.U32(), r.U32()}, {4, 5, 6}, {r.U32(), r.U32(), 1}};
      rec.CopyImage(r.Handle<VkImage>(), VkImageLayout(r.Below(8)), r.Handle<VkImage>(),
                    VkImageLayout(r.Below(8)), region);
    } break;
    case 21: {
      VkBufferImageCopy region = {r.Next(), r.U32(), r.U32(), {r.U32(), 0, 0, 1}, {0, 0, 0}, {r.U32(), 1, 1}};
      rec.CopyImageToBuffer(r.Handle<VkImage>(), VkImageLayout(r.Below(8)), r.Handle<VkBuffer>(), region);
    } break;
  }
}

constexpr uint32_t kOps = 22;

// One run: frames of random commands with random drains, then stop. Returns (hash, calls).
std::pair<uint64_t, uint64_t> Run(bool threaded, uint64_t seed, uint32_t frames, uint32_t per_frame,
                                  uint32_t drain_every = 2000, CommandRecorder::Stats* stats = nullptr) {
  g_hash = 1469598103934665603ull;
  g_calls = 0;
  CommandRecorder rec;
  bool started = false;
  rec.Start(FakeDispatch(), threaded, [&] { started = true; });
  Random r{seed};
  for (uint32_t f = 0; f < frames; ++f) {
    rec.BeginFrame(reinterpret_cast<VkCommandBuffer>(uintptr_t(0x1000 + f * 16)));
    const uint32_t n = per_frame / 2 + r.Below(per_frame);
    for (uint32_t i = 0; i < n; ++i) {
      // Mostly the draw-loop mix, every op type still frequent.
      const uint32_t pick = r.Below(10);
      Issue(rec, r, pick < 3 ? 6 : pick < 5 ? 16 : pick < 6 ? 14 : r.Below(kOps));
      if (drain_every && r.Below(drain_every) == 0) rec.Drain();
    }
    rec.EndFrame();
    assert(rec.target() == VK_NULL_HANDLE);
  }
  // Records left after the last frame are replayed by Stop.
  rec.BeginFrame(reinterpret_cast<VkCommandBuffer>(uintptr_t(0xF000)));
  for (uint32_t i = 0; i < 100; ++i) Issue(rec, r, r.Below(kOps));
  if (stats) *stats = rec.TakeStats();
  rec.Stop();
  assert(started == threaded);
  return {g_hash, g_calls};
}
}  // namespace

int main() {
  // Small and large frames, many seeds; the large runs wrap the 4 MiB ring repeatedly.
  for (uint64_t seed = 1; seed <= 24; ++seed) {
    const uint32_t frames = seed % 3 ? 6 : 2, per_frame = seed % 3 ? 900 : 40000;
    const auto immediate = Run(false, seed, frames, per_frame);
    const auto threaded = Run(true, seed, frames, per_frame);
    assert(immediate.first == threaded.first && immediate.second == threaded.second);
    assert(immediate.second > 100);
  }
  // The hash is sensitive: different command streams never collide here.
  assert(Run(false, 1, 2, 300) != Run(false, 2, 2, 300));
  // Slow replay forces the producer to wait for ring space (backpressure).
  g_slow = true;
  CommandRecorder::Stats stats;
  const auto immediate = Run(false, 99, 3, 150000, 0);
  const auto threaded = Run(true, 99, 3, 150000, 0, &stats);
  g_slow = false;
  assert(immediate == threaded);
  assert(stats.backpressure_waits > 0);
  // Start/stop reuse, empty frames and drains with nothing queued.
  {
    CommandRecorder rec;
    for (int i = 0; i < 3; ++i) {
      rec.Start(FakeDispatch(), true);
      rec.BeginFrame(reinterpret_cast<VkCommandBuffer>(uintptr_t(0x77)));
      rec.Drain();
      rec.EndFrame();
      rec.Stop();
      rec.Stop();
    }
  }
  std::printf("command recorder: 24 seeds x immediate/threaded identical (%llu calls in last run), "
              "ring wrap, %llu backpressure waits, drains, start/stop\n",
              static_cast<unsigned long long>(threaded.second),
              static_cast<unsigned long long>(stats.backpressure_waits));
  return 0;
}
