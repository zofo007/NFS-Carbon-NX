#include <rex/system/tail_call_thunk.h>
#include <rex/system/xam/fake_live.h>
#include <rex/system/xam/fesl_shim.h>
#include <rex/system/xam/overlapped_recv.h>
#include <rex/system/xam/udp_tunnel.h>
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
namespace overlapped = rex::system::xam::overlapped;
namespace tn = rex::system::xam::tunnel;

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
  // The game reads only userIds.{<own xuid>}.userId (an int64 above zero) from the lookup reply.
  CHECK(reply.find("userIds.[]=1\n") != std::string::npos);
  CHECK(reply.find("userIds.{2802565664681494}.userId=" + user_id + "\n") != std::string::npos);
  CHECK(reply.find("xuids.{") == std::string::npos);
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

constexpr uint64_t kSecret = 0x1122334455667788ULL;

uint32_t WordOf(const std::string& frame) {
  return (uint32_t{static_cast<uint8_t>(frame[4])} << 24) | (uint32_t{static_cast<uint8_t>(frame[5])} << 16) |
         (uint32_t{static_cast<uint8_t>(frame[6])} << 8) | uint32_t{static_cast<uint8_t>(frame[7])};
}

// The value of `key` in the frame's "key=value" lines, or "<missing>".
std::string Value(const std::string& frame, const std::string& key) {
  const std::string body = "\n" + frame.substr(12);
  const size_t at = body.find("\n" + key + "=");
  if (at == std::string::npos) {
    return "<missing>";
  }
  const size_t start = at + key.size() + 2;
  return body.substr(start, body.find('\n', start) - start);
}

std::string AddAccountRequest(uint32_t word) {
  return fesl::EncodeFrame("acct", word,
                           "TXN=XBL360AddAccount\neaMailFlag=0\nthirdPartyMailFlag=0\nlang=0\n"
                           "country=0\nage=0\ngamertag=Racer8236\nxuid=2802565664681494\n");
}

std::string LoginRequest(uint32_t word) {
  return fesl::EncodeFrame("acct", word,
                           "TXN=XBL360Login\ngamertag=Racer8236\nxuid=2802565664681494\n");
}

std::string PcSession(uint32_t word) {
  return fesl::EncodeFrame("acct", word,
                           "lkey=KEY.1\nname=Racer8236\nprofileId=7\nTXN=Login\nuserId=164681494\n"
                           "displayName=Racer8236\n");
}

std::string PcError(const std::string& txn, uint32_t word, const std::string& code) {
  return fesl::EncodeFrame("acct", word,
                           "TXN=" + txn + "\nlocalizedMessage=\"no\"\nerrorCode=" + code +
                               "\nerrorContainer.[]=0\n");
}

fesl::Translation FromServer(fesl::Shim& shim, const std::string& frame) {
  return shim.FromServer(Bytes(frame).data(), frame.size());
}

void PasswordIsStableAndFits() {
  const std::string password = fesl::AccountPassword(kSecret);
  CHECK(password.size() == 12);  // the PC server accepts 1 to 16 characters
  for (const char c : password) {
    CHECK((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'));
  }
  CHECK(password == fesl::AccountPassword(kSecret));
  CHECK(password != fesl::AccountPassword(kSecret + 1));
}

void AddAccountBecomesPcAddAccountThenLogin() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = AddAccountRequest(0x80000004);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  CHECK(step.to_game.empty());
  const std::string add = Text(step.to_server);
  CHECK(add.substr(0, 4) == "acct");
  CHECK(Value(add, "TXN") == "AddAccount");
  CHECK(Value(add, "name") == "Racer8236");
  CHECK(Value(add, "password") == fesl::AccountPassword(kSecret));
  step = FromServer(shim, fesl::EncodeFrame("acct", WordOf(add), "TXN=NuAddAccount\n"));
  CHECK(step.to_game.empty());  // the game never sees the PC-only step
  const std::string login = Text(step.to_server);
  CHECK(Value(login, "TXN") == "Login");
  CHECK(Value(login, "name") == "Racer8236");
  CHECK(Value(login, "password") == fesl::AccountPassword(kSecret));
  step = FromServer(shim, PcSession(WordOf(login)));
  CHECK(step.to_server.empty());
  const std::string done = Text(step.to_game);
  CHECK(done.substr(0, 4) == "acct" && WordOf(done) == 0x80000004);
  CHECK(Value(done, "TXN") == "XBL360AddAccount");
  CHECK(Value(done, "lkey") == "KEY.1");
  CHECK(Value(done, "userId") == "164681494");
  CHECK(Value(done, "profileId") == "7");
  CHECK(Value(done, "displayName") == "Racer8236");
}

void ExistingPcAccountStillLogsIn() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = AddAccountRequest(0x80000004);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  const uint32_t add_word = WordOf(Text(step.to_server));
  step = FromServer(shim, PcError("NuAddAccount", add_word, "160"));  // name already taken
  CHECK(Value(Text(step.to_server), "TXN") == "Login");  // the login decides whether it is ours
}

void RejectedAddAccountIsReportedToTheGame() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = AddAccountRequest(0x80000004);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  const uint32_t add_word = WordOf(Text(step.to_server));
  step = FromServer(shim, PcError("NuAddAccount", add_word, "21"));
  CHECK(step.to_server.empty());
  const std::string reply = Text(step.to_game);
  CHECK(WordOf(reply) == 0x80000004);
  CHECK(Value(reply, "TXN") == "XBL360AddAccount");
  CHECK(Value(reply, "errorCode") == "21");
}

void FailedLoginAfterAddIsReportedToTheGame() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = AddAccountRequest(0x80000004);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  step = FromServer(shim, fesl::EncodeFrame("acct", WordOf(Text(step.to_server)), "TXN=NuAddAccount\n"));
  const uint32_t login_word = WordOf(Text(step.to_server));
  step = FromServer(shim, PcError("Login", login_word, "101"));
  const std::string reply = Text(step.to_game);
  CHECK(Value(reply, "TXN") == "XBL360AddAccount");
  CHECK(Value(reply, "errorCode") == "101");
}

void LoginIsTranslatedToPcLogin() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = LoginRequest(0x80000005);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  CHECK(step.to_game.empty());
  const std::string login = Text(step.to_server);
  CHECK(Value(login, "TXN") == "Login");
  CHECK(Value(login, "name") == "Racer8236");
  step = FromServer(shim, PcSession(WordOf(login)));
  const std::string done = Text(step.to_game);
  CHECK(WordOf(done) == 0x80000005);
  CHECK(Value(done, "TXN") == "XBL360Login");
  CHECK(Value(done, "lkey") == "KEY.1");
}

void LoginCreatesTheAccountWhenItIsMissing() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = LoginRequest(0x80000005);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  step = FromServer(shim, PcError("Login", WordOf(Text(step.to_server)), "101"));
  CHECK(step.to_game.empty());
  const std::string add = Text(step.to_server);
  CHECK(Value(add, "TXN") == "AddAccount");
  step = FromServer(shim, fesl::EncodeFrame("acct", WordOf(add), "TXN=NuAddAccount\n"));
  const std::string login = Text(step.to_server);
  CHECK(Value(login, "TXN") == "Login");
  step = FromServer(shim, PcSession(WordOf(login)));
  CHECK(Value(Text(step.to_game), "TXN") == "XBL360Login");
  CHECK(Value(Text(step.to_game), "lkey") == "KEY.1");
}

void LoginWithTakenNameReportsTheLoginError() {
  fesl::Shim shim(2802565664681494ULL, "Racer8236", kSecret);
  const std::string request = LoginRequest(0x80000005);
  fesl::Translation step = shim.FromGame(Bytes(request).data(), request.size());
  step = FromServer(shim, PcError("Login", WordOf(Text(step.to_server)), "101"));
  step = FromServer(shim, PcError("NuAddAccount", WordOf(Text(step.to_server)), "160"));
  CHECK(step.to_server.empty());  // someone else owns the name: do not loop
  const std::string reply = Text(step.to_game);
  CHECK(Value(reply, "TXN") == "XBL360Login");
  CHECK(Value(reply, "errorCode") == "101");
}

void ServerFramesPassThroughToTheGame() {
  fesl::Shim shim(1, "Racer0001", kSecret);
  const std::string memcheck = fesl::EncodeFrame("fsys", 0x80000000, "TXN=MemCheck\nsalt=1\n");
  const fesl::Translation whole = FromServer(shim, memcheck);
  CHECK(Text(whole.to_game) == memcheck);
  CHECK(whole.to_server.empty());
  const fesl::Translation first = shim.FromServer(Bytes(memcheck).data(), 7);
  CHECK(first.to_game.empty());
  const fesl::Translation rest = shim.FromServer(Bytes(memcheck).data() + 7, memcheck.size() - 7);
  CHECK(Text(rest.to_game) == memcheck);
}

void ServerGarbageIsForwardedUntouched() {
  fesl::Shim shim(1, "Racer0001", kSecret);
  const std::string junk = "not a frame at all, just text";
  CHECK(Text(FromServer(shim, junk).to_game) == junk);
  const std::string memcheck = fesl::EncodeFrame("fsys", 0x80000000, "TXN=MemCheck\n");
  CHECK(Text(FromServer(shim, memcheck).to_game) == memcheck);  // still raw once it stopped parsing
}

void Base64Decodes() {
  CHECK(fesl::Base64Decode("VFhOPVN0YXJ0") == "TXN=Start");
  CHECK(fesl::Base64Decode("W109MQpwbGF5ZXJzLjAub3duZXJJZD03Cg==") == "[]=1\nplayers.0.ownerId=7\n");
  CHECK(fesl::Base64Decode("").empty());
}

// A request the game splits over frames: each carries "size=<total base64 characters>" (seen on
// hardware: size=18644 over ten frames) and a slice of base64.
std::string Fragment(const std::string& command, uint32_t word, const std::string& slice) {
  return fesl::EncodeFrame(command, word, "size=60\ndata=" + slice + "\n");
}

void FragmentedMatchRequestIsDecodedForTheTrace() {
  fesl::Shim shim(1, "Racer0001", kSecret);
  const std::string first = Fragment("pnow", 0xB000000E, "VFhOPVN0YXJ0CnBsYXllcnMu");
  const std::string second = Fragment("pnow", 0xB000000E, "W109MQpwbGF5ZXJzLjAub3duZXJJZD03Cg==");
  fesl::Translation out = shim.FromGame(Bytes(first).data(), first.size());
  CHECK(Text(out.to_server) == first);  // the server still gets every byte as sent
  CHECK(shim.TakeTrace().empty());      // nothing to show until the last piece arrives
  out = shim.FromGame(Bytes(second).data(), second.size());
  CHECK(Text(out.to_server) == second);
  const std::vector<std::string> trace = shim.TakeTrace();
  CHECK(trace.size() == 1);
  CHECK(trace[0].find("pnow") != std::string::npos);
  CHECK(trace[0].find("TXN=Start|players.[]=1|players.0.ownerId=7|") != std::string::npos);
  CHECK(shim.TakeTrace().empty());  // taken once
}

void OtherFragmentedCommandsAreNotTraced() {
  fesl::Shim shim(1, "Racer0001", kSecret);
  const std::string whole = Fragment("mtrx", 0xB0000006, "VFhOPVN0YXJ0CnBsYXllcnMuW109MQpwbGF5ZXJzLjAub3duZXJJZD03Cg==");
  shim.FromGame(Bytes(whole).data(), whole.size());
  CHECK(shim.TakeTrace().empty());
}

void CallRingKeepsTheLastCallsOldestFirst() {
  fl::CallRing<4> ring;
  CHECK(ring.Snapshot(3).empty());
  for (uint32_t i = 1; i <= 6; ++i) {
    ring.Record(i * 10, 7, "Call", {i, 0, 0, 0});
  }
  const auto last_three = ring.Snapshot(3);
  CHECK(last_three.size() == 3);
  CHECK(last_three[0].args[0] == 4 && last_three[1].args[0] == 5 && last_three[2].args[0] == 6);
  CHECK(last_three[2].time_ms == 60 && last_three[2].thread == 7);
  CHECK(std::string(last_three[0].name) == "Call");
  CHECK(ring.Snapshot(100).size() == 4);  // never more than it holds
  CHECK(ring.LastCallMs() == 60);
}

// --- The game's UDP tunnel (encrypted datagrams) -------------------------------------------------

std::vector<uint8_t> Hex(const char* text) {
  std::vector<uint8_t> bytes;
  for (const char* p = text; *p;) {
    if (*p == ' ') {
      ++p;
      continue;
    }
    bytes.push_back(static_cast<uint8_t>(std::stoi(std::string(p, 2), nullptr, 16)));
    p += 2;
  }
  return bytes;
}

std::vector<uint8_t> Concat(std::initializer_list<std::vector<uint8_t>> parts) {
  std::vector<uint8_t> all;
  for (const auto& part : parts) {
    all.insert(all.end(), part.begin(), part.end());
  }
  return all;
}

const char kTunnelKey[] = "9181081919";
// The first datagram the game sent on hardware (23:54:39.545): CONNECT with channel-7 identity.
const char kFirstDatagram[] = "00 00 5c c5 de 2e b7 4a 8b d7 ac a5 14 6d c0 38 0b b5 7b 62";

void Rc4MatchesTheStandardVector() {
  const std::vector<uint8_t> stream = tn::Rc4Keystream("Key", 0, 9);
  const std::vector<uint8_t> expected = Hex("eb 9f 77 81 b7 34 ca 72 a7");  // RC4("Key") keystream
  CHECK(stream == expected);
  // Skipping bytes gives the same stream from further on.
  CHECK(tn::Rc4Keystream("Key", 4, 5) == std::vector<uint8_t>(expected.begin() + 4, expected.end()));
}

void CapturedDatagramDecodes() {
  const std::vector<uint8_t> wire = Hex(kFirstDatagram);
  tn::Datagram datagram;
  CHECK(tn::Decode(wire.data(), wire.size(), kTunnelKey, &datagram));
  CHECK(datagram.prefix == 0);
  CHECK(datagram.packets.size() == 2);
  CHECK(datagram.packets[0].channel == 7 && datagram.packets[0].body == Hex("00 e1 49 76 72 00"));
  CHECK(datagram.packets[1].channel == 1 && datagram.packets[1].body == Hex("00 00 00 01 b6 ce 59 2c"));
}

void EncodeReproducesTheCapturedDatagram() {
  tn::Datagram datagram;
  datagram.prefix = 0;
  datagram.packets = {{7, Hex("00 e1 49 76 72 00")}, {1, Hex("00 00 00 01 b6 ce 59 2c")}};
  CHECK(tn::Encode(datagram, kTunnelKey) == Hex(kFirstDatagram));
}

void GarbageIsNotADatagram() {
  const std::vector<uint8_t> junk = Hex("00 00 11 22 33 44 55");
  tn::Datagram datagram;
  CHECK(!tn::Decode(junk.data(), junk.size(), kTunnelKey, &datagram));
  CHECK(!tn::Decode(junk.data(), 3, kTunnelKey, &datagram));
}

// A CommUDP "active" packet as the 360 builds it: header (n older records | sequence, ack), the newest
// record, then each older record followed by its length.
std::vector<uint8_t> ActivePacket(uint32_t older, uint32_t sequence, uint32_t ack,
                                  const std::vector<std::vector<uint8_t>>& records) {
  std::vector<uint8_t> packet;
  const uint32_t first = (older << 28) | sequence;
  for (int shift = 24; shift >= 0; shift -= 8) packet.push_back(static_cast<uint8_t>(first >> shift));
  for (int shift = 24; shift >= 0; shift -= 8) packet.push_back(static_cast<uint8_t>(ack >> shift));
  for (size_t i = 0; i < records.size(); ++i) {
    packet.insert(packet.end(), records[i].begin(), records[i].end());
    if (i > 0) packet.push_back(static_cast<uint8_t>(records[i].size()));
  }
  return packet;
}

const std::vector<uint8_t> kTicketRecord = Hex("01 80 80 04 80 00 00 0a 31 39 35 32 34 30 35 34 37 31 04");
const std::vector<uint8_t> kFooterRecord = Hex("11 22 33 44 55 66 77 88 99 aa bb cc 40");

tn::Datagram AggregateDatagram(uint16_t prefix, uint32_t older, uint32_t sequence,
                               const std::vector<std::vector<uint8_t>>& records) {
  tn::Datagram datagram;
  datagram.prefix = prefix;
  datagram.packets = {{1, ActivePacket(older, sequence, 0xFF, records)}};
  return datagram;
}

std::vector<uint8_t> WithRecordBody(uint32_t sequence, const std::vector<uint8_t>& record) {
  std::vector<uint8_t> body = Hex("00 00 00 00 00 00 00 ff");
  body[0] = static_cast<uint8_t>(sequence >> 24);
  body[1] = static_cast<uint8_t>(sequence >> 16);
  body[2] = static_cast<uint8_t>(sequence >> 8);
  body[3] = static_cast<uint8_t>(sequence);
  body.insert(body.end(), record.begin(), record.end());
  return body;
}

void TicketBundleIsSplitIntoSingleRecords() {
  tn::ClientNormalizer normalizer(kTunnelKey);
  // seq 0x101 = ticket, one older record (seq 0x100, footer only): "10 00 01 01 ..." on the wire.
  const tn::Datagram bundle = AggregateDatagram(16, 1, 0x101, {kTicketRecord, kFooterRecord});
  const std::vector<uint8_t> wire = tn::Encode(bundle, kTunnelKey);
  const auto rewritten = normalizer.Normalize(wire.data(), wire.size());
  CHECK(rewritten.has_value());
  tn::Datagram out;
  CHECK(tn::Decode(rewritten->data(), rewritten->size(), kTunnelKey, &out));
  CHECK(out.prefix == 16);  // the stream position the game chose is kept
  CHECK(out.packets.size() == 2);
  CHECK(out.packets[0].channel == 1 && out.packets[0].body == WithRecordBody(0x100, kFooterRecord));
  CHECK(out.packets[1].channel == 1 && out.packets[1].body == WithRecordBody(0x101, kTicketRecord));
}

void OlderRecordsAreNotForwardedTwice() {
  tn::ClientNormalizer normalizer(kTunnelKey);
  const std::vector<uint8_t> first =
      tn::Encode(AggregateDatagram(16, 1, 0x101, {kTicketRecord, kFooterRecord}), kTunnelKey);
  CHECK(normalizer.Normalize(first.data(), first.size()).has_value());
  // The next tick: new footer-only record 0x102 on top of the history 0x101 (ticket), 0x100.
  const std::vector<uint8_t> second = tn::Encode(
      AggregateDatagram(27, 2, 0x102, {kFooterRecord, kTicketRecord, kFooterRecord}), kTunnelKey);
  const auto rewritten = normalizer.Normalize(second.data(), second.size());
  CHECK(rewritten.has_value());
  tn::Datagram out;
  CHECK(tn::Decode(rewritten->data(), rewritten->size(), kTunnelKey, &out));
  CHECK(out.packets.size() == 1);  // only the new record: 0x100 and 0x101 already went out
  CHECK(out.packets[0].body == WithRecordBody(0x102, kFooterRecord));
}

void NewestRecordIsSentAgainWhenRetransmitted() {
  tn::ClientNormalizer normalizer(kTunnelKey);
  const std::vector<uint8_t> wire =
      tn::Encode(AggregateDatagram(16, 1, 0x101, {kTicketRecord, kFooterRecord}), kTunnelKey);
  CHECK(normalizer.Normalize(wire.data(), wire.size()).has_value());
  // The same packet again (the game retransmits): the newest record goes out again in case the first
  // copy was lost; the older one does not.
  const auto again = normalizer.Normalize(wire.data(), wire.size());
  CHECK(again.has_value());
  tn::Datagram out;
  CHECK(tn::Decode(again->data(), again->size(), kTunnelKey, &out));
  CHECK(out.packets.size() == 1 && out.packets[0].body == WithRecordBody(0x101, kTicketRecord));
}

void PlainDatagramsAreLeftAlone() {
  tn::ClientNormalizer normalizer(kTunnelKey);
  const std::vector<uint8_t> connect = Hex(kFirstDatagram);
  CHECK(!normalizer.Normalize(connect.data(), connect.size()).has_value());  // no bundle in it
  const std::vector<uint8_t> junk = Hex("de ad be ef 00 11 22 33");
  CHECK(!normalizer.Normalize(junk.data(), junk.size()).has_value());        // not a datagram at all
}

overlapped::PendingReceive Receive(uint32_t socket, uint32_t overlapped_ptr, uint32_t buffer) {
  return {socket, buffer, 1000, 0x2000, 0x2010, overlapped_ptr, 9};
}

void PendingReceiveIsKeptPerOverlapped() {
  overlapped::PendingReceiveTable table;
  CHECK(!table.HasActive());
  table.Add(Receive(7, 0x3000, 0x1000));
  CHECK(table.HasActive() && table.IsPending(0x3000));
  table.Add(Receive(7, 0x3000, 0x1100));  // the same overlapped replaces, never duplicates
  CHECK(table.Active().size() == 1 && table.Active()[0].buffer == 0x1100);
  table.Add(Receive(8, 0x4000, 0x1200));
  CHECK(table.Active().size() == 2);
}

void CompletedReceiveIsCollectedOnce() {
  overlapped::PendingReceiveTable table;
  table.Add(Receive(7, 0x3000, 0x1000));
  uint32_t bytes = 0;
  CHECK(!table.Collect(0x3000, &bytes));  // nothing arrived yet
  table.Complete(0x3000, 28);
  CHECK(!table.HasActive());              // a completed receive is not polled again
  CHECK(table.IsPending(0x3000));         // but it stays until the game collects it
  CHECK(table.Collect(0x3000, &bytes) && bytes == 28);
  CHECK(!table.Collect(0x3000, &bytes));  // collected exactly once
  CHECK(!table.IsPending(0x3000));
}

void CancelDropsASocketsReceives() {
  overlapped::PendingReceiveTable table;
  table.Add(Receive(7, 0x3000, 0x1000));
  table.Add(Receive(8, 0x4000, 0x1200));
  table.CancelSocket(7);
  CHECK(!table.IsPending(0x3000) && table.IsPending(0x4000));
  uint32_t bytes = 0;
  table.Complete(0x3000, 5);  // completing something already cancelled changes nothing
  CHECK(!table.Collect(0x3000, &bytes));
}

void ServerPortReplacesOnlyTheLoginPort() {
  CHECK(fl::RedirectedPort(18220, 18210) == 18210);  // the 360's account port becomes the server's
  CHECK(fl::RedirectedPort(18215, 18210) == 18215);  // Theater keeps the port the server announced
  CHECK(fl::RedirectedPort(13505, 18210) == 13505);  // so does the messenger
  CHECK(fl::RedirectedPort(18220, 0) == 18220);      // no override set
  CHECK(fl::RedirectedPort(18220, -5) == 18220);
}

std::string HelloReply(uint32_t word) {
  return fesl::EncodeFrame("fsys", word,
                           "domainPartition.domain=eagames\nmessengerIp=188.27.48.18\nmessengerPort=13505\n"
                           "domainPartition.subDomain=NFS-2007\nTXN=Hello\nactivityTimeoutSecs=0\n"
                           "theaterIp=188.27.48.18\ntheaterPort=18215\n");
}

void TheaterAddressesAreRewrittenToTheReachableHost() {
  // The Hello on the account connection tells the process which address the server announces...
  fesl::Shim account(1, "Racer0001", kSecret, "188.27.34.215");
  FromServer(account, HelloReply(0x80000001));
  // ...and the Theater connection (a different socket, so a different shim) uses it.
  fesl::Shim theater(1, "Racer0001", kSecret, "188.27.34.215");
  const std::string entered = fesl::EncodeFrame(
      "EGEG", 0, "PL=PC\nTICKET=3175867091\nINT-PORT=19119\nINT-IP=188.27.48.18\nI=188.27.48.18\n"
                 "PEER-IP=10.0.0.5\nGID=235282\n");
  const std::string reply = Text(FromServer(theater, entered).to_game);
  CHECK(reply.substr(0, 4) == "EGEG" && WordOf(reply) == 0);
  CHECK(Value(reply, "INT-IP") == "188.27.34.215");
  CHECK(Value(reply, "I") == "188.27.34.215");
  CHECK(Value(reply, "PEER-IP") == "10.0.0.5");  // other players' addresses stay as they are
  CHECK(Value(reply, "INT-PORT") == "19119");
  CHECK(Value(reply, "GID") == "235282");
  CHECK(reply.back() == '\0');
}

void TheaterFramesWithoutTheAnnouncedAddressAreUntouched() {
  fesl::Shim account(1, "Racer0001", kSecret, "188.27.34.215");
  FromServer(account, HelloReply(0x80000001));
  fesl::Shim theater(1, "Racer0001", kSecret, "188.27.34.215");
  const std::string other = fesl::EncodeFrame("LDAT", 0, "NAME=Internet\nLID=257\nI=10.1.2.3\n");
  CHECK(Text(FromServer(theater, other).to_game) == other);
}

void EntryReplyStartsTheTunnelWithItsKey() {
  fesl::Shim theater(1, "Racer0001", kSecret);
  const std::vector<uint8_t> bundle = tn::Encode(
      AggregateDatagram(16, 1, 0x101, {kTicketRecord, kFooterRecord}), "4455667788");
  // Before any entry reply nothing is rewritten.
  CHECK(!tn::GlobalTunnel().Normalize(bundle.data(), bundle.size()).has_value());
  CHECK(tn::GlobalTunnel().ServerPort() == 0);
  const std::string entered =
      fesl::EncodeFrame("EGEG", 0, "TICKET=1\nEKEY=4455667788\nINT-PORT=19119\nGID=7\n");
  CHECK(Text(FromServer(theater, entered).to_game) == entered);  // the game still gets it as it was
  CHECK(tn::GlobalTunnel().Normalize(bundle.data(), bundle.size()).has_value());
  CHECK(tn::GlobalTunnel().ServerPort() == 19119);  // where the server's UDP socket really listens
  // A new entry starts the flow over: the same bundle is split in full again.
  const std::string again = fesl::EncodeFrame("EGEG", 0, "TICKET=2\nEKEY=4455667788\nGID=8\n");
  FromServer(theater, again);
  const auto rewritten = tn::GlobalTunnel().Normalize(bundle.data(), bundle.size());
  CHECK(rewritten.has_value());
  tn::Datagram out;
  CHECK(tn::Decode(rewritten->data(), rewritten->size(), "4455667788", &out));
  CHECK(out.packets.size() == 2);
}

void TheaterIsUntouchedWithoutAHost() {
  fesl::Shim account(1, "Racer0001", kSecret, "188.27.34.215");
  FromServer(account, HelloReply(0x80000001));
  fesl::Shim theater(1, "Racer0001", kSecret);  // no reachable host known
  const std::string entered = fesl::EncodeFrame("EGEG", 0, "INT-IP=188.27.48.18\n");
  CHECK(Text(FromServer(theater, entered).to_game) == entered);
}

void HelloPointsTheaterAndMessengerAtTheReachableHost() {
  fesl::Shim shim(1, "Racer0001", kSecret, "188.27.34.215");
  const std::string reply = Text(FromServer(shim, HelloReply(0x80000001)).to_game);
  CHECK(reply.substr(0, 4) == "fsys" && WordOf(reply) == 0x80000001);
  CHECK(Value(reply, "theaterIp") == "188.27.34.215");
  CHECK(Value(reply, "messengerIp") == "188.27.34.215");
  CHECK(Value(reply, "theaterPort") == "18215");  // ports stay as the server gave them
  CHECK(Value(reply, "messengerPort") == "13505");
  CHECK(Value(reply, "TXN") == "Hello");
  CHECK(Value(reply, "domainPartition.subDomain") == "NFS-2007");
  const uint32_t length = (uint32_t{static_cast<uint8_t>(reply[8])} << 24) |
                          (uint32_t{static_cast<uint8_t>(reply[9])} << 16) |
                          (uint32_t{static_cast<uint8_t>(reply[10])} << 8) |
                          uint32_t{static_cast<uint8_t>(reply[11])};
  CHECK(length == reply.size());  // the header length matches the rewritten body
  CHECK(reply.back() == '\0');
}

void HelloIsUntouchedWithoutAHost() {
  fesl::Shim shim(1, "Racer0001", kSecret);
  const std::string hello = HelloReply(0x80000001);
  CHECK(Text(FromServer(shim, hello).to_game) == hello);
}

void OtherFeslFramesKeepTheirAddresses() {
  fesl::Shim shim(1, "Racer0001", kSecret, "188.27.34.215");
  const std::string other =
      fesl::EncodeFrame("fsys", 0x80000004, "TXN=GetPingSites\ntheaterIp=188.27.48.18\n");
  CHECK(Text(FromServer(shim, other).to_game) == other);
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
  PasswordIsStableAndFits();
  AddAccountBecomesPcAddAccountThenLogin();
  ExistingPcAccountStillLogsIn();
  RejectedAddAccountIsReportedToTheGame();
  FailedLoginAfterAddIsReportedToTheGame();
  LoginIsTranslatedToPcLogin();
  LoginCreatesTheAccountWhenItIsMissing();
  LoginWithTakenNameReportsTheLoginError();
  ServerFramesPassThroughToTheGame();
  ServerGarbageIsForwardedUntouched();
  Base64Decodes();
  FragmentedMatchRequestIsDecodedForTheTrace();
  OtherFragmentedCommandsAreNotTraced();
  Rc4MatchesTheStandardVector();
  CapturedDatagramDecodes();
  EncodeReproducesTheCapturedDatagram();
  GarbageIsNotADatagram();
  TicketBundleIsSplitIntoSingleRecords();
  OlderRecordsAreNotForwardedTwice();
  NewestRecordIsSentAgainWhenRetransmitted();
  PlainDatagramsAreLeftAlone();
  PendingReceiveIsKeptPerOverlapped();
  CompletedReceiveIsCollectedOnce();
  CancelDropsASocketsReceives();
  CallRingKeepsTheLastCallsOldestFirst();
  TheaterAddressesAreRewrittenToTheReachableHost();
  TheaterFramesWithoutTheAnnouncedAddressAreUntouched();
  TheaterIsUntouchedWithoutAHost();
  EntryReplyStartsTheTunnelWithItsKey();
  ServerPortReplacesOnlyTheLoginPort();
  HelloPointsTheaterAndMessengerAtTheReachableHost();
  HelloIsUntouchedWithoutAHost();
  OtherFeslFramesKeepTheirAddresses();
  SeedPersists(folder);
  SeedRepairs(folder);
  std::cout << "fake_live: all checks passed\n";
  return EXIT_SUCCESS;
}
