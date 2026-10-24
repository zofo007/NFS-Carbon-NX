#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <rex/filesystem/atomic_file.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template <class... T> void Log(T&&...) {}
#define REXSYS_INFO(...) Log(__VA_ARGS__)
#define REXSYS_WARN(...) Log(__VA_ARGS__)
#define REXSYS_ERROR(...) Log(__VA_ARGS__)
#define XSUCCEEDED(x) ((x) == 0)
#define assert_not_null(x) CHECK(x)
using X_RESULT = uint32_t;
using X_STATUS = uint32_t;
constexpr X_RESULT X_ERROR_SUCCESS = 0, X_ERROR_ACCESS_DENIED = 5, X_ERROR_FILE_NOT_FOUND = 2;
constexpr X_RESULT X_ERROR_BUSY = 170, X_ERROR_ALREADY_EXISTS = 183;
constexpr X_STATUS X_STATUS_SUCCESS = 0;
enum class DummyDeviceId { HDD = 1 };
enum class XContentType { kMarketplaceContent = 2 };
std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> rename_failure;
namespace test_fs {
using namespace std::filesystem;
void rename(const path& from, const path& to, std::error_code& ec) {
  if (rename_failure && rename_failure(from, to)) {
    ec = std::make_error_code(std::errc::permission_denied);
  } else std::filesystem::rename(from, to, ec);
}
}
namespace rex {
std::filesystem::path to_path(std::string_view s) { return std::filesystem::path(s); }
std::string path_to_utf8(const std::filesystem::path& p) { return p.string(); }
std::u16string path_to_utf16(const std::filesystem::path& p) { return p.u16string(); }
namespace string {
struct string_key_case : std::string {
  explicit string_key_case(std::string_view s) : std::string(s) {}
  static string_key_case create(std::string_view s) { return string_key_case(s); }
};
std::string utf8_fix_path_separators(std::string s) { std::replace(s.begin(), s.end(), '\\', '/'); return s; }
std::string to_utf8(const std::u16string&) { return "DLC"; }
}
namespace system { enum class XLanguage { kEnglish }; }
}
struct XCONTENT_AGGREGATE_DATA {
  uint32_t device_id = 0, title_id = 0;
  XContentType content_type = XContentType::kMarketplaceContent;
  uint64_t xuid = 0;
  std::array<char, 64> name{};
  std::array<char16_t, 64> display{};
  std::string file_name() const { return name.data(); }
  void set_file_name(const std::string& s) { std::strncpy(name.data(), s.c_str(), name.size() - 1); }
  void set_display_name(const std::u16string& s) { std::copy_n(s.begin(), std::min(s.size(), display.size() - 1), display.begin()); }
  std::u16string display_name() const { return display.data(); }
};
enum class Fault { none, read, short_read, write, short_write, flush, close, header, journal };
Fault fault = Fault::none;
std::map<FILE*, std::filesystem::path> outputs;
int opened = 0, destroyed_inputs = 0;
size_t CheckedWrite(const void* p, size_t size, size_t count, FILE* file) {
  const auto name = outputs[file].filename().string();
  const bool header = name.find("header") != std::string::npos;
  if (fault == Fault::write || (fault == Fault::header && header)) return 0;
  if (fault == Fault::journal && name.find("journal") != std::string::npos) return 0;
  if (fault == Fault::short_write && count) return std::fwrite(p, size, count - 1, file);
  return std::fwrite(p, size, count, file);
}
int CheckedFlush(FILE* f) { const int result = std::fflush(f); return fault == Fault::flush ? EOF : result; }
int CheckedClose(FILE* f) {
  outputs.erase(f);
  const int result = std::fclose(f);
  return fault == Fault::close ? EOF : result;
}
namespace rex::filesystem {
struct FileInfo {
  enum class Type { kDirectory, kFile };
  Type type;
  std::filesystem::path name;
};
std::vector<FileInfo> ListFiles(const std::filesystem::path& p) {
  std::vector<FileInfo> result;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
    result.push_back({it->is_directory() ? FileInfo::Type::kDirectory : FileInfo::Type::kFile, it->path().filename()});
  return result;
}
constexpr uint32_t kFileAttributeDirectory = 0x10;
enum FileAccess { kFileReadData = 1 };
struct File;
struct Entry {
  std::string path_, bytes;
  bool directory = false;
  std::vector<std::unique_ptr<Entry>> children_;
  std::string path() const { return path_; }
  uint32_t attributes() const { return directory ? kFileAttributeDirectory : 0; }
  size_t size() const { return bytes.size(); }
  auto& children() { return children_; }
  X_STATUS Open(uint32_t, File** out);
};
struct File {
  Entry* entry;
  void Destroy() { ++destroyed_inputs; delete this; }
  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t offset, size_t* read) {
    *read = std::min(buffer.size(), entry->bytes.size() - offset);
    if (fault == Fault::read) { *read = 0; return 1; }
    if (fault == Fault::short_read && *read) --*read;
    std::memcpy(buffer.data(), entry->bytes.data() + offset, *read);
    return 0;
  }
};
X_STATUS Entry::Open(uint32_t, File** out) { *out = new File{this}; ++opened; return 0; }
FILE* OpenFile(const std::filesystem::path& p, const char* mode) {
  FILE* f = std::fopen(p.string().c_str(), mode);
  if (f && mode[0] == 'w') outputs[f] = p;
  return f;
}
bool CreateEmptyFile(const std::filesystem::path& p) {
  FILE* f = std::fopen(p.string().c_str(), "wb");
  return f && std::fclose(f) == 0;
}
Entry package_root;
struct StfsContainerDevice {
  struct {
    struct { std::u16string display_name(rex::system::XLanguage) { return u"New DLC"; } } metadata;
    struct { struct { uint32_t license_flags = 0, license_bits = 0; } licenses[16]; } header;
  } header_;
  StfsContainerDevice(const char*, const std::filesystem::path&) { header_.header.licenses[0] = {1, 7}; }
  bool Initialize() { return true; }
  Entry* ResolvePath(const char*) { return &package_root; }
  auto& header() { return header_; }
};
}
struct CriticalRegion {
  std::recursive_mutex mutex;
  auto Acquire() { return std::unique_lock(mutex); }
};
struct Kernel { uint32_t title_id() { return 0x454107EC; } };
namespace rex::system::xam {
constexpr uint32_t kCurrentlyRunningTitleId = uint32_t(-1);
struct ContentPackage {
  std::filesystem::path path;
  const std::filesystem::path& package_path() const { return path; }
};
class ContentManager {
 public:
  Kernel kernel;
  Kernel* kernel_state_ = &kernel;
  std::filesystem::path root_path_;
  CriticalRegion global_critical_region_;
  std::map<string::string_key_case, ContentPackage*> open_packages_;
  ~ContentManager() { for (auto& [key, package] : open_packages_) delete package; }
  explicit ContentManager(std::filesystem::path p) : root_path_(std::move(p)) {}
  std::filesystem::path ResolvePackageRoot(uint64_t, XContentType, uint32_t) {
    return root_path_ / "0000000000000000/454107EC/00000002";
  }
  std::filesystem::path ResolvePackagePath(uint64_t, const XCONTENT_AGGREGATE_DATA& d) {
    return root_path_ / "0000000000000000/454107EC/00000002" / d.file_name();
  }
  std::filesystem::path ResolvePackageHeaderPath(std::string_view name, uint64_t, uint32_t, XContentType) {
    return root_path_ / "0000000000000000/454107EC/Headers/00000002" / (std::string(name) + ".header");
  }
  // Simulate the old metadata comparison: a current-title alias differs from the
  // concrete title used by installation, despite referring to the same mount.
  bool IsContentOpen(const XCONTENT_AGGREGATE_DATA&) { return false; }
  std::unique_ptr<ContentPackage> ResolvePackage(std::string_view, uint64_t xuid,
                                               const XCONTENT_AGGREGATE_DATA& d) {
    return std::make_unique<ContentPackage>(ResolvePackagePath(xuid, d));
  }
  X_RESULT ReadContentHeaderFile(const std::string&, uint64_t, uint32_t, XContentType,
                                XCONTENT_AGGREGATE_DATA&) { return X_ERROR_FILE_NOT_FOUND; }
  std::vector<XCONTENT_AGGREGATE_DATA> ListContent(uint32_t, uint64_t, XContentType, uint32_t);
  X_RESULT InstallContent(const std::filesystem::path&);
  X_RESULT WriteContentHeaderFile(uint64_t, XCONTENT_AGGREGATE_DATA, uint32_t);
  bool ContentExists(uint64_t, const XCONTENT_AGGREGATE_DATA&);
  X_RESULT CreateContent(std::string_view, uint64_t, const XCONTENT_AGGREGATE_DATA&);
};
// The production RemoveTree adds a Switch-only manual fallback around remove_all; the transaction logic under test only needs
// the remove_all behaviour (and the failure text it logs).
static std::string g_remove_failure;
static bool RemoveTree(const std::filesystem::path& path, std::error_code& ec) {
  std::filesystem::remove_all(path, ec);
  return !ec;
}
#define fwrite CheckedWrite
#define fflush CheckedFlush
#define fclose CheckedClose
#include "methods.inc"
#undef fclose
#undef fflush
#undef fwrite
}

std::string Read(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& p, const std::string& value) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary); f << value; CHECK(bool(f));
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const std::string name = argv[1];
  const auto scratch = std::filesystem::absolute("build/tests/content-install/run-" + name);
  CHECK(scratch.parent_path() == std::filesystem::absolute("build/tests/content-install"));
  std::filesystem::remove_all(scratch);
  std::filesystem::create_directories(scratch);
  rex::system::xam::ContentManager content(scratch / "content");
  XCONTENT_AGGREGATE_DATA data; data.set_file_name("package");
  const auto install = content.ResolvePackagePath(0, data);
  const auto header = content.ResolvePackageHeaderPath("package", 0, 0, XContentType::kMarketplaceContent);
  const auto source = scratch / "source/package";
  Write(source, "synthetic STFS input; virtual device below supplies its entries");
  const bool first = name == "first_failure";
  if (!first && name != "unrelated_mount") {
    Write(install / "old.bin", "GOOD OLD CONTENT"); Write(header, "GOOD OLD HEADER");
  }
  auto& root = rex::filesystem::package_root;
  root.directory = true;
  auto entry = std::make_unique<rex::filesystem::Entry>();
  entry->path_ = name == "escape" ? "../../escape.bin" : "nested/new.bin";
  entry->bytes = "COMPLETE NEW CONTENT";
  root.children_.push_back(std::move(entry));
  if (name == "read_error") fault = Fault::read;
  if (name == "short_read") fault = Fault::short_read;
  if (name == "write_error" || first) fault = Fault::write;
  if (name == "short_write") fault = Fault::short_write;
  if (name == "flush_error") fault = Fault::flush;
  if (name == "close_error") fault = Fault::close;
  if (name == "header_error") fault = Fault::header;
  if (name == "journal_error") fault = Fault::journal;
  if (name == "mounted" || name == "mounted_case")
    content.open_packages_.emplace(rex::string::string_key_case("DLC"),
        new rex::system::xam::ContentPackage{
            name == "mounted_case" ? install.parent_path() / "PACKAGE" : install});
  if (name == "unrelated_mount") {
    const auto unrelated = install.parent_path() / "other-package";
    std::filesystem::create_directories(unrelated);
    content.open_packages_.emplace(rex::string::string_key_case("OTHER"),
                                   new rex::system::xam::ContentPackage{unrelated});
  }
  if (name == "mkdir_error") {
    auto conflict = std::make_unique<rex::filesystem::Entry>();
    conflict->path_ = "nested";
    conflict->bytes = "parent is a file";
    root.children_.insert(root.children_.begin(), std::move(conflict));
  }
  int failed_renames = 0;
  rename_failure = [&](const auto& from, const auto& to) {
    bool fail = (name == "backup_header" && to.filename() == "old.header") ||
                (name == "backup_content" && to.filename() == "old") ||
                (name == "publish_content" && from.filename() == "new") ||
                (name == "publish_header" && from.filename() == "new.header") ||
                (name == "journal_rename" && from.filename() == "journal.tmp");
    if (name == "rollback_retry" || name == "recovery_without_source")
      fail = from.filename() == "new.header" || from.filename() == "old.header";
    if (fail) ++failed_renames;
    return fail;
  };
  if (name == "metadata_error") {
    fault = Fault::write;
    CHECK(content.WriteContentHeaderFile(0, data, 7) != X_ERROR_SUCCESS);
    CHECK(Read(header) == "GOOD OLD HEADER");
    std::filesystem::remove_all(scratch);
    return 0;
  }
  if (name == "existence_recovery" || name == "create_recovery" || name == "create_blocked") {
    const auto work = install.parent_path().parent_path() / ".dlc-install/package";
    std::filesystem::create_directories(work);
    std::filesystem::rename(header, work / "old.header");
    std::filesystem::rename(install, work / "old");
    Write(work / "journal", "DLC111");
    data.title_id = uint32_t(-1);  // Standard XCONTENT_DATA uses current-title alias.
    if (name == "create_blocked") {
      rename_failure = [](const auto&, const auto&) { return true; };
      CHECK(content.CreateContent("DLC", 0, data) == X_ERROR_ACCESS_DENIED);
      CHECK(!std::filesystem::exists(install));
      CHECK(Read(work / "old/old.bin") == "GOOD OLD CONTENT");
    } else {
      if (name == "existence_recovery") CHECK(content.ContentExists(0, data));
      else CHECK(content.CreateContent("DLC", 0, data) == X_ERROR_ALREADY_EXISTS);
      CHECK(Read(install / "old.bin") == "GOOD OLD CONTENT");
      CHECK(Read(header) == "GOOD OLD HEADER");
    }
    CHECK(outputs.empty());
    std::filesystem::remove_all(scratch);
    return 0;
  }
  if (name == "journal_orphan") {
    // A journal says the installed copy existed, but it was deleted by hand: the install must still go through.
    std::filesystem::remove_all(install);
    std::filesystem::remove(header);
    Write(install.parent_path().parent_path() / ".dlc-install/package/journal", "DLC111");
  }
  const auto result = content.InstallContent(source);
  if (name == "rollback_retry" || name == "recovery_without_source") {
    CHECK(result != X_ERROR_SUCCESS && failed_renames >= 2);
    const auto work = install.parent_path().parent_path() / ".dlc-install/package";
    CHECK(Read(work / "old/old.bin") == "GOOD OLD CONTENT");
    CHECK(Read(work / "old.header") == "GOOD OLD HEADER");
    rename_failure = nullptr;
    if (name == "recovery_without_source") {
      std::filesystem::remove(source);
      auto list = content.ListContent(1, 0, XContentType::kMarketplaceContent, 0x454107EC);
      CHECK(list.size() == 1 && list[0].file_name() == "package");
    } else {
      fault = Fault::read;
      CHECK(content.InstallContent(source) != X_ERROR_SUCCESS);
    }
  }
  if (name == "journal_error") {
    fault = Fault::read;
    CHECK(content.InstallContent(source) != X_ERROR_SUCCESS);
    CHECK(opened >= 2);  // recovery allowed another extraction attempt
  }
  if (name == "success" || name == "unrelated_mount" || name == "journal_orphan") {
    CHECK(result == X_ERROR_SUCCESS);
    CHECK(Read(install / "nested/new.bin") == "COMPLETE NEW CONTENT");
    CHECK(!std::filesystem::exists(install / "old.bin"));
    CHECK(std::filesystem::file_size(header) == sizeof(data) + sizeof(uint32_t));
  } else {
    CHECK(result != X_ERROR_SUCCESS);
    if (first) CHECK(!std::filesystem::exists(install) && !std::filesystem::exists(header));
    else {
      CHECK(Read(install / "old.bin") == "GOOD OLD CONTENT");
      CHECK(Read(header) == "GOOD OLD HEADER");
      CHECK(!std::filesystem::exists(install / "nested/new.bin"));
    }
    CHECK(!std::filesystem::exists(install.parent_path() / "escape.bin"));
  }
  CHECK(opened == destroyed_inputs && outputs.empty());
  std::filesystem::remove_all(scratch);
}
