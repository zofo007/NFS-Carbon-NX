#include "audio_resampler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

#include "reference.inc"

namespace {
// Called out of line, as in the game: inlined into CheckCase, GCC may pick different x86
// FMA/sub operand forms for the two identical source expressions, changing NaN payloads.
[[gnu::noinline]] void Original(PPCContext& ctx, uint8_t* base) { ReferenceResample(ctx, base); }
[[gnu::noinline]] void Candidate(PPCContext& ctx, uint8_t* base) { carbon::audio::Resample(ctx, base); }
constexpr uint32_t kBytes = 1 << 20, kConstantPage = 0x820C7000;
struct Memory {
  uint8_t* base;
  Memory() {
    base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
    assert(base && VirtualAlloc(base, kBytes, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + kConstantPage, 4096, MEM_COMMIT, PAGE_READWRITE));
  }
  ~Memory() { VirtualFree(base, 0, MEM_RELEASE); }
};

uint32_t HostFp() { return PPCFPSCRRegister::Platform::getcsr(); }
void SetFp(uint32_t value) { PPCFPSCRRegister::Platform::setcsr(value); }

void CheckCase(std::mt19937_64& random, Memory& a, Memory& b, int n,
               unsigned mode, unsigned rounding, bool flush) {
  auto* base = a.base;
  for (uint32_t i = 0; i < kBytes; i += 8) {
    const uint64_t value = random();
    std::memcpy(base + i, &value, 8);
  }
  PPCContext initial{};
  auto* state = reinterpret_cast<uint8_t*>(&initial);
  for (size_t i = 0; i < sizeof(initial); ++i) state[i] = uint8_t(random());
  initial.r1.u64 = 0x40000;
  initial.r4.u64 = (random() & 0xFFFFFFFF00000000ull) | uint32_t(n);
  initial.r5.u64 = 0x10000;
  initial.r6.u64 = 0x20000;
  initial.r7.u64 = 0x30000;
  initial.r8.u64 = 0x30008;
  initial.r9.u64 = 0x10000 + (random() & 0xFFFF);
  if (mode == 1) initial.r6.u64 = initial.r5.u64 + 4;  // Forward source/output alias.
  if (mode == 2) initial.r6.u64 = initial.r5.u64;      // In-place.
  if (mode == 3) initial.r5.u64 = initial.r1.u64 - 16; // Source reads guest scratch.
  if (mode == 4) initial.r6.u64 = initial.r1.u64 - 16; // Output overwrites scratch/save.
  if (mode == 5) initial.r7.u64 = initial.r8.u64;      // Aliased state writes.
  if (mode == 6) initial.r7.u64 = initial.r6.u64;      // State inside output.
  if (mode == 7) initial.r5.u64 |= 0xF000000000000000ull;
  if (mode == 9) initial.r9.u64 |= random() & 0xFFFFFFFF00000000ull;
  if (mode == 10) initial.r9.u64 = 0;
  const uint32_t pos = mode == 3 ? 0 : mode == 8 ? 0x40000001 : uint32_t(random() % 8);
  REX_STORE_U32(initial.r7.u32, pos);
  REX_STORE_U16(initial.r8.u32, mode == 5 ? 0 : uint16_t(random()));
  const std::array<uint32_t, 8> constants{0x37800000, 0, 0x80000000, 1, 0x7F800000, 0x7FC12345, 0xFF800000, 0x3F800000};
  REX_STORE_U32(0x820C7E18u, constants[random() % constants.size()]);
  std::memcpy(b.base, a.base, kBytes);
  std::memcpy(b.base + kConstantPage, a.base + kConstantPage, 4096);
  PPCContext original = initial, candidate = initial;
  SetFp(HostFp() & ~0x3Fu);  // Start each x86 case with clear exception-status flags.
  original.fpscr.InitHost();
  original.fpscr.storeFromGuest(rounding);
  if (flush) original.fpscr.enableFlushMode();
  else original.fpscr.disableFlushMode();
  candidate.fpscr = original.fpscr;
  const uint32_t host_before = HostFp();
  Original(original, a.base);
  const uint32_t original_host = HostFp();
  SetFp(host_before);
  Candidate(candidate, b.base);
  const uint32_t candidate_host = HostFp();
  assert(std::memcmp(&original, &candidate, sizeof(original)) == 0);
  assert(original_host == candidate_host);
  assert(std::memcmp(a.base, b.base, kBytes) == 0);
}

template <typename Function>
double Benchmark(Function function, Memory& memory) {
  auto* base = memory.base;
  REX_STORE_U32(0x820C7E18u, 0x37800000);
  for (uint32_t i = 0; i < 4096; ++i) REX_STORE_U32(0x10000 + i * 4, 0x3E800000);
  PPCContext ctx{};
  ctx.fpscr.InitHost();
  ctx.fpscr.storeFromGuest(0);
  ctx.fpscr.disableFlushMode();
  const auto start = std::chrono::steady_clock::now();
  for (unsigned call = 0; call < 40000; ++call) {
    ctx.r1.u64 = 0x40000; ctx.r4.u64 = 256; ctx.r5.u64 = 0x10000;
    ctx.r6.u64 = 0x20000; ctx.r7.u64 = 0x30000; ctx.r8.u64 = 0x30008; ctx.r9.u64 = 0x18000;
    REX_STORE_U32(ctx.r7.u32, 0); REX_STORE_U32(ctx.r8.u32, 0);
    function(ctx, base);
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

int main() {
  Memory original, candidate;
  std::mt19937_64 random(0x826CB1D8);
  unsigned cases = 0;
  for (int n : {-5, 0, 1, 2, 63, 256, 1024})
    for (unsigned mode = 0; mode < 11; ++mode)
      for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (bool flush : {false, true}) {
          CheckCase(random, original, candidate, n, mode, rounding, flush);
          ++cases;
        }
  std::printf("PASS %u differential cases: all context bytes, guest writes, host FP state; aliases/NaNs/rounding/FTZ\n", cases);
  const double before = Benchmark(ReferenceResample, original);
  const double after = Benchmark(carbon::audio::Resample, candidate);
  std::printf("Host-only 10,240,000 samples: original %.3f ms, candidate %.3f ms, ratio %.3fx; NOT a Switch timing\n", before, after, before / after);
}
