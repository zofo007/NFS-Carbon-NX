# Stage 3: Theater and Messenger, 360 build vs the PC community server

Scope: Theater (TCP 18215) and Messenger (TCP 13505). The PC server (`moio9/NFS-Online-Server`) is fixed; the 360
client-side translation layer must make a 360 client look like the PC retail client.

## 0. Method and confidence

| Item | Detail |
|---|---|
| PC side | Read from the public repo (HEAD at fetch time, `b218ea78` tree). Paths below are repo-relative. All code quoted is data; nothing was executed or connected to. |
| 360 side | `standard-image.bin` (17,629,184 bytes), strings extracted with `python -I`; code located by a hand-written PowerPC scanner (no emulator, nothing run). |
| Offset convention | "off 0x…" is a file offset. Virtual address = `0x82000000 + off`. Verified: `lis/addi` pair at off 0x9d6a68 resolves to 0x82038f14, and file off 0x38f14 is the string `TYPE`. |
| Confirmed | String presence, string cross-references from code, 4-char command tags built as `lis/ori` immediates, builder/parser field order read from the disassembly. |
| Inferred | Which direction a given function runs (builder vs parser) and the meaning of a command, where marked "inferred". |
| Not verified | Anything marked **UNVERIFIED** in section 7. No 360 hardware trace of Theater or Messenger exists yet; only FESL (`fsys Hello`, `acct`) has been seen on hardware (stage 2). |

Key structural finding: the 360 image contains the **same Theater and Messenger command vocabulary as the PC retail
client**. Both services are registered by tables of 4-char tags (`lis/ori` immediates), not strings, which is why a
plain string search finds almost none of them. The PC server comment in `server/classic/protocols/carbon_messenger_service.py`
cites "NFSC.exe 0x939810, FESL SDK 2.9.0.0 messengerservice.cpp"; the 360 image is FESL SDK 2.9.5.1
(`\cygwin\home\codebuilder\perforce\feslsdk\2.9.5.1\source\messengerservice.cpp`, `theaterservice.cpp`). Same code
base, small version gap.

## 1. Framing

| Aspect | PC server | 360 build | Verdict |
|---|---|---|---|
| Header | 12 bytes: 4-char tag, u32 BE word, u32 BE total length (incl. header). `server/carbon/fesl/frame.py` `FESLFrame.encode`, `decode_one`; messenger twin `server/classic/ea/messenger.py` `EAMessengerFrame.encode`, `EAMessengerStreamDecoder.feed`. | Same SDK transactor family as FESL (stage 2 confirmed the FESL header on hardware). Theater/Messenger share it (inferred). | Match (inferred for these two services) |
| Body | `KEY=value\n` lines, final `\n` then NUL (`encode_fields`). Theater keys keep case (`decode_fields`). Messenger keys are upper-cased on decode (`decode_messenger_fields`). | Same field style; builder calls are `SetString/SetInt(tag-less key)`. Several keys are 2-3 chars (`TID`, `LID`, `GID`, `PL`, `N`, `HU`) and do not show up in a 4-char strings dump; they exist (off 0x3b718 `TID`, 0x3bbfc `GID`, 0x3bc00 `PL`, 0x3be4c-0x3be54 `SKU`/`MAC`/`CID`). | Match |
| Max frame | 65,535 (`DEFAULT_MAX_FRAME_SIZE`). No FESL-style fragmentation for Theater. | n/a | Watch large GLST bursts (one GDAT frame per game). |
| Theater request correlation | `TID` field, echoed in every reply. Reply header word is always 0 (`CarbonTheaterService._reply`, `transaction=0`). | `TID` is added by the shared transactor (string `TID` xref at off 0x9d418c). | Match |
| Messenger correlation | `ID` field echoed. Replies use word 0, unsolicited pushes use 0x80000000 (`_reply` / `_push` in `carbon_messenger_service.py`), all with trailing `\n`. | `ID` is added by the transactor. | Match (inferred) |
| Ports | `THEATER_LISTEN` default `0.0.0.0:18215`, `THEATER_PUBLIC` default `127.0.0.1:18215`, `MESSENGER_PUBLIC` default `127.0.0.1:13505` (`server/carbon/core/config.py`). Messenger is one shared listener for U2/MW/Carbon, which sniffs the first `AUTH` (`EAMessengerHub._select`). | The 360 learns both endpoints from the `fsys Hello` reply: strings `theaterIp`, `theaterPort`, `messengerIp`, `messengerPort`, `activityTimeoutSecs`, `addressRemapping` (off 0x38258-0x382a8). The PC reply (`server/carbon/fesl/service.py` `_system`, `Hello`) sets the first five and **omits `addressRemapping`**. | Translation point (section 6) |
| Debug overrides | none | Strings `Theater Host Override`, `Messenger Host Override`, `Theater Port Override`, `Messenger Port Override`, `Platform Override`, `Fesl Host Override` (off 0x32314-0x3236c, TitleParameters dump at off 0x95a3c0-0x95a4f8). How they are set is **UNVERIFIED**; may be a cleaner redirect than rewriting `Hello`. | Investigate |

## 2. Theater: command-by-command

PC dispatch table: `server/carbon/theater/service.py`, `CarbonTheaterService.dispatch` (handlers `CONN USER ECHO PING
KEEP LLST GLST PCNT GDAT CGAM EGAM ECNL`). Anything else is logged as `Carbon Theater unhandled command` and gets **no
reply**. The socket loop is `server/carbon/runtime/theater.py` `handle_theater_connection`.

### 2.1 The 360 command vocabulary (registration table, off 0x9d89ec-0x9d8e00; tag immediates)

`CONN USER RLST RDAT LLST LDAT GLST GDAT GDET PDAT CGAM UGAM UGDE UPLA UBRA EGAM EGEG ECNL EGRQ EGRS QLEN PENT PLVT
RGAM PING HOMG HMRQ KICK GREM KGAM ABUS DISC RESV KEEP HGAM GRST RCLM PCNT HLST HACK HXFR HTSN HTRS HTFN`

`ECHO` is not in that table. It is built and parsed elsewhere (off 0xa0a858 builder, 0xa0ad28 parser; log strings
`->D` / `<-D` at off 0xa0a918 / 0xa0acf8), so it looks like a datagram echo (inferred), not a Theater TCP command.

The table has 44 tags. The PC server handles 11 of them as requests (`CONN USER LLST GLST GDAT CGAM EGAM ECNL PING KEEP
PCNT`) plus `ECHO`, and emits `LDAT`, `GDET`, `EGEG` as replies. Request tags outside that set are silently dropped
(see 2.3); some of the 44 are server-to-client notifications that a server would send (`PENT`, `KICK`, `QLEN`, ...).

### 2.2 Implemented commands

Builders = tag immediate offset in the 360 image for the **request the 360 sends** (inferred direction).

| Cmd | PC request fields read | PC reply (frame order, fields) | 360 builds / parses (image evidence) | Difference / risk |
|---|---|---|---|---|
| CONN | `TID`, `PROT` (`_dispatch_conn`) | `CONN`: `TIME` (now), `TID`, `activityTimeoutSecs=900`, `PROT` (echo) | Builder off 0x9da550: `PROT`, `PROD`, `VERS`, `PLAT` (only if non-empty), `LOCALE` (xrefs 0x9da55c, 0x9da57c, 0x9da590, 0x9da5d8, 0x9da5ec). `PROT` value is a global string, initial value `"2"` (data at off 0xb65af8 points at off 0x8441c). | Compatible. `PLAT` is extra and ignored by PC. Value of `PLAT` the 360 sends: **UNVERIFIED** (caller not located). `TIME` format the 360 expects: unverified. |
| USER | `TID`, `LKEY` (also `lkey`) -> `identities.resolve_session` (`_dispatch_user`) | OK: `USER` `NAME=<persona>`, `TID`. Bad key: `USER` `TID`, `ERR=INVALID_SESSION`. Duplicate-login path replies OK but marks the connection read-only. | Builder off 0x9da634: `CID`, `MAC`, `SKU`, `LKEY`, `NAME` (xrefs 0x9da640, 0x9da65c, 0x9da670, 0x9da684, 0x9da698). | `CID`/`MAC`/`SKU`/`NAME` are ignored by PC (identity comes from the lkey). **Hard dependency:** the lkey must be one the PC server issued (`IdentityStore.login`, token = 27 url-safe chars + `.`). 360 login parser reads `lkey` into a 64-byte buffer (off 0xa2a534 `li r6,64`), so the PC token fits. |
| LLST | `TID` only (filters ignored) (`_dispatch_lobby_list`) | `LLST` (`NUM-LOBBIES=1`, `TID`) then `LDAT` (`LID=257`, `NAME=Internet`, `LOCALE=en_US`, `MAX-GAMES=10000`, `NUM-GAMES`, `PASSING`, `FAVORITE-GAMES=0`, `FAVORITE-PLAYERS=0`, `TID`) | Builder off 0x9da730: `FILTER-FAV-ONLY`, `FILTER-NOT-FULL`, `FILTER-NOT-PRIVATE`, `FILTER-MIN-SIZE`, `FILTER-ATTR-U-*`, `FAV-PLAYER`, `FAV-GAME`, `FAV-PLAYER-UID`, `FAV-GAME-UID`. Reply keys the 360 knows: `NUM-LOBBIES`, `LID`, `NAME`, `LOCALE`, `MAX-GAMES`, `NUM-GAMES`, `FAVORITE-GAMES`, `FAVORITE-PLAYERS`, `PASSING` (off 0x38554-0x386e0). | Match. PC ignores all filters. |
| GLST | `TID`, `LID` (default 257); filters ignored (`_dispatch_game_list`) | `GLST` (`TID`, `LID`, `LOBBY-NUM-GAMES`, `LOBBY-MAX-GAMES=10000`, `NUM-GAMES`) then one `GDAT` per game in the lobby, each carrying `TID` | Builder off 0x9db544: `LID`, `TYPE` (`G` or `P`, off 0x9db4d0/0x9db4dc), the `FILTER-*` set, `FAV-*`, `GID`, `COUNT`. Reply keys known: `LOBBY-NUM-GAMES`, `LOBBY-MAX-GAMES`, `NUM-GAMES`. | Match on the reply. PC returns every game in lobby 257 regardless of `TYPE`, `COUNT` or filters, no paging. |
| PCNT | `TID`, `LID` | `PCNT`: `COUNT=<number of games>`, `TID`, `LID` | Builder off 0x9db354: `LID`. Reply key `COUNT` (off 0x3868c). | PC server returns the **game** count under a player-count tag (server quirk). Harmless unless the UI shows it. |
| GDAT | `GID`, or `USER` (invite preview) (`_dispatch_game_data`) | `GDAT` row (below). With `USER=`, also a `GDET` (`TID`, `UGID`, `LID`, `GID`) and `AP=0`/`JP`/`QP=0` projection. A miss sends **nothing**. | Builders: `LID`-only off 0x9da840; by `USER` off 0x9db6b4; by `UID` off 0x9db71c; by `XUID` off 0x9db784. Parser (off ~0x9d9300-0x9d9650) reads `LID GID B-U-* N TYPE P MP I PW AP JP QP HN HU V B-version F NF PL J` and `UGID`, `D-*`, `PID` in the GDET parser (off 0x9d967c). | **PC cannot answer `GDAT UID=` or `GDAT XUID=`**: silent miss, 360 would wait. Only `GID` and `USER` work. |
| CGAM | `MAX-PLAYERS` or `B-U-max_online_player`, `N`, `J` or `JOIN`, every `B-*` key (without `|`), `B-U-ranked`/`B-ranked`/`ranked`/`RANKED`/`QROptionsRankedMode` (`_dispatch_create_game`, `CarbonGameDirectory.create` in `server/carbon/theater/directory.py`) | `CGAM`: `TID`, `MAX-PLAYERS`, `EKEY` (server constant `9181081919`), `UGID` (server uuid4), `JOIN`, `SECRET` (= EKEY), `LID`, `J`, `GID`. Needs an authenticated USER, else `ERR=NOT_AUTHENTICATED`. | Builder off 0x9dbd5c: `LID`, `RESERVE-HOST`, `NAME`, `PORT`, `SG-PORT`, `SID`, `HTTYPE` (`A`/`D`), `QLEN`, `HXFR`, `INT-IP` (printed `%hu.%hu.%hu.%hu`), `INT-PORT`, `MAX-PLAYERS`, `PASSWORD`, `UGID`, `SECRET`, `B-U-*`, `B-version`, `RESERVE-IDS`, `RESERVE-TIMEOUT`, `JOIN` (`C`/`W`/`O`), `RT`, `XB360SESS` (tail at off 0x9dc1a0). Reply parser off 0x9dc2b4 reads `MAX-PLAYERS`, `SECRET`, `UGID`, `JOIN`, `EKEY`. | Reply is fully covered. Request: **360 sends `NAME`, PC reads `N`**, so the room name falls back to `LOCALCarbon-<gid>` (cosmetic). The room's published `I`/`P`/`INT-*` are overwritten by the server's relay endpoint, not the client's. |
| EGAM | `GID` (falls back to the GID chosen by GDAT), `R-UID`/`UID`, `R-INT-IP`/`INT-IP` (default `0.0.0.0`), `R-INT-PORT`/`PORT` (`_dispatch_enter_game`) | Batch 1 after 28 ms: `GDET` (`UGID`, `LID`, `GID`). Batch 2 after a further 80 ms: `EGAM` (`TID`, `LID`, `GID`) then `EGEG`. Errors: `ERR=GAME_NOT_FOUND`, `ERR=GAME_FULL`, `ERR=NOT_AUTHENTICATED` (`_reply_schedule` in `runtime/theater.py`). | Builder off 0x9dac38: `LID`, `GID`, `R-U-*` (user attrs), `PORT`, `R-XNADDR` (36-byte XNADDR, hex-encoded, setter at off 0x9dac98), `GROUP.[]` + `GROUP.<n>`. **No `R-INT-IP`, no `R-INT-PORT`, no `R-UID` in this builder.** | `internal_ip` becomes `0.0.0.0`; `internal_port` = `PORT`. Only matters for LAN same-NAT routing (`race_endpoint_for`). `R-XNADDR` / `R-U-*` ignored by PC. |
| EGEG (reply) | n/a | `PL=PC`, `TICKET`, `PID`, `P`, `HUID=<host user id>`, `INT-PORT`, `INT-IP`, `UGID`, `I`, `LID`, `GID`, `EKEY` | Parser off 0x9dc3a4 (calls base parser 0x9d9b38): `HUID`, `I`, `PL`, `P`, `INT-IP`, `INT-PORT`, plus `PID`, `TICKET` (128-byte buffer), `UGID`, `EKEY`, `REASON`. See 2.4. | Every field PC sends is consumed. `REASON` is never sent by PC. |
| ECNL | `GID` (`_dispatch_leave_game`) | `ECNL`: `TID`, `LID`, `GID`. The race transport may defer the actual removal. | Builder off 0x9dadc0: `LID`, `GID`. | Match. |
| PING / KEEP | `TID`, optional `HKEEP` (`_dispatch_liveness`) | Same tag back: `TID` (default `1`), `HKEEP` echoed if present | Builder `PING` off 0x9db01c. `KEEP` is in the registration table only (no builder found). `HKEEP` is set by the `HXFR` builder (off 0x9dbb70, xref 0x9dbbc4). | Match. PC never sends an unsolicited Theater `PING`/`KEEP`; it relies on `activityTimeoutSecs=900`. |
| ECHO | `TID`, `TYPE` -> `ECHO`: `PORT`, `TID`, `IP`, `TXN=ECHO`, `ERR=0`, `TYPE` (`_dispatch_echo`) | see left | Builder off 0xa0a858: `TID`, `TYPE`, `UID`, `UGID`, `SECRET`. Parser off 0xa0ad28: `TID`, `IP`, `PORT`. Likely UDP (see above). | PC only answers on TCP; its UDP listener is the race relay, not a Theater echo. **UNVERIFIED** whether the 360 ECHO ever reaches the PC server. |

### 2.3 360 commands the PC server drops (no handler, no reply)

| 360 tag | Builder off | Fields the 360 builds (from xrefs) | Notes |
|---|---|---|---|
| UGAM | 0x9da8c8 | `LID`, `GID`, `NAME`, `B-U-*`, `JOIN`, `MAX-PLAYERS`, `RT` | Update game attributes. The PC server learns capacity from the UDP game manager instead (`set_authoritative_capacity`). |
| UGDE | 0x9daa2c | `LID`, `GID`, `D-*` | Update game details. |
| UPLA | 0x9daad4 | not decoded | Update player. |
| UBRA | 0x9daba4 | `LID`, `GID`, `START` | meaning unverified |
| EGRS | 0x9dae44 | `LID`, `GID`, `ALLOWED`, `REASON`, player ref | Host's answer to an entry request. PC has no host-approval flow (the rebroadcaster is the host). |
| PLVT | 0x9daf1c | not decoded | Likely "player leave/vote" (inferred) |
| RGAM | 0x9dafa8 | not decoded | |
| RESV | 0x9db220 | `UIDS`, `TIMO`, `ACTION`, abuse types | reservation / abuse reporting |
| PENT | 0x9db974 | `LID`, `GID`, `PID`, `P-<attr>` | Player entered; **parsed** by the 360 with `NAME UID PID TICKET PORT R-XNADDR XUID R-U-* R-XUID R-UID R-USER GROUP.[]` (off 0x9d98a4-0x9d9a88). PC never sends it. |
| HLST / HACK / HXFR / HTRS | 0x9dba5c / 0x9dbaf0 / 0x9dbb70 / 0x9dbc24 | `LID`, `GID`, `HPID`, `PID`, `HKEEP`, `PORT`, `XB360SESS`, `INT-IP`, `INT-PORT` | Host migration family. |
| QLEN / QPOS | parser only | `QLEN`, `QPOS` | Queue position; server never queues. |

Whether the PC retail client sends the same set (and tolerates silence) is the open question: the PC server was built
from PC retail captures and works without these, so the NFS game code probably only emits them in specific modes
(host migration, queued games, reservations). **UNVERIFIED** for the 360.

### 2.4 GDAT row and EGEG: platform handling (the main data difference)

PC row builder: `CarbonGame.row` in `server/carbon/theater/directory.py`. Header order is deliberate ("session
identity/population block before any B-U-* attributes"): `LID GID N TYPE MP I P PW AP JP QP HN HU V B-version PL J`,
then every remaining property. `PL` is forced to `"PC"` in `CarbonGameDirectory.create`; `I`/`P` are the server relay
endpoint (default race UDP port 19118), not the creator's address.

| Item | 360 parser behaviour (disassembly) | Effect with the PC server |
|---|---|---|
| GDAT `PL` | Compared against the literal `"XBOX"` (off 0x9d95c4). If equal, a 32-char string (the `I` value) is stored as the host address; otherwise it is zeroed. | `PL=PC` takes the "not XBOX" branch: no host XNADDR is stored. Listing works. |
| GDAT `F`, `NF` | Read (`F` bool, `NF` int, off 0x9d957c/0x9d958c). | PC never sends them; they default. Effect unknown (**UNVERIFIED**). |
| EGEG `PL` | Lower-cased then compared with `"xbox"`, `"xbox360"`, `"ea"` (off 0x9d9c10, 0x9d9c48, 0x9d9da4). `"xbox"`: `I` is a hex XNADDR string. `"xbox360"`: `INT-IP`/`INT-PORT` are skipped, endpoint built from `P`. `"ea"`: third decoder. **Any other value (`PC`)**: reads `INT-IP` (string) and `INT-PORT` (int) and builds an IP endpoint (off 0x9d9cb8-0x9d9e1c). | The PC `EGEG` (`PL=PC`, `INT-IP`, `INT-PORT`) is parsed as a plain IP:port endpoint. This is the right path to keep. Do **not** rewrite `PL` to `xbox360`. |
| EGEG `HUID` | Read as an int (`GetInt`, off 0x9d9b60). | PC sends the host's decimal profile id. OK. |
| EGEG `TICKET` | 128-byte string buffer (off 0x9dc404). | PC ticket is a 10-digit decimal (`1_000_000_000 + md5 % 9_000_000_000`, `CarbonGameDirectory.ticket`). OK. |
| EGEG `EKEY` | 17-byte buffer (off 0x9dc444, `li r6,17`). | PC `EKEY` is `9181081919` (10 chars). OK. |

What the 360 does **after** it has a plain IP:port endpoint (secure XNet session vs insecure UDP to the relay) is
**UNVERIFIED** and belongs to the in-race stage. The PC relay keys its RC4 and tunnel id from `EKEY` and `HUID`
(`server/carbon/rebroadcaster/service.py`, `transport/prototunnel.py`, `rebroadcaster/handshake.py`), and binds
sessions through `TICKET` (`resolve_ticket`). The 360 SDK strings `ariesudp.cpp`, `xb360session.cpp`,
`xb360demangler.cpp`, `XNetConnect` suggest it prefers XNet sessions keyed on `XB360SESS`/`R-XNADDR`.

## 3. Messenger: command-by-command

PC: shared hub `server/classic/ea/messenger.py` (`EAMessengerHub`), Carbon adapter
`server/classic/protocols/carbon_messenger.py` (`CarbonMessengerAdapter`), service
`server/classic/protocols/carbon_messenger_service.py` (`CarbonMessengerService`). The Carbon server process does not
speak the wire itself; it feeds sessions to the shared Messenger over loopback IPC
(`server/carbon/messenger_ipc.py`, `server/classic/protocols/carbon_messenger_ipc.py`; `MESSENGER_IPC` default
`127.0.0.1:13506`, secret required).

### 3.1 Registration tables

| Table | Contents | Source |
|---|---|---|
| PC `RETAIL_REQUEST_HANDLERS` | `AUTH DISC USCH PADD PDEL PSET RADD RADM RRSP RDEL RDEM RGET MLST TCKL GINV GRSP GRVK SEND BRDC EPST EPGT PING` | `carbon_messenger_service.py` top of file |
| PC `RETAIL_ASYNC_COMMANDS` (receive-only) | `USER PGET RNOT ROST GNOT RECV ADMN` | same |
| PC legacy aliases | `RSET RREM BLCK BLOK RBLK RBLO UBLK UBLO UNBL BDEL` | `_LEGACY_REQUEST_HANDLERS` |
| 360 registration (off 0x9d6b2c-0x9d6dd8, tags in order) | `AUTH DISC USCH USER PADD PDEL PSET PGET RADD RADM RRSP RDEL RDEM RNOT RGET ROST MLST TCKL GINV GRSP GRVK GNOT SEND RECV BRDC EPST EPGT ADMN PING` | image |

The two sets are **identical** (29 tags). This is the strongest single compatibility result in this stage.

### 3.2 Request fields and replies

| Cmd | PC request fields read / PC reply | 360 request builder (off) and fields | Difference |
|---|---|---|---|
| AUTH | `LKEY` -> `resolve_session`. OK reply: `TIID=0`, `TITL="Need for Speed Carbon"` (quoted), `USER=<persona>@messaging.ea.com/eagames/NFS-2007`, `ID`. Errors `INVALID_SESSION`, `ALREADY_AUTHENTICATED`. Adapter selection (`CarbonMessengerAdapter.matches`): `LKEY` present and (`RSRC`/`PRES`/`TITL`/`PROD` contains `nfs-2007` or `carbon`, or the lkey is already a known session). The AUTH is held up to 3 s while the loopback IPC state catches up. | Primary builder off 0x9d6f5c: `LKEY`, `PROD`, `VERS`, `PRES`, `RSRC` (RSRC required: assertion "No resource string specified!" at off 0x3b99f). A second variant (off 0x9d7050) uses `USER`/`PASS` instead of `LKEY`. | Same shape as PC retail. `PROD`/`RSRC` values the 360 sends are **UNVERIFIED**; if neither contains `nfs-2007`/`carbon` the adapter still matches once the lkey is a registered session. |
| DISC | closes the connection | off 0x9d7144 | Match. |
| PING | server pings every 30 s (`heartbeat_interval`), client answers with `PING`; no reply to the client's | off 0x9d83a4 builder | Match. |
| PSET | `SHOW` (`CHAT AWAY XA DND DISC GAME PASS`), `STAT`, `ATTR`, plus passthrough of `RSRC DOMN RICH ATTR EXTR SESS PROD STAT CHNG GROUP UID GSTR TYPE HOST ERRS NOREPLY`. Reply `PSET` `ID`. | off 0x9d73a0: `RSRC`, `SHOW`, `STAT`, `PROD`, `ATTR`, `SESS`, `TITL`, `TIID`, `EXTR` | Match. 360 has the presence set `CHAT AWAY GAME DISC` (off 0x3b7f4-0x3b808, 0x37894). |
| RGET | `LIST` (default `B`). Reply `RGET` (`ID`, `SIZE`), then `ROST` per entry (`USER=name@messaging.ea.com`, `ID`, `UID`, `GROUP`, `ATTR`), plus a `PGET` for online entries | off 0x9d7ddc: `LIST`, `GROUP`, `LRSC`, `PRES` | See 3.3: the 360 may intercept this locally. |
| EPGT / EPST | `ENAB` `T`/`F`, `ADDR` | off 0x9d8354 / 0x9d8290 | Match. |
| PADD / PDEL | `USER`; replies `PADD` (+ `PGET`), `PDEL` `STAT=OK` | off 0x9d72c4 / 0x9d732c | Match. |
| GINV | `USER` (target), `SESS`. Pushes `GNOT` `HOST USER TYPE=I SESS [GSTR]` to the target; replies `GINV` `ID` | off 0x9d86a8: `USER`, `SESS`, `TITL`, `RSRC`, `GSTR` | `GSTR` on the PC side is built from the authoritative room (`CarbonGame.invite_fields`) and is not taken from the request. |
| GRSP | `USER` (host), `ANSW` `Y`/other, `SESS`. Pushes `GNOT` `TYPE=A` or `R` to the host | off 0x9d7f64: `USER`, `ANSW`, `SESS`, `RSRC` | Match. |
| GRVK | `USER`, `SESS` | off 0x9d801c | Match. |
| SEND | `USER`, `TYPE` (`C`/`A`), `SUBJ`, `BODY` (required), `SECS`. Delivers `RECV`; `ERR=USER_OFFLINE`/`BLOCKED`/`INVALID_REQUEST` | off 0x9d80ac: `TYPE`, `USER`, `SUBJ`, `BODY`, `SECS`, and voice attachment `VLEN` `VDUR` `VDAT` (off 0x9d845c-0x9d853c) | 360 can attach voice messages; PC passes only `_MESSAGE_FIELDS` and has no voice fields, so voice attachments are dropped. Cosmetic. |
| RADD/RADM/RRSP/RDEL/RDEM | friend add/response/remove with `USER`, `LRSC`, `GROUP`, `PRES`, `ANSW` | off 0x9d7718 / 0x9d85b0 / 0x9d7950 / 0x9d7b74 / 0x9d7cec | Match in shape; see 3.3. |
| MLST / TCKL / BRDC / USCH | compat replies; `BRDC` returns `ERR=NOT_SUPPORTED`; `USCH` searches personas | off 0x9d7a6c / 0x9d7afc / 0x9d81a4 / 0x9d719c | Match. |

Incoming (server -> client) messages the 360 can parse (parser region off 0x99d1c0-0x99de90): `USER TITL TIID SIZE DOMN
RSRC SHOW STAT PROD SESS EXTR ATTR RICH GROUP CHNG XUID HOST TYPE GSTR ERRS TIME SUBJ BODY NOREPLY ADDR ENAB SECS`,
and `ADMN` types `BOOT`, `UNGAG`, `DUPL`. PC sends `ADMN` `TYPE=DUPL|BOOT`, `SECS=0`. All PC fields are in the 360's list.

### 3.3 The Xbox friends interceptor (the main 360-only difference)

Strings `xb360messengerinterceptor.cpp`, `XB360MessengerInterceptor::HandleFriendsChange`, `XONLINE_FRIEND`,
`XONLINE_PRESENCE`, `TPnewPresenceCount == mPresenceSubscriptionCount` (off 0x3b71c-0x3b888) and the code at off
0x9d4258-0x9d688c show a wrapper around the Messenger connection that **builds `ROST`, `PGET`, `RNOT` locally** from the
Xbox Live friends/presence API (builders at off 0x9d56c4 `ROST` with `USER`+`XUID`, 0x9d4bcc `PGET`, 0x9d4acc and
0x9d59f0 `RNOT` with `CHNG`/`ATTR`). The same dispatcher inspects outgoing tags `AUTH RGET PDEL GINV DISC BRDC` and the
`EPGT`/`EPST` `ENAB=F` case (off 0x9d6480-0x9d6560). Which of those it answers itself and which it forwards to the server
is **UNVERIFIED**.

Consequences for the PC server:

- The roster and friend presence the 360 shows are driven by Xbox Live, not by PC `RGET`. In our port there is no Live, so
  the friend list will be empty unless the shim feeds the interceptor.
- The 360 parses an optional `XUID` integer in `ROST`/`RNOT` (off 0x99d710, 0x99d8bc, via `GetInt`, default 0). The PC
  server never sends `XUID`, so every PC roster entry would carry `XUID=0`. Effect on invites by name: **UNVERIFIED**.
- Invites (`GINV`, `GNOT`, `GRSP`) and text (`SEND`/`RECV`) go through the server in the PC design and are
  wire-compatible.

## 4. Identity, ticket and session handling

| Concept | PC server | 360 build | Translation need |
|---|---|---|---|
| `LKEY` | Issued by `IdentityStore.login` after FESL `acct Login` or `NuLoginPersona`; format `token_urlsafe(20)[:27] + "."`. Required by Theater `USER` and Messenger `AUTH`. | Obtained from the FESL login reply field `lkey` (buffer 64, off 0xa2a534) with `userId` (int) and, in one variant, `encryptedLoginInfo` (512). The 360 never sends `NuLoginPersona` (stage 2). Passed to Theater `USER` and Messenger `AUTH` unchanged. | The shim must log in on the PC server itself (`AddAccount` + `Login` + persona selection), then hand the **PC-issued lkey** to the 360 in its expected login reply. After that Theater and Messenger need no lkey rewrite. |
| User id | `Identity.user_id` = `profile_id` = 100,000,000 + md5(persona) % 900,000,000. Separate 15-bit "wire player id" (`wire_player_id`) used as `EGEG.PID`, Messenger `ROST.UID`, and invite `UID`. | `UID`/`PID`/`HUID` read as ints. | None; use the PC values. |
| Persona name | One persona per account, name returned in `USER`/`NAME` and `AUTH` `USER`. | `NAME` sent in `USER` request = gamertag-like string from the 360 login. | Make the 360 `NAME` equal the PC persona (PC ignores it, but the game may compare). **UNVERIFIED.** |
| XUID | not present anywhere in the Carbon server | `R-XUID`, `XUID`, `XB360SESS`, `R-XNADDR` built into `EGAM`/`CGAM`/`GDAT`-by-XUID/`PENT`; `XUID` parsed in Messenger `ROST`/`RNOT` | PC ignores extra request fields; **cannot** answer `GDAT XUID=`/`UID=`. |
| Ticket | Server-generated per (game, participant), deterministic (`CarbonGameDirectory.ticket`); returned in `EGEG`, presented later on the UDP relay. | Parsed from `EGEG` (128-byte buffer). | Pass through unchanged. |
| EKEY / SECRET | `9181081919` constant (`ekey` argument); used as RC4 key on the race UDP. | `CGAM` reply parsed for `SECRET` and `EKEY`; `CGAM` request also carries a client `SECRET` (ignored by PC). | Pass through. |
| Platform tag | `PL=PC` forced in rooms and `EGEG`. `clients/carbon/net_carbon.ini` has `platform = PC`. FESL `RegisterGame` logs the client `platform`. | `PL` compared to `XBOX`/`xbox`/`xbox360`/`ea`; CONN `PLAT` optional. | Keep `PC` in server replies; see section 2.4. |

## 5. Concrete differences (flat list)

1. 360 `CGAM` sends `NAME`; PC reads `N`. Room name defaults to `LOCALCarbon-<gid>`.
2. 360 `EGAM` sends `PORT` + `R-XNADDR` (+ `R-U-*`, `GROUP.*`) and **no** `R-INT-IP`/`R-INT-PORT`/`R-UID`; PC wants `R-INT-IP`/`R-INT-PORT` (defaults `0.0.0.0` / `0`).
3. 360 can request `GDAT` by `UID` and `XUID`; PC only supports `GID` and `USER`, and answers a miss with silence.
4. 360 registers about 30 Theater tags the PC server does not handle (section 2.3; some are server-to-client). Silence from the server may stall the 360 if it waits for a reply.
5. 360 `CONN` adds `PLAT` and `LOCALE`; `USER` adds `CID`/`MAC`/`SKU`. All ignored by PC.
6. PC `GDAT`/`EGEG` always say `PL=PC`; the 360 takes its generic IP:port branch for that, not the XNADDR branch.
7. PC `Hello` reply omits `addressRemapping`, which the 360 knows (effect unknown).
8. Messenger: 360 sources friend roster and presence from Xbox Live through `XB360MessengerInterceptor`; PC `ROST`/`RNOT` carry no `XUID`.
9. Messenger: voice attachments (`VLEN`, `VDUR`, `VDAT`) in `SEND` are dropped by PC.
10. PC `PCNT` returns the game count (not player count).
11. In-race: 360 prefers Xbox secure sessions (`XB360SESS`, XNADDR); PC relays over the UDP rebroadcaster keyed by `EKEY`/`HUID`/`TICKET`.

## 6. Prioritised work for the client-side translation layer

| Pri | Rewrite / action | Why | Where it bites |
|---|---|---|---|
| P0 | Make the lkey the 360 holds equal the PC-issued session key (shim logs in on the PC server, returns that `lkey`/`userId` in the 360's login reply). | Theater `USER` and Messenger `AUTH` both resolve identity purely from `LKEY`; wrong key gives `ERR=INVALID_SESSION` and nothing else works. | `USER`, `AUTH` |
| P0 | Redirect: `fsys Hello` reply `theaterIp`/`theaterPort` -> PC host:18215, `messengerIp`/`messengerPort` -> PC host:13505 (or use the `Theater/Messenger Host/Port Override` mechanism if it can be set). | The 360 learns both endpoints only from Hello. | before `CONN` |
| P1 | Pass `CONN`, `USER`, `LLST`, `GLST`, `GDAT`, `PCNT`, `ECNL`, `PING` through unchanged. Optionally strip `CID`/`MAC`/`SKU`/`PLAT`. | Wire-compatible already; reply field sets cover everything the 360 parses. | |
| P1 | `EGAM`: add `R-INT-IP` (client LAN IP or `0.0.0.0`) and `R-INT-PORT` (= `PORT`); drop `R-XNADDR` and `R-U-*` (or leave, they are ignored). | Server stores `internal_ip/port` for the roster and LAN routing. | `EGAM` |
| P1 | `CGAM`: add `N` = `NAME`; keep `MAX-PLAYERS`, `JOIN`/`J`, `B-U-*`, `B-version`. | Room name and the PlayNow/ranked attribute match. Without `B-U-game_type` etc. the server substitutes defaults (`CarbonGameDirectory.create`). | `CGAM` |
| P1 | Keep `EGEG` `PL=PC` unmodified. Do not translate to `xbox360`. | PC path gives an IP:port endpoint; `xbox360` path expects XNet data the PC server cannot supply. | `EGEG` |
| P2 | Synthesize local replies (or at least log) for the commands the PC server drops: `UGAM`, `UGDE`, `UBRA`, `UPLA`, `PLVT`, `RGAM`, `RESV`, host-migration family. First step is a trace to see which the game actually emits and whether it waits. | Silent drops look like hangs. | after `CGAM`/`EGAM` |
| P2 | Rewrite or locally answer `GDAT` requests keyed by `UID`/`XUID` (map to `USER`/`GID`). | PC cannot answer them. | invites |
| P2 | Messenger `AUTH` passthrough; ensure `RSRC`/`PROD` contain `NFS-2007` (adapter match) or that the lkey is already registered. Feed the XB360 interceptor a synthetic empty friend list so it does not wait on Xbox Live. | Prevents a stall in the interceptor. | `AUTH`, `RGET` |
| P3 | Add `XUID` (any value) to `ROST`/`RNOT` if a hardware trace shows the 360 requires non-zero. | Currently defaulted. | roster |
| P3 | Decide how race UDP (`I`/`P`/`INT-*`, `TICKET`, `EKEY`) reaches the relay (separate stage). | `EGEG` gives a plain IP endpoint only. | race |

## 7. Could not verify

- No Theater or Messenger traffic from a 360 has been captured. Every "the 360 sends X" here is read from builder code
  in the image (static), not observed on the wire.
- Builder-vs-parser direction is inferred from the setter/getter helper (`0x82951ce0`/`0x82951a98`/`0x82951c18`/
  `0x82952738` set; `0x82951d88`/`0x82951b38`/`0x829527a0`/`0x82952870` get). Helper names are guesses.
- Runtime values: `PLAT`, `PROT` (initial `"2"`), Messenger `PROD`/`VERS`/`PRES`/`RSRC`, and the `XB360SESS` value
  format were not recovered.
- Whether the 360 header word for Theater/Messenger requests matches the PC convention (PC ignores it, so harmless
  unless the 360 requires a specific reply word).
- Whether the 360 requires `addressRemapping` in `Hello`, `F`/`NF` in `GDAT`, or non-zero `XUID` in `ROST`.
- Meanings of `UBRA`, `PLVT`, `RGAM`, `UPLA`, `RESV` (inferred only).
- What the PC retail client sends for the commands the PC server ignores, so I cannot say if their absence is safe.
- Theater `ECHO` transport (TCP vs UDP) on the 360, and whether any listener on the PC server answers it.
- The "Override" TitleParameters (how set), and which Messenger requests the XB360 interceptor answers itself.
- After `EGEG`, whether the 360 can race over a plain IP endpoint (secure-session requirements).
- PC server tests (`server/carbon/tests/test_carbon_theater.py`, `server/classic/tests/test_carbon_shared_messenger.py`)
  were used only to confirm request shapes; they were not run.
- Repo state is the public HEAD at fetch time; later commits may change behaviour.
