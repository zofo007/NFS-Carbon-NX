#include "switch_hotspot_policy.h"

#include <cassert>
#include <limits>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace rex::ui::switch_profile;

int main() {
  assert(SelectMode(false, false) == StackMode::kOff);
  assert(SelectMode(true, false) == StackMode::kFull);
  assert(SelectMode(false, true) == StackMode::kHotspots);
  assert(SelectMode(true, true) == StackMode::kHotspots);
  for (const char* name : {"RWAudioCore Dac", "RWAudioCore Dac (F123)", "GPU Commands", "XMA Decoder (F",
                           "Main XThread (F", "Render XThread", "XThread38DDE40"}) {
    assert(IsHotspotThread(name));
  }
  for (const char* name : {"", "Audio Worker", "Thread38DDE40", "GPU Other", "RWAudioCore", "Profiler"}) {
    assert(!IsHotspotThread(name));
  }
  for (StackMode mode : {StackMode::kOff, StackMode::kFull, StackMode::kHotspots}) {
    assert(!ShouldSample(mode, 0, 0));
    assert(ShouldSample(mode, 123, 123) == (mode != StackMode::kOff));
    assert(ShouldSample(mode, 123, 456) == (mode == StackMode::kFull));
    assert(ShouldSample(mode, 123, 0) == (mode == StackMode::kFull));
  }
  uint32_t sequence = 1;
  assert(SampleDelayNs(StackMode::kOff, sequence) == 100'000'000 && sequence == 1);
  assert(SampleDelayNs(StackMode::kFull, sequence) == 1'000'000 && sequence == 1);
  uint64_t sum = 0, lo = std::numeric_limits<uint64_t>::max(), hi = 0;
  for (unsigned i = 0; i < 65536; ++i) {
    const uint64_t delay = SampleDelayNs(StackMode::kHotspots, sequence);
    assert(delay >= 8'000'000 && delay <= 12'095'000);
    sum += delay;
    if (delay < lo) lo = delay;
    if (delay > hi) hi = delay;
  }
  assert(lo < 8'100'000 && hi > 12'000'000);
  assert(sum / 65536 > 9'900'000 && sum / 65536 < 10'200'000);
}
