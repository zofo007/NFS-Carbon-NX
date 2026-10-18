// BackgroundHasher plus the renderer's id bookkeeping (hash_jobs_ / ApplyTextureHashes / eviction):
// every submitted job is answered with the inline hash, results of evicted ids are dropped, and
// a texture is never re-submitted while its job is pending.
#include "texture_hasher.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <unordered_map>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif
#include <cassert>

namespace {
struct Extent {
  uint32_t start, size;
};
uint64_t HashOf(const Extent& e) { return (uint64_t(e.start) * 0x9E3779B97F4A7C15ull) ^ e.size ^ 1; }

struct Tex {
  Extent extent;
  uint64_t hash_job = 0;
  uint64_t applied = 0;  // hash applied last
  unsigned results = 0;
  bool evicted = false;
};
}  // namespace

int main() {
  carbon::gpu::BackgroundHasher<Extent> hasher;
  hasher.Start(HashOf);
  assert(hasher.Running());
  std::mt19937 random(0x7A5);
  std::vector<Tex> textures(64);
  for (uint32_t i = 0; i < textures.size(); ++i) textures[i].extent = {i * 4096, 1024 + i};
  std::unordered_map<uint64_t, Tex*> jobs;  // as hash_jobs_
  std::vector<carbon::gpu::BackgroundHasher<Extent>::Result> results;
  uint64_t next = 0;
  unsigned submitted = 0, dropped = 0, applied = 0;
  for (int frame = 0; frame < 400; ++frame) {
    // Frame start: apply what is finished.
    hasher.Take(results);
    for (const auto& [id, hash] : results) {
      auto it = jobs.find(id);
      if (it == jobs.end()) {
        ++dropped;
        continue;
      }
      Tex* t = it->second;
      jobs.erase(it);
      assert(!t->evicted && t->hash_job == id);
      t->hash_job = 0;
      assert(hash == HashOf(t->extent));
      t->applied = hash;
      ++t->results;
      ++applied;
    }
    results.clear();
    // Draws: due re-checks, and evictions that may race a pending job.
    for (int n = 0; n < 8; ++n) {
      Tex& t = textures[random() % textures.size()];
      if (t.evicted) continue;
      if (random() % 300 == 0) {
        if (t.hash_job) jobs.erase(t.hash_job);
        t.evicted = true;
        continue;
      }
      if (t.hash_job) continue;  // pending: never submitted twice
      t.hash_job = ++next;
      jobs.emplace(t.hash_job, &t);
      hasher.Submit(t.hash_job, t.extent);
      ++submitted;
    }
    // The rest of the frame (a timed wait: MinGW's sleep_for returns at once for short sleeps).
    for (auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(300);
         std::chrono::steady_clock::now() < end;) {
    }
  }
  // Evicted while its job is pending (as EvictResources erases hash_jobs_): the late result is dropped.
  Tex late{{1, 2}};
  late.hash_job = ++next;
  jobs.emplace(late.hash_job, &late);
  hasher.Submit(late.hash_job, late.extent);
  ++submitted;
  jobs.erase(late.hash_job);
  late.evicted = true;
  // Drain: every job of a live texture is answered.
  for (int spin = 0; spin < 5000 && applied + dropped < submitted; ++spin) {
    hasher.Take(results);
    for (const auto& [id, hash] : results) {
      auto it = jobs.find(id);
      if (it == jobs.end()) {
        ++dropped;
        continue;
      }
      assert(hash == HashOf(it->second->extent));
      it->second->hash_job = 0;
      jobs.erase(it);
      ++applied;
    }
    results.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(jobs.empty());
  hasher.Stop();
  assert(!hasher.Running());
  hasher.Stop();  // idempotent
  std::printf("PASS background hasher: %u submitted, %u applied with the inline hash, %u dropped after eviction\n",
              submitted, applied, dropped);
  assert(applied + dropped == submitted);
  assert(applied > submitted / 2 && dropped > 0);  // both paths exercised
}
