# Stage 1 findings: what the game asks of Live

Status: static reading of the generated game code and the SDK only. No game run has been done yet; the runtime
sections below are filled in after the owner's first run with `online_fake_live = true`.

## Static: Live-related calls the game imports

From the recompiled Standard edition (`carbon-source/generated/default`), call counts are the number of mentions in
the generated headers, so they show what exists, not what runs.

| Call | Before this work |
|---|---|
| `XamUserGetSigninState`, `XamUserGetXUID`, `XamUserGetName`, `XamUserCheckPrivilege`, `XamUserAreUsersFriends`, `XamUserReadProfileSettings`, `XamUserWriteProfileSettings`, `XamShowSigninUI` | implemented (sign-in was "local" only, privileges all denied) |
| `NetDll_XNetGetTitleXnAddr`, `NetDll_XNetGetEthernetLinkStatus` | implemented (loopback address, link 0) |
| `NetDll_socket`, `bind`, `sendto`, `recvfrom`, `connect`, `listen`, `accept`, `select`, ... | implemented |
| `XNetLogonGetTitleID`, `XNetLogonGetMachineID` | stubs |
| `XamUserCreateStatsEnumerator`, `XamUserCreateAchievementEnumerator`, `XamShowSigninUIp` | stubs |
| `NetDll_XNetConnect`, `XNetQosLookup`, `XNetRegisterKey`, `XNetCreateKey`, `XSession*` | stubs (Stage 3 territory) |

Only two `XNetLogon*` names are imported by the game: `XNetLogonGetTitleID` and `XNetLogonGetMachineID`. The plan
listed `XNetLogonGetNatType` as a candidate; the game does not import it, so it is not implemented (YAGNI).

## Static: how the game uses the two logon calls

Both appear in the same function (generated `nfscarbon_recomp.125.cpp` near line 29380, a second copy in
`nfscarbon_recomp.95.cpp`), in this order, on a path that builds a request for Live's web services:

1. `XamAlloc(0, size, &out)`; negative result aborts the request.
2. `XNetLogonGetTitleID(5)` (the other site passes `2`); the result is stored on the stack as a field of the request.
   The argument is a service category. What the value should be is **a guess**: the SDK stub left `r3` unchanged,
   so the old behaviour was "return the argument". With the fake on we return the game's own title id `0x454107EC`.
3. `XamUserGetXUID(user, 2, &xuid)`; negative result aborts.
4. `XNetLogonGetMachineID(&buffer)`; **a negative result aborts**, anything else continues, and the buffer is a
   64-bit value copied into the request. We write the fake machine id there and return 0.
5. The next step (`sub_829182E0`) continues building the request; this is the first thing Stage 2 should look at,
   because it likely produces the `/xuacs/...` or `/xstats/...` HTTP calls the game's strings mention.

## Runtime: hardware runs on 2026-10-31 (Standard, 1.1.0 plus the online patch)

**Run 1 (crash at launch).** `fake Live on: Racer8236 0009f4eb2ae72e16` appeared, the game read sign-in state 2, then
crashed 16 ms later on a worker thread. The crash log put it in a game `strstr` called from `sub_82937B00`, a function
that lists entries from a private enumerator in a 2080-byte stack buffer (10 entries of 208 bytes). The port rejects
that enumerator, and `XamEnumerate` then returned an **uninitialized** item count on the bad handle, so the game walked
off the end of its stack. Fixed in `xam_enum.cpp` (`dummy = 0`). Only reachable when signed in to Live, which is why the
control run (setting off) booted fine.

**Run 2 (no crash, about 3 minutes).** The owner reached the online entry, it was clickable, and the game tried to
connect to EA's servers. Milestone 1 is met. From the log:

- The game polls `XNetGetEthernetLinkStatus` about every 30 s and gets `0x0B`.
- `XamCreateEnumeratorHandle(0xffffffff, 0xfc, 0x58039, 0x5803a, 0xd4, 0xa, 0x0, ptr)` is called and rejected (seen at
  17:25:47, 17:26:32, 17:27:40, 17:27:51). `0xd4` = 212 bytes per entry and `0xa` = 10 entries per call. **Guess:** a
  friends-list enumerator (the size is close to an `XONLINE_FRIEND` record).
- `NetDll_XNetUnregisterKey` is an unimplemented stub, called right before 3 of the 4 enumerator calls.
- No `XamUserCheckPrivilege`, `XNetLogonGetMachineID` or `XNetLogonGetTitleID` call was traced, so the title's own
  logon path has not run.
- The log does not name the host or port the game tried to reach: socket and DNS calls that are implemented do not log.
  That is the first thing Stage 2 needs.
