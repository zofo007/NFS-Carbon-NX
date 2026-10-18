#include "frame_pacing.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {
std::atomic<bool> count_allocations{false};
std::atomic<uint64_t> allocations{0};

using carbon::gpu::FramePacingSample;
using carbon::gpu::FramePacingWindow;

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

void TestVblankCount() {
  const struct { double ms; uint32_t vblanks; } cases[] = {
      {0.0, 1}, {8.0, 1}, {16.7, 1}, {24.9, 1}, {25.1, 2}, {33.3, 2}, {33.7, 2}, {41.6, 2},
      {41.7, 3}, {49.6, 3}, {50.9, 3}, {66.7, 4}, {83.6, 5}, {100.1, 6}, {1808.3, 108}};
  for (const auto& c : cases) assert(FramePacingWindow::VblankCount(c.ms) == c.vblanks);
}

void TestSummaryAndFormat() {
  FramePacingWindow w;
  // 20 frames: nearest-rank P50 is the 10th, P95 the 19th, P99 the 20th sorted value.
  const float intervals[20] = {33.3f, 50.0f, 50.1f, 49.8f, 66.7f, 33.4f, 33.2f, 50.2f, 41.0f, 83.4f,
                               16.7f, 50.0f, 49.9f, 33.6f, 50.3f, 66.6f, 50.0f, 33.3f, 49.7f, 1808.3f};
  double present = 1000.0;
  for (int i = 0; i < 20; ++i) {
    present += intervals[i] / 1000.0;
    w.Add({intervals[i], 30.1f, 0.9f, 10.3f, 1.8f, 2.4f}, present);
  }
  const auto s = w.Summarize();
  assert(s.frames == 20 && s.stored == 20);
  std::vector<float> sorted(intervals, intervals + 20);
  std::sort(sorted.begin(), sorted.end());
  assert(s.p50 == sorted[9] && s.p95 == sorted[18] && s.p99 == sorted[19] && s.worst == sorted[19]);
  // Strictly over 33.333 ms: 33.4/33.6 count, 33.3/33.2 do not.
  uint32_t over = 0;
  for (float v : intervals) over += v > 1000.0 / 30.0;
  assert(s.over_budget == over && over == 16);
  // Vblank bins: 16.7 -> 1; 33.x and 41.0 (2.46) -> 2; 49.7-50.3 -> 3; 66.x -> 4; 83.4 -> 5; 1808.3 -> 6+.
  uint32_t expect[6] = {};
  for (float v : intervals) ++expect[std::min<uint32_t>(FramePacingWindow::VblankCount(v), 6) - 1];
  for (size_t i = 0; i < 6; ++i) assert(s.vblanks[i] == expect[i]);
  assert(s.vblanks[0] == 1 && s.vblanks[1] == 6 && s.vblanks[2] == 9 && s.vblanks[3] == 2 &&
         s.vblanks[4] == 1 && s.vblanks[5] == 1);
  // Off grid: 41.0 is 7.7 ms from 2 vblanks, 1808.3 is 8.3 ms from 108 vblanks.
  assert(s.on_grid == 18);
  const std::string summary = w.FormatSummary();
  assert(Contains(summary, "frame pacing v1 window 0: frames 20 stored 20 t0 1000.0000 end "));
  assert(Contains(summary, "p50/p95/p99/worst 49.9/83.4/1808.3/1808.3"));
  assert(Contains(summary, "over 33.3 ms 16 (80.0%)"));
  assert(Contains(summary, "vblanks 1:1 2:6 3:9 4:2 5:1 6+:1 on-grid 18"));
  assert(w.SampleLineCount() == 1);
  const std::string samples = w.FormatSamples(0);
  assert(samples.rfind("frame pacing samples v1 window 0 part 1/1: 333/301/9/103/18/24 500/", 0) == 0);
  assert(Contains(samples, " 18083/301/9/103/18/24"));
  w.Reset();
  assert(w.window() == 1 && w.stored() == 0 && w.Summarize().frames == 0);
  assert(Contains(w.FormatSummary(), "window 1: frames 0 stored 0 t0 0.0000 end 0.0000"));
  assert(w.SampleLineCount() == 0);
}

void TestCapacityAndNoAllocation() {
  auto* w = new FramePacingWindow();
  count_allocations = true;
  for (int i = 0; i < 1100; ++i) w->Add({i % 2 ? 50.0f : 33.3f, 1, 2, 3, 4, 5}, 10.0 + i * 0.05);
  w->Add({-1.0f, -2.0f, 0.04f, 0.06f, 0, 0}, 70.0);  // clamp/round edge cases, still no allocation
  count_allocations = false;
  assert(allocations == 0);
  const auto s = w->Summarize();
  assert(s.frames == 1101 && s.stored == 1024 && w->SampleLineCount() == 8);
  const std::string last = w->FormatSamples(7);
  assert(last.rfind("frame pacing samples v1 window 0 part 8/8:", 0) == 0);
  size_t tokens = 0;
  for (char c : last) tokens += c == ' ';
  assert(tokens == 7 + 128);  // "frame pacing samples v1 window 0 part 8/8:" has 7 spaces
  FramePacingWindow edge;
  edge.Add({-1.0f, -2.0f, 0.04f, 0.06f, 4.0e9f, 0}, 1.0);
  assert(Contains(edge.FormatSamples(0), ": 0/0/0/1/4000000000/0"));
  delete w;
}

// Writes three report windows through the real formatter for the Python analyzer check.
void WriteSyntheticLog(const char* path) {
  FILE* file = std::fopen(path, "wb");
  assert(file);
  FramePacingWindow w;
  double present = 5000.0;
  uint64_t random = 7;
  int second = 5;
  for (int window = 0; window < 3; ++window) {
    const int frames = window == 1 ? 300 : 100;
    for (int i = 0; i < frames; ++i) {
      random = random * 6364136223846793005ull + 1442695040888963407ull;
      const uint32_t vblanks = window == 1 ? 1 : 2 + uint32_t((random >> 59) % 3);
      const float jitter = float(int((random >> 40) % 9) - 4) * 0.1f;
      const float interval = float(vblanks * (1000.0 / 60.0)) + jitter;
      const float draw = vblanks >= 3 ? 33.0f : 20.0f;
      present += interval / 1000.0;
      w.Add({interval, draw, 0.5f, 5.0f, 0.5f, 2.0f}, present);
    }
    const std::string prefix = fmt::format("[2026-10-15 12:00:{:02}.000] [info] [gpu] [t1] [carbon-gpu] ", second);
    std::fprintf(file, "%sunrelated statistics line\n", prefix.c_str());
    std::fprintf(file, "%s%s\n", prefix.c_str(), w.FormatSummary().c_str());
    for (size_t part = 0; part < w.SampleLineCount(); ++part) {
      std::fprintf(file, "%s%s\n", prefix.c_str(), w.FormatSamples(part).c_str());
    }
    w.Reset();
    second += 5;
  }
  std::fclose(file);
}
}  // namespace

// Keep replacements opaque to GCC's inlined malloc/mismatched-delete analysis.
[[gnu::noinline]] void* operator new(size_t bytes) {
  void* p = std::malloc(bytes ? bytes : 1);
  if (!p) throw std::bad_alloc();
  if (count_allocations) ++allocations;
  return p;
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, size_t) noexcept { ::operator delete(p); }

int main(int argc, char** argv) {
  TestVblankCount();
  TestSummaryAndFormat();
  TestCapacityAndNoAllocation();
  if (argc > 1) WriteSyntheticLog(argv[1]);
  std::printf("frame pacing: vblank bins, nearest-rank percentiles, strict 33.3 ms budget, "
              "capacity 1024, zero allocations in Add\n");
  return 0;
}
