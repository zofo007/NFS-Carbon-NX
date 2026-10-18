// IndexCacheFresh against a guest index buffer that is rewritten at random frames.
#include "index_cache.h"

#include <cstdio>
#include <random>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif
#include <cassert>

using carbon::gpu::IndexCacheEntry;
using carbon::gpu::IndexCacheFresh;

int main() {
  constexpr uint8_t kMax = 2;  // kIndexMaxInterval in renderer.cpp
  std::mt19937 random(0x1D3C);
  unsigned runs = 0, stale_frames = 0, worst_stale = 0;
  for (int run = 0; run < 2000; ++run) {
    uint64_t guest = 1 + random() % 7;  // the guest bytes' hash; never 0
    IndexCacheEntry e;
    e.hash = guest;
    e.checked = 1;
    uint64_t changed_at = 0;  // frame of the first rewrite the cache has not seen
    const uint32_t write_odds = 2 + random() % 40;
    for (uint64_t frame = 1; frame < 400; ++frame) {
      if (random() % write_odds == 0) {
        const uint64_t next = 1 + random() % 7;
        if (next != guest && !changed_at) changed_at = frame;
        guest = next;
      }
      for (int use = 0, uses = 1 + int(random() % 3); use < uses; ++use) {
        unsigned hashes = 0;
        const bool fresh = IndexCacheFresh(e, frame, kMax, [&] { ++hashes; return guest; });
        assert(hashes <= 1);
        if (!fresh) {
          // The renderer drops the entry and copies the current bytes into fresh space.
          assert(guest != e.hash);
          e = IndexCacheEntry{};
          e.hash = guest;
          e.checked = frame;
          changed_at = 0;
          continue;
        }
        if (changed_at && guest != e.hash) {
          // Using an old copy: only until the next due check, at most kMax frames later.
          const unsigned stale = unsigned(frame - changed_at);
          assert(stale < kMax);
          stale_frames += 1;
          worst_stale = stale > worst_stale ? stale : worst_stale;
        } else if (guest == e.hash) {
          changed_at = 0;  // rewritten back to the cached contents
        }
        assert(e.interval >= 1 && e.interval <= kMax);
      }
    }
    ++runs;
  }
  // A second use in the same frame never hashes again.
  IndexCacheEntry e;
  e.hash = 5;
  e.checked = 10;
  e.interval = 1;
  unsigned hashes = 0;
  assert(IndexCacheFresh(e, 11, kMax, [&] { ++hashes; return uint64_t(5); }) && hashes == 1 && e.interval == 2);
  assert(IndexCacheFresh(e, 11, kMax, [&] { ++hashes; return uint64_t(6); }) && hashes == 1);
  assert(IndexCacheFresh(e, 12, kMax, [&] { ++hashes; return uint64_t(6); }) && hashes == 1);  // within interval
  assert(!IndexCacheFresh(e, 13, kMax, [&] { ++hashes; return uint64_t(6); }) && hashes == 2);
  std::printf("PASS %u index cache runs: one hash per frame, stale copies at most %u frames (bound %u), %u stale uses\n",
              runs, worst_stale, unsigned(kMax - 1), stale_frames);
}
