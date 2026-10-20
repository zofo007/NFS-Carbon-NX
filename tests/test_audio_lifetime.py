"""Exercise production audio lifecycle methods with real host threads and fake devices."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-nfsmw-nx/sdk/src/audio/audio_system.cpp"
HEADER = ROOT / "reference-nfsmw-nx/sdk/include/rex/audio/audio_system.h"
XMA_SOURCE = ROOT / "reference-nfsmw-nx/sdk/src/audio/xma_decoder.cpp"
COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(SOURCE.is_file() and COMPILER.is_file(), "local SDK/compiler unavailable")
class AudioLifetimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/tests/audio-lifetime"
        build.mkdir(parents=True, exist_ok=True)
        source = SOURCE.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        start = header.index("  struct {", header.index("kMaximumClientCount ="))
        end = header.index(" clients_[kMaximumClientCount];", start)
        end += len(" clients_[kMaximumClientCount];")
        (build / "client.inc").write_text(header[start:end], encoding="utf-8")
        signatures = ["void AudioSystem::WorkerThreadMain()", "int AudioSystem::FindFreeClient()",
                      "void AudioSystem::Shutdown()", "X_STATUS AudioSystem::RegisterClient(",
                      "void AudioSystem::SubmitFrame(", "void AudioSystem::UnregisterClient(",
                      "bool AudioSystem::Save(", "bool AudioSystem::Restore("]
        for optional in ("void AudioSystem::DestroyClient(", "void AudioSystem::CompleteClientCallback("):
            if optional in source:
                signatures.append(optional)
        bodies = [method(source, signature) for signature in signatures]
        xma = method(XMA_SOURCE.read_text(encoding="utf-8"), "void XmaDecoder::Shutdown()")
        (build / "methods.inc").write_text("\n".join(bodies) + "\n" + xma, encoding="utf-8")
        (build / "declarations.inc").write_text("\n".join(
            body[:body.index("{")].replace("AudioSystem::", "") + ";" for body in bodies), encoding="utf-8")
        cls.exe = build / "audio-lifetime.exe"
        subprocess.run([str(COMPILER), "-std=c++23", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror",
                        "-Wno-missing-field-initializers", "-I" + str(build),
                        str(ROOT / "tests/audio_lifetime_test.cpp"), "-o", str(cls.exe)], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unregister_between_selection_and_dispatch(self):
        self.run_case("selected")

    def test_unregister_during_callback(self):
        self.run_case("executing")

    def test_self_unregister_and_reregister(self):
        self.run_case("self")

    def test_slot_reused_only_after_previous_callback_finishes(self):
        self.run_case("reregister")

    def test_shutdown_waits_beyond_timeout(self):
        self.run_case("shutdown")

    def test_shutdown_wakes_paused_worker(self):
        self.run_case("paused")

    def test_shutdown_cleans_already_finished_worker(self):
        self.run_case("finished")

    def test_external_unregister_waits_for_self_retiring_callback(self):
        self.run_case("self_external")

    def test_save_and_restore_reject_active_callback(self):
        self.run_case("snapshot")

    def test_registration_failures_leave_reusable_slot(self):
        self.run_case("registration")

    def test_full_client_table_returns_failure(self):
        self.run_case("capacity")

    def test_decoder_outlives_audio_callback(self):
        self.run_case("shutdown_order")

    def test_xma_shutdown_waits_for_actual_completion(self):
        self.run_case("xma")


if __name__ == "__main__":
    unittest.main()
