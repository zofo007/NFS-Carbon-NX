#include "../carbon-source/generated/default/nfscarbon_pch.h"
#include "audio_filter_8bb0.h"
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

namespace {
constexpr uint32_t kBytes = 1 << 20, kGuestCallback = uint32_t(REX_CODE_BASE);
constexpr uint32_t kFallbackCallback = 0x90000000, kStack = 0x40000;
constexpr uint64_t kSlot = REX_IMAGE_BASE + REX_IMAGE_SIZE;
PPCContext callback_entry{};
unsigned callback_calls = 0, resolver_calls = 0, cases = 0;
int current_n = 0;
unsigned current_alias = 0, current_callback = 0;

// A real dispatch-table callback; exercise its entry state, mutation and early-return contract.
void Callback(PPCContext& ctx, uint8_t* base) {
  callback_entry = ctx;
  ++callback_calls;
  const uint32_t node = ctx.r3.u32;
  const uint64_t result = REX_LOAD_U64(node + 8);
  const uint32_t object = REX_LOAD_U32(node + 16);
  assert(ctx.r7.u64 == 0xA5 && ctx.lr == 0x826C8BF4);
  REX_STORE_U32(object + 32, 0x3F000000);
  REX_STORE_U32(ctx.r6.u32, 0x3F000000);
  ctx.r4.u64 = 0xAB12345600000001; ctx.r5.u64 = 0xBC12345600000002;
  ctx.r6.u64 = 0xCD12345600000003; ctx.r8.u64 = 0xDE12345600000004;
  ctx.r9.u64 = 0xEF12345600000005; ctx.r10.u64 = 0xF012345600000006;
  ctx.r11.u64 = 0xA012345600000007; ctx.r12.u64 = 0xB012345600000008;
  ctx.lr = 0xC012345678901234;
  ctx.f0.f64 = 1; ctx.f12.f64 = 2; ctx.f13.f64 = 3;
  ctx.fpscr.enableFlushMode();
  ctx.r3.u64 = result;
}

struct Memory {
  uint8_t* base = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS));
  Memory() {
    assert(base && VirtualAlloc(base,kBytes,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(base + kSlot,4096,MEM_COMMIT,PAGE_READWRITE));
    *reinterpret_cast<PPCFunc**>(base + kSlot) = Callback;
  }
  ~Memory() { VirtualFree(base,0,MEM_RELEASE); }
};
uint32_t HostFp() { return PPCFPSCRRegister::Platform::getcsr(); }
void SetFp(uint32_t value) { PPCFPSCRRegister::Platform::setcsr(value); }
}
// Only the cross-module resolver is substituted; the generated dispatch macro/table are real.
namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t address) {
  assert(address == kFallbackCallback);
  ++resolver_calls;
  return Callback;
}
}
#include "reference.inc"

namespace {
[[gnu::noinline]] void Original(PPCContext& ctx, uint8_t* base) { Reference8BB0(ctx,base); }
[[gnu::noinline]] void Candidate(PPCContext& ctx, uint8_t* base) { carbon::audio::Filter8BB0(ctx,base); }

void Compare(PPCContext& a, PPCContext& b, Memory& x, Memory& y) {
  const auto before = HostFp();
  callback_calls = resolver_calls = 0;
  Original(a,x.base);
  const auto original_fp = HostFp(), calls = callback_calls, resolves = resolver_calls;
  const PPCContext entry = callback_entry;
  SetFp(before);
  callback_calls = resolver_calls = 0;
  Candidate(b,y.base);
  if (std::memcmp(&a,&b,sizeof(a)) || original_fp != HostFp() ||
      std::memcmp(x.base,y.base,kBytes) || calls != callback_calls || resolves != resolver_calls ||
      (calls && std::memcmp(&entry,&callback_entry,sizeof(entry)))) {
    std::printf("FAIL case%u n%d alias%u callback%u ctx%d mem%d FP%08x/%08x calls%u/%u\n",
                cases,current_n,current_alias,current_callback,std::memcmp(&a,&b,sizeof(a)),
                std::memcmp(x.base,y.base,kBytes),original_fp,HostFp(),calls,callback_calls);
    std::fflush(stdout); std::exit(1);
  }
  ++cases;
}

void Check(std::mt19937_64& rng, Memory& x, Memory& y, int n, unsigned alias,
           unsigned cb, unsigned rounding, bool flush, uint32_t pattern) {
  current_n = n; current_alias = alias; current_callback = cb;
  auto* base = x.base;
  std::memset(base,0,kBytes);
  PPCContext a{};
  for (size_t i=0;i<sizeof(a);++i) reinterpret_cast<uint8_t*>(&a)[i] = uint8_t(rng());
  a.r1.u64 = kStack; a.r3.u64 = 0x30000;
  a.r4.u64 = (rng() & 0xFFFFFFFF00000000ull) | uint32_t(n);
  a.r5.u64 = 0x10000; a.r6.u64 = 0x20000;
  if (alias == 1) a.r6.u64 = a.r5.u64;
  if (alias == 2) a.r6.u64 = a.r5.u64 + 4;
  if (alias == 3) a.r6.u64 = a.r5.u64 - 4;
  if (alias >= 4 && alias <= 6) a.r6.u64 = a.r3.u64 + 28 + (alias-4)*4;
  if (alias == 7) a.r5.u64 = a.r3.u64 + 28;
  if (alias == 8) a.r3.u64 = kStack - 128 - 28;
  if (alias == 9) a.r3.u64 = kStack - 128 - 8; // Backchain overwrites callback-node field.
  if (alias == 10) {
    a.r1.u64 |= 0x1234000000000000ull; a.r3.u64 |= 0x5678000000000000ull;
    a.r5.u64 |= 0x6789000000000000ull; a.r6.u64 |= 0x789A000000000000ull;
  }
  if (alias == 11) a.r5.u64 = kStack - 128;
  if (alias == 12) a.r6.u64 = kStack - 128;
  for (uint32_t i=0;i<1024;++i) REX_STORE_U32(a.r5.u32+i*4,(i&1)?pattern^0x80023456u:pattern);
  REX_STORE_U32(a.r3.u32+28,pattern);
  REX_STORE_U32(a.r3.u32+32,(alias&1)?pattern^0x00456789u:0x3F000000u);
  REX_STORE_U32(a.r3.u32+36,pattern^0x00123456u);
  REX_STORE_U8(a.r3.u32+26,0xA5);
  const uint32_t node = alias == 9 ? kStack : 0x32000;
  const uint64_t result = cb == 2 ? uint64_t(0) : cb == 3 ? ~uint64_t(0) : uint64_t(1);
  REX_STORE_U32(node,cb == 4 ? kFallbackCallback : kGuestCallback);
  REX_STORE_U64(node+8,result);
  REX_STORE_U32(node+16,a.r3.u32);
  REX_STORE_U32(a.r3.u32+8,cb ? node : 0);
  std::memcpy(y.base,base,kBytes);
  SetFp(HostFp() & ~0x3Fu);
  a.fpscr.InitHost(); a.fpscr.storeFromGuest(rounding);
  if (flush) a.fpscr.enableFlushMode(); else a.fpscr.disableFlushMode();
  PPCContext b = a;
  Compare(a,b,x,y);
}

void LiteralsAndContinuity(Memory& x, Memory& y) {
  auto* base=x.base;
  std::memset(base,0,kBytes);
  PPCContext a{};
  a.fpscr.InitHost(); a.fpscr.storeFromGuest(0); a.fpscr.disableFlushMode();
  a.r1.u64=kStack; a.r3.u64=0x30000; a.r4.u64=2; a.r5.u64=0x10000; a.r6.u64=0x20000;
  REX_STORE_U32(0x3001C,0x40000000); // History 2
  REX_STORE_U32(0x30020,0x3F000000); // Feedback .5
  REX_STORE_U32(0x30024,0x3F000000); // Input gain .5
  REX_STORE_U32(0x10000,0x40800000); // Input 4
  REX_STORE_U32(0x10004,0x41000000); // Input 8
  std::memcpy(y.base,base,kBytes); PPCContext b=a;
  Compare(a,b,x,y);
  assert(REX_LOAD_U32(0x20000)==0x40400000); // 2*.5+4*.5=3
  assert(REX_LOAD_U32(0x20004)==0x40B00000); // 3*.5+8*.5=5.5
  for (unsigned call=0;call<8;++call) {
    a.r3.u64=b.r3.u64=0x30000; a.r4.u64=b.r4.u64=256;
    Compare(a,b,x,y);
  }
  a.r3.u64=0x30000; a.r4.u64=1; a.r5.u64=kBytes-4; a.r6.u64=kBytes-4;
  REX_STORE_U32(kBytes-4,0x3F800000);
  std::memcpy(y.base,base,kBytes); b=a;
  Compare(a,b,x,y); // One sample immediately before a no-access page, in place.
}

template <typename F> double Benchmark(F function,Memory& memory) {
  auto* base=memory.base;
  std::memset(base,0,kBytes);
  for (uint32_t i=0;i<1024;++i) REX_STORE_U32(0x10000+i*4,0x3E800000);
  REX_STORE_U32(0x30020,0x3F000000); REX_STORE_U32(0x30024,0x3F000000);
  PPCContext ctx{};
  ctx.fpscr.InitHost(); ctx.fpscr.storeFromGuest(0); ctx.fpscr.disableFlushMode();
  const auto start=std::chrono::steady_clock::now();
  for (unsigned i=0;i<40000;++i) {
    ctx.r1.u64=kStack; ctx.r3.u64=0x30000; ctx.r4.u64=256;
    ctx.r5.u64=0x10000; ctx.r6.u64=0x20000;
    function(ctx,base);
  }
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}

int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*)->LONG {
    std::printf("FAULT case%u n%d alias%u callback%u\n",cases,current_n,current_alias,current_callback);
    std::fflush(stdout); return EXCEPTION_EXECUTE_HANDLER;
  });
  Memory x,y; std::mt19937_64 rng(0x826C8BB0);
  LiteralsAndContinuity(x,y);
  for (int n:{-5,0,1,2,63,256,257})
    for (unsigned alias=0;alias<13;++alias)
      for (unsigned cb=0;cb<5;++cb)
        for (unsigned rounding=0;rounding<4;++rounding)
          for (bool flush:{false,true})
            for (uint32_t bits:{0u,0x80000000u,1u,0x3F800000u,0x7F800000u,0xFF800000u,0x7FC12345u,0x7FA12345u})
              Check(rng,x,y,n,alias,cb,rounding,flush,bits);
  std::printf("PASS %u differential cases: ctx/guest/FP/callback dispatch+effects/aliases/continuity/guard\n",cases);
  const auto before=Benchmark(Original,x),after=Benchmark(Candidate,y);
  std::printf("Host-only 10,240,000 samples: original %.3f ms native %.3f ms %.3fx; NOT Switch timing\n",before,after,before/after);
}
