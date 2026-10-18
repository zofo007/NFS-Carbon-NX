#include "texture_binding_cache.h"

#include <cassert>
#include <cstring>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::gpu;

namespace {
void FillParams(DrawConstants& constants, uint32_t index, uint32_t value) {
  for (uint32_t i = 0; i < 4; ++i) constants.tex_size[index * 4 + i] = float(value + i);
  for (uint32_t i = 0; i < 3; ++i) constants.tex_info[index * 4 + i] = value + i;
  for (uint32_t i = 0; i < 2; ++i) constants.tex_uv[index * 4 + i] = float(value + 10 + i);
}

void CopyParams(DrawConstants& dest, const DrawConstants& source, uint32_t index) {
  std::memcpy(&dest.tex_size[index * 4], &source.tex_size[index * 4], 16);
  std::memcpy(&dest.tex_info[index * 4], &source.tex_info[index * 4], 12);
  std::memcpy(&dest.tex_uv[index * 4], &source.tex_uv[index * 4], 8);
}
}

int main() {
  RegisterFile regs;
  TranslatedShader vs, ps;
  vs.textures.resize(3);
  vs.textures[0].fetch_index = 0;
  vs.textures[1].fetch_index = 31;
  vs.textures[2].fetch_index = 0;  // Multiple instruction bindings of one fetch.
  vs.texture_descriptor_count = 4;
  ps = vs;
  ps.textures[0].mag_filter = xenos::TextureFilter::kLinear;
  const auto first = reinterpret_cast<VkDescriptorSet>(uintptr_t(1));
  const auto second = reinterpret_cast<VkDescriptorSet>(uintptr_t(2));
  DrawConstants source, output, expected;
  std::memset(&source, 0xA5, sizeof(source));
  FillParams(source, 0, 10);
  FillParams(source, 31, 20);
  // Bitwise copies preserve signed zero/NaN payloads rather than recomputing FP.
  const uint32_t signed_zero = 0x80000000, nan_payload = 0x7FC01234;
  std::memcpy(&source.tex_uv[0], &signed_zero, 4);
  std::memcpy(&source.tex_size[31 * 4], &nan_payload, 4);
  TextureBindingCache cache;
  assert(!cache.Find(regs, vs, output));
  cache.Store(regs, vs, source, first);
  std::memset(&output, 0x5A, sizeof(output));
  expected = output;
  CopyParams(expected, source, 0);
  CopyParams(expected, source, 31);
  assert(cache.Find(regs, vs, output) == first);
  // Header, vertex fields, unused fetches and unused info/UV lanes stay untouched.
  assert(!std::memcmp(&output, &expected, sizeof(output)));
  assert(!cache.Find(regs, ps, output));

  for (uint32_t index : {0u, 31u}) {
    for (uint32_t word = 0; word < 6; ++word) {
      uint32_t& reg = regs.values[kRegFetchConstants + index * 6 + word];
      reg ^= 1;
      std::memset(&output, 0x5A, sizeof(output));
      expected = output;
      assert(!cache.Find(regs, vs, output));
      assert(!std::memcmp(&output, &expected, sizeof(output)));  // No partial restore.
      reg ^= 1;
      assert(cache.Find(regs, vs, output) == first);
    }
  }
  regs.values[kRegFetchConstants + 15 * 6] = 123;  // Unused register is irrelevant.
  assert(cache.Find(regs, vs, output) == first);
  cache.Reset();  // Frame/pool reset or resolve invalidates unchanged fetches.
  assert(!cache.Find(regs, vs, output));
  cache.Store(regs, vs, source, VK_NULL_HANDLE);
  assert(!cache.Find(regs, vs, output));

  TextureBindingCache stages[2];
  unsigned full_binds = 0, hits = 0;
  // Differential sequence: original parameter generation versus the real cache,
  // including shader changes, both stages, overlapping fetches and resolve epochs.
  for (uint32_t frame = 0; frame < 8; ++frame) {
    for (auto& stage : stages) stage.Reset();
    uint32_t resource_epoch = 0;
    for (uint32_t draw = 0; draw < 128; ++draw) {
      if (draw % 17 == 0) {
        ++resource_epoch;
        for (auto& stage : stages) stage.Reset();
      }
      if (draw % 11 == 0) regs.values[kRegFetchConstants + 31 * 6 + draw % 6] ^= 7;
      std::memset(&expected, 0x5A, sizeof(expected));
      output = expected;
      for (uint32_t stage = 0; stage < 2; ++stage) {
        const TranslatedShader& shader = (stage ^ (draw / 23 % 2)) ? ps : vs;
        DrawConstants fresh = expected;
        for (const TextureBinding& binding : shader.textures) {
          FillParams(fresh, binding.fetch_index,
                     resource_epoch * 1000 + stage * 100 + frame * 10 +
                     regs.values[kRegFetchConstants + binding.fetch_index * 6 + 5]);
          CopyParams(expected, fresh, binding.fetch_index);
        }
        if (stages[stage].Find(regs, shader, output)) {
          ++hits;
        } else {
          ++full_binds;
          for (const TextureBinding& binding : shader.textures) {
            CopyParams(output, fresh, binding.fetch_index);
          }
          stages[stage].Store(regs, shader, output, stage ? second : first);
        }
      }
      assert(!std::memcmp(&output, &expected, sizeof(output)));
    }
  }
  assert(hits > full_binds && hits + full_binds == 8 * 128 * 2);
}
