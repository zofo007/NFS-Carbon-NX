#include "cache_file.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int rename_calls = 0;

void NoOverwriteRename(const fs::path& temp, const fs::path& target, std::error_code& error) {
  ++rename_calls;
  if (fs::exists(temp) && fs::exists(target)) {
    error = std::make_error_code(std::errc::file_exists);
  } else {
    fs::rename(temp, target, error);
  }
}

void Write(const fs::path& file, const char* contents) {
  std::ofstream out(file, std::ios::binary);
  out << contents;
  out.close();
  assert(out);
}

std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  assert(in);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  assert(argc == 2);
  // The runner supplies a fresh, dedicated directory; never touch a real cache.
  const fs::path root = argv[1];
  assert(fs::create_directory(root));
  const auto temp = root / "cache.tmp";
  const auto target = root / "cache";
  std::error_code error;
  Write(temp, "first");
  carbon::gpu::CommitCacheFile(temp, target, error);
  assert(!error && !fs::exists(temp) && Read(target) == "first");

  Write(temp, "replacement");
  // Probe the real host filesystem as well as the shared production helper.
  fs::rename(temp, target, error);
  const bool no_overwrite = error == std::errc::file_exists;
  if (!error) {
    Write(temp, "replacement");
  } else {
    assert(no_overwrite);
  }
  carbon::gpu::CommitCacheFile(temp, target, error);
  assert(!error && !fs::exists(temp) && Read(target) == "replacement");

  // Exercise Horizon semantics even when the host already supports overwrite.
  Write(temp, "horizon");
  rename_calls = 0;
  carbon::gpu::CommitCacheFile(temp, target, error, NoOverwriteRename);
  assert(!error && rename_calls == 2 && Read(target) == "horizon");

  // ENOENT must never unlink a valid old cache.
  carbon::gpu::CommitCacheFile(temp, target, error, NoOverwriteRename);
  assert(error && Read(target) == "horizon");

  Write(temp, "next");
  carbon::gpu::CommitCacheFile(temp, target, error,
      [](const fs::path&, const fs::path&, std::error_code& e) {
        e = std::make_error_code(std::errc::permission_denied);
      });
  assert(error == std::errc::permission_denied && Read(target) == "horizon" && Read(temp) == "next");

  // An invalid destination must not be mistaken for a disposable regular file.
  const auto directory = root / "directory";
  fs::create_directory(directory);
  rename_calls = 0;
  carbon::gpu::CommitCacheFile(temp, directory, error, NoOverwriteRename);
  assert(rename_calls == 1);
  assert(error && fs::is_directory(directory) && Read(temp) == "next");

  rename_calls = 0;
  carbon::gpu::CommitCacheFile(temp, target, error,
      [](const fs::path&, const fs::path&, std::error_code& e) {
        e = std::make_error_code(++rename_calls == 1 ? std::errc::file_exists : std::errc::io_error);
      });
  assert(error == std::errc::io_error && !fs::exists(target) && Read(temp) == "next");

  // Only the test's explicitly-created files/directories are removed.
  fs::remove(temp);
  fs::remove(directory);
  fs::remove(root);
  std::cout << "PASS: native save/replace, Horizon EEXIST/retry, ENOENT/EACCES preservation, directory guard, "
            << "retry failure reporting; "
            << "real host no-overwrite branch=" << no_overwrite << '\n';
}
