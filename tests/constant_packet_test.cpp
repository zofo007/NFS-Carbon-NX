#include "constant_batch.h"
#include "constant_blocks.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  std::exit(1); } } while (false)

namespace carbon::gpu {

// Only the notification storage and scalar dispatch recorder are test doubles.
struct Renderer {
  bool float_constants_dirty_[2] = {};
  uint32_t float_constant_dirty_blocks_[2] = {};
  struct {
    uint32_t float_constant_changed = 0, float_constant_identical = 0;
    uint32_t float_constant_batches = 0, float_constant_batch_words = 0;
  } stats_;
#include "notify.inc"
#include "notify-batch.inc"
};

struct CommandProcessor {
#include "reader.inc"
  RegisterFile regs_;
  Renderer* renderer_;
  std::vector<std::pair<uint32_t, uint32_t>> fallback;
  void WriteRegister(uint32_t index, uint32_t value) {
    fallback.emplace_back(index, value);
    if (index >= kRegFloatConstants && index < kRegFetchConstants) {
      renderer_->OnFloatConstantWritten(index - kRegFloatConstants,
                                        regs_.WriteFloatConstant(index, value));
    } else if (index < kRegisterCount) {
      regs_.values[index] = value;
    }
  }
  void WriteRegistersFromReader(Reader&, uint32_t, uint32_t);
  void WriteRegistersFromGuest(uint32_t, const uint32_t*, uint32_t);
};
#include "reader-route.inc"
#include "guest-route.inc"

}  // namespace carbon::gpu

void TestConstantPackets() {
  using namespace carbon::gpu;
  std::array<uint32_t, 16> words{};
  for (uint32_t i = 0; i < words.size(); ++i) words[i] = ByteSwap32(i & 1 ? 0u : 0x7FC00000u + i);
  unsigned cases = 0;
  for (bool ring : {false, true}) {
    for (uint32_t start = 0; start < words.size(); ++start) {
      for (uint32_t n = 0; n < words.size(); ++n) {
        if (!ring && start + n > words.size()) continue;
        for (uint32_t index : {kRegFloatConstants - 1, kRegFloatConstants,
                               kRegFloatConstants + 1023, kRegFetchConstants - 1}) {
          for (uint32_t dirty = 0; dirty < 4; ++dirty) {
            Renderer expected_renderer, actual_renderer;
            for (unsigned stage = 0; stage < 2; ++stage) {
              expected_renderer.float_constants_dirty_[stage] = (dirty >> stage) & 1;
              actual_renderer.float_constants_dirty_[stage] = (dirty >> stage) & 1;
            }
            CommandProcessor expected{{}, &expected_renderer, {}};
            CommandProcessor actual{{}, &actual_renderer, {}};
            CommandProcessor::Reader ref{words.data(), 16, start, (start + n) & 15, ring};
            auto reader = ref;
            for (uint32_t i = 0; i < n; ++i) expected.WriteRegister(index + i, ref.Read());
            actual.WriteRegistersFromReader(reader, index, n);
            CHECK(reader.read == ref.read);
            CHECK(std::memcmp(actual.regs_.values, expected.regs_.values, sizeof(actual.regs_.values)) == 0);
            CHECK(actual_renderer.stats_.float_constant_changed == expected_renderer.stats_.float_constant_changed);
            CHECK(actual_renderer.stats_.float_constant_identical == expected_renderer.stats_.float_constant_identical);
            CHECK(actual_renderer.float_constants_dirty_[0] == expected_renderer.float_constants_dirty_[0]);
            CHECK(actual_renderer.float_constants_dirty_[1] == expected_renderer.float_constants_dirty_[1]);
            // Batched and scalar routes mark the same changed 64-constant blocks.
            CHECK(actual_renderer.float_constant_dirty_blocks_[0] == expected_renderer.float_constant_dirty_blocks_[0]);
            CHECK(actual_renderer.float_constant_dirty_blocks_[1] == expected_renderer.float_constant_dirty_blocks_[1]);
            const bool batched = (!ring || start + n <= 16) && index >= kRegFloatConstants &&
                                 index < kRegFetchConstants && n <= kRegFetchConstants - index;
            CHECK(actual_renderer.stats_.float_constant_batches == unsigned(batched));
            CHECK(actual_renderer.stats_.float_constant_batch_words == (batched ? n : 0));
            CHECK(batched ? actual.fallback.empty() : actual.fallback == expected.fallback);
            if (start + n <= words.size()) {
              Renderer guest_renderer;
              CommandProcessor guest{{}, &guest_renderer, {}};
              guest.WriteRegistersFromGuest(index, words.data() + start, n);
              CHECK(std::memcmp(guest.regs_.values, expected.regs_.values, sizeof(guest.regs_.values)) == 0);
              CHECK(guest_renderer.stats_.float_constant_batches == unsigned(index >= kRegFloatConstants &&
                    index < kRegFetchConstants && n <= kRegFetchConstants - index));
            }
            ++cases;
          }
        }
      }
    }
  }
  std::printf("PASS %u actual reader/guest routes: cursor/wrap/fallback, stage notifications and sticky dirty flags\n", cases);
}
