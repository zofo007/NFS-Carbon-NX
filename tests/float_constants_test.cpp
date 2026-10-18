#include "gpu_common.h"
#include <cassert>
#include <cstring>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::gpu;

// Compare the old dirty-on-every-write path against deduplication at draw boundaries.
struct ConstantUploads {
  RegisterFile regs;
  bool dirty[2] = {true, true};
  uint32_t chunk[2] = {UINT32_MAX, UINT32_MAX}, count[2] = {};
  uint32_t data[2][1024] = {};
  unsigned uploads = 0;

  void Store(uint32_t offset, uint32_t value, bool deduplicate) {
    const uint32_t index = kRegFloatConstants + offset;
    const bool changed = regs.WriteFloatConstant(index, value);
    dirty[offset >> 10] |= !deduplicate || changed;
  }
  void Draw(uint32_t current_chunk, uint32_t vs_count, uint32_t ps_count) {
    for (uint32_t stage = 0; stage < 2; ++stage) {
      const uint32_t n = stage ? ps_count : vs_count;
      if (dirty[stage] || chunk[stage] != current_chunk || count[stage] < n) {
        std::memcpy(data[stage], &regs.values[kRegFloatConstants + stage * 1024], n * 16);
        dirty[stage] = false;
        chunk[stage] = current_chunk;
        count[stage] = n;
        ++uploads;
      }
      assert(std::memcmp(data[stage], &regs.values[kRegFloatConstants + stage * 1024], n * 16) == 0);
    }
  }
};

int main() {
  RegisterFile regs;
  regs.values[kRegFloatConstants - 1] = 0x12345678;
  regs.values[kRegFetchConstants] = 0x87654321;
  for (uint32_t offset : {0u, 1023u, 1024u, 2047u}) {
    for (uint32_t bits : {0u, 0x80000000u, 0x7fc00001u, 0x7fc00002u,
                          0x7f800000u, 0xff800000u, 0x3f800000u, 0u}) {
      const uint32_t index = kRegFloatConstants + offset;
      const bool expected = regs[index] != bits;
      assert(regs.WriteFloatConstant(index, bits) == expected);
      assert(regs[index] == bits);
      assert(!regs.WriteFloatConstant(index, bits));
    }
  }
  assert(regs[kRegFloatConstants - 1] == 0x12345678);
  assert(regs[kRegFetchConstants] == 0x87654321);

  ConstantUploads original, optimized;
  // Initial zero writes must not clear the initial dirty state.
  original.Store(0, 0, false); optimized.Store(0, 0, true);
  assert(optimized.dirty[0] && optimized.dirty[1]);
  original.Draw(0, 1, 1); optimized.Draw(0, 1, 1);
  assert(optimized.uploads == 2);
  for (unsigned i = 0; i < 128; ++i) {
    original.Store(0, 0, false); optimized.Store(0, 0, true);
    original.Store(1024, 0, false); optimized.Store(1024, 0, true);
    original.Draw(0, 1, 1); optimized.Draw(0, 1, 1);
  }
  assert(original.uploads == 258 && optimized.uploads == 2);
  // Changed words, changes reverted before drawing, larger shader prefixes and chunk reuse.
  uint32_t random = 0x5a17c9e3;
  for (unsigned i = 0; i < 1024; ++i) {
    random = random * 1664525 + 1013904223;
    const uint32_t offset = (random >> 16) & 2047;
    const uint32_t before = optimized.regs[kRegFloatConstants + offset];
    const uint32_t value = i % 3 ? random : before;
    original.Store(offset, value, false); optimized.Store(offset, value, true);
    if (i % 11 == 0) {
      original.Store(offset, before, false); optimized.Store(offset, before, true);
    }
    if (i % 19 == 0) {
      original.chunk[0] = original.chunk[1] = UINT32_MAX;
      optimized.chunk[0] = optimized.chunk[1] = UINT32_MAX;
    }
    const uint32_t counts[] = {0, 1, 17, 256};
    const uint32_t vs = counts[i % 4], ps = counts[(i / 4) % 4];
    original.Draw((i / 7) % 3, vs, ps); optimized.Draw((i / 7) % 3, vs, ps);
    assert(std::memcmp(original.data[0], optimized.data[0], vs * 16) == 0);
    assert(std::memcmp(original.data[1], optimized.data[1], ps * 16) == 0);
  }
}
