#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <thread>
#include <unordered_map>
#include "ownership.inc"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#define assert_always() CHECK(false)
#define assert_not_null(x) CHECK((x) != nullptr)
#define REXSYS_WARN(...) ((void)0)
#define REX_FATAL(...) CHECK(false)
#define REX_PLATFORM_ANDROID 0
#define REX_PLATFORM_SWITCH 1
#define REX_PLATFORM_LINUX 1
#define REX_PLATFORM_MAC 0
#define REX_PLATFORM_WIN32 0
#define __SWITCH__ 1
using SwitchPthread = void*;
thread_local SwitchPthread calling_thread = reinterpret_cast<void*>(1);
thread_local uint32_t calling_handle = 1;
SwitchPthread FakeSelf() { return calling_thread; }
int FakeEqual(SwitchPthread a, SwitchPthread b) { return a == b; }
struct Exit { int code; };
bool raw_exit_mode = false;
[[noreturn]] void FakeExit(void* value) {
  if (raw_exit_mode) ::ExitThread(DWORD(reinterpret_cast<intptr_t>(value)));
  throw Exit{int(reinterpret_cast<intptr_t>(value))};
}
int FakeCancel(SwitchPthread) { return ENOSYS; }
int joins = 0;
int FakeJoin(SwitchPthread, void**) { ++joins; return 0; }
#define pthread_t SwitchPthread
#define pthread_self FakeSelf
#define pthread_equal FakeEqual
#define pthread_exit FakeExit
#define pthread_cancel FakeCancel
#define pthread_join FakeJoin
#define pthread_getschedparam ShimGetPriority
#define pthread_setschedparam ShimSetPriority
#define pthread_getaffinity_np ShimGetAffinity
#define pthread_setaffinity_np ShimSetAffinity
#define pthread_attr_setschedpolicy ShimSetPolicy
#define pthread_mutexattr_setrobust ShimSetRobust
#define pthread_mutexattr_getrobust ShimGetRobust
#define pthread_mutex_consistent ShimConsistent

using RexSwitchHandle = uint32_t;
using Handle = uint32_t;
using s32 = int32_t;
using u32 = uint32_t;
using u64 = uint64_t;
constexpr Handle REX_SWITCH_INVALID_HANDLE = 0;
constexpr Handle CUR_THREAD_HANDLE = 0xFFFF8000;
struct cpu_set_t { uint64_t bits; };
struct Thread { Handle handle; };
Thread* threadGetSelf() { return nullptr; }
Handle threadGetCurHandle() { return calling_handle; }
int svcGetCurrentProcessorNumber() { return 0; }
#define R_FAILED(x) ((x) != 0)
#define R_SUCCEEDED(x) ((x) == 0)
constexpr int ThreadActivity_Paused = 1, ThreadActivity_Runnable = 0;
int pause_error = 0, resume_error = 0;
int pauses = 0, resumes = 0, priority = 0;
uint64_t affinity = 7;
Handle last_handle = 0;
std::function<void()> priority_hook;
int svcSetThreadActivity(Handle h, int activity) {
  last_handle = h;
  if (activity == ThreadActivity_Paused) { ++pauses; return pause_error; }
  ++resumes;
  return resume_error;
}
int svcGetThreadCoreMask(s32* preferred, u64* mask, Handle h) {
  last_handle = h; *preferred = 0; *mask = affinity; return 0;
}
int svcSetThreadCoreMask(Handle h, s32, u64 mask) {
  last_handle = h; affinity = mask; return 0;
}
int svcGetThreadPriority(s32* out, Handle h) {
  last_handle = h; *out = priority; return 0;
}
int svcSetThreadPriority(Handle h, u32 value) {
  if (priority_hook) priority_hook();
  last_handle = h; priority = int(value); return 0;
}
#include "shim.inc"
#if !defined(REX_TEST_BULK_RESUME)
bool RexSwitchSetThreadSuspensionEnabled(bool) { return true; }
#endif

namespace rex::thread {
class Thread {
 public:
  virtual ~Thread() = default;
};
struct ThreadStartData {
  std::function<void()> start_routine;
  bool create_suspended;
  Thread* thread_obj;
};
template <class T> class PosixCondition;
template <> class PosixCondition<Thread> {
 public:
  enum class State { kUninitialized, kRunning, kSuspended, kFinished };
  PosixCondition() : thread_(reinterpret_cast<void*>(2)), signaled_(false), exit_code_(0),
                     state_(State::kRunning), suspend_count_(0) { sem_init(&suspend_sem_, 0, 0); }
  ~PosixCondition() { sem_destroy(&suspend_sem_); }
  static void* ThreadStartRoutine(void*);
  std::mutex mutex_;
  std::condition_variable cond_;
#include "controls.inc"
#include "fields.inc"
  // Test code accesses state under the same locks as the real wrapper.
  void WaitForState(State value) {
    std::unique_lock lock(state_mutex_);
    CHECK(state_signal_.wait_for(lock, std::chrono::seconds(2), [&] { return state_ == value; }));
  }
};
class PosixThread : public Thread {
 public:
  PosixCondition<Thread> handle_;
  std::unique_ptr<ThreadStartData> start_data_;
};
thread_local PosixThread* current_thread_ = nullptr;
thread_local PosixCondition<Thread>* current_thread_condition_ = nullptr;
void set_current_thread_name(const char*) {}
#include "start.inc"
}  // namespace rex::thread

using Condition = rex::thread::PosixCondition<rex::thread::Thread>;
using Worker = rex::thread::PosixThread;

namespace rex::system {
using X_STATUS = int;
constexpr X_STATUS X_STATUS_SUCCESS = 0, X_STATUS_UNSUCCESSFUL = -1;
struct X_KTHREAD { uint8_t suspend_count = 0; };
struct Kernel { bool is_terminating_title() { return false; } };
class XThread {
 public:
  X_KTHREAD guest;
  Condition* thread_;
  std::mutex suspend_mutex_;
  std::condition_variable suspend_cv_;
  Kernel kernel;
  Kernel* kernel_state_ = &kernel;
  explicit XThread(Condition* host) : thread_(host) {}
  template <class T> T* guest_object() { return &guest; }
  static bool IsInThread(XThread* t) { return t->thread_->thread_ == calling_thread; }
  X_STATUS Resume(uint32_t* out_suspend_count = nullptr);
  X_STATUS Suspend(uint32_t* out_suspend_count = nullptr);
  uint32_t SelfSuspend();
  X_STATUS Exit(int code) { FakeExit(reinterpret_cast<void*>(intptr_t(code))); }
};
#include "guest.inc"
}  // namespace rex::system

void RegisterTarget(Condition& c) {
  calling_thread = c.thread_;
  calling_handle = 42;
  RexSwitchRegisterCurrentThread();
  calling_thread = reinterpret_cast<void*>(1);
  calling_handle = 1;
}

void Startup(bool suspended, bool explicit_exit) {
  Worker worker;
  auto& c = worker.handle_;
  c.state_ = Condition::State::kUninitialized;
  std::mutex gate_mutex;
  std::condition_variable gate;
  bool leave = false;
  std::atomic<bool> entered = false;
  auto* data = new rex::thread::ThreadStartData{[&] {
    entered = true;
    {
      std::unique_lock lock(gate_mutex);
      gate.wait(lock, [&] { return leave; });
    }
    if (explicit_exit) c.Terminate(7);
  }, suspended, &worker};
#if REX_TEST_OWN_START_DATA
  worker.start_data_.reset(data);
#endif
  std::thread host([&] {
    calling_thread = c.thread_;
    calling_handle = 42;
    try { Condition::ThreadStartRoutine(data); } catch (const Exit& exit) { CHECK(exit.code == 7); }
  });
  c.WaitStarted();
  CHECK(RexSwitchHandleFor(c.thread_) == 42);
  if (suspended) {
    // Cross-thread controls must work as soon as startup is visible, before user code runs.
    CHECK(!entered);
    sched_param param{}; param.sched_priority = 43;
    CHECK(ShimSetPriority(c.thread_, SCHED_FIFO, &param) == 0);
    CHECK(last_handle == 42 && priority == 43);
    cpu_set_t cpus{3};
    CHECK(ShimSetAffinity(c.thread_, sizeof(cpus), &cpus) == 0);
    CHECK(last_handle == 42 && affinity == 3);
    uint32_t previous;
    CHECK(c.Suspend(&previous) && previous == 1);
    CHECK(pauses == 0);
    CHECK(c.Resume(&previous) && previous == 2);
    CHECK(!entered);
    CHECK(c.Resume(&previous) && previous == 1);
    CHECK(resumes == 0);  // startup waits cooperatively, never paused by Horizon
  }
  {
    std::lock_guard lock(gate_mutex);
    leave = true;
  }
  gate.notify_all();
  host.join();
  CHECK(RexSwitchHandleFor(c.thread_) == REX_SWITCH_INVALID_HANDLE);
  sched_param param{};
  CHECK(ShimGetPriority(c.thread_, nullptr, &param) == ESRCH);
}

void PauseFailure() {
  Condition c;
  RegisterTarget(c);
  pause_error = EPERM;
  uint32_t previous = 9;
  CHECK(!c.Suspend(&previous));
  CHECK(previous == 0);
  CHECK(c.state_ == Condition::State::kRunning && c.suspend_count_ == 0);
  pause_error = 0;
  CHECK(c.Suspend(&previous) && previous == 0);
  CHECK(c.Resume(&previous) && previous == 1);
}

void ResumeFailure() {
  Condition c;
  RegisterTarget(c);
  CHECK(c.Suspend());
  resume_error = EPERM;
  uint32_t previous = 9;
  CHECK(!c.Resume(&previous));
  CHECK(previous == 1);
  CHECK(c.state_ == Condition::State::kSuspended && c.suspend_count_ == 1);
  resume_error = 0;
  CHECK(c.Resume(&previous) && previous == 1);
  CHECK(c.state_ == Condition::State::kRunning && c.suspend_count_ == 0);
}

void Nested() {
  Condition c;
  RegisterTarget(c);
  uint32_t previous;
  CHECK(c.Suspend(&previous) && previous == 0);
  CHECK(c.Suspend(&previous) && previous == 1);
  CHECK(pauses == 1 && resumes == 0);
  CHECK(c.Resume(&previous) && previous == 2);
  CHECK(c.state_ == Condition::State::kSuspended && resumes == 0);
  CHECK(c.Resume(&previous) && previous == 1);
  CHECK(c.state_ == Condition::State::kRunning && resumes == 1);
}

void Finished() {
  Condition c;
  RegisterTarget(c);
  c.state_ = Condition::State::kFinished;
  CHECK(!c.Suspend());
  CHECK(!c.Resume());
  CHECK(c.state_ == Condition::State::kFinished && pauses == 0 && resumes == 0);
}

void Self() {
  Condition c;
  RegisterTarget(c);
  std::atomic<bool> returned = false;
  std::thread host([&] {
    calling_thread = c.thread_;
    calling_handle = 42;
    CHECK(c.Suspend());
    returned = true;
  });
  c.WaitForState(Condition::State::kSuspended);
  uint32_t previous;
  CHECK(c.Suspend(&previous) && previous == 1);
  CHECK(c.Resume(&previous) && previous == 2);
  CHECK(!returned);
  CHECK(c.Resume(&previous) && previous == 1);
  host.join();
  CHECK(returned && pauses == 0 && resumes == 0);
}

void GuestNested() {
  Condition c;
  RegisterTarget(c);
  rex::system::XThread guest(&c);
  uint32_t previous;
  CHECK(guest.Suspend(&previous) == 0 && previous == 0);
  CHECK(guest.Suspend(&previous) == 0 && previous == 1);
  CHECK(guest.Resume(&previous) == 0 && previous == 2);
  CHECK(guest.Resume(&previous) == 0 && previous == 1);
  CHECK(guest.guest.suspend_count == 0 && c.suspend_count_ == 0);
  CHECK(c.state_ == Condition::State::kRunning);
  CHECK(pauses == 1 && resumes == 1);
}

void GuestFailure() {
  Condition c;
  RegisterTarget(c);
  rex::system::XThread guest(&c);
  uint32_t previous;
  pause_error = EPERM;
  CHECK(guest.Suspend(&previous) != 0 && previous == 0);
  CHECK(guest.guest.suspend_count == 0 && c.suspend_count_ == 0);
  pause_error = 0;
  CHECK(guest.Suspend(&previous) == 0 && previous == 0);
  resume_error = EPERM;
  CHECK(guest.Resume(&previous) != 0 && previous == 1);
  CHECK(guest.guest.suspend_count == 1 && c.suspend_count_ == 1);
  resume_error = 0;
  CHECK(guest.Resume(&previous) == 0 && previous == 1);
  CHECK(guest.guest.suspend_count == 0 && c.suspend_count_ == 0);
}

void GuestOverflow() {
  Condition c;
  RegisterTarget(c);
  rex::system::XThread guest(&c);
  for (int i = 0; i < 255; ++i) CHECK(guest.Suspend() == 0);
  uint32_t previous;
  CHECK(guest.Suspend(&previous) != 0 && previous == 255);
  CHECK(guest.guest.suspend_count == 255 && c.suspend_count_ == 255);
  for (int i = 0; i < 255; ++i) CHECK(guest.Resume() == 0);
  CHECK(guest.Resume(&previous) == 0 && previous == 0);
  CHECK(guest.guest.suspend_count == 0 && c.suspend_count_ == 0);
}

void GuestSelf() {
  Condition c;
  RegisterTarget(c);
  rex::system::XThread guest(&c);
  std::atomic<bool> returned = false;
  std::thread host([&] {
    calling_thread = c.thread_;
    CHECK(guest.Suspend() == 0);
    returned = true;
  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (;;) {
    std::unique_lock lock(guest.suspend_mutex_);
    if (guest.guest.suspend_count == 1) break;
    CHECK(std::chrono::steady_clock::now() < deadline);
    lock.unlock();
    std::this_thread::yield();
  }
  CHECK(guest.Suspend() == 0);
  CHECK(guest.Resume() == 0);
  CHECK(!returned);
  CHECK(guest.Resume() == 0);
  host.join();
  CHECK(returned && guest.guest.suspend_count == 0 && c.suspend_count_ == 0);
}

void RegistryRace() {
  std::promise<void> ready, exit_now, exiting, exited, in_syscall, finish_syscall;
  auto exit_signal = exit_now.get_future();
  auto finish_signal = finish_syscall.get_future();
  std::thread target([&] {
    calling_thread = reinterpret_cast<void*>(2);
    calling_handle = 42;
    RexSwitchRegisterCurrentThread();
    ready.set_value();
    exit_signal.wait();
    exiting.set_value();
    RexSwitchUnregisterCurrentThread();
    exited.set_value();
  });
  ready.get_future().wait();
  priority_hook = [&] { in_syscall.set_value(); finish_signal.wait(); };
  std::thread control([&] {
    sched_param param{}; param.sched_priority = 43;
    CHECK(ShimSetPriority(reinterpret_cast<void*>(2), SCHED_FIFO, &param) == 0);
  });
  in_syscall.get_future().wait();
  exit_now.set_value();
  exiting.get_future().wait();
  CHECK(exited.get_future().wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
  finish_syscall.set_value();
  control.join();
  target.join();
  CHECK(RexSwitchHandleFor(reinterpret_cast<void*>(2)) == REX_SWITCH_INVALID_HANDLE);
}

void NativeTerminate() {
  Condition c;
  c.Terminate(7);
  CHECK(c.state_ == Condition::State::kRunning && !c.signaled_);
  CHECK(c.exit_code_ == 0);
}

void RepeatedJoin() {
  Condition c;
  c.state_ = Condition::State::kFinished;
  c.post_execution();
  c.post_execution();
  CHECK(joins == 1);
}

void BulkResume() {
  Condition c;
  RegisterTarget(c);
  CHECK(c.Suspend());
  resume_error = EPERM;
  CHECK(!RexSwitchSetThreadSuspensionEnabled(false));
  CHECK(c.suspend_count_ == 1 && c.state_ == Condition::State::kSuspended);
  resume_error = 0;
  CHECK(RexSwitchSetThreadSuspensionEnabled(false));
  CHECK(resumes == 2);
  resume_error = EPERM;  // consuming the logical count must not call the kernel again
  CHECK(c.Resume());
  CHECK(c.suspend_count_ == 0 && c.state_ == Condition::State::kRunning && resumes == 2);
  CHECK(!c.Suspend());  // teardown cannot acquire another hardware pause
  CHECK(RexSwitchSetThreadSuspensionEnabled(true));
  CHECK(c.Suspend());  // a subsequent kernel instance can suspend normally
}

void RawCapture() {
  raw_exit_mode = true;
  std::weak_ptr<int> capture;
  {
    Worker worker;
    auto payload = std::make_shared<int>(123);
    capture = payload;
    auto* data = new rex::thread::ThreadStartData{[payload] {
      CHECK(*payload == 123);
      rex::thread::current_thread_condition_->Terminate(7);
    }, false, &worker};
#if REX_TEST_OWN_START_DATA
    worker.start_data_.reset(data);
#endif
    payload.reset();
    HANDLE host = ::CreateThread(nullptr, 128 * 1024, [](void* p) -> DWORD {
      calling_thread = reinterpret_cast<void*>(2);
      calling_handle = 42;
      Condition::ThreadStartRoutine(p);
      return 0;
    }, data, 0, nullptr);
    CHECK(host);
    CHECK(::WaitForSingleObject(host, 2000) == WAIT_OBJECT_0);
    CHECK(::CloseHandle(host));
    CHECK(!capture.expired());
  }
  CHECK(capture.expired());
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::string name = argv[1];
  if (name == "startup") Startup(true, false);
  else if (name == "return") Startup(false, false);
  else if (name == "exit") Startup(false, true);
  else if (name == "pause_failure") PauseFailure();
  else if (name == "resume_failure") ResumeFailure();
  else if (name == "nested") Nested();
  else if (name == "finished") Finished();
  else if (name == "self") Self();
  else if (name == "guest_nested") GuestNested();
  else if (name == "guest_failure") GuestFailure();
  else if (name == "guest_self") GuestSelf();
  else if (name == "registry_race") RegistryRace();
  else if (name == "guest_overflow") GuestOverflow();
  else if (name == "native_terminate") NativeTerminate();
  else if (name == "repeated_join") RepeatedJoin();
  else if (name == "bulk_resume") BulkResume();
  else if (name == "raw_capture") RawCapture();
  else CHECK(false);
}
