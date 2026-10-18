#include "constant_batch.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::gpu;

// Fail without a Windows assertion dialog that leaves automation waiting.
#undef assert
#define assert(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
  std::exit(1); } } while (false)

void TestConstantPackets();

int main() {
  TestConstantPackets();
  std::array<uint32_t, 2048> guest{};
  RegisterFile scalar, batch;
  uint32_t random = 0xC012AB34;
  unsigned cases = 0;
  for (uint32_t offset = 0; offset < 2048; ++offset) {
    for (uint32_t requested : {0u, 1u, 3u, 4u, 7u, 16u, 63u, 256u, 1024u, 2048u}) {
      const uint32_t n = std::min(requested, 2048 - offset);
      for (unsigned round = 0; round < 3; ++round) {
        for (uint32_t i = 0; i < kRegisterCount; ++i) {
          random = random * 1664525 + 1013904223;
          scalar.values[i] = random;
        }
        batch = scalar;
        FloatConstantWriteCounts expected{}, actual{};
        for (uint32_t i = 0; i < n; ++i) {
          const uint32_t index = kRegFloatConstants + offset + i;
          // Mixed, all identical, and signed-zero/NaN-payload changes.
          const uint32_t bits = round == 1 ? scalar[index] : round == 2
              ? std::array{0u, 0x80000000u, 0x7FC00001u, 0x7FC00002u}[i & 3]
              : i % 3 ? scalar[index] : random + i;
          guest[i] = ByteSwap32(bits);
          const uint32_t stage = (offset + i) / 1024;
          ++expected.written[stage];
          const uint32_t changed = scalar.WriteFloatConstant(index, bits);
          expected.changed[stage] += changed;
          expected.changed_blocks[stage] |= changed << (((offset + i) % 1024) >> 8);
        }
        assert(TryWriteFloatConstantsBE(batch, kRegFloatConstants + offset, guest.data(), n, actual));
        assert(std::memcmp(scalar.values, batch.values, sizeof(scalar.values)) == 0);
        assert(std::memcmp(&expected, &actual, sizeof(expected)) == 0);
        ++cases;
      }
    }
  }
  // A mixed/overflowing range must leave everything untouched for scalar fallback.
  for (auto [index, count] : {std::pair{kRegFloatConstants - 1, 2u},
                             std::pair{kRegFetchConstants - 1, 2u},
                             std::pair{kRegFetchConstants, 1u},
                             std::pair{UINT32_MAX, UINT32_MAX}}) {
    scalar = batch;
    FloatConstantWriteCounts counts{};
    assert(!TryWriteFloatConstantsBE(batch, index, guest.data(), count, counts));
    assert(std::memcmp(scalar.values, batch.values, sizeof(scalar.values)) == 0);
  }
  FloatConstantWriteCounts counts{};
  assert(TryWriteFloatConstantsBE(batch, kRegFloatConstants, nullptr, 0, counts));
  assert(counts.written[0] == 0 && counts.written[1] == 0);
  std::printf("PASS %u constant-span differential cases; full register file and exact stage counters; invalid spans unchanged\n", cases);

  // Host timing is just a microbenchmark, never a Switch FPS assertion.
  for (uint32_t i = 0; i < guest.size(); ++i) guest[i] = ByteSwap32(i);
  uint64_t checksum = 0;
  const auto bench = [&](bool bulk) {
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t call = 0; call < 200000; ++call) {
      guest[0] = ByteSwap32(call);
      if (bulk) {
        FloatConstantWriteCounts c{};
        assert(TryWriteFloatConstantsBE(batch, kRegFloatConstants, guest.data(), 256, c));
        checksum += c.changed[0];
      } else {
        for (uint32_t i = 0; i < 256; ++i)
          checksum += batch.WriteFloatConstant(kRegFloatConstants + i, ByteSwap32(guest[i]));
      }
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  const double before = bench(false), after = bench(true);
  std::printf("Host-only51.2Mwords scalar%.3fms bulk%.3fms checksum%llu; NOT Switch performance\n", before, after, static_cast<unsigned long long>(checksum));
}
