# Online Stage 2a: Reach the Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (the owner chose inline execution). Steps use checkbox (`- [ ]`) syntax.

**Goal:** With `online_fake_live = true` and `online_server_host` set, the game's lookups of EA service names (`fesl.ea.com`, `messaging.ea.com`, and any other `*.ea.com`) resolve to the configured server, and the log shows the game connecting to it and what it sends first.

**Architecture:** A pure helper in `fake_live.h` decides which names are redirected (unit-tested). The SDK's `NetDll_XNetDnsLookup` stops being a stub: it resolves the configured host with `getaddrinfo` and fills the `XNDNS` result. On the Switch, the BSD socket service is started once, lazily, with small buffers. Warn-level traces record every DNS lookup, `connect`, and the first bytes of the first sends, so the next hardware run shows whether the game expects TLS.

**Tech Stack:** Same as Stage 1. libnx `socketInitialize` on the Switch (`__SWITCH__`).

**Spec:** `docs/superpowers/specs/2026-10-31-online-features-design.md` (Stage 2). Findings so far: `docs/superpowers/findings/stage1-call-trace.md`.

## Global Constraints

- The server address is runtime config only (`online_server_host`, default empty). No public host is written into any committed file. The owner has approval from the server's operator to connect this game to `brake.go.ro`; it goes in the owner's local test config only.
- With `online_fake_live` off, or `online_server_host` empty, behaviour is exactly as today (DNS lookups keep failing, no socket service started).
- Memory: the Switch process has about 4 MB free. The socket service must use small buffers (target under 256 KiB of transfer memory) and start only when both settings are on.
- Nothing is logged that could hold credentials: payload traces cover only the first 32 bytes of the first 3 sends per socket.
- Plain TCP only for now. TLS handling is decided after the first trace shows what the game sends.

## Review Focus

1. A name that merely ends in `ea.com` (`notea.com`) or an empty name must not be redirected.
2. `online_server_host` set to an IP literal must work without a lookup; a host that does not resolve must report failure to the game (status non-zero) and never leave `XNDNS` uninitialized.
3. Socket start failing (no Wi-Fi, no bsd service) must log once and leave the game running with lookups failing, not crash.
4. Lookups must not block the game's main thread for long: a 5 second cap on resolution.

## File Structure

| File | Responsibility |
|---|---|
| `reference-nfsmw-nx/sdk/include/rex/system/xam/fake_live.h` (modify) | `IsEaServiceHost(std::string_view)`, `kDnsOk`, `kDnsHostNotFound` |
| `tests/fake_live_test.cpp` (modify) | tests for the helper |
| `reference-nfsmw-nx/sdk/src/kernel/xam/xam_net.cpp` (modify) | real `NetDll_XNetDnsLookup`, Switch socket start, traces in `connect`/`sendto`/`send` |
| `reference-nfsmw-nx/sdk/src/system/xam/user_profile.cpp` (modify) | defines `online_server_host` next to `online_fake_live` |
| `docs/CONFIG.md`, `config/nfscarbon.toml`, `CHANGELOG.md` (modify) | document the new setting |
| `patches/horizon-sdk.patch` (regenerate) | exported SDK changes |

---

### Task 1: Which names get redirected (pure, tested)

**Files:** Modify `fake_live.h`, `tests/fake_live_test.cpp`.

**Interfaces:**
- Produces in `namespace rex::system::xam::fake_live`: `bool IsEaServiceHost(std::string_view host)` is true for `ea.com` and any name ending in `.ea.com`, ignoring ASCII case; false for empty, `notea.com`, `ea.com.evil.example`, `example.com`. `constexpr int32_t kDnsOk = 0, kDnsHostNotFound = 11001;`

- [ ] **Step 1: Write the failing test `EaServiceHosts`** with these assertions: `IsEaServiceHost("fesl.ea.com")`, `("MESSAGING.EA.COM")`, `("ea.com")`, `("a.b.ea.com")` true; `("")`, `("notea.com")`, `("ea.com.evil.example")`, `("example.com")`, `("fesl.ea.co")` false; `kDnsOk == 0`, `kDnsHostNotFound == 11001`.
- [ ] **Step 2: Run `tests/check_fake_live.ps1`; expect a compile failure** (`IsEaServiceHost` is not a member).
- [ ] **Step 3: Implement it** in `fake_live.h` (lowercase compare, suffix test with a leading dot or exact match).
- [ ] **Step 4: Run the check; expect `fake_live: all checks passed`.**
- [ ] **Step 5: Commit** the test file and note that the header is carried by the patch.

### Task 2: Real DNS lookup, socket start and traces

**Files:** Modify `xam_net.cpp`, `user_profile.cpp`.

**Interfaces:**
- Consumes: `fake_live::IsEaServiceHost`, `kDnsOk`, `kDnsHostNotFound`, `UserProfile::fake_live()`.
- Produces: `REXCVAR_DEFINE_STRING(online_server_host, "", "Online", ...)` in `user_profile.cpp`; in `xam_net.cpp`: `NetDll_XNetDnsLookup_entry` returning an `XNDNS` with `status` 0 and `aina[0]` set on success.

- [ ] **Step 1: Define `online_server_host`** (restart-required) next to `online_fake_live`.
- [ ] **Step 2: Implement lookup:** when `fake_live()` is on, `online_server_host` is non-empty and `IsEaServiceHost(host)` is true, resolve the configured host (IP literal directly, otherwise `getaddrinfo`, IPv4 only, 5 s cap); fill `XNDNS` (`status = kDnsOk`, `cina = 1`, `aina[0]` in network order). Otherwise keep the old failing result with `status = kDnsHostNotFound` (initialized). Always warn-trace the requested name, the chosen target and the result (first 20 lookups).
- [ ] **Step 3: Switch socket start** (`#if defined(__SWITCH__)`): a function `EnsureSocketService()` called before the first redirected lookup, using `socketInitialize` with a small `SocketInitConfig` (tcp buffers 0x4000, udp 0x2400, `sb_efficiency` 1, `num_bsd_sessions` 3); log one warn line with the result code; remember failure so it is not retried.
- [ ] **Step 4: Traces** in `NetDll_connect_entry` (remote address and port) and `NetDll_sendto_entry`/`NetDll_send_entry` (length and first 32 bytes in hex, first 3 per socket), warn level, only when `fake_live()`.
- [ ] **Step 5: Syntax-check** the edited files with the Switch flags (`syntax_check.py`); expect `ok:` for each.
- [ ] **Step 6: Docs and config:** add `online_server_host` to `docs/CONFIG.md`, a commented line in `config/nfscarbon.toml` (no real host), and a changelog line.
- [ ] **Step 7: Export the patch** (`git diff` against the pin with the usual pathspecs), verify it applies to the pinned source, commit.

### Task 3: Build and hardware run (owner)

- [ ] **Step 1:** Apply the incremental patch to the owner's SDK clone and run `scripts/build-switch.ps1 -Jobs 2` from the main checkout.
- [ ] **Step 2:** Stage a test folder whose `nfscarbon.toml` has `online_fake_live = true`, `online_server_host = "brake.go.ro"`, `log_level = "info"`.
- [ ] **Step 3:** The owner runs it, opens the online entry, and sends the logs. Record in the findings file: the names looked up, whether `connect` happened to which port, the first bytes sent (a TLS handshake starts `16 03`), and the server's reaction.

## Self-Review

Spec coverage: host redirect and connection trace (Stage 2) are Tasks 1 and 2; TLS and protocol differences are deliberately left for the findings of Task 3. Types: `IsEaServiceHost`, `kDnsOk`, `kDnsHostNotFound` are named the same in all tasks.
