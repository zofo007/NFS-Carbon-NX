"""Run the Switch thread shim and POSIX control methods with fake Horizon calls."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "reference-nfsmw-nx/sdk/src/core"
COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(CORE.is_dir() and COMPILER.is_file(), "local SDK/compiler unavailable")
class ThreadControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/tests/thread-controls"
        build.mkdir(parents=True, exist_ok=True)
        source = (CORE / "threading_posix.cpp").read_text(encoding="utf-8")
        condition = source[source.index("class PosixCondition<Thread>"):]
        (build / "ownership.inc").write_text(
            f"#define REX_TEST_OWN_START_DATA {int('std::unique_ptr<ThreadStartData> start_data_' in source)}\n",
            encoding="utf-8")
        signatures = ["bool Resume(", "bool Suspend(", "void Terminate(",
                      "void WaitStarted()", "void WaitSuspended()"]
        for optional in ("void Join()",):
            if optional in condition:
                signatures.append(optional)
        bodies = [method(condition, sig) for sig in signatures]
        bodies.append(method(condition, "inline void post_execution()").replace(" override", ""))
        (build / "controls.inc").write_text("\n".join(bodies), encoding="utf-8")
        fields = condition[condition.index("  pthread_t thread_;"):condition.index("\n};")]
        (build / "fields.inc").write_text(fields, encoding="utf-8")
        (build / "start.inc").write_text(method(source,
            "void* PosixCondition<Thread>::ThreadStartRoutine("), encoding="utf-8")
        guest = (ROOT / "reference-nfsmw-nx/sdk/src/system/xthread.cpp").read_text(encoding="utf-8")
        (build / "guest.inc").write_text("\n".join(method(guest, sig) for sig in (
            "X_STATUS XThread::Resume(", "X_STATUS XThread::Suspend(",
            "uint32_t XThread::SelfSuspend()")), encoding="utf-8")
        shim = (CORE / "threading_switch.cpp").read_text(encoding="utf-8")
        bulk_flag = "#define REX_TEST_BULK_RESUME 1\n" if "bool RexSwitchSetThreadSuspensionEnabled(" in shim else ""
        shim = shim[shim.index("namespace {"):shim.rindex("#endif")]
        (build / "shim.inc").write_text(bulk_flag + shim, encoding="utf-8")
        cls.exe = build / "thread-controls.exe"
        subprocess.run([str(COMPILER), "-std=c++23", "-O2", "-pthread", "-Wall", "-Wextra",
                        "-Werror", "-I" + str(build), str(ROOT / "tests/thread_controls_test.cpp"),
                        "-o", str(cls.exe)], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_registered_before_start_is_published(self):
        self.run_case("startup")

    def test_normal_exit_removes_registry_entry(self):
        self.run_case("return")

    def test_explicit_exit_removes_registry_entry(self):
        self.run_case("exit")

    def test_failed_pause_preserves_state(self):
        self.run_case("pause_failure")

    def test_failed_resume_is_retryable(self):
        self.run_case("resume_failure")

    def test_nested_suspend_only_resumes_on_final_release(self):
        self.run_case("nested")

    def test_finished_thread_rejects_suspend_and_resume(self):
        self.run_case("finished")

    def test_self_suspend_waits_for_final_resume(self):
        self.run_case("self")

    def test_guest_nested_suspend_keeps_host_counts_in_sync(self):
        self.run_case("guest_nested")

    def test_guest_preserves_count_on_failed_pause_or_resume(self):
        self.run_case("guest_failure")

    def test_guest_self_suspend_and_external_suspend_compose(self):
        self.run_case("guest_self")

    def test_registry_keeps_handle_live_during_syscall(self):
        self.run_case("registry_race")

    def test_guest_suspend_count_cannot_wrap(self):
        self.run_case("guest_overflow")

    def test_unsupported_external_termination_keeps_native_thread_live(self):
        self.run_case("native_terminate")

    def test_repeated_wait_joins_only_once(self):
        self.run_case("repeated_join")

    def test_shutdown_unpauses_hardware_before_draining_logical_counts(self):
        self.run_case("bulk_resume")

    def test_raw_native_exit_releases_entry_function_captures_after_join(self):
        self.run_case("raw_capture")


if __name__ == "__main__":
    unittest.main()
