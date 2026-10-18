#include <rex/audio/xma/profile.h>

#include <cassert>
#include <thread>
#include <vector>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace rex::audio;
namespace {
XmaProfileClock clock_value;
unsigned reads = 0;
XmaProfileClock Clock() { ++reads; return clock_value; }
using Scope = SampledXmaProfileScope<Clock>;
void ForceSample(XmaProfileStage stage) {
  for (uint32_t seed = 0;; ++seed) {
    if (((seed * 1664525u + 1013904223u) >> 26) == 0) {
      g_xma_profile_sequence[size_t(stage)] = seed;
      return;
    }
  }
}
}

int main() {
  XmaProfileStats stats;
  stats.Record({10, 4, true}, {30, 12, true});
  stats.Record({100, 20, false}, {105, 25, true});
  stats.Record({200, 20, true}, {204, 25, false});
  stats.Record({300, 20, true}, {303, 19, true});  // Reject backward CPU clocks.
  auto snapshot = stats.Take();
  assert(snapshot.samples == 4 && snapshot.wall_ns == 32 && snapshot.wall_max_ns == 20);
  assert(snapshot.cpu_samples == 1 && snapshot.cpu_ns == 8 && snapshot.cpu_max_ns == 8);
  snapshot = stats.Take();
  assert(!snapshot.samples && !snapshot.wall_ns && !snapshot.cpu_samples);

  {
    Scope disabled(XmaProfileStage::kEnable, false);
    disabled.Next(XmaProfileStage::kWorkBody);
  }
  assert(!reads && !g_xma_profile[0].Take().calls);
  assert(!g_xma_profile[size_t(XmaProfileStage::kWorkBody)].Take().calls);

  ForceSample(XmaProfileStage::kWorkLock);
  clock_value = {100, 10, true};
  {
    Scope scope(XmaProfileStage::kWorkLock, true);
    clock_value = {150, 12, true};
    scope.Next(XmaProfileStage::kWorkBody);
    clock_value = {250, 100, true};
  }
  const auto lock = g_xma_profile[size_t(XmaProfileStage::kWorkLock)].Take();
  const auto body = g_xma_profile[size_t(XmaProfileStage::kWorkBody)].Take();
  assert(reads == 3 && lock.calls == 1 && body.calls == 1);
  assert(lock.samples == 1 && lock.wall_ns == 50 && lock.cpu_ns == 2);
  assert(body.samples == 1 && body.wall_ns == 100 && body.cpu_ns == 88);

  reads = 0;
  for (unsigned i = 0; i < 65536; ++i) {
    Scope sample(XmaProfileStage::kCodec, true);
  }
  snapshot = g_xma_profile[size_t(XmaProfileStage::kCodec)].Take();
  assert(snapshot.calls == 65536 && snapshot.samples > 900 && snapshot.samples < 1150);
  assert(reads == snapshot.samples * 2);  // No clock/syscall on unsampled calls.

  ForceSample(XmaProfileStage::kConvert);
  clock_value = {100, 0, false};
  { Scope unavailable(XmaProfileStage::kConvert, true); }
  snapshot = g_xma_profile[size_t(XmaProfileStage::kConvert)].Take();
  assert(snapshot.samples == 1 && snapshot.cpu_samples == 0);

  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 4; ++i) {
    workers.emplace_back([&stats]() {
      for (unsigned n = 0; n < 10000; ++n) stats.Record({10, 20, true}, {30, 25, true});
    });
  }
  for (auto& worker : workers) worker.join();
  snapshot = stats.Take();
  assert(snapshot.samples == 40000 && snapshot.wall_ns == 800000);
  assert(snapshot.cpu_samples == 40000 && snapshot.cpu_ns == 200000);
  assert(snapshot.wall_max_ns == 20 && snapshot.cpu_max_ns == 5);
}
