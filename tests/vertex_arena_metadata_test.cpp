#include "vertex_arena_metadata.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

namespace {
std::atomic<bool> count_allocations{false};
std::atomic<size_t> allocations{0}, frees{0};

struct OldEntry {
  uint32_t offset = 0, size = 0;
  std::vector<uint64_t> hash, checked;
  std::vector<uint8_t> interval;
  explicit OldEntry(size_t pages) : hash(pages, 0), checked(pages, 0), interval(pages, 1) {}
};

// Same page-update decisions as ArenaFetch, without Vulkan or guest memory.
unsigned UpdateOld(OldEntry& entry, size_t p, uint64_t frame, uint64_t hash) {
  if (entry.checked[p] == frame) return 0;
  if (entry.hash[p] && frame < entry.checked[p] + entry.interval[p]) return 0;
  const bool copy = !entry.hash[p];
  if (copy) entry.hash[p] = hash;
  else if (hash != entry.hash[p]) return 2;
  else entry.interval[p] = std::min<uint8_t>(entry.interval[p] * 2, 4);
  entry.checked[p] = frame;
  return copy ? 1 : 0;
}

unsigned UpdateNew(carbon::gpu::VertexArenaEntry& entry, size_t p, uint64_t frame, uint64_t hash) {
  auto& page = entry.pages[p];
  if (page.checked == frame) return 0;
  if (page.hash && frame < page.checked + page.interval) return 0;
  const bool copy = !page.hash;
  if (copy) page.hash = hash;
  else if (hash != page.hash) return 2;
  else page.interval = std::min<uint8_t>(page.interval * 2, 4);
  page.checked = frame;
  return copy ? 1 : 0;
}

template <typename Entry, typename Initialize>
void CheckAllocations(size_t per_entry, Initialize initialize) {
  constexpr size_t kEntries = 65296;  // Count in the hardware freeze interval.
  std::vector<Entry> entries;
  entries.reserve(kEntries);  // Exclude the fixture container itself.
  allocations = frees = 0;
  count_allocations = true;
  for (size_t i = 0; i < kEntries; ++i) initialize(entries, 1 + i % 4);
  assert(allocations.load() == per_entry * kEntries && frees.load() == 0);
  entries.clear();
  assert(frees.load() == allocations.load());
  count_allocations = false;
  std::printf("%zu entries: %zu metadata allocations / %zu frees\n",
              kEntries, allocations.load(), frees.load());
}

struct CountingResource : std::pmr::memory_resource {
  size_t allocated = 0, released = 0, live_bytes = 0, peak_bytes = 0;
  void* do_allocate(size_t bytes, size_t alignment) override {
    ++allocated;
    live_bytes += bytes;
    peak_bytes = std::max(peak_bytes, live_bytes);
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* p, size_t bytes, size_t alignment) override {
    ++released;
    live_bytes -= bytes;
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  }
  bool do_is_equal(const memory_resource& other) const noexcept override { return this == &other; }
};

void CheckBatchLifetime() {
  using Map = std::pmr::unordered_map<uint64_t, carbon::gpu::VertexArenaEntry>;
  CountingResource direct;
  {
    Map unpooled(&direct);
    for (uint64_t i = 0; i < 65296; ++i)
      unpooled.try_emplace(i, &direct).first->second.pages.resize(1 + i % 4);
  }
  assert(direct.allocated >= 2 * 65296 && direct.released == direct.allocated);
  std::printf("unpooled nodes+pages: %zu allocations/frees\n", direct.allocated);
  CountingResource upstream;
  std::pmr::monotonic_buffer_resource memory(&upstream);
  std::optional<Map> entries(std::in_place, &memory);
  for (unsigned epoch = 0; epoch < 4; ++epoch) {
    const size_t allocated_before = upstream.allocated;
    for (uint64_t i = 0; i < 65296; ++i) {
      auto& entry = entries->try_emplace(i, &memory).first->second;
      entry.offset = uint32_t(i * 256);
      entry.pages.resize(1 + i % 4);
      entry.pages.back().hash = i | 1;
    }
    assert(entries->at(65295).pages.back().hash == 65295);
    entries->erase(10);
    auto& fresh = entries->try_emplace(10, &memory).first->second;
    fresh.pages.resize(2);
    assert(fresh.pages[0].hash == 0 && fresh.pages[0].interval == 1);
    assert(upstream.released == allocated_before);
    const size_t batch_allocations = upstream.allocated - allocated_before;
    assert(batch_allocations < 64 && upstream.live_bytes < 32 * 1024 * 1024);
    entries.reset();
    assert(upstream.released == allocated_before);  // No per-entry upstream frees.
    memory.release();
    assert(upstream.live_bytes == 0 && upstream.released == upstream.allocated);
    entries.emplace(&memory);
    assert(entries->empty());
    std::printf("epoch %u: 65296 entries, %zu batch allocations/frees\n", epoch, batch_allocations);
  }
}
}  // namespace

// Keep replacements opaque to GCC's inlined malloc/mismatched-delete analysis.
[[gnu::noinline]] void* operator new(size_t bytes) {
  void* p = std::malloc(bytes ? bytes : 1);
  if (!p) throw std::bad_alloc();
  if (count_allocations) ++allocations;
  return p;
}
[[gnu::noinline]] void operator delete(void* p) noexcept {
  if (p && count_allocations) ++frees;
  std::free(p);
}
[[gnu::noinline]] void operator delete(void* p, size_t) noexcept { ::operator delete(p); }

// carbon_gpu_vertex_memo: a hit needs the same key and frame, a range inside the stored one and no
// invalidation since; colliding keys replace each other.
void CheckMemo() {
  carbon::gpu::VertexArenaMemo memo;
  uint32_t offset = 0;
  const uint64_t key = (uint64_t(0x1234000) << 32) | 4096;
  assert(!memo.Find(key, 7, 0, 64, offset));
  memo.Store(key, 7, 128, 2048, 99);
  assert(memo.Find(key, 7, 128, 2048, offset) && offset == 99);
  assert(memo.Find(key, 7, 512, 1024, offset) && offset == 99);
  assert(!memo.Find(key, 7, 64, 1024, offset));   // starts before the validated range
  assert(!memo.Find(key, 7, 512, 4096, offset));  // ends after it
  assert(!memo.Find(key, 8, 512, 1024, offset));  // next frame: pages may be due again
  assert(!memo.Find(key + 1, 7, 512, 1024, offset));
  memo.Invalidate();
  assert(!memo.Find(key, 7, 512, 1024, offset));  // entry dropped or arena reset
  memo.Store(key, 7, 0, 4096, 5);
  uint64_t other = key;
  for (uint64_t probe = 1; probe < 100000; ++probe) {  // find a key in the same slot
    carbon::gpu::VertexArenaMemo trial;
    trial.Store(key, 1, 0, 1, 1);
    trial.Store(key + probe * 4096, 1, 0, 1, 2);
    if (!trial.Find(key, 1, 0, 1, offset)) { other = key + probe * 4096; break; }
  }
  assert(other != key);
  memo.Store(other, 7, 0, 4096, 6);
  assert(!memo.Find(key, 7, 0, 64, offset) && memo.Find(other, 7, 0, 64, offset) && offset == 6);
}

int main() {
  CheckMemo();
  using carbon::gpu::VertexArenaEntry;
  for (size_t pages : {size_t(1), size_t(4), size_t(33)}) {
    OldEntry old(pages);
    VertexArenaEntry current;
    current.offset = 256;
    current.size = uint32_t(pages * 16384 - 1);
    current.pages.resize(pages);
    for (const auto& page : current.pages) {
      assert(page.hash == 0 && page.checked == 0 && page.interval == 1);
    }
    uint64_t random = 1;
    for (uint64_t step = 0; step < 4096; ++step) {
      // Duplicate same-frame visits, adaptive checks, changes and frame wrap.
      const uint64_t frame = step < 4000 ? step / 2 : std::numeric_limits<uint64_t>::max() - 31 + (step - 4000) / 2;
      for (size_t p = 0; p < pages; ++p) {
        random = random * 6364136223846793005ull + 1;
        const uint64_t hash = old.hash[p] && (random >> 60) ? old.hash[p] : random | 1;
        const unsigned before = UpdateOld(old, p, frame, hash);
        assert(before == UpdateNew(current, p, frame, hash));
        assert(current.pages[p].hash == old.hash[p]);
        assert(current.pages[p].checked == old.checked[p]);
        assert(current.pages[p].interval == old.interval[p]);
        if (before == 2) {  // Changed data gets fresh storage, never overwrites in-flight data.
          old.hash[p] = old.checked[p] = 0;
          old.interval[p] = 1;
          current.pages[p] = {};
        }
      }
    }
    auto moved = std::move(current);
    assert(moved.offset == 256 && moved.size == pages * 16384 - 1 && moved.pages.size() == pages);
    assert(moved.pages.back().hash == old.hash.back());
  }
  CheckAllocations<OldEntry>(3, [](auto& entries, size_t pages) { entries.emplace_back(pages); });
  CheckBatchLifetime();
}
