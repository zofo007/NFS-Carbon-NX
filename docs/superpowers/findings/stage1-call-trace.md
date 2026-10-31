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

## Runtime: to be filled in after the first run

Please run with `online_fake_live = true` and `log_level = "info"`, open the online entry in the main menu, and send
the log. What to look for, in this order:

- `fake Live on: Racer#### <xuid>` at startup (the fake identity is active).
- `STUB` warnings naming `Xam`, `XNet`, `NetDll` or `XSession` calls, in the order they appear.
- Whether the online screen opened, or which message appeared instead.
