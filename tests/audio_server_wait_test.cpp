// Host test of carbon-source/src/audio_server_wait.h: when the Audio Worker waits for the mixer's packet.
#include "audio_server_wait.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <thread>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

using namespace carbon::audio_server;

namespace {
constexpr uint32_t kObj = 0x40000000;

int64_t RealUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, sizeof(value));
}

void SetStates(uint8_t* base, uint32_t a, uint32_t b) {
  Store32(base, kObj + kStates, a);
  Store32(base, kObj + kStates + 4, b);
}

// After `delay_ms`, the mixer marks slot 1 submitted and submits it, as sub_826C6AB0 does.
std::thread LateSubmit(uint8_t* base, Waiter& w, int delay_ms, bool mark_first) {
  return std::thread([=, &w] {
    w.BeginSubmit();  // the game marks state 2 a few instructions before; that gap is not covered
    if (mark_first) SetStates(base, 0, kSubmitted);
    const int64_t until = RealUs() + delay_ms * 1000;
    while (RealUs() < until) std::this_thread::yield();
    if (!mark_first) SetStates(base, 0, kSubmitted);
    w.EndSubmit(RealUs());
  });
}
}  // namespace

int main() {
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS));
  assert(base);
  assert(VirtualAlloc(base + (kRunning & ~0xFFFu), 0x1000, MEM_COMMIT, PAGE_READWRITE));
  assert(VirtualAlloc(base + kObj, 0x4000, MEM_COMMIT, PAGE_READWRITE));
  base[kRunning] = 1;
  base[kMixing] = 1;

  {  // No submission yet (boot, loading): never wait, even without a packet.
    Waiter w;
    SetStates(base, 0, 0);
    assert(w.Wait(base, kObj, 30, RealUs) == -1 && w.waits == 0);
  }
  {  // A packet is on the voice, the limit is 0, or the mixer is paused/stopped: no wait.
    Waiter w;
    w.BeginSubmit();
    w.EndSubmit(RealUs());
    SetStates(base, 1, kSubmitted);
    assert(w.Wait(base, kObj, 30, RealUs) == -1);
    SetStates(base, 1, 0);
    assert(w.Wait(base, kObj, 0, RealUs) == -1);
    base[kMixing] = 0;
    assert(w.Wait(base, kObj, 30, RealUs) == -1);
    base[kMixing] = 1;
    base[kRunning] = 0;
    assert(w.Wait(base, kObj, 30, RealUs) == -1);
    base[kRunning] = 1;
    const int64_t later = RealUs() + kStoppedUs + 1000;  // last submission 301 ms ago
    assert(w.Wait(base, kObj, 30, [&] { return later; }) == -1);
    assert(w.waits == 0);
  }
  {  // Late packet: waits until the submission, not until the limit.
    Waiter w;
    w.BeginSubmit();
    w.EndSubmit(RealUs());
    SetStates(base, 1, 0);
    std::thread t = LateSubmit(base, w, 5, false);
    const int64_t waited = w.Wait(base, kObj, 30, RealUs);
    t.join();
    assert(waited >= 4000 && waited < 25000 && w.waits == 1 && w.timeouts == 0);
    std::printf("late packet: waited %lld us\n", static_cast<long long>(waited));
  }
  {  // State 2 is written before SubmitPacket: keep waiting until the call returns.
    Waiter w;
    w.BeginSubmit();
    w.EndSubmit(RealUs());
    SetStates(base, 1, 0);
    std::thread t = LateSubmit(base, w, 5, true);
    while (Load32(base, kObj + kStates + 4) != kSubmitted) std::this_thread::yield();
    const int64_t waited = w.Wait(base, kObj, 30, RealUs);
    t.join();
    assert(waited >= 3000 && w.timeouts == 0 && w.submitting == 0);
  }
  {  // The mixer never delivers: the wait ends at the limit.
    Waiter w;
    w.BeginSubmit();
    w.EndSubmit(RealUs());
    SetStates(base, 1, 0);
    const int64_t waited = w.Wait(base, kObj, 10, RealUs);
    assert(waited >= 10000 && waited < 40000 && w.waits == 1 && w.timeouts == 1);
    assert(w.max_us == uint64_t(waited) && w.waited_us == uint64_t(waited));
  }
  VirtualFree(base, 0, MEM_RELEASE);
  std::printf("PASS audio server wait: no wait before first submit/with packet/off/paused/stopped; late packet, "
              "in-flight submit, timeout\n");
}
