#include "gpu_common.h"
#include "index_cache.h"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if __has_include("index_upload_cache.h")
#include "index_upload_cache.h"
#define HAS_INDEX_CACHE 1
#endif

#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  std::exit(1); } } while (false)

using namespace carbon::gpu;
#include "converters.inc"

struct Fixture {
  using VkDeviceSize = uint64_t;
  struct UploadAllocation { uint8_t* ptr; VkBuffer buffer; VkDeviceSize offset; uint32_t chunk; };
  std::deque<std::vector<uint8_t>> storage;
  uint64_t frame_number_ = 1;
  bool index_cache_enabled = false, arena_accept = false, fail_upload = false;
  enum SkipReason { kSkipUpload };
  unsigned skipped_uploads = 0;
  void Skip(SkipReason) { ++skipped_uploads; }
  bool FLAGS_carbon_gpu_index_cache_storage_() const { return index_cache_enabled; }
  unsigned arena_fetches = 0;
  VkBuffer arena_buffer_ = reinterpret_cast<VkBuffer>(uintptr_t(0xCAFE));
  bool IndexArenaFetch(const IndexCacheKey&, const uint8_t*, uint32_t, uint32_t& offset,
                       uint32_t& lo, uint32_t& hi) {
    ++arena_fetches;
    offset = 128;
    lo = 2;
    hi = 9;
    return arena_accept;
  }
  struct {
    uint32_t index_reuse_hits = 0, index_conversions = 0;
    uint64_t index_upload_avoided_bytes = 0;
  } stats_;
#ifdef HAS_INDEX_CACHE
  IndexUploadCache index_upload_cache_;
#endif
  UploadAllocation Upload(VkDeviceSize n, VkDeviceSize) {
    if (fail_upload) return {nullptr, VK_NULL_HANDLE, 0, 0};
    storage.emplace_back(n, 0xA5);
    return {storage.back().data(), reinterpret_cast<VkBuffer>(storage.size()), 0, uint32_t(storage.size())};
  }
  struct Result {
    VkBuffer buffer; VkDeviceSize offset; uint32_t count, lo, hi; bool known; VkIndexType type;
  };
  Result Draw(const uint8_t* guest_indices, uint32_t count, bool is16, xenos::Endian endian,
              bool restart = false, uint32_t reset_index = 0xFFFFFF, uint32_t index_offset_v = 0,
              uint32_t address = 0x123400) {
    Result result{};
    const auto draw = [&]() {
    struct { bool indexed; xenos::IndexFormat index_format; xenos::Endian index_endian; uint32_t index_base; }
      info{true, is16 ? xenos::IndexFormat::kInt16 : xenos::IndexFormat::kInt32, endian, address};
    enum class Expand { kNone };
    const Expand expand = Expand::kNone;
    struct { bool primitive_restart; } key{restart};
    uint32_t host_count = count;
    bool host_indexed = false, indices_done = false;
    VkIndexType index_type = VK_INDEX_TYPE_UINT32;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    struct { bool known = false; uint32_t min_index = 0, max_index = 0; } vertex_range;
#include "draw.inc"
    CHECK(host_indexed && indices_done);
    result = {index_buffer, index_offset, host_count, vertex_range.min_index,
              vertex_range.max_index, vertex_range.known, index_type};
    };
    draw();
    return result;
  }
};

int main() {
  {
    Fixture failing;
    uint16_t input[] = {5, 2, 9};
    failing.fail_upload = true;
    auto skipped = failing.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    CHECK(!skipped.buffer && !skipped.known && failing.skipped_uploads == 1 && failing.storage.empty());
    failing.fail_upload = false;
    auto retried = failing.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    CHECK(retried.buffer && retried.lo == 2 && retried.hi == 9 && failing.storage.size() == 1);
    std::puts("PASS actual Draw skips failed index upload and retries without caching invalid data");
  }
  {
    Fixture arena;
    uint16_t input[] = {5, 2, 9};
    arena.index_cache_enabled = true;
    arena.arena_accept = true;
    auto hit = arena.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    CHECK(hit.buffer == arena.arena_buffer_ && hit.offset == 128 && hit.lo == 2 && hit.hi == 9);
    CHECK(arena.arena_fetches == 1 && arena.storage.empty() && arena.stats_.index_reuse_hits == 0);
    arena.arena_accept = false;
    auto fallback = arena.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    auto reused = arena.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    CHECK(arena.arena_fetches == 3 && arena.storage.size() == 1);
    CHECK(fallback.buffer == reused.buffer && arena.stats_.index_reuse_hits == 1);
    arena.index_cache_enabled = false;
    reused = arena.Draw(reinterpret_cast<uint8_t*>(input), 3, true, xenos::Endian::kNone);
    CHECK(arena.arena_fetches == 3 && reused.buffer == fallback.buffer);
    std::puts("PASS actual Draw arena hit, unavailable fallback, exact reuse and disabled arena routing");
  }
  Fixture f;
  uint16_t values[] = {5, 2, 9};
  auto first = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  auto repeated = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  CHECK(first.buffer == repeated.buffer);
  CHECK(f.storage.size() == 1);  // Fails on the original: one allocation/conversion per draw.
  values[1] = 1;
  auto changed = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  CHECK(changed.buffer != first.buffer && changed.lo == 1 && changed.hi == 9);
  auto changing_again = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  CHECK(changing_again.buffer != changed.buffer);  // Rewritten keys back off for this frame.
  ++f.frame_number_;
  changed = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  auto offset = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone, false, 0xFFFFFF, 7);
  CHECK(offset.buffer == changed.buffer && offset.lo == 8 && offset.hi == 16);
  ++f.frame_number_;
  auto next_frame = f.Draw(reinterpret_cast<uint8_t*>(values), 3, true, xenos::Endian::kNone);
  CHECK(next_frame.buffer != changed.buffer);

  uint16_t restart16[] = {2, 0xFFFF, 7};
  auto r16 = f.Draw(reinterpret_cast<uint8_t*>(restart16), 3, true, xenos::Endian::kNone, true, 0xFFFF);
  auto r16_again = f.Draw(reinterpret_cast<uint8_t*>(restart16), 3, true, xenos::Endian::kNone, true, 0xFFFF);
  CHECK(r16.buffer == r16_again.buffer && r16.lo == 2 && r16.hi == 7);
  uint16_t all_reset[] = {0xFFFF, 0xFFFF};
  auto empty = f.Draw(reinterpret_cast<uint8_t*>(all_reset), 2, true, xenos::Endian::kNone, true, 0xFFFF);
  CHECK(!empty.known);
  auto empty_again = f.Draw(reinterpret_cast<uint8_t*>(all_reset), 2, true, xenos::Endian::kNone, true, 0xFFFF);
  CHECK(empty.buffer == empty_again.buffer && !empty_again.known);
  uint16_t custom16[] = {3, 1};
  auto before_reset = f.Draw(reinterpret_cast<uint8_t*>(custom16), 2, true, xenos::Endian::kNone, true, 3);
  custom16[0] = 0xFFFF;  // Identical converted bytes, DIFFERENT range: must not reuse.
  auto after_reset = f.Draw(reinterpret_cast<uint8_t*>(custom16), 2, true, xenos::Endian::kNone, true, 3);
  CHECK(after_reset.buffer != before_reset.buffer && before_reset.hi == 1 && after_reset.hi == 0xFFFF);
  uint32_t restart32[] = {0xAB000001, 0xABFFFFFF, 0x00123456};
  auto r32 = f.Draw(reinterpret_cast<uint8_t*>(restart32), 3, false, xenos::Endian::kNone, true, 0xFFFFFF);
  CHECK(r32.lo == 1 && r32.hi == 0x123456);
  restart32[0] ^= 0xFF000000;  // The original converter ignores high eight bits.
  auto r32_again = f.Draw(reinterpret_cast<uint8_t*>(restart32), 3, false, xenos::Endian::kNone, true, 0xFFFFFF);
  CHECK(r32.buffer == r32_again.buffer);
  // 37 words exercise two full blocks, one remaining vector, then a scalar tail.
  const std::array<std::array<unsigned, 4>, 4> byte_orders{{
      {{0, 1, 2, 3}}, {{1, 0, 3, 2}}, {{3, 2, 1, 0}}, {{2, 3, 0, 1}}}};
  unsigned simd_restart_cases = 0;
  for (unsigned mode = 0; mode < 4; ++mode) {
    for (uint32_t reset : {7u, 0xFFFFFFu}) {
      for (uint32_t position : {0u, 15u, 16u, 31u, 32u, 35u, 36u}) {
        ++f.frame_number_;
        std::array<uint8_t, 37 * 4> guest{};
        const auto put = [&](uint32_t index, uint32_t value) {
          for (unsigned b = 0; b < 4; ++b) guest[index * 4 + b] = uint8_t(value >> (byte_orders[mode][b] * 8));
        };
        for (uint32_t i = 0; i < 37; ++i) put(i, i + 2);
        put(position, reset);
        const auto endian = static_cast<xenos::Endian>(mode);
        auto a = f.Draw(guest.data(), 37, false, endian, true, reset);
        auto b = f.Draw(guest.data(), 37, false, endian, true, reset);
        CHECK(a.buffer == b.buffer);
        put(position, 0xFFFFFE);
        auto c = f.Draw(guest.data(), 37, false, endian, true, reset);
        CHECK(c.buffer != b.buffer && c.lo == (position == 0 ? 3u : 2u) && c.hi == 0xFFFFFE);
        ++simd_restart_cases;
      }
    }
  }
  std::printf("PASS %u deterministic SIMD restart block/vector/tail cases across all endian modes\n", simd_restart_cases);

  uint32_t random = 0x94C73115;
  unsigned cases = 0;
  for (bool is16 : {false, true}) {
    for (auto endian : {xenos::Endian::kNone, xenos::Endian::k8in16,
                        xenos::Endian::k8in32, xenos::Endian::k16in32}) {
      for (bool restart : {false, true}) {
        for (uint32_t count : {1u, 2u, 3u, 4u, 7u, 8u, 9u, 31u, 64u, 257u}) {
          for (uint32_t alignment = 0; alignment < 4; ++alignment) {
            ++f.frame_number_;
            std::vector<uint8_t> guest(count * (is16 ? 2 : 4) + 4);
            for (auto& b : guest) { random = random * 1664525 + 1013904223; b = uint8_t(random >> 24); }
            uint8_t* src = guest.data() + alignment;
            const uint32_t reset = random & (is16 ? 0xFFFF : 0xFFFFFF);
            auto a = f.Draw(src, count, is16, endian, restart, reset);
            auto b = f.Draw(src, count, is16, endian, restart, reset);
            const bool cacheable = !is16 || !restart || reset == 0xFFFF;
            CHECK((a.buffer == b.buffer) == cacheable);
            CHECK(a.lo == b.lo && a.hi == b.hi && a.known == b.known);
            uint32_t lo = UINT32_MAX, hi = 0;
            for (uint32_t i = 0; i < count; ++i) {
              uint32_t v = 0;
              std::memcpy(&v, src + i * (is16 ? 2 : 4), is16 ? 2 : 4);
              if (is16) {
                if (endian == xenos::Endian::k8in16 || endian == xenos::Endian::k8in32)
                  v = ((v & 255) << 8) | (v >> 8);
              } else {
                if (endian == xenos::Endian::k8in16) v = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8);
                if (endian == xenos::Endian::k8in32) v = ((v & 255) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
                if (endian == xenos::Endian::k16in32) v = (v << 16) | (v >> 16);
                v &= 0xFFFFFF;
              }
              const bool reset_word = restart && v == reset;
              const uint32_t want = reset_word ? (is16 ? 0xFFFF : UINT32_MAX) : v;
              uint32_t got = 0;
              const auto& uploaded = f.storage[size_t(reinterpret_cast<uintptr_t>(a.buffer)) - 1];
              std::memcpy(&got, uploaded.data() + a.offset + i * (is16 ? 2 : 4), is16 ? 2 : 4);
              CHECK(got == want);
              if (!reset_word) { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
            // Preserve the original converter's 16-bit all-restart sentinel.
            if (is16 && lo == UINT32_MAX) lo = 0xFFFF;
            CHECK(a.known == (lo <= hi));
            if (a.known) CHECK(a.lo == lo && a.hi == hi);
            src[!is16 && endian == xenos::Endian::k8in32 ? 3 : 0] ^= 1;
            auto c = f.Draw(src, count, is16, endian, restart, reset);
            CHECK(c.buffer != b.buffer);
            ++cases;
          }
        }
      }
    }
  }
  std::printf("PASS %u actual indexed-draw cases: exact bytes/ranges, content changes, modes, unaligned sources, frame and explicit-buffer lifetime\n", cases);

#ifdef HAS_INDEX_CACHE
  IndexUploadCache cache;
  using Probe = const IndexUploadCache::Entry* (*)(IndexUploadCache&, const IndexUploadCache::Key&, const uint8_t*, uint64_t);
  // Opaque call matches the production guest-memory boundary (no host array size assumptions).
  Probe volatile probe = +[](IndexUploadCache& c, const IndexUploadCache::Key& k, const uint8_t* p, uint64_t frame) {
    return c.Find(k, p, frame);
  };
  IndexUploadCache::Key key{0, 3, 0, xenos::Endian::kNone, true, false};
  cache.Store(key, reinterpret_cast<const uint8_t*>(1), VK_NULL_HANDLE, 0, 0, 0, 10);
  CHECK(!probe(cache, key, nullptr, 11));  // Expired storage must never be read.
  for (uint32_t i = 0; i < 8192; ++i) {
    key.address = i * 16;
    cache.Store(key, reinterpret_cast<uint8_t*>(values), reinterpret_cast<VkBuffer>(uintptr_t(i + 1)),
                i * 16, 1, 9, 12);
  }
  unsigned retained = 0;
  for (uint32_t i = 0; i < 8192; ++i) {
    key.address = i * 16;
    if (const auto* e = probe(cache, key, reinterpret_cast<uint8_t*>(values), 12)) {
      CHECK(e->buffer == reinterpret_cast<VkBuffer>(uintptr_t(i + 1)) && e->offset == i * 16);
      ++retained;
    }
  }
  CHECK(retained > 0 && retained <= 4096);
  std::printf("PASS restart ambiguity, masked bits, stale-frame guard and 8192 colliding keys; cache metadata %zu bytes\n", sizeof(cache));

  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  const size_t page = system_info.dwPageSize;
  auto* guarded_guest = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  auto* guarded_out = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(guarded_guest && guarded_out);
  DWORD old_protection;
  CHECK(VirtualProtect(guarded_guest + page, page, PAGE_NOACCESS, &old_protection));
  CHECK(VirtualProtect(guarded_out + page, page, PAGE_NOACCESS, &old_protection));
  for (bool is16 : {false, true}) {
    for (auto endian : {xenos::Endian::kNone, xenos::Endian::k8in16,
                        xenos::Endian::k8in32, xenos::Endian::k16in32}) {
      for (uint32_t n = 1; n <= 97; ++n) {
        const size_t bytes = n * (is16 ? 2 : 4);
        uint8_t* guest = guarded_guest + page - bytes;
        uint8_t* output = guarded_out + page - bytes;
        std::memset(guest, 0, bytes);
        std::memset(output, 0, bytes);
        key = {0, n, 0, endian, is16, false};
        cache.Store(key, output, VK_NULL_HANDLE, 0, 0, 0, 20);
        CHECK(probe(cache, key, guest, 20));
        // Last-index changes must be checked, without reading past either span.
        std::memset(guest + bytes - (is16 ? 2 : 4), 1, is16 ? 2 : 4);
        CHECK(!probe(cache, key, guest, 20));
      }
    }
  }
  CHECK(VirtualFree(guarded_guest, 0, MEM_RELEASE));
  CHECK(VirtualFree(guarded_out, 0, MEM_RELEASE));
  std::puts("PASS 776 guard-page tail cases: full validation, no guest/upload overreads");

  // Opaque calls prevent loop hoisting; measure only conversion versus exact validation.
  std::vector<uint16_t> input(4096), output(4096);
  for (uint32_t i = 0; i < input.size(); ++i) input[i] = uint16_t(i);
  auto volatile convert = &ConvertIndices16;
  using Find = const IndexUploadCache::Entry* (*)(IndexUploadCache&, const IndexUploadCache::Key&, const uint8_t*);
  Find volatile find = +[](IndexUploadCache& c, const IndexUploadCache::Key& k, const uint8_t* p) {
    return c.Find(k, p, 100);
  };
  key = {0, uint32_t(input.size()), 0, xenos::Endian::k8in16, true, false};
  uint32_t lo, hi;
  convert(reinterpret_cast<uint8_t*>(input.data()), output.data(), key.count, true, false, 0, lo, hi);
  cache.Store(key, reinterpret_cast<uint8_t*>(output.data()), VK_NULL_HANDLE, 0, lo, hi, 100);
  uint64_t checksum = 0;
  const auto a = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < 20000; ++i) {
    convert(reinterpret_cast<uint8_t*>(input.data()), output.data(), key.count, true, false, 0, lo, hi);
    checksum += lo + hi;
  }
  const auto b = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < 20000; ++i) {
    const auto* e = find(cache, key, reinterpret_cast<uint8_t*>(input.data()));
    CHECK(e);
    checksum += e->lo + e->hi;
  }
  const auto c = std::chrono::steady_clock::now();
  std::printf("Host-only 81.92M 16-bit indices: convert %.3f ms, validate %.3f ms, checksum %llu; NOT Switch performance\n",
      std::chrono::duration<double, std::milli>(b - a).count(),
      std::chrono::duration<double, std::milli>(c - b).count(), static_cast<unsigned long long>(checksum));
  const auto d = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < 20000; ++i) {
    input[0] ^= 1;
    CHECK(!find(cache, key, reinterpret_cast<uint8_t*>(input.data())));
    convert(reinterpret_cast<uint8_t*>(input.data()), output.data(), key.count, true, false, 0, lo, hi);
    cache.Store(key, reinterpret_cast<uint8_t*>(output.data()), VK_NULL_HANDLE, 0, lo, hi, 100);
    checksum += lo + hi;
  }
  const auto e = std::chrono::steady_clock::now();
  std::printf("Host-only changing same-key first index: validate+convert %.3f ms; checksum %llu; NOT Switch performance\n",
      std::chrono::duration<double, std::milli>(e - d).count(), static_cast<unsigned long long>(checksum));
#endif
}
