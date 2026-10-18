#include "draw_profile.h"
#include "descriptor_bind_batch.h"
#include "ready_pipeline_cache.h"

#include <cassert>
#include <cmath>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

namespace {
double now = 0;
unsigned reads = 0;
double Clock() { ++reads; return now; }
using namespace carbon::gpu;
using Timer = DrawStageTimer<Clock>;
bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

int main() {
  const auto vs_set = reinterpret_cast<VkDescriptorSet>(uintptr_t(1));
  const auto ps_set = reinterpret_cast<VkDescriptorSet>(uintptr_t(2));
  for (bool has_vs : {false, true}) {
    for (bool has_ps : {false, true}) {
      for (bool vs_changed : {false, true}) {
        for (bool ps_changed : {false, true}) {
          if ((vs_changed && !has_vs) || (ps_changed && !has_ps)) continue;
          const VkDescriptorSet sets[3] = {ps_set, has_vs ? vs_set : VK_NULL_HANDLE,
                                          has_ps ? ps_set : VK_NULL_HANDLE};
          const uint32_t count = DescriptorBindCount(sets[1], vs_changed, ps_changed);
          assert(count >= 1 && count <= 3);
          for (uint32_t i = 0; i < count; ++i) assert(sets[i]);
          assert(!vs_changed || count >= 2);
          const uint32_t calls = 1 + uint32_t(ps_changed && count < 3);
          assert(calls <= 1 + uint32_t(vs_changed) + uint32_t(ps_changed));
          assert(!ps_changed || !has_vs || count == 3);
        }
      }
    }
  }
  assert(DescriptorBindCount(vs_set, false, true) == 3);  // Empty VS bridge too.
  assert(DescriptorBindCount(VK_NULL_HANDLE, false, true) == 1);  // Failed bridge.

  ReadyPipelineCache cache;
  const auto first = reinterpret_cast<VkPipeline>(uintptr_t(1));
  const auto second = reinterpret_cast<VkPipeline>(uintptr_t(2));
  assert(!cache.Find(0) && !cache.Find(1));
  cache.Store(0, first);
  assert(cache.Find(0) == first && !cache.Find(1));
  cache.Store(1, VK_NULL_HANDLE);
  assert(cache.Find(0) == first && !cache.Find(1));
  cache.Store(1, second);
  assert(cache.Find(1) == second && !cache.Find(0));
  cache = {};
  assert(!cache.Find(0) && !cache.Find(1));

  DrawStageStats disabled;
  { Timer timer(disabled, false, 0); timer.Next(DrawStage::kTextures); }
  assert(reads == 0 && disabled.requests == 0 && disabled.samples == 0);

  DrawStageStats stages;
  now = 10;
  {
    Timer timer(stages, true, 0);
    now = 11; timer.Next(DrawStage::kIndices);
    now = 12; timer.Next(DrawStage::kVertices);
    now = 13; timer.Next(DrawStage::kIndices);
    now = 14; timer.Next(DrawStage::kRecord);
    now = 15;
  }
  assert(reads == 6 && stages.requests == 1 && stages.samples == 1);
  assert(Near(stages.seconds[size_t(DrawStage::kSetupTargets)], 1));
  assert(Near(stages.seconds[size_t(DrawStage::kIndices)], 2));
  assert(Near(stages.seconds[size_t(DrawStage::kVertices)], 1));
  assert(Near(stages.seconds[size_t(DrawStage::kRecord)], 1));

  DrawStageStats early;
  now = 0;
  auto early_return = [&] { Timer timer(early, true, 0); now = 0.005; return; };
  early_return();
  assert(Near(early.seconds[size_t(DrawStage::kSetupTargets)], 0.005));
  assert(early.samples == 1);

  DrawStageStats phase;
  reads = 0;
  for (uint64_t frame = 0; frame < 4; ++frame) {
    for (uint64_t draw = 0; draw < Timer::kStride; ++draw) {
      const auto before = reads;
      { Timer timer(phase, true, frame); }
      assert((reads != before) == (draw == ((Timer::kStride - frame) & (Timer::kStride - 1))));
    }
  }
  assert(phase.requests == 256 && phase.samples == 4 && reads == 8);
  phase = DrawStageStats{};
  assert(phase.requests == 0 && phase.samples == 0 && phase.seconds == decltype(phase.seconds){});
}
