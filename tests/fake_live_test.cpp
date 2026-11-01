#include <rex/system/tail_call_thunk.h>
#include <rex/system/xam/fake_live.h>
#include <rex/system/xam/fesl_shim.h>
#include <rex/system/xam/xlive_async.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace fl = rex::system::xam::fake_live;
namespace fesl = rex::system::xam::fesl;
namespace xa = rex::system::xam::xlive_async;

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

void OnlineCallNames() {
  CHECK(fl::IsOnlineCallName("__imp__NetDll_socket"));
  CHECK(fl::IsOnlineCallName("__imp__NetDll_XNetGetTitleXnAddr"));
  CHECK(fl::IsOnlineCallName("__imp__XNetLogonGetMachineID"));
  CHECK(fl::IsOnlineCallName("__imp__XamUserGetSigninState"));
  CHECK(fl::IsOnlineCallName("__imp__XamUserReadProfileSettings"));
  CHECK(fl::IsOnlineCallName("__imp__XamShowSigninUI"));
  CHECK(fl::IsOnlineCallName("__imp__XamEnumerate"));
  CHECK(fl::IsOnlineCallName("__imp__XNotifyGetNext"));
  CHECK(fl::IsOnlineCallName("__imp__XSessionCreate"));
  CHECK(fl::IsOnlineCallName("__imp__XMsgInProcessCall"));
  CHECK(fl::IsOnlineCallName("__imp__XMsgStartIORequestEx"));
  CHECK(fl::IsOnlineCallName("__imp__XMsgSystemProcessCall"));
  CHECK(!fl::IsOnlineCallName("__imp__XamContentCreate"));
  CHECK(!fl::IsOnlineCallName("__imp__XamAlloc"));
  CHECK(!fl::IsOnlineCallName("__imp__NtCreateFile"));
  CHECK(!fl::IsOnlineCallName("__imp__KeQueryPerformanceFrequency"));
  CHECK(!fl::IsOnlineCallName("NetDll_socket"));
  CHECK(!fl::IsOnlineCallName(""));
  static_assert(fl::IsOnlineCallName("__imp__NetDll_connect"));
}

void InitialNotifications() {
  const auto notes = fl::InitialNotifications();
  CHECK(notes.size() == 3);
  CHECK(notes[0].id == 0x0000000A && notes[0].param == 1);            // XN_SYS_SIGNINCHANGED, user 0
  CHECK(notes[1].id == 0x02000001 && notes[1].param == 0x001510F0);   // connection established
  CHECK(notes[2].id == 0x02000003 && notes[2].param == 1);            // link state: up
  // A listener only gets an id whose area bit is in its mask; the Live ids belong to area 1 (0x2).
  CHECK(((notes[1].id >> 25) & 0x3F) == 1);
  CHECK(((notes[2].id >> 25) & 0x3F) == 1);
  CHECK(((notes[0].id >> 25) & 0x3F) == 0);  // system area (0x1)
}

void ServiceInfoBytes() {
  CHECK(fl::kEaServiceId == 0x45410004);
  CHECK(fl::kEaServicePort == 18210);
  // XONLINE_SERVICE_INFO: id (be32), IPv4 (network order), port (be16), reserved (be16 = 0).
  const auto bytes = fl::EncodeServiceInfo(fl::kEaServiceId, 0xC0A80001, 18210);
  const uint8_t expected[12] = {0x45, 0x41, 0x00, 0x04, 0xC0, 0xA8, 0x00, 0x01,
                                0x47, 0x22, 0x00, 0x00};
  CHECK(bytes.size() == 12);
  CHECK(std::memcmp(bytes.data(), expected, 12) == 0);
  const auto other = fl::EncodeServiceInfo(0x16, 0x0A000002, 80);
  const uint8_t expected_other[12] = {0x00, 0x00, 0x00, 0x16, 0x0A, 0x00, 0x00, 0x02,
                                      0x00, 0x50, 0x00, 0x00};
  CHECK(std::memcmp(other.data(), expected_other, 12) == 0);
}

void ConnectStatusValues() {
  // XNET_CONNECT_STATUS_*: the game polls XNetGetConnectStatus and moves on at CONNECTED (2).
  CHECK(fl::kConnectIdle == 0);
  CHECK(fl::kConnectPending == 1);
  CHECK(fl::kConnectConnected == 2);
  CHECK(fl::kConnectLost == 3);
}

void WinsockErrors() {
  // What a Winsock caller sees for a failed connect: in progress for a non-blocking socket,
  // refused, timed out, unreachable; anything else is a generic network failure.
  CHECK(fl::WinsockErrorFromErrno(EINPROGRESS) == 10036);
  CHECK(fl::WinsockErrorFromErrno(EWOULDBLOCK) == 10035);
  CHECK(fl::WinsockErrorFromErrno(EALREADY) == 10037);
  CHECK(fl::WinsockErrorFromErrno(ECONNREFUSED) == 10061);
  CHECK(fl::WinsockErrorFromErrno(ETIMEDOUT) == 10060);
  CHECK(fl::WinsockErrorFromErrno(ENETUNREACH) == 10051);
  CHECK(fl::WinsockErrorFromErrno(EHOSTUNREACH) == 10065);
  CHECK(fl::WinsockErrorFromErrno(EISCONN) == 10056);
  CHECK(fl::WinsockErrorFromErrno(ECONNRESET) == 10054);  // the server closed the connection
  CHECK(fl::WinsockErrorFromErrno(ENOTCONN) == 10057);
  CHECK(fl::WinsockErrorFromErrno(ECONNABORTED) == 10053);
  CHECK(fl::WinsockErrorFromErrno(ENOENT) == 10050);  // WSAENETDOWN for anything unlisted
}

// The 360 game's request, as captured on a Switch: acct GetUserIdsByXuids for one Xbox ID.
std::string GameLookupFrame(uint32_t word, const std::string& xuid) {
  return fesl::EncodeFrame("acct", word,
                           "TXN=GetUserIdsByXuids\nxuids.[]=1\nxuids.0=" + xuid + "\n");
}

std::string HelloFrame() {
  return fesl::EncodeFrame("fsys", 0x80000001, "TXN=Hello\nclientString=nfs-360\n");
}

std::vector<uint8_t> Bytes(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::string Text(const std::vector<uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

void FrameEncoding() {
  const std::string frame = fesl::EncodeFrame("fsys", 0x80000001, "TXN=Hello\n");
  CHECK(frame.size() == 12 + 10 + 1);
  CHECK(frame.substr(0, 4) == "fsys");
  CHECK(static_cast<uint8_t>(frame[4]) == 0x80 && static_cast<uint8_t>(frame[7]) == 0x01);
  CHECK(static_cast<uint8_t>(frame[11]) == frame.size());  // total length, header included
  CHECK(frame.back() == '\0');
}

void LookupIsAnsweredLocally() {
  fesl::Shim shim(2802565664681494ULL, "Racer1234");
  const std::string request = GameLookupFrame(0x80000002, "2802565664681494");
  const fesl::Translation out = shim.FromGame(Bytes(request).data(), request.size());
  CHECK(out.to_server.empty());  // the PC server cannot answer this, so it never sees it
  const std::string reply = Text(out.to_game);
  CHECK(reply.substr(0, 4) == "acct");
  CHECK(static_cast<uint8_t>(reply[4]) == 0x80 && static_cast<uint8_t>(reply[7]) == 0x02);
  CHECK(reply.find("TXN=GetUserIdsByXuids\n") != std::string::npos);
  const std::string user_id = std::to_string(fesl::LocalUserId(2802565664681494ULL));
  CHECK(reply.find("xuids.{2802565664681494}.userId=" + user_id + "\n") != std::string::npos);
  CHECK(reply.find("userInfo.0.userName=Racer1234\n") != std::string::npos);
  CHECK(fesl::LocalUserId(2802565664681494ULL) == fesl::LocalUserId(2802565664681494ULL));
  CHECK(fesl::LocalUserId(2802565664681494ULL) != 0);
}

void OtherFramesPassThrough() {
  fesl::Shim shim(1, "Racer0001");
  const std::string hello = HelloFrame();
  const fesl::Translation out = shim.FromGame(Bytes(hello).data(), hello.size());
  CHECK(Text(out.to_server) == hello);
  CHECK(out.to_game.empty());
}

void SplitFramesAreHeldUntilComplete() {
  fesl::Shim shim(1, "Racer0001");
  const std::string request = GameLookupFrame(0x80000002, "1");
  const fesl::Translation first = shim.FromGame(Bytes(request).data(), 20);
  CHECK(first.to_server.empty() && first.to_game.empty());
  const fesl::Translation second =
      shim.FromGame(Bytes(request).data() + 20, request.size() - 20);
  CHECK(second.to_server.empty());
  CHECK(!second.to_game.empty());
}

void TwoFramesInOneBuffer() {
  fesl::Shim shim(1, "Racer0001");
  const std::string hello = HelloFrame();
  const std::string both = hello + GameLookupFrame(0x80000002, "1");
  const fesl::Translation out = shim.FromGame(Bytes(both).data(), both.size());
  CHECK(Text(out.to_server) == hello);
  CHECK(!out.to_game.empty());
}

void GarbageIsForwardedUntouched() {
  fesl::Shim shim(1, "Racer0001");
  const std::string junk = "not a frame at all, just text";
  const fesl::Translation out = shim.FromGame(Bytes(junk).data(), junk.size());
  CHECK(Text(out.to_server) == junk);
  CHECK(out.to_game.empty());
  // Once the stream stopped looking like frames it stays out of the way.
  const std::string request = GameLookupFrame(0x80000002, "1");
  const fesl::Translation later = shim.FromGame(Bytes(request).data(), request.size());
  CHECK(Text(later.to_server) == request);
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

// Guest code as big-endian bytes, and a small guest memory for the thunks to read.
std::vector<uint8_t> Code(std::initializer_list<uint32_t> words) {
  std::vector<uint8_t> bytes;
  for (uint32_t word : words) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      bytes.push_back(static_cast<uint8_t>(word >> shift));
    }
  }
  return bytes;
}

struct ThunkRun {
  bool ok = false;
  uint32_t target = 0;
  uint64_t regs[13] = {};
  std::map<uint32_t, uint32_t> memory;
  bool Run(const std::vector<uint8_t>& code) {
    return ok = rex::system::RunTailCallThunk(
               code.data(), code.size(), regs,
               [this](uint32_t address) { return memory.at(address); }, &target);
  }
};

void AdjustorThunkMovesThisAndJumps() {
  // lwz r10,4(r3); lwz r11,12(r3); lwz r9,8(r3); add r3,r11,r10; mtctr r9; bctr (0x82A30D40)
  ThunkRun run;
  run.regs[3] = 0x3000;
  run.memory = {{0x3004, 0x10}, {0x3008, 0x82A30D58}, {0x300C, 0x7000}};
  CHECK(run.Run(Code({0x81430004, 0x8163000C, 0x81230008, 0x7C6B5214, 0x7D2903A6, 0x4E800420})));
  CHECK(run.regs[3] == 0x7010);
  CHECK(run.target == 0x82A30D58);
  CHECK(run.regs[10] == 0x10 && run.regs[11] == 0x7000 && run.regs[9] == 0x82A30D58);
}

void VirtualCallThunkReadsTheVtableSlot() {
  // lwz r12,0(r3); lwz r11,68(r12); mtctr r11; bctr (0x82A2BD58)
  ThunkRun run;
  run.regs[3] = 0x1000;
  run.memory = {{0x1000, 0x2000}, {0x2044, 0x82AB0000}};
  CHECK(run.Run(Code({0x81830000, 0x816C0044, 0x7D6903A6, 0x4E800420})));
  CHECK(run.target == 0x82AB0000);
  CHECK(run.regs[12] == 0x2000);
  CHECK(run.regs[3] == 0x1000);  // untouched: the real method still gets its own this
}

void MoveAndAddThunkKeepsArguments() {
  // or r11,r5,r5; or r5,r6,r6; add r11,r3,r11; lwz r11,0(r11); mtctr r11; bctr
  ThunkRun run;
  run.regs[3] = 0x100;
  run.regs[5] = 0x10;
  run.regs[6] = 0x20;
  run.memory = {{0x110, 0x82001234}};
  CHECK(run.Run(Code({0x7CAB2B78, 0x7CC53378, 0x7D635A14, 0x816B0000, 0x7D6903A6, 0x4E800420})));
  CHECK(run.regs[5] == 0x20);
  CHECK(run.target == 0x82001234);
}

void AddressArithmeticThunk() {
  // lis r11,0x82c6; ori r11,r11,0x8000; lwz r11,4(r11); mtctr r11; bctr
  ThunkRun run;
  run.memory = {{0x82C68004u, 0x82777777}};
  CHECK(run.Run(Code({0x3D6082C6, 0x616B8000, 0x816B0004, 0x7D6903A6, 0x4E800420})));
  CHECK(run.target == 0x82777777);
}

void CodeThatIsNotAPureTailCallIsRefused() {
  ThunkRun run;
  run.regs[3] = 0x1000;
  run.memory = {{0x1000, 0x2000}, {0x2044, 0x82AB0000}};
  CHECK(!run.Run(Code({0x4E800020})));                                          // blr
  CHECK(!run.Run(Code({0x81830000, 0x816C0044, 0x7D6903A6, 0x4E800421})));      // bctrl
  CHECK(!run.Run(Code({0x81830000, 0x90830000, 0x7D6903A6, 0x4E800420})));      // stw
  CHECK(!run.Run(Code({0x48000001, 0x4E800420})));                              // bl
  CHECK(!run.Run(Code({0x81A30000, 0x7DA903A6, 0x4E800420})));                  // reads r13
  CHECK(!run.Run(Code({0x81830000, 0x816C0044, 0x7D6903A6})));                  // runs out of code
  std::vector<uint32_t> loop(40, 0x39600001);                                   // never reaches bctr
  std::vector<uint8_t> long_code;
  for (uint32_t word : loop) {
    for (int shift = 24; shift >= 0; shift -= 8) long_code.push_back(static_cast<uint8_t>(word >> shift));
  }
  CHECK(!run.Run(long_code));
}

void AsyncMessagesAreTheFiveThousandRange() {
  CHECK(xa::IsAsyncTaskMessage(0x0005000E));
  CHECK(xa::IsAsyncTaskMessage(0x000500C6));
  CHECK(!xa::IsAsyncTaskMessage(0x00058004));  // logon and service queries are 0x580xx
  CHECK(!xa::IsAsyncTaskMessage(0x0007000E));
  CHECK(!xa::IsAsyncTaskMessage(0x0000000E));
}

void AsyncTaskFieldsAreReadBigEndian() {
  uint8_t task[xa::kTaskSize] = {};
  const auto put = [&](size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) task[offset + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
  };
  put(0x00, 0x0604);      // ordinal
  put(0x18, 0x40001000);  // marshalled request pointer
  put(0x1C, 0x58);        // marshalled request size
  put(0x2C, 0x40002000);  // results pointer
  put(0x30, 0x200);       // results size
  const xa::Task decoded = xa::DecodeTask(task);
  CHECK(decoded.ordinal == 0x0604);
  CHECK(decoded.request_ptr == 0x40001000);
  CHECK(decoded.request_size == 0x58);
  CHECK(decoded.results_ptr == 0x40002000);
  CHECK(decoded.results_size == 0x200);
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
  OnlineCallNames();
  InitialNotifications();
  ServiceInfoBytes();
  ConnectStatusValues();
  WinsockErrors();
  FrameEncoding();
  LookupIsAnsweredLocally();
  OtherFramesPassThrough();
  SplitFramesAreHeldUntilComplete();
  TwoFramesInOneBuffer();
  GarbageIsForwardedUntouched();
  AdjustorThunkMovesThisAndJumps();
  VirtualCallThunkReadsTheVtableSlot();
  MoveAndAddThunkKeepsArguments();
  AddressArithmeticThunk();
  CodeThatIsNotAPureTailCallIsRefused();
  AsyncMessagesAreTheFiveThousandRange();
  AsyncTaskFieldsAreReadBigEndian();
  SeedPersists(folder);
  SeedRepairs(folder);
  std::cout << "fake_live: all checks passed\n";
  return EXIT_SUCCESS;
}
