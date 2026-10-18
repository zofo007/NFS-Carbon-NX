#include "../carbon-source/generated/default/nfscarbon_pch.h"
#include "audio_resampler_15d0.h"
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
#ifdef NDEBUG
#error Assertions must be enabled.
#endif
#include "reference.inc"

namespace {
[[gnu::noinline]] void Original(PPCContext& ctx, uint8_t* base) { Reference15D0(ctx, base); }
[[gnu::noinline]] void Candidate(PPCContext& ctx, uint8_t* base) { carbon::audio::Resample15D0(ctx, base); }
constexpr uint32_t kBytes = 8 << 20, kConstants = 0x820C7E00;
struct Memory {
  uint8_t* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
  Memory() {
    assert(base && VirtualAlloc(base, kBytes, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + (kConstants & ~4095u), 4096, MEM_COMMIT, PAGE_READWRITE));
  }
  ~Memory() { VirtualFree(base, 0, MEM_RELEASE); }
};
uint32_t HostFp() { return PPCFPSCRRegister::Platform::getcsr(); }
void SetFp(uint32_t v) { PPCFPSCRRegister::Platform::setcsr(v); }
unsigned cases = 0;
int current_n = 0;
unsigned current_alias = 0;
void Compare(PPCContext& a, PPCContext& b, Memory& x, Memory& y) {
  const auto before = HostFp();
  Original(a, x.base);
  const auto original_fp = HostFp();
  SetFp(before);
  Candidate(b, y.base);
  if (std::memcmp(&a, &b, sizeof(a)) || original_fp != HostFp() ||
      std::memcmp(x.base, y.base, kBytes) ||
      std::memcmp(x.base + (kConstants & ~4095u), y.base + (kConstants & ~4095u), 4096)) {
    std::printf("FAIL case %u: context=%d fp=%08x/%08x memory=%d\n", cases,
                std::memcmp(&a, &b, sizeof(a)), original_fp, HostFp(), std::memcmp(x.base, y.base, kBytes));
    std::fflush(stdout);
    std::exit(1);
  }
  ++cases;
}

void Check(std::mt19937_64& rng, Memory& x, Memory& y, int n, unsigned alias,
           unsigned rounding, bool flush, uint32_t pattern) {
  current_n = n; current_alias = alias;
  auto* base = x.base;
  std::memset(base, 0, kBytes);
  // Aliased position cells must remain bounded, even when their output overwrites them.
  for (uint32_t at = 0x10000; at < 0x18000; at += 4)
    REX_STORE_U32(at, alias == 3 || (alias >= 6 && alias <= 11) ? 0 :
                     (at & 4) ? pattern ^ 0x80023456u : pattern);
  PPCContext a{};
  for (size_t i = 0; i < sizeof(a); ++i) reinterpret_cast<uint8_t*>(&a)[i] = uint8_t(rng());
  a.r1.u64 = 0x40000; a.r3.u64 = (rng() & 0xFFFFFFFF00000000ull) | uint32_t(n);
  a.r4.u64 = 0x10000; a.r5.u64 = 0x20000; a.r6.u64 = 0x30000; a.r7.u64 = 0x30008;
  // r8/r9 encode the integer/fractional parts in their low/high halfwords.
  a.r8.u64 = (rng() & ~0xFFFFull) | (alias == 3 ? 0 : rng() % 3);
  a.r9.u64 = rng();
  if (alias == 1) a.r5.u64 = a.r4.u64;
  if (alias == 2) a.r5.u64 = a.r4.u64 + 4;
  if (alias == 3) a.r7.u64 = a.r6.u64;
  if (alias == 4) { a.r4.u64 = a.r1.u64 - 16; a.r8.u64 = a.r9.u64 = 0; }
  if (alias == 5) a.r5.u64 = a.r1.u64 - 16;
  if (alias == 6) a.r6.u64 = a.r5.u64;
  if (alias == 7) a.r7.u64 = a.r5.u64;
  if (alias == 8) { a.r4.u64 |= 0xABCDEF1200000000ull; a.r5.u64 |= 0x1234567800000000ull; }
  if (alias == 9) { a.r6.u64 = a.r1.u64 - 12; a.r7.u64 = a.r1.u64 - 16; a.r8.u64 = a.r9.u64 = 0; }
  if (alias == 10) a.r6.u64 = a.r4.u64;
  if (alias == 11) a.r7.u64 = a.r4.u64 + 4;
  if (alias == 12) a.r8.u64 = a.r9.u64 = ~uint64_t(0);
  if (alias == 14) a.r5.u64 = a.r4.u64 - 4;
  REX_STORE_U32(a.r6.u32, alias == 13 ? 0x40000000u : 0);
  REX_STORE_U32(a.r7.u32, uint32_t(rng() & 0xFFFF) << 16);
  REX_STORE_U32(kConstants + 0x18, pattern == 0x7FC12345 && alias != 6 ? pattern : 0x37800000);
  std::memcpy(y.base, base, kBytes);
  std::memcpy(y.base + (kConstants & ~4095u), base + (kConstants & ~4095u), 4096);
  SetFp(HostFp() & ~0x3Fu);
  a.fpscr.InitHost(); a.fpscr.storeFromGuest(rounding);
  if (flush) a.fpscr.enableFlushMode(); else a.fpscr.disableFlushMode();
  PPCContext b = a;
  Compare(a, b, x, y);
}

void LiteralAndContinuity(Memory& x, Memory& y) {
  auto* base = x.base;
  std::memset(base, 0, kBytes);
  REX_STORE_U32(kConstants + 0x18, 0x37800000); // 1/65536
  REX_STORE_U32(0x10000, 0x3F800000); // 1
  REX_STORE_U32(0x10004, 0x40400000); // 3
  REX_STORE_U32(0x10008, 0x40A00000); // 5
  REX_STORE_U32(0x30008, 0x80000000); // 0.5 fraction on entry
  PPCContext a{};
  a.fpscr.InitHost(); a.fpscr.storeFromGuest(0); a.fpscr.disableFlushMode();
  a.r1.u64 = 0x40000; a.r3.u64 = 2; a.r4.u64 = 0x10000; a.r5.u64 = 0x20000;
  a.r6.u64 = 0x30000; a.r7.u64 = 0x30008; a.r8.u64 = 1;
  std::memcpy(y.base, base, kBytes);
  std::memcpy(y.base + (kConstants & ~4095u), base + (kConstants & ~4095u), 4096);
  PPCContext b = a;
  Compare(a, b, x, y);
  assert(REX_LOAD_U32(0x20000) == 0x40000000); // 2
  assert(REX_LOAD_U32(0x20004) == 0x40800000); // 4
  assert(REX_LOAD_U32(0x30000) == 2 && REX_LOAD_U32(0x30008) == 0x80000000);
  // Return state is the next call's input: catch phase discontinuity across packets.
  for (unsigned call = 0; call < 8; ++call) {
    a.r3.u64 = b.r3.u64 = 256; a.r5.u64 = b.r5.u64 = 0x20000;
    a.r8.u64 = b.r8.u64 = 1; a.r9.u64 = b.r9.u64 = 0;
    Compare(a, b, x, y);
  }
  // Last two readable floats abut a no-access page: catch widened reads/writes.
  a = PPCContext{};
  a.fpscr.InitHost(); a.fpscr.storeFromGuest(0); a.fpscr.disableFlushMode();
  a.r1.u64 = 0x40000; a.r3.u64 = 1; a.r4.u64 = kBytes - 8; a.r5.u64 = kBytes - 4;
  a.r6.u64 = 0x30000; a.r7.u64 = 0x30008; a.r8.u64 = 1;
  REX_STORE_U32(kBytes - 8, 0x3F800000);
  REX_STORE_U32(kBytes - 4, 0x40400000);
  REX_STORE_U32(0x30000, 0); REX_STORE_U32(0x30008, 0x80000000);
  std::memcpy(y.base, base, kBytes);
  b = a;
  Compare(a, b, x, y);
  assert(REX_LOAD_U32(kBytes - 4) == 0x40000000);
}

template <typename F> double Benchmark(F function, Memory& memory) {
  auto* base = memory.base;
  REX_STORE_U32(kConstants + 0x18, 0x37800000);
  for (uint32_t i = 0; i < 4096; ++i) REX_STORE_U32(0x10000 + i * 4, 0x3E800000);
  PPCContext ctx{};
  ctx.fpscr.InitHost(); ctx.fpscr.storeFromGuest(0); ctx.fpscr.disableFlushMode();
  const auto start = std::chrono::steady_clock::now();
  for (unsigned call = 0; call < 40000; ++call) {
    ctx.r1.u64 = 0x40000; ctx.r3.u64 = 256; ctx.r4.u64 = 0x10000; ctx.r5.u64 = 0x20000;
    ctx.r6.u64 = 0x30000; ctx.r7.u64 = 0x30008; ctx.r8.u64 = 1; ctx.r9.u64 = 0x80000000;
    REX_STORE_U32(ctx.r6.u32, 0); REX_STORE_U32(ctx.r7.u32, 0x80000000);
    function(ctx, base);
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}

int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
    std::printf("FAULT case %u n=%d alias=%u at %p accessing %p\n", cases, current_n,
                current_alias, e->ExceptionRecord->ExceptionAddress,
                reinterpret_cast<void*>(e->ExceptionRecord->ExceptionInformation[1]));
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
  });
  Memory x, y;
  std::mt19937_64 rng(0x826D15D0);
  LiteralAndContinuity(x, y);
  constexpr std::array<uint32_t, 9> values{0, 0x80000000, 1, 0x3F800000, 0x7F800000,
                                         0xFF800000, 0x7FC12345, 0x7FA12345, 0xBF123456};
  for (int n : {-5, 0, 1, 2, 63, 256, 257})
    for (unsigned alias = 0; alias < 15; ++alias)
      for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (bool flush : {false, true})
          for (uint32_t bits : values)
            if (alias != 12 || n <= 2) Check(rng, x, y, n, alias, rounding, flush, bits);
  std::printf("PASS %u cases: full context, guest memory, FP state, aliases, NaNs, packet continuity\n", cases);
  const double before = Benchmark(Original, x), after = Benchmark(Candidate, y);
  std::printf("Host-only 10,240,000 samples: original %.3f ms native %.3f ms (%.3fx); NOT Switch performance\n",
              before, after, before / after);
}
