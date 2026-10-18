// Differential tests: each function in scripts/native_leaf_list.txt as generated vs the register-local
// copy scripts/localize_leaf.py makes for the build: 826D1678 (VMX gain ramp, 256 samples) and 826CD8D0
// (VMX gain-sum out += in * gain, scalar tail when unaligned). Both sides come from the current
// generated code; nothing generated is committed.
#include "../carbon-source/generated/default/nfscarbon_pch.h"
#include "native_leaf.h"
#include "audio_unpack_fast.h"
#include "audio_gain_fast.h"
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
using Function = void (*)(PPCContext&, uint8_t*);
[[gnu::noinline]] void Original1678(PPCContext& ctx, uint8_t* base) { Reference826D1678(ctx, base); }
[[gnu::noinline]] void Candidate1678(PPCContext& ctx, uint8_t* base) { carbon::leaf::Leaf1678(ctx, base); }
[[gnu::noinline]] void OriginalCD8D0(PPCContext& ctx, uint8_t* base) { Reference826CD8D0(ctx, base); }
[[gnu::noinline]] void CandidateCD8D0(PPCContext& ctx, uint8_t* base) { carbon::leaf::LeafCD8D0(ctx, base); }
[[gnu::noinline]] void OriginalCD528(PPCContext& ctx, uint8_t* base) { Reference826CD528(ctx, base); }
[[gnu::noinline]] void CandidateCD528(PPCContext& ctx, uint8_t* base) { carbon::leaf::LeafCD528(ctx, base); }
[[gnu::noinline]] void OriginalB56A8(PPCContext& ctx, uint8_t* base) { Reference826B56A8(ctx, base); }
[[gnu::noinline]] void CandidateB56A8(PPCContext& ctx, uint8_t* base) { carbon::leaf::LeafB56A8(ctx, base); }
// The fast unpack must leave memory, host FP state and everything the PPC ABI keeps (non-volatile registers,
// lr, ctr, fpscr, r1) as the original does; the volatile registers r0, r3-r12, f0-f13, v0-v13 are scratch there.
[[gnu::noinline]] void FastB56A8(PPCContext& ctx, uint8_t* base) {
  if (!carbon::audio::TryUnpackB56A8(ctx, base)) carbon::leaf::LeafB56A8(ctx, base);
}
[[gnu::noinline]] void FastCD528(PPCContext& ctx, uint8_t* base) {
  if (!carbon::audio::TryGain(ctx, base, false)) carbon::leaf::LeafCD528(ctx, base);
}
[[gnu::noinline]] void FastCD8D0(PPCContext& ctx, uint8_t* base) {
  if (!carbon::audio::TryGain(ctx, base, true)) carbon::leaf::LeafCD8D0(ctx, base);
}
constexpr uint32_t kBytes = 4 << 20, kConstants = 0x82040000, kConstantBytes = 0x90000;
constexpr uint32_t kIn = 0x100000, kOut = 0x200000;
constexpr std::array<uint32_t, 8> kSpecial{0, 0x80000000, 1, 0x00400000, 0x7F800000, 0x7FC12345, 0x7FA12345,
                                           0xBF123456};
struct Memory {
  uint8_t* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
  Memory() {
    assert(base && VirtualAlloc(base, kBytes, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + kConstants, kConstantBytes, MEM_COMMIT, PAGE_READWRITE));
  }
  ~Memory() { VirtualFree(base, 0, MEM_RELEASE); }
};
uint32_t HostFp() { return PPCFPSCRRegister::Platform::getcsr(); }
void SetFp(uint32_t v) { PPCFPSCRRegister::Platform::setcsr(v); }
unsigned cases = 0;
uint8_t* g_base_x = nullptr;
uint8_t* g_base_y = nullptr;

void Compare(Function original, Function candidate, PPCContext& a, PPCContext& b, Memory& x, Memory& y) {
  std::memcpy(y.base, x.base, kBytes);
  std::memcpy(y.base + kConstants, x.base + kConstants, kConstantBytes);
  SetFp(HostFp() & ~0x3Fu);
  const auto before = HostFp();
  original(a, x.base);
  const auto original_fp = HostFp();
  SetFp(before);
  candidate(b, y.base);
  if (std::memcmp(&a, &b, sizeof(a)) || original_fp != HostFp() || std::memcmp(x.base, y.base, kBytes) ||
      std::memcmp(x.base + kConstants, y.base + kConstants, kConstantBytes)) {
    std::printf("FAIL case %u: context=%d fp=%08x/%08x memory=%d\n", cases, std::memcmp(&a, &b, sizeof(a)),
                original_fp, HostFp(), std::memcmp(x.base, y.base, kBytes));
    std::exit(1);
  }
  ++cases;
}

#define REX_LOAD_U32_AT(b, x) __builtin_bswap32(*(volatile uint32_t*)((b) + (x)))
void ClearVolatile(PPCContext& c) {
  c.r0.u64 = 0; c.r3.u64 = 0; c.r4.u64 = 0; c.r5.u64 = 0; c.r6.u64 = 0; c.r7.u64 = 0; c.r8.u64 = 0;
  c.r9.u64 = 0; c.r10.u64 = 0; c.r11.u64 = 0; c.r12.u64 = 0;
  c.lr = 0;  // the caller sets lr before every call; the original leaves its own bl value in it
  std::memset(&c.xer, 0, sizeof(c.xer)); std::memset(&c.cr0, 0, sizeof(c.cr0)); std::memset(&c.cr1, 0, sizeof(c.cr1));
  std::memset(&c.cr5, 0, sizeof(c.cr5)); std::memset(&c.cr6, 0, sizeof(c.cr6)); std::memset(&c.cr7, 0, sizeof(c.cr7));
  c.f0.u64 = 0; c.f1.u64 = 0; c.f2.u64 = 0; c.f3.u64 = 0; c.f4.u64 = 0; c.f5.u64 = 0; c.f6.u64 = 0; c.f7.u64 = 0;
  c.f8.u64 = 0; c.f9.u64 = 0; c.f10.u64 = 0; c.f11.u64 = 0; c.f12.u64 = 0; c.f13.u64 = 0;
  std::memset(&c.v0, 0, sizeof(c.v0)); std::memset(&c.v1, 0, sizeof(c.v1)); std::memset(&c.v2, 0, sizeof(c.v2));
  std::memset(&c.v3, 0, sizeof(c.v3)); std::memset(&c.v4, 0, sizeof(c.v4)); std::memset(&c.v5, 0, sizeof(c.v5));
  std::memset(&c.v6, 0, sizeof(c.v6)); std::memset(&c.v7, 0, sizeof(c.v7)); std::memset(&c.v8, 0, sizeof(c.v8));
  std::memset(&c.v9, 0, sizeof(c.v9)); std::memset(&c.v10, 0, sizeof(c.v10)); std::memset(&c.v11, 0, sizeof(c.v11));
  std::memset(&c.v12, 0, sizeof(c.v12)); std::memset(&c.v13, 0, sizeof(c.v13));
}


void CompareAbi(Function original, Function candidate, PPCContext& a, PPCContext& b, Memory& x, Memory& y) {
  std::memcpy(y.base, x.base, kBytes);
  std::memcpy(y.base + kConstants, x.base + kConstants, kConstantBytes);
  SetFp(HostFp() & ~0x3Fu);
  const auto before = HostFp();
  const PPCContext in = b;
  original(a, x.base);
  const auto original_fp = HostFp();
  SetFp(before);
  candidate(b, y.base);
  // The original spills scratch values below r1 (dead after the call: the PPC red zone); not compared.
  std::memset(x.base + 0x80000 - 512, 0, 512);
  std::memset(y.base + 0x80000 - 512, 0, 512);
  PPCContext ca = a, cb = b;
  ClearVolatile(ca);
  ClearVolatile(cb);
  if (std::memcmp(&ca, &cb, sizeof(ca)) || original_fp != HostFp() || std::memcmp(x.base, y.base, kBytes) ||
      std::memcmp(x.base + kConstants, y.base + kConstants, kConstantBytes)) {
    std::printf("FAIL ABI case %u: context=%d fp=%08x/%08x memory=%d\n", cases, std::memcmp(&ca, &cb, sizeof(ca)),
                original_fp, HostFp(), std::memcmp(x.base, y.base, kBytes));
    for (size_t i = 0; i < sizeof(ca); ++i)
      if (reinterpret_cast<uint8_t*>(&ca)[i] != reinterpret_cast<uint8_t*>(&cb)[i]) {
        std::printf("  first diff at byte %zu (lr %zu ctr %zu fpscr %zu r1 %zu r13 %zu f14 %zu v0 %zu)\n", i,
                    offsetof(PPCContext, lr), offsetof(PPCContext, ctr), offsetof(PPCContext, fpscr),
                    offsetof(PPCContext, r1), offsetof(PPCContext, r13), offsetof(PPCContext, f14),
                    offsetof(PPCContext, v0));
        break;
      }
    for (uint32_t i = 0; i < kBytes; ++i)
      if (x.base[i] != y.base[i]) {
        std::printf("  first memory diff at guest 0x%x: original %02x candidate %02x\n", i, x.base[i], y.base[i]);
        break;
      }
    std::printf("  lr in %llx original %llx candidate %llx\n", (unsigned long long)in.lr, (unsigned long long)ca.lr,
                (unsigned long long)cb.lr);
    std::printf("  inputs: mode %u n1 %d n2 %d dst %08x src %08x off %08x f0const %08x\n", in.r8.u32, in.r9.s32,
                in.r10.s32, in.r4.u32, in.r6.u32, in.r7.u32, REX_LOAD_U32_AT(y.base, 0x820C7F24));
    std::exit(1);
  }
  ++cases;
}

struct Fill {
  std::mt19937_64& rng;
  uint32_t special;
  std::uniform_real_distribution<float> sample{-1.0f, 1.0f};
  uint32_t operator()() {
    if (rng() % 16 == 0) return special;
    float v = sample(rng);
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
  }
};

PPCContext RandomContext(std::mt19937_64& rng, unsigned rounding, bool flush) {
  PPCContext a{};
  for (size_t i = 0; i < sizeof(a); ++i) reinterpret_cast<uint8_t*>(&a)[i] = uint8_t(rng());
  a.r1.u64 = 0x80000;
  a.fpscr.InitHost();
  a.fpscr.storeFromGuest(rounding);
  if (flush) a.fpscr.enableFlushMode(); else a.fpscr.disableFlushMode();
  return a;
}

// 826CD8D0 (out += in * gain) and 826CD528 (out = in * gain): out r3, in r4, count r6, gain f1.
void GainCases(Function original, Function candidate, std::mt19937_64& rng, Memory& x, Memory& y, bool abi = false) {
  auto* base = x.base;
  for (uint32_t count : {0u, 1u, 3u, 63u, 64u, 128u, 256u, 320u})
    for (unsigned align = 0; align < 4; ++align)
      for (unsigned rounding = 0; rounding < 4; ++rounding)
        for (bool flush : {false, true})
          for (uint32_t bits : kSpecial) {
            // Count 0 on the aligned path loops until the address wraps, in the original too: never passed.
            if (count == 0 && (align == 0 || align == 3)) continue;
            Fill f{rng, bits};
            for (uint32_t at = 0; at < kBytes; at += 4) REX_STORE_U32(at, f());
            if (abi) {  // mostly clean data so the NEON path runs; sometimes one special word in the blocks
              std::uniform_real_distribution<float> clean{-1.0f, 1.0f};
              for (uint32_t at = 0; at < kBytes; at += 4) {
                const float v = clean(rng);
                uint32_t u;
                std::memcpy(&u, &v, 4);
                REX_STORE_U32(at, u);
              }
              if (rng() % 4 == 0) {
                REX_STORE_U32(kIn + 4 * (rng() % 512), kSpecial[rng() % kSpecial.size()]);
                REX_STORE_U32(kOut + 4 * (rng() % 512), kSpecial[rng() % kSpecial.size()]);
              }
            }
            PPCContext a = RandomContext(rng, rounding, flush);
            // align 0: both 128-aligned (vector path when count % 64 == 0); 1: output off by 4;
            // 2: input off by 16; 3: in place.
            a.r3.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kOut + 128 * (rng() % 64) + (align == 1 ? 4 : 0));
            a.r4.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kIn + 128 * (rng() % 64) + (align == 2 ? 16 : 0));
            if (align == 3) a.r4.u64 = a.r3.u64;
            a.r6.u64 = (rng() & 0xFFFFFFFF00000000ull) | count;
            const uint32_t gain_bits = abi && rng() % 8 ? 0 : f();
            float gain;
            std::memcpy(&gain, &gain_bits, 4);
            if (abi && rng() % 8) gain = std::uniform_real_distribution<float>{-4.0f, 4.0f}(rng);
            a.f1.f64 = gain;
            PPCContext b = a;
            if (abi) CompareAbi(original, candidate, a, b, x, y); else Compare(original, candidate, a, b, x, y);
          }
}

// 826B56A8: 16-bit samples at r6 (+ r7 offset) to floats at r4; r8 mode (1 = one path), r9/r10 counts.
void UnpackCases(std::mt19937_64& rng, Memory& x, Memory& y, bool fast = false) {
  auto* base = x.base;
  for (uint32_t mode : {0u, 1u, 2u})
    for (uint32_t n9 : {0u, 1u, 15u, 16u, 17u, 64u, 200u})
      for (uint32_t n10 : {0u, 1u, 16u, 33u, 128u})
        for (unsigned rounding = 0; rounding < 4; ++rounding)
          for (bool flush : {false, true}) {
            Fill f{rng, kSpecial[rng() % kSpecial.size()]};
            for (uint32_t at = 0; at < kBytes; at += 4) REX_STORE_U32(at, f());
            for (uint32_t at = kConstants; at < kConstants + kConstantBytes; at += 4) REX_STORE_U32(at, f());
            PPCContext a = RandomContext(rng, rounding, flush);
            a.r4.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kOut + 128 * (rng() % 64) + (rng() % 4 == 0 ? 8 : 0));
            a.r6.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kIn + 128 * (rng() % 64) + 2 * (rng() % 8));
            a.r7.u64 = (rng() & 0xFFFFFFFF00000000ull) | (2 * (rng() % 64));
            a.r8.u64 = (rng() & 0xFFFFFFFF00000000ull) | mode;
            a.r9.u64 = (rng() & 0xFFFFFFFF00000000ull) | n9;
            a.r10.u64 = (rng() & 0xFFFFFFFF00000000ull) | n10;
            a.r5.u64 = (rng() & 0xFFFFFFFF00000000ull) | (16 * (rng() % 8));  // the mode != 1 path reads it
            PPCContext b = a;
            if (fast) {
              // f0 constant: the game's value 2^-15 about 7/8 of the time, so both outcomes are exercised.
              if (rng() % 8) REX_STORE_U32(0x820C7F24, 0x38000000u);
              if (mode == 1 && rng() % 4 == 0) {  // overlap and alignment corner cases
                a.r4.u64 = (a.r4.u64 & ~0xFFFFFFFFull) | (a.r6.u32 + 2 * (rng() % 32)); b = a;
              }
              CompareAbi(OriginalB56A8, FastB56A8, a, b, x, y);
            } else {
              Compare(OriginalB56A8, CandidateB56A8, a, b, x, y);
            }
          }
}

double Time(Function function, Memory& m, uint32_t count) {
  PPCContext ctx{};
  ctx.fpscr.InitHost();
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 40000; ++i) {
    ctx.r1.u64 = 0x80000; ctx.r3.u64 = kOut; ctx.r4.u64 = kIn; ctx.r6.u64 = count;
    ctx.f1.f64 = 0.5; ctx.f2.f64 = 0.0001;
    function(ctx, m.base);
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
    const auto at = reinterpret_cast<uint8_t*>(e->ExceptionRecord->ExceptionInformation[1]);
    std::printf("FAULT case %u at %p accessing guest 0x%llx (x) / 0x%llx (y)\n", cases,
                e->ExceptionRecord->ExceptionAddress, (unsigned long long)(at - g_base_x),
                (unsigned long long)(at - g_base_y));
    return EXCEPTION_EXECUTE_HANDLER;
  });
  Memory x, y;
  g_base_x = x.base;
  g_base_y = y.base;
  std::mt19937_64 rng(0x826D1678);
  auto* base = x.base;
  for (unsigned alias = 0; alias < 5; ++alias)
    for (unsigned rounding = 0; rounding < 4; ++rounding)
      for (bool flush : {false, true})
        for (uint32_t bits : kSpecial)
          for (int repeat = 0; repeat < 4; ++repeat) {
            Fill f{rng, bits};
            for (uint32_t at = 0; at < kBytes; at += 4) REX_STORE_U32(at, f());
            for (uint32_t at = kConstants; at < kConstants + kConstantBytes; at += 4) REX_STORE_U32(at, f());
            PPCContext a = RandomContext(rng, rounding, flush);
            a.r3.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kOut + 128 * (rng() % 64));
            a.r4.u64 = (rng() & 0xFFFFFFFF00000000ull) | (kIn + 16 * (rng() % 256));
            if (alias == 1) a.r4.u64 = a.r3.u64;                           // in place
            if (alias == 2) a.r4.u64 = a.r3.u64 + 128;                     // input one line ahead
            if (alias == 3) a.r3.u64 = a.r4.u64 + 128 - (a.r4.u32 & 127);  // output inside the input
            if (alias == 4) a.r4.u64 = a.r1.u64 - 512;                     // input over the stack scratch
            float f1 = f.sample(rng), f2 = f.sample(rng) * 0.01f;
            if (rng() % 8 == 0) std::memcpy(&f1, &bits, 4);
            a.f1.f64 = f1;
            a.f2.f64 = f2;
            PPCContext b = a;
            Compare(Original1678, Candidate1678, a, b, x, y);
          }
  const unsigned ramp = cases;
  std::printf("PASS 826D1678 %u cases: full context, guest memory, constants, host FP state; aliases, NaNs, "
              "denormals, rounding/FTZ\n", ramp);
  GainCases(OriginalCD8D0, CandidateCD8D0, rng, x, y);
  std::printf("PASS 826CD8D0 %u cases: counts/alignment/in place, NaNs, denormals, rounding/FTZ\n", cases - ramp);
  unsigned before = cases;
  GainCases(OriginalCD528, CandidateCD528, rng, x, y);
  std::printf("PASS 826CD528 %u cases: counts/alignment/in place, NaNs, denormals, rounding/FTZ\n", cases - before);
  before = cases;
  UnpackCases(rng, x, y);
  std::printf("PASS 826B56A8 %u cases: modes/counts/offsets, rounding/FTZ\n", cases - before);
  before = cases;
  for (int round = 0; round < 6; ++round) UnpackCases(rng, x, y, true);
  std::printf("PASS 826B56A8 fast path %u cases: memory, host FP state, non-volatile registers, lr/ctr/fpscr\n",
              cases - before);
  before = cases;
  for (int round = 0; round < 8; ++round) {
    GainCases(OriginalCD528, FastCD528, rng, x, y, true);
    GainCases(OriginalCD8D0, FastCD8D0, rng, x, y, true);
  }
  std::printf("PASS 826CD528/826CD8D0 NEON path %u cases: memory, host FP state, non-volatile registers\n", cases - before);
  const double r0 = Time(Original1678, x, 0), r1 = Time(Candidate1678, y, 0);
  const double s0 = Time(OriginalCD8D0, x, 256), s1 = Time(CandidateCD8D0, y, 256);
  std::printf("Host-only 40,000 calls: 826D1678 %.3f -> %.3f ms (%.3fx), 826CD8D0 %.3f -> %.3f ms (%.3fx); "
              "NOT Switch performance\n", r0, r1, r0 / r1, s0, s1, s0 / s1);
}
