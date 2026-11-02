# Stage 4: 360 account flow (FESL SDK 2.9.5.1) - static analysis

Image: `standard-image.bin`, load 0x82000000. All addresses are VAs. `sub_X` = recompiled function; instruction
addresses are VAs in the original image. Reading the recompiled C++ comments + raw words; nothing was executed.

## TL;DR

The second `GetUserIdsByXuids`+`GetTos` is NOT triggered by the AddAccount reply content. It is the state machine
itself: **XBL360AddAccount success -> go back to the lookup state (3)**; the lookup reply does not contain the
key shape the game looks for, so it falls into the "no such user" branch again -> state 5 (GetTos) again.

The game looks for exactly one key family in the lookup reply:

    userIds.{<own xuid, decimal>}.userId = <non-zero int64>

Our reply uses `xuids.{<xuid>}.userId` (prefix `xuids.{`), which the lookup state never reads. Fix = rename that
key. With a match the machine goes to state 4 = `XBL360Login`.

## 0. Architecture (ConnMgr = FSM, `sub_829E2880` ctor)

ConnMgr derives from an FSM (`fsm.cpp`, strings "mStates.find(stateId) == mStates.end()"). States are registered by
`sub_829E3738` via `sub_829E1710(connMgr, id, obj)`; a transition is `sub_829E1708(connMgr, id, arg)` which is
`b sub_82A2A1F8`: exit current state (vtable slot 2), look up `id` in the state map (+4), enter new state
(vtable slot 1 = Enter). Errors go through `sub_829E2350(connMgr, kind, code, 0)` (kinds 3/5 -> `sub_829E2000`,
others -> `sub_829E1E38`; kind 6/7 etc. are reported to listeners).

State ids (from `sub_829E3738`, vtable in brackets, Enter = vtable slot 1):

| id | vtable | Enter | role |
|----|--------|-------|------|
| 3 | 0x8203C880 | `sub_82A31548` | `GetUserIdsByXuids` lookup (own xuid) |
| 4 | 0x8203C8D0 | `sub_82A31668` | `XBL360Login` |
| 5 | 0x8203C970 | `sub_82A31730` | `GetTos`; this is `Xb360PromptCreateUser` (strings 0x412E4/0x41308); sends `XBL360AddAccount` |
| 9 | 0x8204107C | `sub_82A2AED0` | after login: "ConnMgr: Communicator connected / Unable to connect to Communicator/Msgconn" (messenger), then Theater/Gamebrowser, Club, Achievements strings (0x40DC6-0x40EC8) |

Request builders are vtable methods of the account-service object (vtable 0x82033560; created by `sub_82967238`,
fetched by `sub_8294EA18`; slot offset = 4*slot): off 40 `GetTos` (`sub_82965B90`), off 88 `XBL360Login`
(`sub_829662A0`), off 92 `XBL360UpdateAccount` (`sub_82966760`), off 96 `XBL360AddAccount` (`sub_829666C0`/
`sub_829663A8`), off 132 `GetUserIdsByXuids` (`sub_82966D78`). The command-name -> id globals are filled once by
`sub_82964628` (e.g. 0x82B9BC78 = "GetUserIdsByXuids").

## 1. GetUserIdsByXuids request and reply parser

Request builder `sub_82966D78`: sets `TXN`, `xuids.[]` = count, then per entry `xuids.%d` (fmt 0x82033824) = the
xuid via `sub_82952738` (formats "%I64d", signed decimal). Matches what the game was observed to send.

Reply parser: two key-enumerator "classes" share base ctor `sub_8295BEA8` (vtable 0x82032C80) + init `sub_8295BF28`
(stores data ptr at +72, **prefix at +40**, **suffix at +8**):

* `sub_82964D20` : prefix `"xuids.{"`, suffix `"}"` (GetXuidsByUserIds style; users: `sub_829D48E8`, `sub_829D60E8`,
  `sub_82618F88`, not the account FSM).
* `sub_82964ED0` : prefix `"userIds.{"`, suffix `"}.userId"`. **This is the one used for GetUserIdsByXuids**:
  called at 0x82A31108 in the reply handler `sub_82A31098`; also by `sub_829D4A40`, `sub_829D5670`, `sub_82A12650`.
* `sub_82965080` : prefix `"screenNames.{"` suffix `"}"` (GetScreenNamesByUserIds).

Key composition: `sub_8295C008` is `snprintf(buf, n, "%s%s%s", prefix(+40), <key>, suffix(+8))`
(format 0x82089280). So the value key is `userIds.{<key>}.userId`.

Enumeration `sub_8295C088` (vtable slot 17): walks the reply's key list (`sub_829DFA40`, cursor at +76); a key
qualifies if `strstr(key, prefix) == key` (0x8295C100-0x8295C10C) and, with a non-empty suffix, `strstr(key+len(prefix),
suffix)` is found (0x8295C14C-0x8295C158); the text between them (must be non-empty, 0x8295C1A0-0x8295C1A4) is the
inner key. `sub_82964FC0` (next) does: inner key -> `sscanf("%I64d")` into out1 (0x82965010), then `sub_82964F48`
re-formats `%I64d`, fetches `prefix+inner+suffix` as int64 (`sub_8295CB08` -> `sub_829527A0`, default 0) into out2
and returns **out2 != 0**. Only the first qualifying key is examined (called once).

Count keys like `userIds.[]`, `userInfo.[]` and per-index shapes `userInfo.%i.*` are NOT read here.
(`userInfo.[]`, `userInfo.%i.userName/userId/xuid` is the separate LookupUserInfo parser `sub_82965240`/`sub_82965298`,
used by `LookupUserCb` in `sub_829C45F8`.)

Handler `sub_82A31098(state, txn)`:
1. `r3 = sub_82951A88(txn)` = txn+32 = reply result code (set in `sub_829D2758` from the protocol getter
   `sub_829D0918`, which reads key `errorCode`, default 0). Non-zero -> assert + `sub_829E2350(cm, 3, -201)` (error).
2. Else build `userIds.{`/`}.userId` enumerator (0x82A31108), call next (0x82A3111C).
3. If next returned true (key present AND value != 0): `ld r10,80(r1)` (parsed xuid) is compared with `ld r11,16(state)`
   (own xuid saved in Enter, 0x82A31130-0x82A31138). Equal -> found. (The userId value `r1+88` is never read afterwards.)
4. Not found: executes an SDK assert (`"false"`, line 303, non-fatal on hardware) and falls through.
5. 0x82A31184-0x82A311D8: `r30 = connMgr[124]` (ConnectionData); **found -> state 4**; **not found -> state 4 if
   `r30[20]` is 0 or 1, else state 5**. `r30[20]` is the first word of the embedded ConnectionInfo (ctor
   `sub_82A29558`/`sub_8296BE68` default `li r4,2`; the game builds it with type 2 in `sub_8262EE08`).
   Type 2 + the observed GetTos on hardware agree: not found -> state 5.

Enter `sub_82A31548` (state 3): zero own xuid, get user obj `sub_8294F7D8` (client+96), own xuid via `sub_82942B28`
(0 = ok; else error -252), then `acct->slot132(txn, &xuid, 1)`, reply callback `sub_82A31098`, timeout 10000.

## 2. State machine, in order

```
3 GetUserIdsByXuids --found (userIds.{ownXuid}.userId != 0)--> 4 XBL360Login --ok--> 9 messenger/Theater...
        |  not found (connection type 2)                         |  code 101: type 0/1 handling, else -> 5
        v                                                        |  other code: error kind 7
5 GetTos -> listener OnTos(text) -> game UI accepts -> slot 8 GetExtra360Info/XOverlapped
   -> per-frame slot 3 `sub_82A31A58` (poll overlapped; when done) -> `sub_82A31820` sends XBL360AddAccount
   -> reply `sub_82A30B18`: code 0 -> back to state 3
```

Details:
* State 5 Enter `sub_82A31730`: if `connectionData[573] != 0` it skips the request and calls listeners with an empty
  TOS (`sub_82A2B8B0`, thunk 0x82A2BD58 = listener vtable +68); otherwise sends `GetTos` (off 40) with callback
  `sub_82A312D0`. That handler: code 0 -> read raw length at txn+12, `ConnectionData::AllocateTOSBuffer`
  (`sub_82A28D80`), read key **`tos`** into it, notify listeners (OnTos); non-zero code -> error kind 6.
  GetTos request fields: `TXN` only.
* XBL360AddAccount request (`sub_829663A8`): `TXN`, then if the extra-info struct exists: `firstName lastName street1
  street2 city state zipCode lang email country age`, then `gamertag` (<=31 chars, `sub_829424C8`) and `xuid`
  (`%I64d`). Sent from `sub_82A31820` (off 96), reply callback `sub_82A30B18`.
* **AddAccount reply handler `sub_82A30B18`** reads ONLY the result code (txn+32):
  * 0 -> `sub_829E1708(cm, 3, 0)` (0x82A30B50): **back to state 3**. It does not read `lkey`, `userId`, `profileId`,
    `displayName` or `name` at all (nothing stored).
  * 21 with `errorContainer.[]>0` and `errorContainer.0.fieldName == "Account.BirthDate"` -> error -165.
  * anything else -> `sub_829E2350(cm, 6, code)`.

## 3. Why the second lookup + GetTos, and the fix

Observed sequence = state 3 (reply w/o `userIds.{xuid}.userId` -> not found) -> 5 (GetTos) -> user accepts ->
AddAccount ok -> state 3 again (same request) -> not found again (same wrong reply) -> state 5 Enter again =
second GetTos. After that the game waits for the TOS listener/UI which already finished, so it idles on
"Connecting" and only answers server pings. This chain is fully supported by the code above (state ids, branch at
0x82A31184, 0x82A30B50). The "stuck on Connecting after the second GetTos" part (UI/listener side) was NOT traced.

Corrected GetUserIdsByXuids reply (found case), `<xuid>` = the decimal from the request's `xuids.0`, byte for byte:

```
TXN=GetUserIdsByXuids
userIds.[]=1
userIds.{<xuid>}.userId=<uid>
```
Rules: key prefix must be exactly `userIds.{`, suffix exactly `}.userId`; `<uid>` must be non-zero decimal int64;
the own xuid must be the FIRST key matching that prefix (send a single entry); no `errorCode` key (or 0).
`userIds.[]` is harmless and unread. The `xuids.{...}`, `userInfo.*` keys of the old reply are ignored by this state
and can be dropped.

Two valid shim strategies:
* Skip TOS/create: always answer "found". Game goes 3 -> 4 and sends `XBL360Login`; the shim must then create the
  PC account on demand (AddAccount) inside the XBL360Login translation.
* Keep TOS/create: first lookup "not found" (reply `TXN=GetUserIdsByXuids` only, no `userIds.*`) -> TOS -> AddAccount
  (reply just `TXN=XBL360AddAccount`, no errorCode needed) -> second lookup must be "found" (remember per xuid).
  A "not found" reply still makes the SDK fire a non-fatal assert (seen on hardware, harmless).

Confidence: high (~90%) that the key shape is the blocker and that "found" -> `XBL360Login`; every branch was read
from raw words. Not verified: runtime value of `connectionData+20` (static: type 2, consistent with hardware),
what the game UI does with the 2nd `tos` reply, and the `errorCode` getter link (slot 3 of the protocol vtable
0x8203B280 = `sub_829D0918`, inferred from `sub_829D2758` calling slot offset 12 and the key string at 0x8203B248).

## 4. XBL360Login (state 4)

Enter `sub_82A31668`: `acct->slot88(txn, userObj)`, reply callback `sub_82A30A10`, timeout 10000.
Request builder `sub_829662A0` (TXN tag "acct"): keys in order
* `TXN=XBL360Login`
* `xuid` = own xuid, `%I64d` decimal (`sub_82942B28`, `sub_82952738`)
* `gamertag` = console gamertag (<=31 chars, `sub_829424C8`)
* `machineId`, `macAddr` = strings from the client config object (vtable slot 1 of the object at builder+4)
No password/lkey/name keys.

Reply handler `sub_82A30A10`, result code = `errorCode` of the reply:
* **0 (success)**: parser `sub_82A304B0` reads exactly two keys: **`lkey`** (string, buffer 64 -> max 63 chars,
  `sub_82951D88`) and **`userId`** (int64 decimal, `sub_829527A0`, default 0). Then `sub_829E1690(cm, name=gamertag
  from the console (not from the reply), lkey, userId)`: copies lkey into ConnMgr+132 (64 bytes, later used for the
  Communicator/Theater logins) and `sub_829C4348(userMgr, 0, gamertag, userId)` creates the local user (id=userId,
  name=gamertag, xuid) -> transition to **state 9** (messenger "Communicator/Msgconn", then Theater/Gamebrowser).
* **101**: `sub_82A2A8F8`: for connection type 0/1 either retries (`sub_829E1B80`) or errors -220; for other types it
  returns 0 and the handler moves to **state 5** (GetTos/create).
* any other code: error kind 7 with that code (`sub_829E2350(cm, 7, code)`).

Not consumed by the game in this flow: `profileId`, `displayName`, `name`, `TXN` value. Our 360 reply
`TXN=XBL360Login lkey=.. userId=..` is therefore sufficient (extra keys harmless). `userId` should be the same
persona/user id the later Theater/messenger frames expect (it becomes the local user id).

## Evidence index (key instructions)

* Reply key strings: `xuids.{` 0x82033374, `}` 0x8203337C, `userIds.{` 0x82033380, `}.userId` 0x8203338C,
  `screenNames.{` 0x82033398, `userInfo.[]` 0x82033438.
* Compose: `sub_8295C008` `bl snprintf` with fmt "%s%s%s" (0x82089280), args prefix(+40), key, suffix(+8).
* Branch to state 4/5: 0x82A31184-0x82A311D8; own-xuid compare 0x82A31130-0x82A31144; match flag `r31`.
* AddAccount -> state 3: 0x82A30B50 `li r4,3; bl 0x829E1708`.
* State registration: `sub_829E3738` (`li r4,3/4/5/9` before each `bl 0x829E1710`).
