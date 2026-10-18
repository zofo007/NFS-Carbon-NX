// Host stress test of reference-nfsmw-nx/sdk/src/audio/xma_despertar.h: a kick that only signals a
// sleeping XMA worker must never leave a marked context unserved (no lost wakeup).
#include "xma_despertar.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

namespace {
// Auto-reset event like rex::thread::Event, with a timeout that counts as a lost wakeup.
struct Event {
  std::mutex m;
  std::condition_variable cv;
  bool signaled = false;
  void Set() {
    { std::lock_guard<std::mutex> l(m); signaled = true; }
    cv.notify_one();
  }
  bool Wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> l(m);
    const bool ok = cv.wait_for(l, timeout, [&] { return signaled; });
    signaled = false;
    return ok;
  }
};
}  // namespace

// Hardware 19d24a38cf07: the worker was already waiting when the option was switched on. The first kick
// must still wake it.
void FirstKickWakesWorkerThatIsAlreadyWaiting() {
  rex::audio::DespertarXma despertar;
  assert(despertar.DebeAvisar());   // fresh state counts as asleep
  rex::audio::DespertarXma again;
  std::atomic<uint32_t> pending{0};
  assert(again.PuedeDormir([&] { return pending.load() != 0; }));
  pending.store(1);
  assert(again.DebeAvisar());       // kick after the worker declared sleep: signal
  assert(!again.DebeAvisar());      // a second kick before it wakes: no extra signal needed
  again.Despierto();
}

int main() {
  FirstKickWakesWorkerThatIsAlreadyWaiting();
  constexpr int kKickers = 3, kKicksEach = 100000;
  rex::audio::DespertarXma despertar;
  Event event;
  std::atomic<uint32_t> pending{0};   // one bit per kicker, like g_pendientes
  std::atomic<uint64_t> served{0}, sets{0};
  std::atomic<bool> running{true};
  uint64_t lost = 0, sleeps = 0;

  std::thread worker([&] {
    while (running.load()) {
      uint32_t bits = pending.exchange(0, std::memory_order_acquire);
      served.fetch_add(__builtin_popcount(bits), std::memory_order_relaxed);
      if (bits) continue;
      if (!despertar.PuedeDormir([&] { return pending.load(std::memory_order_acquire) != 0; })) continue;
      ++sleeps;
      // A marked context while asleep with no signal would stall here: 200 ms is a lost wakeup.
      if (!event.Wait(std::chrono::milliseconds(200)) && pending.load() != 0) ++lost;
      despertar.Despierto();
    }
  });
  std::vector<std::thread> kickers;
  std::atomic<uint64_t> kicked{0};
  for (int k = 0; k < kKickers; ++k) {
    kickers.emplace_back([&, k] {
      for (int i = 0; i < kKicksEach; ++i) {
        // Kick only once the previous one was served, like a context the game re-kicks after its data.
        while (pending.load(std::memory_order_acquire) & (1u << k)) std::this_thread::yield();
        pending.fetch_or(1u << k, std::memory_order_release);
        kicked.fetch_add(1, std::memory_order_relaxed);
        if (despertar.DebeAvisar()) {
          sets.fetch_add(1, std::memory_order_relaxed);
          event.Set();
        }
        if (i % 64 == 0) std::this_thread::yield();
      }
    });
  }
  for (auto& t : kickers) t.join();
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (served.load() < kicked.load() && std::chrono::steady_clock::now() < until) std::this_thread::yield();
  running.store(false);
  event.Set();
  worker.join();
  assert(lost == 0);
  assert(served.load() == kicked.load());
  std::printf("PASS xma wake-if-asleep: %llu kicks served, %llu signals (%.1f%%), %llu worker sleeps, 0 lost wakeups\n",
              static_cast<unsigned long long>(kicked.load()), static_cast<unsigned long long>(sets.load()),
              100.0 * double(sets.load()) / double(kicked.load()), static_cast<unsigned long long>(sleeps));
}
