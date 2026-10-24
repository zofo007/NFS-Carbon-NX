"""Exercise production STFS extraction/install with real files and injected I/O failures."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference-nfsmw-nx/sdk/src/system/xam/content_manager.cpp"
COMPILER = ROOT / "toolchains/msys64/mingw64/bin/g++.exe"


def method(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    while ";" in source[start:brace]:
        start = source.index(signature, start + len(signature))
        brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless(SOURCE.is_file() and COMPILER.is_file(), "local SDK/compiler unavailable")
class ContentInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = ROOT / "build/tests/content-install"
        build.mkdir(parents=True, exist_ok=True)
        source = SOURCE.read_text(encoding="utf-8")
        # These shared production helpers are optional only for the initial red run.
        signatures = ["static X_RESULT ExtractEntry(", "X_RESULT ContentManager::InstallContent(",
                      "bool ContentManager::ContentExists(",
                      "X_RESULT ContentManager::CreateContent(",
                      "X_RESULT ContentManager::WriteContentHeaderFile(",
                      "std::vector<XCONTENT_AGGREGATE_DATA> ContentManager::ListContent("]
        helpers = ["static bool WriteContentFile(", "static X_RESULT WriteContentHeader(",
                   "static bool RecoverContentInstall(", "static bool RecoverMarketplaceContent(",
                   "static X_RESULT PublishContentInstall("]
        bodies = [method(source, sig) for sig in helpers if sig in source]
        bodies += [method(source, sig) for sig in signatures]
        # Redirect filesystem primitives only; run the production transaction logic unchanged.
        (build / "methods.inc").write_text("\n".join(bodies).replace("std::filesystem::", "test_fs::"), encoding="utf-8")
        cls.exe = build / "content-install.exe"
        subprocess.run([str(COMPILER), "-std=c++23", "-O2", "-pthread", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-I" + str(build),
                        "-I" + str(ROOT / "reference-nfsmw-nx/sdk/include"),
                        str(ROOT / "tests/content_install_test.cpp"), "-o", str(cls.exe)], check=True)

    def run_case(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_complete_install_replaces_files_and_license_header(self):
        self.run_case("success")

    def test_read_error_preserves_previous_install(self):
        self.run_case("read_error")

    def test_truncated_read_preserves_previous_install(self):
        self.run_case("short_read")

    def test_zero_byte_write_preserves_previous_install(self):
        self.run_case("write_error")

    def test_short_write_preserves_previous_install(self):
        self.run_case("short_write")

    def test_flush_failure_preserves_previous_install(self):
        self.run_case("flush_error")

    def test_close_failure_preserves_previous_install(self):
        self.run_case("close_error")

    def test_header_write_failure_preserves_previous_install(self):
        self.run_case("header_error")

    def test_failed_first_install_is_not_enumerable(self):
        self.run_case("first_failure")

    def test_invalid_entry_path_cannot_escape_stage(self):
        self.run_case("escape")

    def test_parent_creation_failure_is_reported(self):
        self.run_case("mkdir_error")

    def test_open_content_is_not_replaced(self):
        self.run_case("mounted")

    def test_case_alias_of_open_content_is_not_replaced(self):
        self.run_case("mounted_case")

    def test_unrelated_mount_allows_first_install(self):
        self.run_case("unrelated_mount")

    def test_rename_failures_roll_back_both_components(self):
        for case in ("backup_header", "backup_content", "publish_content", "publish_header", "journal_rename"):
            with self.subTest(case=case):
                self.run_case(case)

    def test_failed_rollback_retains_backups_for_retry(self):
        self.run_case("rollback_retry")

    def test_enumeration_recovers_without_original_package(self):
        self.run_case("recovery_without_source")

    def test_journal_whose_installed_copy_was_deleted_does_not_block_installs(self):
        self.run_case("journal_orphan")

    def test_journal_write_failure_does_not_block_retry(self):
        self.run_case("journal_error")

    def test_metadata_save_failure_preserves_previous_header(self):
        self.run_case("metadata_error")

    def test_guest_existence_check_recovers_before_disposition(self):
        self.run_case("existence_recovery")

    def test_guest_create_recovers_before_creating(self):
        self.run_case("create_recovery")

    def test_guest_create_refuses_unrecoverable_transaction(self):
        self.run_case("create_blocked")
