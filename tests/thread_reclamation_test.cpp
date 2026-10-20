#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define assert_true(x) CHECK(x)
#define assert_always() CHECK(false)
#define assert_not_null(x) CHECK((x) != nullptr)
#define REX_FATAL(...) CHECK(false)
#define REXSYS_WARN(...) ((void)0)
#define PROFILE_THREAD_EXIT() ((void)0)
#define PROFILE_THREAD_ENTER(...) ((void)0)
#define PROFILE_THREAD_CREATED() ((void)0)
#define REXSYS_ERROR(...) ((void)0)
#define REXSYS_NOISY_DEBUG(...) ((void)0)
#define REXCVAR_GET(...) 1
#define X_CREATE_SUSPENDED 1
#define REX_PLATFORM_SWITCH 1
constexpr int REX_SWITCH_PRIO_PRESENT = 44;
constexpr int X_STATUS_SUCCESS = 0, X_STATUS_NOT_SUPPORTED = -2;
constexpr int X_STATUS_UNSUCCESSFUL = -3, X_STATUS_NO_MEMORY = -4;
constexpr int X_STATUS_USER_APC = -5, X_STATUS_TIMEOUT = -6, X_STATUS_ABANDONED_WAIT_0 = -7;
constexpr size_t operator""_MiB(unsigned long long n) { return n * 1024 * 1024; }
namespace fmt { template <class... T> std::string format(const char*, T...) { return "Worker"; } }
namespace rex { void initialize_seh_thread() {} }
int SwitchPriorityFor(bool, const std::string&) { return 44; }
void RexSwitchSetCurrentThreadPriority(int) {}
std::function<void()> bulk_resume_hook;
bool RexSwitchSetThreadSuspensionEnabled(bool enabled) {
  if (!enabled && bulk_resume_hook) bulk_resume_hook();
  return true;
}
using X_STATUS = int;
std::atomic<int> destroyed = 0, joined = 0, stacks = 0, freed_guest_stacks = 0;
std::function<void()> before_raw_exit;
std::function<void()> configuring_native;
std::atomic<int> linked_threads = 0;
std::atomic<int> wait_calls = 0, signal_calls = 0;
bool fail_native_create = false;

namespace rex::thread {
enum class WaitResult { kSuccess, kTimeout, kUserCallback, kAbandoned, kFailed };
enum class SleepResult { kSuccess, kAlerted };
struct ThreadPriority { static constexpr int kBelowNormal = 8; };
struct WaitHandle {
  HANDLE handle = ::CreateEventA(nullptr, false, false, nullptr);
  ~WaitHandle() { CHECK(::CloseHandle(handle)); }
};
WaitResult Wait(WaitHandle* h, bool, std::chrono::milliseconds timeout) {
  ++wait_calls;
  const DWORD ms = timeout == std::chrono::milliseconds::max() ? INFINITE : DWORD(timeout.count());
  return ::WaitForSingleObject(h->handle, ms) == WAIT_OBJECT_0 ? WaitResult::kSuccess : WaitResult::kTimeout;
}
std::pair<WaitResult, size_t> WaitAny(std::vector<WaitHandle*> handles, bool, std::chrono::milliseconds timeout) {
  ++wait_calls;
  std::vector<HANDLE> native;
  for (auto* h : handles) native.push_back(h->handle);
  const DWORD ms = timeout == std::chrono::milliseconds::max() ? INFINITE : DWORD(timeout.count());
  const DWORD result = ::WaitForMultipleObjects(DWORD(native.size()), native.data(), false, ms);
  return result == WAIT_TIMEOUT ? std::pair{WaitResult::kTimeout, size_t(0)} : std::pair{WaitResult::kSuccess, size_t(result - WAIT_OBJECT_0)};
}
WaitResult WaitAll(std::vector<WaitHandle*> handles, bool, std::chrono::milliseconds timeout) {
  ++wait_calls;
  std::vector<HANDLE> native;
  for (auto* h : handles) native.push_back(h->handle);
  const DWORD ms = timeout == std::chrono::milliseconds::max() ? INFINITE : DWORD(timeout.count());
  return ::WaitForMultipleObjects(DWORD(native.size()), native.data(), true, ms) == WAIT_TIMEOUT ? WaitResult::kTimeout : WaitResult::kSuccess;
}
WaitResult SignalAndWait(WaitHandle* signal, WaitHandle* wait, bool alertable, std::chrono::milliseconds timeout) {
  ++signal_calls;
  CHECK(::SetEvent(signal->handle));
  return Wait(wait, alertable, timeout);
}
void MaybeYield() { std::this_thread::yield(); }
template <class R, class P> void Sleep(std::chrono::duration<R, P> time) { std::this_thread::sleep_for(time); }
SleepResult AlertableSleep(std::chrono::milliseconds time) { Sleep(time); return SleepResult::kSuccess; }
class Thread {
 public:
  struct CreationParameters { size_t stack_size = 128 * 1024; bool create_suspended = false; };
  HANDLE handle_ = nullptr;
  std::mutex join_mutex_;
  bool joined_ = false;
  std::function<void()> body_;
  static std::unique_ptr<Thread> Create(CreationParameters params, std::function<void()> body) {
    if (fail_native_create) { fail_native_create = false; return nullptr; }
    auto t = std::make_unique<Thread>();
    t->body_ = std::move(body);
    t->handle_ = ::CreateThread(nullptr, params.stack_size, [](void* p) -> DWORD {
      static_cast<Thread*>(p)->body_(); return 0;
    }, t.get(), CREATE_SUSPENDED, nullptr);
    CHECK(t->handle_);
    ++stacks;
    if (!params.create_suspended) CHECK(::ResumeThread(t->handle_) == 1);
    return t;
  }
  ~Thread() {
    if (handle_) {
      CHECK(::WaitForSingleObject(handle_, 0) == WAIT_OBJECT_0);
      CHECK(joined_);  // ownership must join before deleting the native wrapper
      CHECK(::CloseHandle(handle_));
    }
  }
  static void Exit(int code) {
    if (before_raw_exit) before_raw_exit();
    ::ExitThread(DWORD(code));  // deliberately skips C++ destructors, as Switch does
  }
  void Terminate(int) {}  // Switch cancellation is unsupported
  void set_name(const std::string&) {}
  void set_priority(int) {}
  uint32_t system_id() { return ::GetThreadId(handle_); }
  void QueueUserCallback(std::function<void()>) {}
  bool Resume() {
    const DWORD previous = ::ResumeThread(handle_);
    return previous != DWORD(-1) && previous != 0;
  }
};
WaitResult Wait(Thread* t, bool, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
  std::lock_guard lock(t->join_mutex_);
  DWORD ms = timeout == std::chrono::milliseconds::max() ? INFINITE : DWORD(timeout.count());
  if (::WaitForSingleObject(t->handle_, ms) != WAIT_OBJECT_0) return WaitResult::kTimeout;
  if (!t->joined_) { t->joined_ = true; ++joined; --stacks; }
  return WaitResult::kSuccess;
}
}  // namespace rex::thread
namespace rex::thread { void set_current_thread_id(int) {} }

namespace rex::system {
namespace chrono { struct Clock { static uint32_t ScaleGuestDurationMillis(uint32_t ms) { return ms; } }; }
class XThread;
#include "object_ref.inc"
class CriticalRegion {
 public:
  std::recursive_mutex mutex;
  std::function<void()> before_lock;
  struct Deferred {
    CriticalRegion& owner;
    std::unique_lock<std::recursive_mutex> held;
    explicit Deferred(CriticalRegion& o) : owner(o), held(o.mutex, std::defer_lock) {}
    void lock() { if (owner.before_lock) owner.before_lock(); held.lock(); }
    void unlock() { held.unlock(); }
  };
  auto AcquireDeferred() { return Deferred(*this); }
  auto Acquire() { return std::unique_lock(mutex); }
};
class KernelState {
 public:
  CriticalRegion global_critical_region_;
  std::unique_ptr<rex::thread::Thread> thread_reaper_;
  std::mutex thread_reaper_mutex_;
  std::condition_variable thread_reaper_cond_;
  bool thread_reaper_stopping_ = false;
  std::list<object_ref<XThread>> active_threads_, retired_threads_;
  std::atomic<bool> terminating_title_{false};
  std::atomic<bool> dispatch_thread_running_{false};
  std::condition_variable_any dispatch_cond_;
  std::list<std::function<void()>> dispatch_queue_;
  int DispatchForTest();
  KernelState() { StartThreadReaper(); }
  ~KernelState() { StopThreadReaper(); }
  bool TrackThread(XThread*);
  void RetireThread(XThread*);
  void OnThreadExit(XThread*);
  void StartThreadReaper();
  void ThreadReaperMain();
  void StopThreadReaper();
  void SignalAllWaitableObjects() { auto lock = global_critical_region_.Acquire(); }
  bool is_terminating_title() { return terminating_title_; }
};
struct X_KTHREAD {
  bool terminated = false;
  struct { int signal_state = 0; } header;
  int exit_status = 0;
  uint32_t process = 0;
  int process_threads = 0;
};
struct ProcessCount {
  int value = 0;
  operator int() const { return value; }
  ProcessCount& operator=(int n) { linked_threads += n - value; value = n; return *this; }
};
struct X_KPROCESS { int thread_list_spinlock = 0; ProcessCount thread_count; };
struct Memory {
  X_KPROCESS process;
  template <class T> T TranslateVirtual(uint32_t) { return &process; }
};
struct ThreadState {
  void* context() { return nullptr; }
  static void Bind(ThreadState*) {}
};
namespace util { void XeRemoveEntryList(int*, Memory*) {} }
class XThread;
thread_local XThread* current_xthread_tls_ = nullptr;
class XThread {
 public:
  KernelState* kernel_state_;
  std::unique_ptr<rex::thread::Thread> thread_;
  std::unique_ptr<ThreadState> thread_state_ = std::make_unique<ThreadState>();
  Memory memory_;
  X_KTHREAD guest;
  std::atomic<bool> running_{false};
  std::atomic<bool> host_thread_ready_{false};
  bool process_linked_ = false;
  bool guest_stack_alive_ = false;
  bool guest_thread_ = true;
  int priority_ = 0;
  std::string thread_name_;
  struct { uint32_t creation_flags = 0; } creation_params_;
  std::function<void()> run_;
  std::atomic<int> refs{2};  // caller plus primary handle table entry
  int handles = 1;
  std::mutex handle_mutex;
  explicit XThread(KernelState* k) : kernel_state_(k) {}
  ~XThread() { thread_.reset(); FreeStack(); ++destroyed; }
  void Retain() { ++refs; }
  void Release() { if (--refs == 0) delete this; }
  void RetainHandle() { std::lock_guard lock(handle_mutex); ++handles; }
  bool ReleaseHandle() {
    bool last;
    { std::lock_guard lock(handle_mutex); CHECK(handles > 0); last = --handles == 0; }
    if (last) Release();
    return true;
  }
  rex::thread::Thread* thread() { return thread_.get(); }
  bool is_guest_thread() { return true; }
  bool host_thread_ready() { return host_thread_ready_.load(std::memory_order_acquire); }
  static XThread* GetCurrentThread() { return current_xthread_tls_; }
  static bool IsInThread(XThread* t) { return current_xthread_tls_ == t; }
  static bool IsInThread() { return current_xthread_tls_ != nullptr; }
  template <class T> T* guest_object() { return &guest; }
  Memory* memory() { return &memory_; }
  void RundownAPCs() {}
  void FreeStack() { if (guest_stack_alive_) { guest_stack_alive_ = false; ++freed_guest_stacks; } }
  void FakeGuestSetup() {
    guest.process = 1;
    memory_.process.thread_count = 1;
    process_linked_ = true;
    guest_stack_alive_ = true;
  }
  void UnlinkFromProcess();
  int handle() { return 1; }
  void set_name(const std::string& name) { thread_name_ = name; }
  void SetActiveCpu(uint8_t) { if (configuring_native) configuring_native(); }
  static void CheckTitleTermination() {
    if (auto* t = current_xthread_tls_; t && t->kernel_state_->is_terminating_title()) t->Exit(0);
  }
  void Execute() { run_(); Exit(7); }
  X_STATUS Create();
  X_STATUS Exit(int);
  X_STATUS Terminate(int);
  X_STATUS Delay(uint32_t, uint32_t, uint64_t);
  X_STATUS Resume(uint32_t* previous = nullptr) { if (previous) *previous = 0; return 0; }
};
class XObject {
 public:
  rex::thread::WaitHandle native;
  int callbacks = 0;
  auto GetWaitHandle() { return &native; }
  void WaitCallback() { ++callbacks; }
  static uint32_t TimeoutTicksToMs(int64_t);
  X_STATUS Wait(uint32_t, uint32_t, uint32_t, uint64_t*);
  static X_STATUS SignalAndWait(XObject*, XObject*, uint32_t, uint32_t, uint32_t, uint64_t*);
  static X_STATUS WaitMultiple(uint32_t, XObject**, uint32_t, uint32_t, uint32_t, uint32_t, uint64_t*);
};
}  // namespace rex::system
namespace rex::kernel::xboxkrnl {
int xeKeKfAcquireSpinLock(void*, int*) { return 0; }
void xeKeKfReleaseSpinLock(void*, int*, int) {}
}
namespace rex::system {
namespace runtime { using ThreadState = rex::system::ThreadState; }
#include "wait_helper.inc"
#include "methods.inc"
}
using rex::system::KernelState;
using rex::system::XThread;
using rex::system::object_ref;
using rex::system::XObject;

template <class F> void Eventually(F condition) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!condition()) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}

object_ref<XThread> Start(KernelState& kernel, std::function<void()> run, bool fail = false) {
  object_ref<XThread> t(new XThread(&kernel));
  t->run_ = std::move(run);
  fail_native_create = fail;
  CHECK(t->Create() == (fail ? X_STATUS_NO_MEMORY : X_STATUS_SUCCESS));
  return t;
}

void Repeated() {
  KernelState kernel;
  for (int i = 0; i < 128; ++i) {
    auto t = Start(kernel, [] {});
    t->ReleaseHandle();
    t.reset();
    Eventually([&] { return destroyed == i + 1; });
  }
  CHECK(joined == 128 && stacks == 1);  // only the dedicated reaper remains
}

void KeptHandle() {
  KernelState kernel;
  auto t = Start(kernel, [] {});
  Eventually([] { return joined == 1 && stacks == 1; });
  CHECK(destroyed == 0 && stacks == 1 && t->guest.exit_status == 7);
  t->ReleaseHandle();
  t.reset();
  Eventually([] { return destroyed == 1; });
}

void EarlyClose() {
  KernelState kernel;
  std::promise<void> leave, entered;
  auto leave_signal = leave.get_future();
  auto t = Start(kernel, [&] { entered.set_value(); leave_signal.wait(); });
  entered.get_future().wait();
  t->ReleaseHandle();
  t.reset();
  CHECK(destroyed == 0);
  leave.set_value();
  Eventually([] { return destroyed == 1; });
}

void LateExit() {
  KernelState kernel;
  std::promise<void> in_exit, leave;
  auto leave_signal = leave.get_future();
  before_raw_exit = [&] { in_exit.set_value(); leave_signal.wait(); };
  auto t = Start(kernel, [] {});
  in_exit.get_future().wait();
  CHECK(!t->running_ && joined == 0 && destroyed == 0);
  t->ReleaseHandle(); t.reset();
  CHECK(destroyed == 0);
  leave.set_value();
  Eventually([] { return destroyed == 1; });
  before_raw_exit = nullptr;
}

void CreationFailure() {
  KernelState kernel;
  auto t = Start(kernel, [] {}, true);
  t.reset();
  Eventually([] { return destroyed == 1; });
  CHECK(joined == 0 && stacks == 1 && linked_threads == 0);
}

void ExternalTerminate() {
  KernelState kernel;
  object_ref<XThread> t(new XThread(&kernel));
  t->running_ = true;
  CHECK(t->Terminate(9) == X_STATUS_NOT_SUPPORTED);
  CHECK(t->running_ && !t->guest.header.signal_state && t->guest.exit_status == 0);
  CHECK(t->handles == 1 && freed_guest_stacks == 0);
  t->ReleaseHandle();
}

void SelfTerminate() {
  KernelState kernel;
  auto t = Start(kernel, [] { XThread::GetCurrentThread()->Terminate(9); });
  Eventually([] { return joined == 1; });
  CHECK(t->guest.terminated && t->guest.exit_status == 9 && freed_guest_stacks == 1);
  t->ReleaseHandle(); t.reset();
  Eventually([] { return destroyed == 1; });
}

void Shutdown() {
  KernelState kernel;
  std::promise<void> entered, leave, stopping, stopped;
  auto leave_signal = leave.get_future();
  auto t = Start(kernel, [&] { entered.set_value(); leave_signal.wait(); });
  entered.get_future().wait();
  t->ReleaseHandle(); t.reset();
  std::thread shutdown([&] {
    stopping.set_value(); kernel.StopThreadReaper(); stopped.set_value();
  });
  stopping.get_future().wait();
  auto completion = stopped.get_future();
  CHECK(completion.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
  CHECK(destroyed == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(550));
  CHECK(destroyed == 0);  // the warning timeout must not free a live guest
  leave.set_value();
  CHECK(completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  shutdown.join();
  CHECK(destroyed == 1 && stacks == 0);
}

void RejectedCreate() {
  KernelState kernel;
  kernel.StopThreadReaper();
  object_ref<XThread> t(new XThread(&kernel));
  CHECK(t->Create() == X_STATUS_UNSUCCESSFUL);
  t.reset();
  CHECK(destroyed == 1 && linked_threads == 0 && stacks == 0);
}

void CreationShutdown() {
  KernelState kernel;
  std::promise<void> configuring, finish_creation, stopped;
  auto finish = finish_creation.get_future();
  configuring_native = [&] { configuring.set_value(); finish.wait(); };
  object_ref<XThread> t(new XThread(&kernel));
  t->creation_params_.creation_flags = X_CREATE_SUSPENDED;
  t->run_ = [] { CHECK(false); };  // shutdown must exit before guest execution
  std::thread creator([&] { CHECK(t->Create() == X_STATUS_SUCCESS); });
  configuring.get_future().wait();
  std::thread shutdown([&] { kernel.StopThreadReaper(); stopped.set_value(); });
  Eventually([&] { return kernel.terminating_title_.load(); });
  finish_creation.set_value();
  creator.join();
  CHECK(stopped.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  shutdown.join();
  t->ReleaseHandle(); t.reset();
  CHECK(destroyed == 1 && linked_threads == 0 && stacks == 0);
}

void PausedLock() {
  KernelState kernel;
  std::promise<void> paused, resumed;
  auto resume_signal = resumed.get_future();
  auto t = Start(kernel, [&] {
    auto lock = kernel.global_critical_region_.Acquire();
    paused.set_value();
    resume_signal.wait();  // simulate a hardware-paused holder of the kernel lock
  });
  paused.get_future().wait();
  bulk_resume_hook = [&] { resumed.set_value(); };
  kernel.StopThreadReaper();
  bulk_resume_hook = nullptr;
  t->ReleaseHandle(); t.reset();
  CHECK(destroyed == 1 && stacks == 0);
}

void DispatchStop() {
  KernelState kernel;
  auto lock = kernel.global_critical_region_.Acquire();
  std::promise<void> entering, stopped;
  kernel.global_critical_region_.before_lock = [&] { entering.set_value(); };
  kernel.dispatch_thread_running_ = true;
  std::thread dispatch([&] { kernel.DispatchForTest(); stopped.set_value(); });
  entering.get_future().wait();  // worker passed the old running check, but has no lock
  kernel.dispatch_thread_running_ = false;
  kernel.dispatch_cond_.notify_all();
  lock.unlock();
  CHECK(stopped.get_future().wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  dispatch.join();
}

void CancelWait(bool multiple) {
  KernelState kernel;
  XObject objects[2];  // unsignaled timer/auto-reset-event backends
  XObject* handles[] = {&objects[0], &objects[1]};
  auto a = Start(kernel, [&] {
    for (;;) {
      if (multiple) XObject::WaitMultiple(2, handles, 1, 0, 0, 0, nullptr);
      else objects[0].Wait(0, 0, 0, nullptr);
    }
  });
  auto b = Start(kernel, [&] {
    for (;;) {
      if (multiple) XObject::WaitMultiple(2, handles, 0, 0, 0, 0, nullptr);
      else objects[0].Wait(0, 0, 0, nullptr);
    }
  });
  Eventually([] { return wait_calls >= 2; });
  if (!multiple) CHECK(::SetEvent(objects[0].native.handle));  // only one waiter can consume it
  // A waiter released by the auto-reset event waits again; cancellation must release both.
  kernel.StopThreadReaper();
  a->ReleaseHandle(); b->ReleaseHandle(); a.reset(); b.reset();
  CHECK(destroyed == 2 && stacks == 0);
}

void SignalOnce() {
  KernelState kernel;
  XObject signal, wait;
  auto t = Start(kernel, [&] { CHECK(XObject::SignalAndWait(&signal, &wait, 0, 0, 0, nullptr) == X_STATUS_SUCCESS); });
  Eventually([] { return wait_calls > 0; });
  std::this_thread::sleep_for(std::chrono::milliseconds(130));
  CHECK(::SetEvent(wait.native.handle));
  Eventually([] { return joined == 1; });
  CHECK(signal_calls == 1 && wait.callbacks == 1);
  t->ReleaseHandle(); t.reset();
}

void WaitTimeout() {
  KernelState kernel;
  XObject wait;
  auto t = Start(kernel, [&] {
    uint64_t timeout = uint64_t(int64_t(-1300000));
    const auto start = std::chrono::steady_clock::now();
    CHECK(wait.Wait(0, 0, 0, &timeout) == X_STATUS_TIMEOUT);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed >= std::chrono::milliseconds(125) && elapsed < std::chrono::milliseconds(500));
  });
  Eventually([] { return joined == 1; });
  t->ReleaseHandle(); t.reset();
}

void CancelDelay() {
  KernelState kernel;
  std::promise<void> delaying;
  auto t = Start(kernel, [&] {
    delaying.set_value();
    XThread::GetCurrentThread()->Delay(0, 0, uint64_t(int64_t(-600000000)));
    CHECK(false);
  });
  delaying.get_future().wait();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  kernel.StopThreadReaper();
  t->ReleaseHandle(); t.reset();
  CHECK(destroyed == 1 && stacks == 0);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::string name = argv[1];
  if (name == "repeated") Repeated();
  else if (name == "kept_handle") KeptHandle();
  else if (name == "early_close") EarlyClose();
  else if (name == "late_exit") LateExit();
  else if (name == "creation_failure") CreationFailure();
  else if (name == "external_terminate") ExternalTerminate();
  else if (name == "self_terminate") SelfTerminate();
  else if (name == "shutdown") Shutdown();
  else if (name == "rejected_create") RejectedCreate();
  else if (name == "creation_shutdown") CreationShutdown();
  else if (name == "paused_lock") PausedLock();
  else if (name == "dispatch_stop") DispatchStop();
  else if (name == "cancel_single") CancelWait(false);
  else if (name == "cancel_multiple") CancelWait(true);
  else if (name == "signal_once") SignalOnce();
  else if (name == "wait_timeout") WaitTimeout();
  else if (name == "cancel_delay") CancelDelay();
  else CHECK(false);
  CHECK(stacks == 0);
  CHECK(linked_threads == 0);
}
