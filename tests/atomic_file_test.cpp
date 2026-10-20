#include <rex/filesystem/atomic_file.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

void Check(bool success, const char* expression, int line) {
  if (!success) {
    std::cerr << "FAIL at line " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
  }
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void Write(const fs::path& path, const char* contents) {
  std::ofstream file(path, std::ios::binary);
  file << contents;
  file.close();
  CHECK(file);
}

std::string Read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  CHECK(file);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void NoOverwriteRename(const fs::path& source, const fs::path& destination,
                       std::error_code& error) {
  if (fs::exists(source) && fs::exists(destination)) {
    error = std::make_error_code(std::errc::file_exists);
    return;
  }
  fs::rename(source, destination, error);
}

void FailReplacementRename(const fs::path& source, const fs::path& destination,
                           std::error_code& error) {
  if (source.extension() == ".tmp" && !fs::exists(destination)) {
    error = std::make_error_code(std::errc::io_error);
    return;
  }
  NoOverwriteRename(source, destination, error);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const fs::path root = argv[1];
  CHECK(fs::create_directory(root));

  const auto target = root / "achievements.toml";
  const auto temporary = root / "achievements.toml.tmp";
  Write(target, "old unlocks");
  Write(temporary, "new unlocks");

  std::error_code error;
  rex::filesystem::CommitFile(temporary, target, error, FailReplacementRename);

  CHECK(error == std::errc::io_error);
  CHECK(Read(target) == "old unlocks");
  CHECK(Read(temporary) == "new unlocks");
  CHECK(!fs::exists(target.string() + ".bak"));

  rex::filesystem::CommitFile(temporary, target, error, NoOverwriteRename);
  CHECK(!error && Read(target) == "new unlocks" && !fs::exists(temporary));
  CHECK(!fs::exists(target.string() + ".bak"));

  // Missing input and unrelated rename errors must never touch a valid save.
  rex::filesystem::CommitFile(temporary, target, error, NoOverwriteRename);
  CHECK(error && Read(target) == "new unlocks");
  Write(temporary, "later unlocks");
  rex::filesystem::CommitFile(temporary, target, error,
      [](const fs::path&, const fs::path&, std::error_code& e) {
        e = std::make_error_code(std::errc::permission_denied);
      });
  CHECK(error == std::errc::permission_denied && Read(target) == "new unlocks");
  CHECK(Read(temporary) == "later unlocks");

  // First save and native replacement exercise the actual host filesystem.
  fs::remove(target);
  rex::filesystem::CommitFile(temporary, target, error);
  CHECK(!error && Read(target) == "later unlocks" && !fs::exists(temporary));
  Write(temporary, "final unlocks");
  rex::filesystem::CommitFile(temporary, target, error);
  if (error) std::cerr << "native replacement: " << error.value() << " " << error.message() << '\n';
  CHECK(!error && Read(target) == "final unlocks" && !fs::exists(temporary));

  fs::remove(target);
  fs::remove(root);
  std::cout << "PASS: rollback, Horizon replacement, missing input, permission failure, native save/replace\n";
}
