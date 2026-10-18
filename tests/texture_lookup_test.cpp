// Differential test: PickTexture's one walk against the former two searches of GetTexture
// (FindResolvedTexture(base, 0, 0), then the first matching non-resolved texture).
#include "texture_lookup.h"

#include <cstdio>
#include <memory>
#include <random>
#include <unordered_map>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif
#include <cassert>

namespace {
struct Entry {
  bool resolved;
  unsigned last_used_frame;
  int guest;
};
using Map = std::unordered_multimap<unsigned, std::unique_ptr<Entry>>;

std::pair<Entry*, Entry*> Reference(Map& map, unsigned key, int guest, bool resolvable) {
  Entry* resolved = nullptr;
  if (resolvable) {
    auto range = map.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
      Entry* t = it->second.get();
      if (!t->resolved) continue;
      if (!resolved || t->last_used_frame > resolved->last_used_frame) resolved = t;
    }
    if (resolved) return {resolved, nullptr};
  }
  auto range = map.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (!it->second->resolved && it->second->guest == guest) return {nullptr, it->second.get()};
  }
  return {nullptr, nullptr};
}
}  // namespace

int main() {
  std::mt19937 random(0x7E57);
  unsigned cases = 0;
  for (int round = 0; round < 20000; ++round) {
    Map map;
    const int entries = int(random() % 9);
    for (int i = 0; i < entries; ++i) {
      // Few keys, guests and frames so buckets share addresses, guests repeat and frames tie.
      map.emplace(random() % 3, std::make_unique<Entry>(Entry{random() % 3 == 0, unsigned(random() % 4),
                                                              int(random() % 3)}));
    }
    const unsigned key = random() % 3;
    const int guest = int(random() % 3);
    const bool resolvable = random() % 2;
    auto range = map.equal_range(key);
    auto [resolved, match] = carbon::gpu::PickTexture(range.first, range.second, guest, resolvable);
    auto expected = Reference(map, key, guest, resolvable);
    // GetTexture returns the resolved image when there is one, else the guest match.
    Entry* got = resolved ? resolved : match;
    Entry* want = expected.first ? expected.first : expected.second;
    assert(got == want);
    assert((resolved != nullptr) == (expected.first != nullptr));
    ++cases;
  }
  std::printf("PASS %u texture lookup cases: same resolved priority, tie order and guest match\n", cases);
}
