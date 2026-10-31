# Online features for NFS Carbon NX: design

Status: draft for approval. Nothing here is built.

## Intent

NFS Carbon NX runs the Xbox 360 release of Need for Speed: Carbon on the Switch. The game has a full online mode, but
Xbox Live and EA's 2006-era servers are gone, so the online menus are dead. The owner wants a "fake Xbox Live": make the
Switch build believe it is signed in to Live, and point its EA service connections at a community server, so online
play can work again.

Why this is plausible: the game was built on EA's FESL SDK (the same protocol family the PC game uses), and community
servers for the PC game already exist. The 360 build also has developer hooks for redirecting servers (see Evidence).

## Goals

1. **Milestone 1 (the owner's definition of a good first night): get past sign-in into the online menus.** The game must
   see a Live-signed-in profile with online privileges and a live network link, and the online menus must open.
2. **Milestone 2: reach a server.** The game's login connection (FESL) goes to a server we choose and gets a
   response it accepts. Target server to try first: moio9's open-source NFS-Online-Server (Python, AGPL), which
   already implements Carbon's FESL, Theater and game manager for the PC game.
3. **Later, not scheduled: lobby, matchmaking, races between players.** Possibly PSP-style ad-hoc / local play with no
   server. Both are separate projects; each needs its own brainstorm before work starts.

## Non-goals

- Real Xbox Live, real EA servers, or any account on Microsoft's or EA's services.
- Cracking or bypassing anything on a console other than this port.
- Hosting or running a public server as part of this repository. The server is a separate project.
- Supporting game editions other than the one the port already supports (title 454107EC, media 51D2B53F).

## Anti-goals (a failure even if it technically works)

- Offline play, saves, performance or startup time get worse. All online behaviour must be switchable in
  `nfscarbon.toml` and the default stays safe for people who do not use it.
- Real credentials or personal data are collected, logged or sent anywhere. The fake profile uses a made-up identity.
- The repository gains game files, EA code or other proprietary material.
- The port starts connecting to a server the user did not choose. The server address must be explicit config, never a
  hard-coded public host.

## Constraints

- Code lives in sibling folders, not in this repository's tree. This repository only holds patches and docs
  (`patches/carbon-switch.patch` for the game-specific project `carbon-source`, `patches/horizon-sdk.patch` for the
  Switch SDK layer; `scripts/export-source-patches.ps1` refreshes them). Online work follows that flow.
- The kernel layer (Xbox 360 OS calls) comes from the ReXGlue SDK. The Switch build compiles its SDK sources from
  `reference-nfsmw-nx/sdk` (the object files in `build/switch/rexglue-sdk` point there), and `patches/horizon-sdk.patch`
  does not touch the Xam network or user files. So the table below describes what is actually built.
- Game settings are read from `nfscarbon.toml` by the game project (`carbon-source/src/nfscarbon_app.h`), not by the
  SDK. The SDK kernel calls have no path to those settings yet; the builder has to create one (a small settings hand-off
  from the app to the SDK) and carry it in both patch files.
- `scripts/run-host.ps1` launches the real game on Windows with a window; whether it gets as far as the online menus is
  untested.
- The Switch process is within a few MB of its memory limit (see `docs/HOW_IT_WORKS.md`). Online code must not add
  noticeable memory or CPU cost, especially on the three game cores.
- Testing needs a real Switch (the maintainer's, overclocked). A Windows host build also exists
  (`scripts/run-host.ps1`) and is the cheaper place to test protocol work.
- Licences: this repository is GPL-3.0; the server is AGPL-3.0 and stays a separate program.

## Evidence so far (read from files and strings; nothing run)

**What the game imports.** The generated code calls `NetDll_*` sockets, `XNetLogon*`, `XamShowSigninUI`,
`XamUserGetSigninState`, `XamUserCheckPrivilege`, `XamUserAreUsersFriends`, and the stats and achievement enumerators.

**What the SDK answers today** (read in the reference copy; the build copy may differ):

| Call | Today | Effect |
|---|---|---|
| `XamUserGetSigninState` | always "signed in locally" (1) | Game expects "signed in to Live" (2) before online |
| `XamUserCheckPrivilege` | returns 0 (denied) for everything | No online or multiplayer privilege |
| `NetDll_XNetGetEthernetLinkStatus` | 0 | Reports no cable |
| `NetDll_XNetGetTitleXnAddr` | loopback, no online address | No online identity |
| `XNetLogon*` | empty stubs | No NAT type, machine ID, service info |
| `XamUserCreateStatsEnumerator` | stub | No stats |
| `XNetConnect`, `XNetQosLookup`, `XNetRegisterKey`, `XNetCreateKey` | stubs | No secure peer connections |
| plain sockets (`socket`, `bind`, `sendto`, `recvfrom`, ...) | implemented | Can reach a server |

**What the 360 game image contains** (strings in the unpacked image
`build/collectors-windows-probe/standard-image.bin`):

- EA FESL SDK 2.9.5.1 modules: game manager, game browser (Theater), messenger, ladder, association manager,
  playgroup manager, achievement manager, Xbox 360 session and messenger interfaces.
- Hard-coded hosts `fesl.ea.com` and `messaging.ea.com`, and log lines for `Fesl Host Override`,
  `Theater Host Override`, `Messenger Host Override`, `Fesl Port`, `Fesl Service Id`.
- `-nosecure` and `TitleParameters::SSL_OVERRIDE_OFF`: a switch to turn off TLS.
- An Xbox-side account service over HTTP (`/xuacs/XePassportLogin.ashx` and others), `/XArb/ReportResult.ashx`,
  `/xstats/...`.
- Race connection code: "Getting secure address for Server %d", "XNetConnect failed", `XSessionArbitrationRegister`.

**What the PC community server speaks** (moio9/NFS-Online-Server, read its README, TODO and frame codec): Carbon FESL
on TCP 18210, Theater on 18215, a UDP race transport on 19119, plain TCP (the PC client plugin avoids TLS). Frames are a
4-character command, a 4-byte transaction word, a 4-byte length, then `key=value` text. The project says it does not yet
work with the unmodified PC game, only with its own client plugin. NFSCO (nfsco.club) runs its own PC services but
publishes no server code.

## Unknowns (each is a thing to find out, in this order)

1. Is the 360 game's FESL wire format the same as the PC server's? (Same family and SDK version is not proof.)
2. Where do the `* Host Override` and `-nosecure` settings come from on a retail build (command line, a title
   parameter file, or compiled out)? If they are not reachable, the port needs its own hook for host and TLS.
3. Does the online menu gate on more than sign-in state and privileges (for example the Xbox account service or
   `XNetLogon` values)?
4. How does race traffic between players work on the 360 (secure addresses and keys, then UDP)? The PC server's UDP
   relay probably does not match it. This decides whether real races are feasible at all.
5. Does the 360 login send anything that needs an Xbox identity (XUID, machine ID, Live ticket) that the server has to
   accept or ignore?

## Proposed approach (the builder's decisions to refine; marked as my guess)

**Stage 1: fake Live, local only.** In the SDK layer, behind a config switch: report "signed in to Live" for the
first user with a made-up gamertag and XUID; grant the online privileges; report a live link; return a fixed,
plausible `XNADDR`, MAC and machine ID; fill in `XNetLogon*` with believable values (service connected, open NAT).
Success: the online menus open. Test on the Windows host build first, then on the Switch.

**Stage 2: reach a server.** Resolve the game's service host names to the configured server address (in the
`XNetDnsLookup` handling), turn TLS off through the game's own switch if reachable or a hook if not, and watch what
the game sends. Run moio9's server locally on a PC and compare the game's login frames with what the server expects.
Success: the game sends a login and the server answers it; the menus show a logged-in state.

**Stage 3 (separate brainstorm).** Lobby and races; ad-hoc play.

Order stays strict: no Stage 2 work until Stage 1 opens the menus, because Stage 1 may change what the game asks.

## Decided by the owner

- First success is getting into the online menus.
- Long-term target is a public community server, reusing PC community servers if they can work.
- Ad-hoc play like the PSP versions would be wonderful, but is not the first goal.

- If the 360 game's login messages differ from the PC server's, we may adapt moio9's server (a fork or a contribution
  upstream). It stays a separate AGPL-3.0 program, outside this repository; the port does not translate messages.
  Which of fork or contribution is decided when the first change is needed, and any contact with moio9 is the owner's.

## My calls (not discussed; please check)

- The online code is off by default and enabled in `nfscarbon.toml`.
- The first server to try is moio9's, run locally, rather than writing a new one.
- The fake identity is generated once and stored in the user's save area, not hard-coded.
- Stage 1 is tested on the Windows host build before the Switch.
- The server address is a config value; the repository never names a public host.
- **What "online menus open" means (Milestone 1 exit test):** the game's online entry in the main menu opens its online
  screen without a sign-in prompt or an "online unavailable" message, and the log shows the faked Live sign-in being
  queried. The game may fail later when it tries to connect; that is Stage 2.
- **The Xbox-side account service (`/xuacs/...`, `/XArb`, `/xstats`):** Stage 1 tries SDK-only. If the menus will not
  open without those HTTP calls being answered, the builder stops, records exactly which request blocks the menu, and
  asks the owner. A small throwaway responder is allowed for testing only, kept under `scripts/` and not shipped.
- **TLS:** use the game's own switch if it is reachable. If not, the most the port may do is turn that flag off with a
  small hook. The port never terminates or re-implements TLS.
- **Stage 2 may end as an investigation.** If the 360's login frames differ from the PC server's, a documented
  frame-by-frame diff is an acceptable result; the owner has approved adapting the server afterwards (see below).
- The unpacked game image `build/collectors-windows-probe/standard-image.bin` exists only on the owner's machine; the
  builder must work from the strings quoted above or ask for a re-extract. Never commit it.

## Left to the builder

Which SDK copy and which files to change; exact `XNADDR` and `XNetLogon` return values; how to log game-to-server
traffic for the comparison in Stage 2; how to structure the config; tests; patch export.
