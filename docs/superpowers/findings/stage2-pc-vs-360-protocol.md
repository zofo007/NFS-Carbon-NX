# Xbox 360 build vs the PC community server: protocol comparison

Sources: the unpacked 360 game image (`build/collectors-windows-probe/standard-image.bin`, strings only),
Switch hardware logs (`fake Live: send/recv ... text:` lines), and the public source of
`moio9/NFS-Online-Server` (`server/carbon`). Nothing here comes from running the PC game.

## Verdict

Login is not the only difference. It is the first one, and the only one confirmed on hardware so far.
The transport and the first handshake are shared; the account model, the matchmaking identity data, the
messenger and the in-race networking are built on Xbox services in the 360 game.

## Shared (confirmed on hardware)

| Layer | Evidence |
|---|---|
| Frame format: 4-char command, 4-byte transaction word, 4-byte total length, `key=value\n` lines, NUL | The server answered the 360's `fsys Hello` and the game parsed it |
| `fsys Hello` / `MemCheck` | Exchange completed in full on a Switch |
| FESL services named in both: `asso`, `dobj`, `mtrx`, `rank`, `blob`, `pnow` | Names present in the 360 SDK strings and in the server dispatch |

## Different or unknown

| Area | 360 game (FESL SDK 2.9.5.1) | PC server (Carbon) | Status |
|---|---|---|---|
| Account lookup | `acct GetUserIdsByXuids` (`xuids.[]`, `xuids.N`), also `GetXuidsByUserIds`, `GetXuidsByGamertags`, `LookupUserInfo` | Not implemented, no reply | Confirmed (hardware); shim answers it locally |
| Login | `XBL360Login`, `XBL360AddAccount`, `XBLLogin`, plain `Login`; fields include `gamertag`, `xuid`, `macAddr`, `machineId`, `ticket` | `acct Login` (`name`, `password`), then `NuGetPersonas`, `NuLoginPersona`; accounts must exist (`AddAccount`) | Next request not yet seen |
| Personas | No `Nu*` or persona strings in the binary | Required: Theater `USER` needs the `lkey` the persona login returns | Likely mismatch |
| Theater join data | Join/reservation carries `R-XUID`, `R-XNADDR`, `XB360SESS`, `HUID`, `TICKET`, `SECRET`, `PLAT` | `EGEG` reply carries `PL=PC`, `INT-IP`, `INT-PORT`, `PID`, `TICKET`, `EKEY` | Unknown: Theater tags may be compiled as immediates, not strings |
| Messenger | `msgr` over FESL frames, Xbox friends/presence interceptor, `messaging.ea.com` | Port 13505 is a separate service; Carbon only talks to it by local IPC | Not compared (shared Messenger source unread) |
| In-race networking | Xbox secure sessions (`XNetXnAddrToInAddr`, `XNetConnect`, `ariesudp`) | UDP relay (`rebroadcaster`, default 19118) | Unknown |
| Cross-play with PC players | 360 build | PC build | Not expected to work (different builds, platform tags) |

## Consequence for the plan

Match the two in order of appearance, one hardware run per step:

1. Login: answer `GetUserIdsByXuids` locally (done in `fesl_shim.h`); see what the game sends next
   (`XBL360Login` expected) and map it to PC `AddAccount` + `Login`, returning the PC session key and user id.
2. Personas: the PC server wants `NuGetPersonas`/`NuLoginPersona`; the 360 never sends them, so the shim
   must run them itself and hide the replies.
3. Theater: capture the 360's connect/join frames and compare with the PC tags.
4. Messenger and race traffic: compare once Theater works.

Open point: the PC server only exposes what the PC game uses. Each 360-only request it ignores will look
like a hang to the game, so unknown requests are logged as readable text for the next iteration.
