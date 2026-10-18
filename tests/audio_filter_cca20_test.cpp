#include "../carbon-source/generated/default/nfscarbon_pch.h"
#include "audio_filter_cca20.h"
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
[[gnu::noinline]] void Original(PPCContext& ctx, uint8_t* base) { ReferenceCCA20(ctx, base); }
[[gnu::noinline]] void Candidate(PPCContext& ctx, uint8_t* base) { carbon::audio::FilterCCA20(ctx, base); }
constexpr uint32_t kBytes = 8 << 20, kConstantPage = 0x820C7000, kAntiDenormal = 0x820C7F28;
// Guest layout used by the cases: filter object r4 holds the input/output block descriptor
// pointers at +0x3000C/+0x30010; a descriptor has the data pointer at +4 and the channel stride
// (floats) at +14. r3 is the per-channel history, r5 the five coefficients.
constexpr uint32_t kState = 0x60000, kCoeffs = 0x61000, kObject = 0x100000, kInDesc = 0x200000,
                   kOutDesc = 0x200020, kIn = 0x300000, kOut = 0x380000;
struct Memory {
  uint8_t* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
  Memory() {
    assert(base && VirtualAlloc(base, kBytes, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + kConstantPage, 4096, MEM_COMMIT, PAGE_READWRITE));
  }
  ~Memory() { VirtualFree(base, 0, MEM_RELEASE); }
};
uint32_t HostFp() { return PPCFPSCRRegister::Platform::getcsr(); }
void SetFp(uint32_t v) { PPCFPSCRRegister::Platform::setcsr(v); }
unsigned cases = 0;
// GCC compiles the original's -fma(a, b, -c) either as one negated FMA or as FMA + negate, which
// gives a NaN result opposite signs; that choice is instruction selection, not semantics. So two
// NaNs with the same payload may differ in the sign bit only; everything else must be bit-exact.
unsigned nan_sign_only = 0;
template <typename T> bool SameBits(T u, T v) {
  if (u == v) return true;
  constexpr T sign = T(1) << (sizeof(T) * 8 - 1);
  constexpr T exponent = sizeof(T) == 4 ? T(0x7F800000u) : T(0x7FF0000000000000ull);
  const bool nan = (u & exponent) == exponent && (u & ~(sign | exponent)) != 0;
  if (nan && (u ^ v) == sign) {
    ++nan_sign_only;
    return true;
  }
  return false;
}
bool SameMemory(const uint8_t* x, const uint8_t* y, uint32_t bytes) {
  for (uint32_t i = 0; i < bytes; i += 4) {
    uint32_t u, v;
    std::memcpy(&u, x + i, 4);
    std::memcpy(&v, y + i, 4);
    if (!SameBits(__builtin_bswap32(u), __builtin_bswap32(v))) return false;
  }
  return true;
}
bool SameContext(const PPCContext& a, const PPCContext& b) {
  PPCContext c = b;
  const PPCRegister* fa = &a.f0;
  PPCRegister* fc = &c.f0;
  for (int i = 0; i < 32; ++i)
    if (fa[i].u64 != fc[i].u64 && SameBits(fa[i].u64, fc[i].u64)) fc[i].u64 = fa[i].u64;
  return std::memcmp(&a, &c, sizeof(a)) == 0;
}

void Compare(PPCContext& a, PPCContext& b, Memory& x, Memory& y) {
  std::memcpy(y.base, x.base, kBytes);
  std::memcpy(y.base + kConstantPage, x.base + kConstantPage, 4096);
  SetFp(HostFp() & ~0x3Fu);
  const auto before = HostFp();
  Original(a, x.base);
  const auto original_fp = HostFp();
  SetFp(before);
  Candidate(b, y.base);
  if (!SameContext(a, b) || original_fp != HostFp() || !SameMemory(x.base, y.base, kBytes) ||
      std::memcmp(x.base + kConstantPage, y.base + kConstantPage, 4096)) {
    std::printf("FAIL case %u: context=%d fp=%08x/%08x memory=%d\n", cases, std::memcmp(&a, &b, sizeof(a)),
                original_fp, HostFp(), std::memcmp(x.base, y.base, kBytes));
    for (size_t i = 0; i < sizeof(a); ++i)
      if (reinterpret_cast<uint8_t*>(&a)[i] != reinterpret_cast<uint8_t*>(&b)[i]) {
        std::printf("  first differing context byte %zu\n", i);
        break;
      }
    for (uint32_t i = 0; i < kBytes; i += 4)
      if (std::memcmp(x.base + i, y.base + i, 4)) {
        uint32_t u, v;
        std::memcpy(&u, x.base + i, 4);
        std::memcpy(&v, y.base + i, 4);
        std::printf("  first differing word 0x%x: %08x vs %08x\n", i, __builtin_bswap32(u), __builtin_bswap32(v));
        break;
      }
    std::exit(1);
  }
  ++cases;
}

PPCContext Setup(std::mt19937_64& rng, Memory& x, unsigned channels, unsigned alias, unsigned rounding,
                 bool flush, uint32_t pattern) {
  auto* base = x.base;
  std::uniform_real_distribution<float> sample(-1.0f, 1.0f);
  const auto f = [&] {
    const uint64_t pick = rng() % 16;
    if (pick == 0) return pattern;
    float v = sample(rng);
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return bits;
  };
  for (uint32_t at = kState; at < kState + 0x200; at += 4) REX_STORE_U32(at, f());
  for (uint32_t at = kIn; at < kOut + 0x40000; at += 4) REX_STORE_U32(at, f());
  for (uint32_t i = 0; i < 5; ++i) REX_STORE_U32(kCoeffs + 4 * i, f());
  REX_STORE_U32(kAntiDenormal, alias == 9 ? pattern : 0x1E3CE508);
  const uint16_t stride = alias == 4 ? 0 : uint16_t(256 + rng() % 64);
  uint32_t in_desc = kInDesc, out_desc = kOutDesc, in = kIn, out = kOut;
  if (alias == 1) out = in;                       // in place
  if (alias == 2) out_desc = in_desc;             // one descriptor for both
  if (alias == 3) out = in + 4;                   // output one sample ahead of input
  if (alias == 5) REX_STORE_U32(kCoeffs, 0);      // zero b0
  if (alias == 6) out = kCoeffs - 8;              // output overwrites the coefficients
  if (alias == 7) out = kState;                   // output overwrites the history
  if (alias == 8) in = kState;                    // input reads the history
  REX_STORE_U32(kObject + 0x3000C, in_desc);
  REX_STORE_U32(kObject + 0x30010, out_desc);
  REX_STORE_U32(in_desc + 4, in);
  REX_STORE_U16(in_desc + 14, stride);
  if (out_desc != in_desc) {
    REX_STORE_U32(out_desc + 4, out);
    REX_STORE_U16(out_desc + 14, uint16_t(alias == 10 ? stride + 7 : stride));
  }
  PPCContext a{};
  for (size_t i = 0; i < sizeof(a); ++i) reinterpret_cast<uint8_t*>(&a)[i] = uint8_t(rng());
  a.r1.u64 = 0x40000;
  a.r3.u64 = (rng() & 0xFFFFFFFF00000000ull) | kState;
  a.r4.u64 = (rng() & 0xFFFFFFFF00000000ull) | kObject;
  a.r5.u64 = (rng() & 0xFFFFFFFF00000000ull) | kCoeffs;
  a.r6.u64 = (rng() & 0xFFFFFFFF00000000ull) | channels;
  a.fpscr.InitHost();
  a.fpscr.storeFromGuest(rounding);
  if (flush) a.fpscr.enableFlushMode(); else a.fpscr.disableFlushMode();
  return a;
}

template <typename F> double Benchmark(F function, Memory& memory) {
  std::mt19937_64 rng(1);
  PPCContext ctx = Setup(rng, memory, 2, 0, 0, false, 0);
  auto* base = memory.base;
  // A stable low-pass (b0 b1 b2 a1 a2 as the loop reads them) so the timing is not of NaN/Inf math.
  const uint32_t coefficients[5] = {0x3E000000, 0x3E800000, 0x3E000000, 0x3F000000, 0xBE000000};
  for (uint32_t i = 0; i < 5; ++i) REX_STORE_U32(kCoeffs + 4 * i, coefficients[i]);
  for (uint32_t at = kState; at < kState + 0x200; at += 4) REX_STORE_U32(at, 0);
  const auto start = std::chrono::steady_clock::now();
  for (unsigned call = 0; call < 20000; ++call) {
    ctx.r3.u64 = kState; ctx.r4.u64 = kObject; ctx.r5.u64 = kCoeffs; ctx.r6.u64 = 2;
    function(ctx, memory.base);
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

int main() {
  Memory x, y;
  std::mt19937_64 rng(0x826CCA20);
  constexpr std::array<uint32_t, 9> values{0, 0x80000000, 1, 0x3F800000, 0x7F800000,
                                         0xFF800000, 0x7FC12345, 0x7FA12345, 0xBF123456};
  for (unsigned channels : {0u, 1u, 2u, 6u})
    for (unsigned alias = 0; alias < 11; ++alias)
      for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (bool flush : {false, true})
          for (uint32_t bits : values) {
            PPCContext a = Setup(rng, x, channels, alias, rounding, flush, bits), b = a;
            Compare(a, b, x, y);
          }
  // Consecutive blocks: the swapped descriptors and saved history feed the next call.
  {
    auto* base = x.base;
    PPCContext a = Setup(rng, x, 2, 0, 0, false, 0), b = a;
    for (unsigned call = 0; call < 8; ++call) {
      Compare(a, b, x, y);
      a.r3.u64 = b.r3.u64 = kState; a.r4.u64 = b.r4.u64 = kObject;
      a.r5.u64 = b.r5.u64 = kCoeffs; a.r6.u64 = b.r6.u64 = 2;
    }
    assert(REX_LOAD_U32(kObject + 0x3000C) == kInDesc && REX_LOAD_U32(kObject + 0x30010) == kOutDesc);
  }
  std::printf("PASS %u cases: full context, guest memory, FP state; aliases, NaNs, rounding/FTZ, chained blocks; %u NaN results "
              "differ only in sign\n", cases, nan_sign_only);
  const double after = Benchmark(Candidate, y), before = Benchmark(Original, x);
  std::printf("Host-only 20,000 two-channel blocks: original %.3f ms native %.3f ms (%.3fx); NOT Switch performance\n",
              before, after, before / after);
}
