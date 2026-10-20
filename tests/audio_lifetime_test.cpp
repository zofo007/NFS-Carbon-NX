#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <array>

using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define assert_true(x) CHECK(x)
#define assert_not_null(x) CHECK(x)
#define REXAPU_WARN(...) ((void)0)
#define REXAPU_ERROR(...) ((void)0)
#define REXAPU_DEBUG(...) ((void)0)
#define REXAPU_NOISY_DEBUG(...) ((void)0)
#define SCOPE_profile_cpu_i(...) ((void)0)
#define SCOPE_profile_cpu_f(...) ((void)0)
#define XFAILED(x) (((x) & 0xC0000000u) != 0)
using X_STATUS = uint32_t;
constexpr X_STATUS X_STATUS_SUCCESS = 0, X_STATUS_NO_MEMORY = 0xC0000017u,
                   X_STATUS_UNSUCCESSFUL = 0xC0000001u;
constexpr auto kWorkerShutdownTimeout = 500ms;
constexpr uint32_t kAudioSaveSignature = 0x58415544;

struct Gate {
  std::mutex mutex;
  std::condition_variable cv;
  bool open = false;
  void Set() { std::lock_guard lock(mutex); open = true; cv.notify_all(); }
  void Wait() { std::unique_lock lock(mutex); CHECK(cv.wait_for(lock, 3s, [&] { return open; })); }
};

namespace rex {
template <typename T, size_t N> constexpr size_t countof(T (&)[N]) { return N; }
[[noreturn]] void FatalError(const char* message) { std::fprintf(stderr, "%s\n", message); std::exit(2); }
namespace thread {
enum class WaitResult { kSuccess, kFailed, kTimeout };
struct WaitHandle { virtual ~WaitHandle() = default; virtual bool Poll() = 0; };
struct Semaphore : WaitHandle {
  std::mutex mutex;
  uint32_t count = 0;
  bool Release(uint32_t n, void*) { std::lock_guard lock(mutex); count += n; return true; }
  bool Poll() override { std::lock_guard lock(mutex); if (!count) return false; --count; return true; }
};
struct Event : WaitHandle {
  std::atomic<bool> signaled{false};
  void Set() { signaled = true; }
  bool Poll() override { return signaled.exchange(false); }
};
struct Fence { Gate gate; void Signal() { gate.Set(); } };
struct Thread : WaitHandle {
  std::thread native;
  std::atomic<bool> done{false}, terminated{false};
  std::atomic<unsigned> waits{0};
  bool Poll() override { return done; }
  ~Thread() { if (native.joinable()) native.join(); }
};
WaitResult Wait(WaitHandle* handle, bool, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  if (auto* thread = dynamic_cast<Thread*>(handle)) ++thread->waits;
  const auto end = timeout == std::chrono::milliseconds::max() ? std::chrono::steady_clock::time_point::max()
                                                             : std::chrono::steady_clock::now() + timeout;
  do {
    if (handle->Poll()) return WaitResult::kSuccess;
    if (std::chrono::steady_clock::now() >= end) return WaitResult::kTimeout;
    std::this_thread::sleep_for(100us);
  } while (true);
}
std::pair<WaitResult, size_t> WaitAny(WaitHandle* handles[], size_t count, bool,
                                    std::chrono::milliseconds timeout) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  do {
    for (size_t i = 0; i < count; ++i) if (handles[i]->Poll()) return {WaitResult::kSuccess, i};
    std::this_thread::sleep_for(100us);
  } while (std::chrono::steady_clock::now() < end);
  return {WaitResult::kTimeout, 0};
}
void Sleep(std::chrono::milliseconds time) { std::this_thread::sleep_for(time); }
struct global_critical_region {
  static inline std::recursive_mutex mutex;
  static inline std::function<void()> acquired;
  auto Acquire() {
    std::unique_lock lock(mutex);
    if (acquired) acquired();
    return lock;
  }
};
}
namespace system {
struct XHostThread;
inline thread_local XHostThread* current = nullptr;
struct XHostThread {
  rex::thread::Thread host;
  auto* thread() { return &host; }
  void* thread_state() { return this; }
  void Terminate(int) { host.terminated = true; }  // Horizon reports unsupported; the thread keeps running.
};
struct XThread { static bool IsInThread(XHostThread* other) { return current == other; } };
}
namespace memory {
template <typename T> void store_and_swap(uint8_t* p, T value) { std::memcpy(p, &value, sizeof(value)); }
}
namespace stream {
struct ByteStream {
  unsigned reads = 0, writes = 0;
  template <typename T> void Write(T) { ++writes; }
  template <typename T> T Read() { ++reads; return {}; }
};
}
}

struct Memory {
  uint32_t next = 4;
  bool fail = false;
  std::unordered_map<uint32_t, std::array<uint8_t, 4>> allocations;
  uint32_t SystemHeapAlloc(uint32_t n) { CHECK(n == 4); if (fail) return 0; const auto p = next; next += 4; allocations[p] = {}; return p; }
  void SystemHeapFree(uint32_t p) { CHECK(allocations.erase(p) == 1); }
  uint8_t* TranslateVirtual(uint32_t p) { return allocations.at(p).data(); }
  bool Alive(uint32_t p) { return allocations.contains(p); }
};
struct AudioDriver { unsigned submitted = 0; void SubmitFrame(uint32_t) { ++submitted; } };
struct Decoder {
  std::function<void()> on_shutdown;
  void Shutdown() { if (on_shutdown) on_shutdown(); }
};
struct Dispatcher {
  std::function<void(uint32_t)> callback;
  void Execute(void*, uint32_t, uint64_t args[], size_t count) { CHECK(count == 1); callback(uint32_t(args[0])); }
};
using GanchoAntesDeTrama = void (*)(size_t);
std::atomic<GanchoAntesDeTrama> g_gancho_antes_de_trama{nullptr};
Gate selected, release_callback;
void BeforeFrame(size_t) { selected.Set(); release_callback.Wait(); }

namespace rex::audio {
struct XmaDecoder {
  std::shared_ptr<system::XHostThread> worker_thread_ = std::make_shared<system::XHostThread>();
  std::unique_ptr<thread::Event> work_event_ = std::make_unique<thread::Event>();
  bool worker_running_ = true, paused_ = true;
  Memory storage_;
  Memory* memory() { return &storage_; }
  uint32_t context_data_first_ptr_ = 0, context_data_last_ptr_ = 0;
  unsigned resumes = 0;
  void Resume() { paused_ = false; ++resumes; }
  void Shutdown();
};
struct AudioSystem {
  static constexpr size_t kMaximumClientCount = 8;
  rex::thread::global_critical_region global_critical_region_;
#include "client.inc"
  std::condition_variable_any client_callbacks_done_;
  std::unique_ptr<rex::thread::Semaphore> client_semaphores_[kMaximumClientCount];
  std::unique_ptr<rex::thread::Event> shutdown_event_ = std::make_unique<rex::thread::Event>();
  std::unique_ptr<rex::thread::Event> resume_event_ = std::make_unique<rex::thread::Event>();
  rex::thread::WaitHandle* wait_handles_[kMaximumClientCount + 1];
  rex::thread::Fence pause_fence_;
  std::atomic<bool> worker_running_{true}, paused_{false};
  std::shared_ptr<rex::system::XHostThread> worker_thread_ = std::make_shared<rex::system::XHostThread>();
  std::unique_ptr<Decoder> xma_decoder_ = std::make_unique<Decoder>();
  uint32_t queued_frames_ = 4;
  Memory memory_;
  Memory* memory() { return &memory_; }
  Dispatcher dispatcher_;
  Dispatcher* function_dispatcher_ = &dispatcher_;
  unsigned destroyed = 0;
  bool fail_driver = false;
  void Initialize() {}
  X_STATUS CreateDriver(size_t, rex::thread::Semaphore*, AudioDriver** out) {
    if (fail_driver) return X_STATUS_UNSUCCESSFUL;
    *out = new AudioDriver; return 0;
  }
  void DestroyDriver(AudioDriver* driver) { CHECK(driver); delete driver; ++destroyed; }
  AudioSystem() {
    std::memset(clients_, 0, sizeof(clients_));
    for (size_t i = 0; i < kMaximumClientCount; ++i) {
      client_semaphores_[i] = std::make_unique<rex::thread::Semaphore>();
      wait_handles_[i] = client_semaphores_[i].get();
    }
    wait_handles_[kMaximumClientCount] = shutdown_event_.get();
  }
#include "declarations.inc"
  void StartWorker() {
    auto host = worker_thread_;
    host->host.native = std::thread([this, host] {
      rex::system::current = host.get();
      WorkerThreadMain();
      host->host.done = true;
      rex::system::current = nullptr;
    });
  }
};
#include "methods.inc"
}  // namespace rex::audio
using rex::audio::AudioSystem;

void Await(const std::function<bool()>& predicate) {
  const auto end = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::sleep_for(100us); }
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "xma") {
    rex::audio::XmaDecoder decoder;
    const auto data = decoder.storage_.SystemHeapAlloc(4);
    decoder.context_data_first_ptr_ = decoder.context_data_last_ptr_ = data;
    auto host = decoder.worker_thread_;
    Gate release_worker;
    host->host.native = std::thread([&] { release_worker.Wait(); host->host.done = true; });
    std::atomic<bool> returned{false};
    std::thread shutdown([&] { decoder.Shutdown(); returned = true; });
    Await([&] { return returned.load() || host->host.waits >= 2; });
    CHECK(!returned && decoder.storage_.Alive(data) && !host->host.terminated);
    release_worker.Set();
    shutdown.join();
    CHECK(host->host.done && decoder.storage_.allocations.empty() && decoder.resumes == 1);
    CHECK(!decoder.worker_thread_ && decoder.context_data_first_ptr_ == 0 && decoder.context_data_last_ptr_ == 0);
    std::puts("PASS: XMA context survives timeout until worker completion");
    return 0;
  }
  AudioSystem audio;
  size_t index = 99;
  CHECK(audio.RegisterClient(0x1000, 0x2000, &index) == 0 && index == 0);
  const auto arg = audio.clients_[index].wrapped_callback_arg;
  uint32_t expected_arg = arg;
  auto host = audio.worker_thread_;
  unsigned calls = 0;
  audio.dispatcher_.callback = [&](uint32_t p) {
    CHECK(p == expected_arg && audio.memory_.Alive(p));
    ++calls;
    if (scenario == "executing" || scenario == "snapshot") { selected.Set(); release_callback.Wait(); }
    if (scenario == "self_external") {
      audio.UnregisterClient(index);
      selected.Set();
      release_callback.Wait();
    }
    if (scenario == "self") {
      audio.UnregisterClient(index);
      CHECK(audio.memory_.Alive(p) && audio.destroyed == 0);
      size_t next;
      CHECK(audio.RegisterClient(0x3000, 0x4000, &next) == 0 && next != index);
    }
    audio.SubmitFrame(index, 0x5000);
    audio.worker_running_ = false;
  };

  if (scenario == "self") {
    audio.StartWorker();
    Await([&] { return host->host.done.load(); });
    CHECK(calls == 1 && audio.destroyed == 1 && !audio.memory_.Alive(arg));
    audio.Shutdown();
    CHECK(audio.destroyed == 2 && audio.memory_.allocations.empty());
  } else if (scenario == "selected" || scenario == "executing" || scenario == "self_external" || scenario == "reregister") {
    if (scenario == "selected" || scenario == "reregister") g_gancho_antes_de_trama = BeforeFrame;
    audio.StartWorker();
    selected.Wait();
    Gate unregister_locked;
    std::atomic<bool> returned{false};
    std::thread unregister([&] {
      rex::thread::global_critical_region::acquired = [&] { unregister_locked.Set(); };
      audio.UnregisterClient(index);
      returned = true;
    });
    unregister_locked.Wait();
    {
      std::lock_guard lock(rex::thread::global_critical_region::mutex);
      rex::thread::global_critical_region::acquired = {};
      CHECK(audio.memory_.Alive(arg) && audio.destroyed == 0 && !returned);
    }
    release_callback.Set();
    unregister.join();
    if (scenario == "reregister") {
      {
        std::lock_guard lock(rex::thread::global_critical_region::mutex);
        g_gancho_antes_de_trama = nullptr;
        size_t reused = 99;
        CHECK(audio.RegisterClient(0x3000, 0x4000, &reused) == 0 && reused == index);
        expected_arg = audio.clients_[reused].wrapped_callback_arg;
        CHECK(expected_arg != arg);
      }
      Await([&] { return host->host.done.load(); });
      audio.Shutdown();
      CHECK(calls == 1 && audio.destroyed == 2 && audio.memory_.allocations.empty());
      std::puts("PASS: old callback skipped, retired slot reused for the new callback only");
      return 0;
    }
    audio.worker_running_ = false;
    audio.shutdown_event_->Set();
    audio.Shutdown();
    Await([&] { return host->host.done.load(); });
    CHECK(audio.destroyed == 1 && audio.memory_.allocations.empty());
    CHECK(calls == (scenario == "selected" ? 0u : 1u));
  } else if (scenario == "snapshot") {
    audio.StartWorker();
    selected.Wait();
    rex::stream::ByteStream stream;
    CHECK(!audio.Save(&stream) && stream.writes == 0);
    CHECK(!audio.Restore(&stream) && stream.reads == 0);
    release_callback.Set();
    Await([&] { return host->host.done.load(); });
    audio.Shutdown();
    CHECK(audio.destroyed == 1 && audio.memory_.allocations.empty());
  } else if (scenario == "registration") {
    size_t untouched = 99;
    audio.fail_driver = true;
    CHECK(audio.RegisterClient(1, 2, &untouched) == X_STATUS_UNSUCCESSFUL && untouched == 99);
    CHECK(!audio.client_semaphores_[1]->Poll() && !audio.clients_[1].in_use);
    audio.fail_driver = false;
    audio.memory_.fail = true;
    CHECK(audio.RegisterClient(1, 2, &untouched) == X_STATUS_NO_MEMORY && untouched == 99);
    CHECK(!audio.client_semaphores_[1]->Poll() && !audio.clients_[1].in_use && audio.destroyed == 1);
    audio.memory_.fail = false;
    CHECK(audio.RegisterClient(1, 2, &untouched) == 0 && untouched == 1);
    audio.UnregisterClient(untouched);
    audio.worker_running_ = false;
    audio.StartWorker();
    Await([&] { return host->host.done.load(); });
    audio.Shutdown();
    CHECK(audio.destroyed == 3 && audio.memory_.allocations.empty());
  } else if (scenario == "capacity") {
    for (size_t i = 1; i < 8; ++i) {
      size_t next;
      CHECK(audio.RegisterClient(1, 2, &next) == 0 && next == i);
    }
    size_t untouched = 99;
    CHECK(audio.RegisterClient(1, 2, &untouched) == X_STATUS_NO_MEMORY && untouched == 99);
    audio.worker_running_ = false;
    audio.StartWorker();
    Await([&] { return host->host.done.load(); });
    audio.Shutdown();
    CHECK(audio.destroyed == 8 && audio.memory_.allocations.empty());
  } else if (scenario == "shutdown" || scenario == "shutdown_order") {
    g_gancho_antes_de_trama = BeforeFrame;
    audio.StartWorker();
    selected.Wait();
    if (scenario == "shutdown_order") {
      audio.xma_decoder_->on_shutdown = [&] { CHECK(host->host.done); };
    }
    std::atomic<bool> returned{false};
    std::thread shutdown([&] { audio.Shutdown(); returned = true; });
    Await([&] { return host->host.terminated.load() || host->host.waits >= 2; });
    {
      std::lock_guard lock(rex::thread::global_critical_region::mutex);
      CHECK(audio.memory_.Alive(arg) && audio.destroyed == 0 && !returned && !host->host.terminated);
    }
    release_callback.Set();
    shutdown.join();
    CHECK(host->host.done && calls == 0 && audio.destroyed == 1 && audio.memory_.allocations.empty());
  } else if (scenario == "paused") {
    audio.paused_ = true;
    // Avoid the primed client semaphore so the worker reaches its pause wait.
    while (audio.client_semaphores_[index]->Poll()) {}
    audio.shutdown_event_->Set();
    audio.StartWorker();
    audio.pause_fence_.gate.Wait();
    audio.Shutdown();
    CHECK(host->host.done && !host->host.terminated && audio.destroyed == 1);
  } else if (scenario == "finished") {
    audio.worker_running_ = false;
    audio.StartWorker();
    Await([&] { return host->host.done.load(); });
    audio.Shutdown();
    CHECK(audio.destroyed == 1 && audio.memory_.allocations.empty());
  } else { CHECK(false); }
  std::printf("PASS: %s\n", scenario.c_str());
}
