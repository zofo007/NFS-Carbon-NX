"""Exercise real guest exit/reaper methods using Windows' non-unwinding ExitThread."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "reference-nfsmw-nx/sdk"
COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(SDK.is_dir() and COMPILER.is_file(), "local SDK/compiler unavailable")
class ThreadReclamationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/tests/thread-reclamation"
        build.mkdir(parents=True, exist_ok=True)
        guest = (SDK / "src/system/xthread.cpp").read_text(encoding="utf-8")
        kernel = (SDK / "src/system/kernel_state.cpp").read_text(encoding="utf-8")
        waits = (SDK / "src/system/xobject.cpp").read_text(encoding="utf-8")
        obj = (SDK / "include/rex/system/xobject.h").read_text(encoding="utf-8")
        begin = obj.index("template <typename T>\nclass object_ref {")
        end = obj.index("\ntemplate <class _Ty>", begin)
        retain = method(obj, "object_ref<T> retain_object(")
        (build / "object_ref.inc").write_text(obj[begin:end] + "\ntemplate <typename T>\n" + retain,
                                               encoding="utf-8")
        bodies = [method(guest, sig) for sig in ("X_STATUS XThread::Exit(", "X_STATUS XThread::Terminate(")]
        bodies.append(method(guest, "X_STATUS XThread::Delay("))
        wait_helper = "template <typename Wait>\n" + method(waits, "auto WaitWithTitleTermination(") if "auto WaitWithTitleTermination(" in waits else ""
        (build / "wait_helper.inc").write_text(wait_helper, encoding="utf-8")
        for sig in ("uint32_t XObject::TimeoutTicksToMs(", "X_STATUS XObject::Wait(",
                    "X_STATUS XObject::SignalAndWait(", "X_STATUS XObject::WaitMultiple("):
            bodies.append(method(waits, sig))
        create = method(guest, "X_STATUS XThread::Create()")
        # Keep the real creation guard, native creation, publication and failure
        # paths. Replace only guest memory/PCR allocation with a controlled setup.
        setup_begin = create.index("  // Thread kernel object.")
        native_begin = create.index("  // Keep the primary handle valid")
        create = create[:setup_begin] + "  FakeGuestSetup();\n  uint8_t cpu_index = 0;\n" + create[native_begin:]
        bodies.append(create)
        sig = "void XThread::UnlinkFromProcess()"
        bodies.append(method(guest, sig) if sig in guest else "void XThread::UnlinkFromProcess() {}")
        bodies.append(method(kernel, "void KernelState::OnThreadExit("))
        dispatch_begin = kernel.index("      auto global_lock = global_critical_region_.AcquireDeferred();")
        dispatch_end = kernel.index("    }));", dispatch_begin)
        bodies.append("int KernelState::DispatchForTest() {\n" + kernel[dispatch_begin:dispatch_end] + "\n}")
        defaults = {
            "bool KernelState::TrackThread(": "bool KernelState::TrackThread(XThread*) { return true; }",
            "void KernelState::RetireThread(": "void KernelState::RetireThread(XThread*) {}",
            "void KernelState::StartThreadReaper()": "void KernelState::StartThreadReaper() {}",
            "void KernelState::ThreadReaperMain()": "void KernelState::ThreadReaperMain() {}",
            "void KernelState::StopThreadReaper()": "void KernelState::StopThreadReaper() {}",
        }
        for sig, fallback in defaults.items():
            bodies.append(method(kernel, sig) if sig in kernel else fallback)
        (build / "methods.inc").write_text("\n".join(bodies), encoding="utf-8")
        cls.exe = build / "thread-reclamation.exe"
        subprocess.run([str(COMPILER), "-std=c++23", "-O2", "-pthread", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-I" + str(build), str(ROOT / "tests/thread_reclamation_test.cpp"),
                        "-o", str(cls.exe)], check=True)

    def run_case(self, case):
        result = subprocess.run([str(self.exe), case], capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repeated_unwaited_exits_reclaim_objects_and_host_stacks(self):
        self.run_case("repeated")

    def test_kept_guest_handle_does_not_keep_native_stack(self):
        self.run_case("kept_handle")

    def test_early_handle_close_keeps_running_object_alive(self):
        self.run_case("early_close")

    def test_reaper_waits_for_actual_host_completion(self):
        self.run_case("late_exit")

    def test_failed_native_creation_releases_running_reference(self):
        self.run_case("creation_failure")

    def test_external_guest_termination_reports_unsupported_without_mutation(self):
        self.run_case("external_terminate")

    def test_self_termination_runs_exit_cleanup(self):
        self.run_case("self_terminate")

    def test_shutdown_retains_live_threads_until_completion(self):
        self.run_case("shutdown")

    def test_shutdown_rejects_creation_without_leaving_process_links(self):
        self.run_case("rejected_create")

    def test_shutdown_waits_for_creation_in_progress(self):
        self.run_case("creation_shutdown")

    def test_shutdown_resumes_paused_lock_holder_before_signaling(self):
        self.run_case("paused_lock")

    def test_dispatch_stop_cannot_be_lost_before_wait(self):
        self.run_case("dispatch_stop")

    def test_shutdown_cancels_timer_and_multiple_auto_reset_waiters(self):
        self.run_case("cancel_single")

    def test_shutdown_cancels_wait_any_and_wait_all(self):
        self.run_case("cancel_multiple")

    def test_signal_and_wait_signals_only_once_across_polling(self):
        self.run_case("signal_once")

    def test_guest_wait_preserves_timeout(self):
        self.run_case("wait_timeout")

    def test_shutdown_cancels_nonalertable_delay(self):
        self.run_case("cancel_delay")


if __name__ == "__main__":
    unittest.main()
