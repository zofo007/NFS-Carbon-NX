# Online Stage 1: Fake Xbox Live Sign-in Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** With an opt-in setting on, the Switch build reports a Live-signed-in profile with online privileges and a live network link, so the game's online menus open (Milestone 1).

**Architecture:** One header-only module of pure functions (`fake_live.h`) decides every faked value and is unit-tested on its own. Thin edits in the SDK's Xam user and network calls read a new `online_fake_live` setting and delegate to it. Which `XNetLogon*` calls to answer is decided by an observation task first, because the game's real call pattern is not known yet.

**Tech Stack:** C++23, the ReXGlue SDK kernel layer (`reference-nfsmw-nx/sdk`), MinGW g++ for unit tests (same as the existing `tests/check_*.ps1`), PowerShell scripts.

**Spec:** `docs/superpowers/specs/2026-10-31-online-features-design.md` (Stage 1 only; Stages 2 and 3 get their own plans once Stage 1's findings are in).

## Global Constraints

- Opt-in: setting `online_fake_live`, default `false`, in `nfscarbon.toml`. With it `false`, every call below behaves exactly as today.
- No server address, public host, or hard-coded online identity anywhere in the repository.
- The fake identity is generated once and stored under the user data folder (`user/`), never in `game-data/`.
- Edition under test: title 454107EC, media 51D2B53F. No other edition is in scope.
- No added memory or measurable CPU cost on the game's three cores (these calls are rare; do nothing per frame).
- Never commit game files, `standard-image.bin`, or any EA material.
- Source edits go in the separate git clones next to this repository (`reference-nfsmw-nx/sdk`, `carbon-source`) in the main checkout `C:\Users\Daniel\Documents\NFS-Carbon-NX`; this worktree has no toolchains, game data or clones. Tests, docs and exported patches are what get committed on this branch. `scripts/export-source-patches.ps1` regenerates `patches/horizon-sdk.patch`.
- Stage 1 may stop and ask the owner if the menus need the Xbox account HTTP service (`/xuacs/...`); see Task 6.

## Review Focus

1. Identity file missing, empty, corrupt, or on a read-only folder: the game must still start; the profile must never sign in with XUID 0 or an empty gamertag.
2. `online_fake_live=false`: sign-in state 1, every privilege 0, link status 0, address as today (a regression here breaks offline play for everyone).
3. Gamertag longer than the game's 15-character buffer, or containing non-ASCII bytes.
4. XUID must be stable across launches, differ between installs, and have the online-XUID form (top 16 bits `0x0009`).
5. Users 1 to 3 must still read as not signed in, and `XamUserCheckPrivilege` with user index `0xFF` (all users) must still be accepted.

## File Structure

| File | Responsibility |
|---|---|
| `reference-nfsmw-nx/sdk/include/rex/system/xam/fake_live.h` (create) | Pure functions and constants: sign-in state, privileges, link status, `XNADDR` status, NAT type, identity generation, seed load/create. No kernel dependencies. |
| `reference-nfsmw-nx/sdk/src/kernel/xam/xam_user.cpp` (modify) | Defines the `online_fake_live` setting; sign-in, name, XUID, privilege calls delegate to `fake_live.h` when it is on. |
| `reference-nfsmw-nx/sdk/include/rex/system/xam/user_profile.h` (modify) | `xuid()`, `name()`, `signin_state()` return the fake identity when the setting is on. |
| `reference-nfsmw-nx/sdk/src/kernel/xam/xam_net.cpp` (modify) | Link status and `XNetGetTitleXnAddr` delegate to `fake_live.h` when on. |
| `reference-nfsmw-nx/sdk/src/kernel/xam/xam_misc.cpp` (modify) | Real implementations for the `XNetLogon*` calls Task 1 shows the game uses (replacing `REX_EXPORT_STUB`). |
| `tests/fake_live_test.cpp`, `tests/check_fake_live.ps1` (create) | Unit tests for `fake_live.h`, same style as `tests/atomic_file_test.cpp` and `tests/check_atomic_file.ps1`. |
| `docs/superpowers/findings/stage1-call-trace.md` (create) | Task 1's recorded call order and signatures. |
| `docs/CONFIG.md`, `config/nfscarbon.toml`, `CHANGELOG.md` (modify) | Document the new setting. |
| `patches/horizon-sdk.patch` (regenerate) | Exported SDK changes. |

---

### Task 1: Observe which online calls the game makes

**Files:**
- Create: `docs/superpowers/findings/stage1-call-trace.md`

**Interfaces:**
- Produces: for each online call the game makes before and while opening the online menu: name, call order, and for each `XNetLogon*` call its argument list and what the game does with the result. Tasks 4 and 5 consume this.

- [ ] **Step 1: Build and launch the host build**

Run `scripts/build-host.ps1`, then `scripts/run-host.ps1`. Expected: the game window opens and reaches the main menu. If it cannot (crash, black screen), stop: record why in the findings file and continue with Step 5 using the Switch instead.

- [ ] **Step 2: Reach the online menu entry**

In the game, choose the online entry in the main menu. Expected today: sign-in prompt or "unavailable" message.

- [ ] **Step 3: Collect the stub log lines**

Every unimplemented call logs `<name> STUB - returning <value>` (see `REX_STUB_RETURN` in `sdk/include/rex/hook.h`). Search the host log folder (`build/host-user` and `logs/`) for `STUB` lines mentioning `Xam`, `XNet`, `NetDll`, `XUser`, `XSession`, `XOnline`. Also note every `XamUser*` and `NetDll_XNet*` call that has a real implementation and is reached (add a temporary log line in a local, uncommitted edit if the log does not show it).

- [ ] **Step 4: Find signatures at the call sites**

For each `XNetLogon*` name seen, search `carbon-source/generated/default/*.h` for its call sites, read the registers set before the call (`r3` onwards) and how `r3` is used after it. Record the argument types and the return-value meaning (a guess is acceptable if marked as one).

- [ ] **Step 5: Write `stage1-call-trace.md` and commit**

List calls in the order first seen, each with: real or stub, arguments, what the game does next (continues, retries, shows an error). End with the list Task 5 must implement. Run `git add docs/superpowers/findings/stage1-call-trace.md` then `git commit -m "docs: record stage 1 online call trace"`.

---

### Task 2: Pure fake-Live module with unit tests

**Files:**
- Create: `reference-nfsmw-nx/sdk/include/rex/system/xam/fake_live.h`
- Create: `tests/fake_live_test.cpp`, `tests/check_fake_live.ps1`

**Interfaces:**
- Produces, in `namespace rex::system::xam::fake_live`:
  - `constexpr uint32_t kSigninLocal = 1, kSigninLive = 2;`
  - `uint32_t SigninState(bool live)` returns `kSigninLive` or `kSigninLocal`.
  - `uint32_t PrivilegeValue(bool live, uint32_t privilege_id)` returns 1 when `live` and `privilege_id != 251` (communications friends-only restricts rather than grants; ids are the Xbox 360 `XPRIVILEGE_TYPE` values, confirm against Xenia's `xam_user.cc` notes), otherwise 0.
  - `uint32_t EthernetLinkStatus(bool live)` returns `0x01 | 0x02 | 0x08` (active, 100 Mbps, full duplex) when live, else 0.
  - `uint32_t XnAddrStatus(bool live)` returns `0x04 | 0x02 | 0x20 | 0x40 | 0x80` (static, ethernet, gateway, DNS, online) when live, else `0x04` (today's value).
  - `constexpr uint32_t kNatOpen = 1;`
  - `struct Identity { uint64_t xuid; char gamertag[16]; uint8_t mac[6]; uint64_t machine_id; };`
  - `Identity MakeIdentity(uint64_t seed)`: deterministic; `xuid >> 48 == 0x0009`; `gamertag` is `Racer` plus four digits and a NUL; `mac[0] == 0x02` (locally administered, unicast); `machine_id != 0`.
  - `uint64_t LoadOrCreateSeed(const std::filesystem::path& file)`: returns the stored seed; if the file is missing, unreadable, or does not hold exactly 8 bytes, creates a new random nonzero seed, tries to write it (using `rex/filesystem/atomic_file.h`; read its API first), and returns it even if the write fails.

- [ ] **Step 1: Write the failing tests in `tests/fake_live_test.cpp`**

Use the `CHECK` macro style of `tests/atomic_file_test.cpp`. Tests (name: assertion):
  - `OffIsUnchanged`: `SigninState(false)==1`, `PrivilegeValue(false, 254)==0`, `EthernetLinkStatus(false)==0`, `XnAddrStatus(false)==0x04`.
  - `OnReportsLive`: `SigninState(true)==2`, `PrivilegeValue(true, 254)==1`, `PrivilegeValue(true, 251)==0`, `EthernetLinkStatus(true)==0x0B`, `XnAddrStatus(true)==0xE6`.
  - `IdentityShape`: for seeds `1`, `0xFFFFFFFFFFFFFFFF`, `0x123456789ABCDEF0`: `xuid>>48==0x0009`, `strlen(gamertag)==9`, all gamertag bytes printable ASCII, `mac[0]==0x02`, `machine_id!=0`.
  - `IdentityStable`: `MakeIdentity(7)` equals itself byte for byte on two calls; `MakeIdentity(7).xuid != MakeIdentity(8).xuid`.
  - `SeedPersists`: in a temp folder, two calls of `LoadOrCreateSeed` on one path return the same nonzero value.
  - `SeedRepairs`: a file containing `"abc"` and an empty file each give a nonzero seed and no crash; a path inside a missing folder gives a nonzero seed.

- [ ] **Step 2: Write `tests/check_fake_live.ps1`**

Copy `tests/check_atomic_file.ps1`, rename to `fake-live`, pass a fresh temp folder as the argument as the atomic-file check does.

- [ ] **Step 3: Run it to make sure it fails**

Run `powershell -File tests/check_fake_live.ps1` from the main checkout. Expected: compile error, `fake_live.h` not found.

- [ ] **Step 4: Implement `fake_live.h`** per the Interfaces block (header-only, `inline`/`constexpr`, no kernel includes). Seed randomness: `std::random_device`, retry until nonzero. Identity fields from `seed` with a fixed mixing function (splitmix64).

- [ ] **Step 5: Run it to make sure it passes**

Run the same command. Expected: log ends without `FAIL`, exit code 0.

- [ ] **Step 6: Commit**

`git add tests/fake_live_test.cpp tests/check_fake_live.ps1` and `git commit -m "test: unit tests for the fake Live module"`. (The header lives in the SDK clone; it is exported by the patch in Task 7.)

---

### Task 3: Setting, sign-in, name, XUID and privileges

**Files:**
- Modify: `reference-nfsmw-nx/sdk/src/kernel/xam/xam_user.cpp` (`XamUserGetSigninState_entry`, `XamUserGetSigninInfo_entry`, `XamUserGetName_entry`, `XamUserGetXUID_entry`, `XamUserCheckPrivilege_entry`)
- Modify: `reference-nfsmw-nx/sdk/include/rex/system/xam/user_profile.h` (`xuid()`, `name()`, `signin_state()`)
- Modify: `docs/CONFIG.md`, `config/nfscarbon.toml`, `CHANGELOG.md`

**Interfaces:**
- Consumes: everything in Task 2's Interfaces block.
- Produces: `REXCVAR_DEFINE_BOOL(online_fake_live, false, "Online", "Pretend the profile is signed in to Xbox Live so the online menus open")` in `xam_user.cpp`; a process-wide `const fake_live::Identity& fake_live_identity()` helper (declared in `user_profile.h`, defined in `xam_user.cpp`) that loads the seed once from `<user_data_root>/online_identity.bin` and caches the identity; Task 4 and 5 call it.

- [ ] **Step 1: Add the setting and `fake_live_identity()`**; read the existing cvar declaration style at `xam_user.cpp:31` and the `REXCVAR_DECLARE` macro at `sdk/include/rex/cvar.h:340` for use from other files.

- [ ] **Step 2: Delegate the five calls**: when `online_fake_live` is true, user index 0 uses the fake identity, `SigninState(true)`, and `PrivilegeValue(true, mask)`; indexes 1 to 3 stay "not signed in"; `0xFF` stays accepted. When false, leave the code path exactly as today. Log once at startup at info level: `fake Live on: <gamertag> <xuid hex>`.

- [ ] **Step 3: Document the setting**: add `online_fake_live = false` with a one-line comment to `config/nfscarbon.toml`, a row to `docs/CONFIG.md`, and a CHANGELOG line under a new unreleased heading.

- [ ] **Step 4: Verify the off case**: build the host build (`scripts/build-host.ps1`) with the setting off; launch; confirm the log has no `fake Live on` line and the online menu behaves as in Task 1 Step 2.

- [ ] **Step 5: Verify the on case**: set `online_fake_live = true` in the host config; launch; confirm the log line shows a `Racer####` gamertag and an XUID starting `0009`; confirm `online_identity.bin` exists in the host user folder and is 8 bytes; relaunch and confirm the same XUID. Also confirm, from temporary log lines, that `XamUserGetSigninState` for users 1 to 3 still returns 0 and `XamUserCheckPrivilege` with user index `0xFF` returns success.

- [ ] **Step 6: Commit** the docs and config: `git add docs/CONFIG.md config/nfscarbon.toml CHANGELOG.md`, `git commit -m "docs: document online_fake_live"`.

---

### Task 4: Network link and title address

**Files:**
- Modify: `reference-nfsmw-nx/sdk/src/kernel/xam/xam_net.cpp` (`NetDll_XNetGetEthernetLinkStatus_entry`, `NetDll_XNetGetTitleXnAddr_entry`)

**Interfaces:**
- Consumes: `fake_live::EthernetLinkStatus`, `fake_live::XnAddrStatus`, `fake_live_identity()`.

- [ ] **Step 1: Link status**: return `fake_live::EthernetLinkStatus(online_fake_live)`.
- [ ] **Step 2: Title address**: when on, set `ina` to `10.0.0.2` (network order), `inaOnline` to `10.0.0.2`, `wPortOnline` to 3074, `abEnet` to the identity MAC, `abOnline` to the 20 bytes (identity `machine_id` little-endian in the first 8, zeros after), and return `XnAddrStatus(true)`. When off, keep today's loopback code unchanged.
- [ ] **Step 3: Verify**: host build, setting on, relaunch, open the online menu; record in `stage1-call-trace.md` (append a "After Task 4" section) whether the menu behaviour changed and any new `STUB` lines that now appear.
- [ ] **Step 4: Commit** the findings update: `git add docs/superpowers/findings/stage1-call-trace.md`, `git commit -m "docs: stage 1 findings after link and address"`.

---

### Task 5: Answer the `XNetLogon*` calls the game uses

**Files:**
- Modify: `reference-nfsmw-nx/sdk/src/kernel/xam/xam_misc.cpp` (lines around 234-250: the `XNetLogon*` stubs the game actually calls, per Task 1)

**Interfaces:**
- Consumes: `docs/superpowers/findings/stage1-call-trace.md` for the call list and signatures, `fake_live_identity()`, `fake_live::kNatOpen`.

- [ ] **Step 1: Implement each called `XNetLogon*` as a real entry** (replace its `REX_EXPORT_STUB` with a `REX_EXPORT` of an `_entry` function, following the style of `xam_user.cpp`): `XNetLogonGetMachineID` writes `identity.machine_id` and returns success; `XNetLogonGetTitleID` writes title id `0x454107EC`; `XNetLogonGetNatType` returns `kNatOpen`; any other called one returns success with a believable value, chosen from Task 1's findings. When `online_fake_live` is false, keep the old stub behaviour.
- [ ] **Step 2: Verify**: host build, setting on; the stub log lines for the implemented names are gone; online menu behaviour recorded in the findings file ("After Task 5").
- [ ] **Step 3: Commit** the findings update with `git commit -m "docs: stage 1 findings after logon calls"`.

---

### Task 6: Milestone 1 check, Switch run and patch export

**Files:**
- Regenerate: `patches/horizon-sdk.patch`
- Modify: `docs/superpowers/findings/stage1-call-trace.md`

- [ ] **Step 1: Host exit test**: with `online_fake_live = true`, the online entry opens its online screen with no sign-in prompt and no "online unavailable" message, and the log shows the faked sign-in being queried. If it does not, find the first call or HTTP request the game stalls on (log, last `STUB`, a connection attempt to `fesl.ea.com`/`messaging.ea.com`), record it in the findings file, and stop here: ask the owner whether to continue (this is the spec's account-service decision point). Do not continue to Step 2.
- [ ] **Step 2: Regenerate patches**: `scripts/export-source-patches.ps1`. Expected: `patches/horizon-sdk.patch` now contains `fake_live.h` and the edits in `xam_user.cpp`, `xam_net.cpp`, `xam_misc.cpp`, `user_profile.h`; `patches/carbon-switch.patch` is unchanged in content.
- [ ] **Step 3: Switch run**: build with `scripts/build-switch.ps1`, package with `scripts/make-release.ps1` (or the hardware-test packaging script the owner normally uses), and have the owner run it with the setting on. Ask them for the log (`scripts/pack-logs.ps1`) and whether the online screen opened. Record the result in the findings file.
- [ ] **Step 4: Commit**: `git add patches/horizon-sdk.patch docs/superpowers/findings/stage1-call-trace.md`, `git commit -m "feat: fake Xbox Live sign-in behind online_fake_live"`.
- [ ] **Step 5: Hand back**: summarize for the owner what opened, what did not, and what Stage 2's plan needs (the first connection the game attempts after the menu opens).

## Self-Review

- **Spec coverage:** Stage 1 items (Live state, privileges, link, `XNADDR`/MAC/machine ID, `XNetLogon*`, opt-in setting, identity stored in `user/`, host-first testing, Xbox account service decision point, exit test) map to Tasks 2 to 6. Stages 2 and 3 are deliberately out of this plan.
- **Placeholders:** none; the only open items (privilege ids, `XNetLogon` signatures) are explicit work in Task 2 and Task 1.
- **Types:** `fake_live_identity()`, `Identity`, `SigninState`, `PrivilegeValue`, `EthernetLinkStatus`, `XnAddrStatus`, `kNatOpen` are named identically in Tasks 2 to 5.
- **Proportion:** the plan is longer than the spec's Stage 1 section because it pins names, values and checks; it carries no function bodies.
