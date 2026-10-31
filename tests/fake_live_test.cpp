#include <rex/system/xam/fake_live.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace fl = rex::system::xam::fake_live;

void Check(bool success, const char* expression, int line) {
  if (!success) {
    std::cerr << "FAIL at line " << line << ": " << expression << '\n';
    std::exit(EXIT_FAILURE);
  }
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void OffIsUnchanged() {
  CHECK(fl::SigninState(false) == 1);
  CHECK(fl::PrivilegeValue(false, 254) == 0);
  CHECK(fl::EthernetLinkStatus(false) == 0);
  CHECK(fl::XnAddrStatus(false) == 0x04);
}

void OnReportsLive() {
  CHECK(fl::SigninState(true) == 2);
  CHECK(fl::PrivilegeValue(true, 254) == 1);
  CHECK(fl::PrivilegeValue(true, 251) == 0);
  CHECK(fl::EthernetLinkStatus(true) == 0x0B);
  CHECK(fl::XnAddrStatus(true) == 0xE6);
  CHECK(fl::kNatOpen == 1);
}

void IdentityShape() {
  const uint64_t seeds[] = {1, 0xFFFFFFFFFFFFFFFFull, 0x123456789ABCDEF0ull};
  for (const uint64_t seed : seeds) {
    const fl::Identity id = fl::MakeIdentity(seed);
    CHECK((id.xuid >> 48) == 0x0009);
    CHECK(std::strlen(id.gamertag) == 9);
    for (size_t i = 0; i < 9; ++i) {
      CHECK(id.gamertag[i] >= 0x20 && id.gamertag[i] <= 0x7E);
    }
    CHECK(id.gamertag[9] == '\0');
    CHECK(id.mac[0] == 0x02);
    CHECK(id.machine_id != 0);
  }
}

void IdentityStable() {
  const fl::Identity a = fl::MakeIdentity(7);
  const fl::Identity b = fl::MakeIdentity(7);
  CHECK(a.xuid == b.xuid);
  CHECK(std::strcmp(a.gamertag, b.gamertag) == 0);
  CHECK(std::memcmp(a.mac, b.mac, sizeof(a.mac)) == 0);
  CHECK(a.machine_id == b.machine_id);
  CHECK(fl::MakeIdentity(7).xuid != fl::MakeIdentity(8).xuid);
}

void OnlineIdBytes() {
  const fl::Identity id = fl::MakeIdentity(5);
  const auto bytes = fl::OnlineId(id);
  uint64_t round_trip = 0;
  for (int i = 7; i >= 0; --i) {
    round_trip = (round_trip << 8) | bytes[i];
  }
  CHECK(round_trip == id.machine_id);
  for (size_t i = 8; i < bytes.size(); ++i) {
    CHECK(bytes[i] == 0);
  }
  CHECK(fl::kFakeIpv4 == 0x0A000002);
  CHECK(fl::kFakeOnlinePort == 3074);
}

void EaServiceHosts() {
  CHECK(fl::IsEaServiceHost("fesl.ea.com"));
  CHECK(fl::IsEaServiceHost("MESSAGING.EA.COM"));
  CHECK(fl::IsEaServiceHost("ea.com"));
  CHECK(fl::IsEaServiceHost("a.b.ea.com"));
  CHECK(!fl::IsEaServiceHost(""));
  CHECK(!fl::IsEaServiceHost("notea.com"));
  CHECK(!fl::IsEaServiceHost("ea.com.evil.example"));
  CHECK(!fl::IsEaServiceHost("example.com"));
  CHECK(!fl::IsEaServiceHost("fesl.ea.co"));
  CHECK(fl::kDnsOk == 0);
  CHECK(fl::kDnsHostNotFound == 11001);
}

void SeedPersists(const fs::path& folder) {
  const fs::path file = folder / "online_identity.bin";
  const uint64_t first = fl::LoadOrCreateSeed(file);
  CHECK(first != 0);
  CHECK(fs::file_size(file) == 8);
  CHECK(fl::LoadOrCreateSeed(file) == first);
}

void SeedRepairs(const fs::path& folder) {
  const fs::path short_file = folder / "short.bin";
  {
    std::ofstream out(short_file, std::ios::binary);
    out << "abc";
  }
  CHECK(fl::LoadOrCreateSeed(short_file) != 0);

  const fs::path empty_file = folder / "empty.bin";
  { std::ofstream out(empty_file, std::ios::binary); }
  CHECK(fl::LoadOrCreateSeed(empty_file) != 0);

  CHECK(fl::LoadOrCreateSeed(folder / "missing-folder" / "x.bin") != 0);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const fs::path folder = argv[1];
  fs::create_directories(folder);
  OffIsUnchanged();
  OnReportsLive();
  IdentityShape();
  IdentityStable();
  OnlineIdBytes();
  EaServiceHosts();
  SeedPersists(folder);
  SeedRepairs(folder);
  std::cout << "fake_live: all checks passed\n";
  return EXIT_SUCCESS;
}
