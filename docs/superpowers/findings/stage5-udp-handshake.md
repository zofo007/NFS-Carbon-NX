# Stage 5: the 20-byte UDP datagram and why the PC server never answers

Scope: the first UDP packet the 360 build sends to `<server ip>:19119` after Theater `EGEG`, and the PC rebroadcaster
(`moio9/NFS-Online-Server`, `server/carbon/...`). Read-only analysis: nothing was run against a game server, no
packet was sent. Fetched repo files were read as data only; the decode checks below use my own re-implementation of the
documented RC4 / table layout, not the repo code.

## 0. Method and confidence

| Item | Detail |
|---|---|
| 360 side | Recompiled C++ (`carbon-source/generated/default/nfscarbon_recomp.*.cpp`, functions `sub_<VA>`, asm in comments) plus strings in `standard-image.bin` (VA = 0x82000000 + off). No emulator, nothing executed. |
| PC side | Public repo `main` (fetched this session): `transport/prototunnel.py`, `transport/commudp.py`, `transport/rc4.py`, `rebroadcaster/service.py`, `rebroadcaster/handshake.py`, `theater/service.py`, `theater/directory.py`, `core/udp.py`, `app.py`. |
| Confirmed | Datagram layout (decoded byte for byte against both the 360 builder and the server decoder), the clear-vs-RC4 switch in the 360, the server's reject path. |
| Inferred / unverified | Marked **UNVERIFIED** inline and collected in section 6. No hardware or server log for this packet exists in the repo, so the server-side reject is derived from code, not observed. |

## 1. What the 20 bytes are

```
00 12 | 00 67 | 00 81 | 00 e1 49 76 72 00 | 00 00 00 01 b6 ee 59 2c
 (a)    (b)     (c)     (d) ch7 body 6B      (e) ch1 body 8B
```

The grouping in the question (`00 81 00 e1`) is off by one word: `00 e1` belongs to the channel-7 body.

| Part | Value | Meaning | Evidence |
|---|---|---|---|
| (a) bytes 0-1 | `0x0012` = 18 | First u16 of a ProtoTunnel datagram. In the **PC/RC4 format** it is the RC4 stream offset in 4-byte words; in the **360's clear format** it is the byte length of the "cipher region" = 2 bytes per table entry + all encrypted body bytes = 2*2 + 6 + 8 = 18. | 360 `sub_82A41A88` (tunnel flush): in the `mode != 0` branch (loc_82A41C70) it stores `htons(r27)`, `r27 = identity(6) + encrypted bytes + table(2n+2) - 2`. Server side: `prototunnel.py decode_datagram` reads `payload[:2]` as `offset_words`. |
| (b) | `0x0067` | Table entry 0: length `(0x00<<4)\|(0x67>>4)` = 6, channel `0x67 & 0xF` = 7 | 360 flush writes the literal `li r11,103` (0x67) as the first entry (loc_82A41B9C area). Server `_decode_candidate`: `length = (first<<4)\|(second>>4); channel = second & 0x0F`. |
| (c) | `0x0081` | Table entry 1: length 8, channel 1 | Same decoder. Channel 1 = CommUDP (see below). |
| (d) | `00 e1497672 00` | Channel-7 identity, 6 bytes: `00` + 32-bit tunnel id + stage byte. Stage 0 = "not yet confirmed by peer". | 360: bytes 80..85 of the flush stack frame = `0`, `tunnel+12` (big-endian, the `clid`), `slot+1652` (stage). Server: `handshake.py channel7_identity`: `b"\x00" + id[4] + stage`. |
| (e) | `00000001 b6ee592c` | CommUDP **CONNECT** (kind 1) + 32-bit connection id | Server `commudp.py`: 8-byte channel-1 payload, `CommUDPType.CONNECT = 1`, `CommUDPControl(kind, connection_id)`. 360 CommUDP update `sub_82A43FB0`: while state 2 and >1000 ms since last send it builds `{kind=1, id=slot+156}` and calls `sub_82A43560`. |

So it is a ProtoTunnel datagram carrying two virtual packets (channel 7 identity, channel 1 CommUDP CONNECT). It is the
first leg of the handshake the server's `EndpointHandshake` expects. The table and bodies are in **clear text**.

* `0xe1497672` is the 360's own tunnel id (`clid`). It is `hash_sar("%I64d", <local user id>)`: `sub_82A08928` calls
  `sub_82A08430` (format string `%I64d` at 0x8203148c, then the same shift-xor hash as `hash_sar_decimal`) and passes the
  result to `ProtoTunnelControl(tunnel, 'clid', v)` (`sub_82A42990`). It is **not** the hash of TICKET, PID, HUID, EKEY,
  LID or GID (I computed all of them; none equals it). Same value in both runs, so it is per user/console, not per
  attempt. Which local id feeds it (XUID vs persona id) is **UNVERIFIED** (virtual calls `vtbl[36]` then `vtbl[56]`).
* The last four bytes (`b6 8e 59 2c` vs `b6 ee 59 2c`) are the per-connection CommUDP id (`slot+156`), the only thing
  that varies. It is not derived from EGEG (inferred; the generator of the id was not located).
* The 360 sends it once per second: `sub_82A43FB0` resends the CONNECT when `now - slot+232 > 1000`. The count of five
  and the give-up condition were only observed on hardware; the give-up counter was **not located**.

### Why the server drops it

Ingress path: `core/udp.py UDPListener._loop` -> `CarbonRebroadcasterService.handle_datagram` (`rebroadcaster/service.py`,
wired in `app.py:240`). There is no source-address allow-list; any source reaches `_decode_ingress_datagram`.

1. `_decode_ingress_datagram` takes `selected_key = wire.tunnel_key or self.key`; `self.key = str(games.ekey).encode("ascii")`
   = `b"9181081919"` (`service.py:__init__`).
2. `_decode_client_datagram` tries stream offsets `0x0012`, `0x10012`, `0x20012` (first packet from an address), each via
   `decode_datagram(payload, key, stream_offset_words=cand)`. That RC4-decrypts the 2-byte-per-entry table starting at
   byte skip `offset*4` and requires a table with positive lengths whose total exactly matches the datagram.
3. On `ProtoTunnelError` it retries with `key ^ 0xFF`, then gives up.
4. On failure: `self._rejected += 1`, `log.warning("Carbon UDP datagram rejected: src=%s:%d bytes=%d error=%s prefix=%s")`
   and returns `None`; `handle_datagram` returns `[]`. **No reply is ever sent.**

I reproduced steps 1-3 with an independent RC4 / table decoder on both captured variants (tails `b6ee592c` and
`b68e592c`), with both keys and the three candidate offsets, header counts 1-9: every combination fails. Read as clear
text, the same bytes parse perfectly (section 1 table). Conclusion: the datagram is well-formed ProtoTunnel, but
unencrypted, and the server only accepts RC4(EKEY) input.

## 2. Server-side identity: what associates a datagram with a participant

Two separate stages, `rebroadcaster/service.py`:

| Stage | Code | Needs |
|---|---|---|
| A. Transport handshake (CONNECT -> reply) | `_accept_endpoint_handshake`, `_dedicated_handshake_hint`, `handshake.py EndpointHandshake.accept` | The datagram must decode (section 1). The **source address** picks a "dedicated hint": an entered participant of a `server_hosted` game that has no endpoint yet. The hint decides the server's channel-7 id: `hash_sar_decimal(game.host.user_id)` (=`0x2768fb60` for HUID 799270239), else `hash_sar_decimal(51)`. |
| B. Binding to a participant | `_ingest_active_packets` -> `_bind_active_ticket` -> `games.resolve_ticket(ticket)` -> `_bind(addr, resolution)` | A later CommUDP *active* packet carrying GameManager message `0x0180` (`parse_session_ticket`: tag + `8004800000` + length + ASCII digits). The ticket is the EGEG `TICKET` (`CarbonGameDirectory.ticket`, md5 of gid/pid/user_id). |

HUID, EKEY, PID and UGID are never read from the client's UDP. EKEY is the global RC4 key (`games.ekey`, default
`9181081919`), and HUID only shapes the *server's* channel-7 id. The client's own `0xe1497672` is ignored apart from the
`0x37` low-byte test in the non-dedicated "joiner" branch (`handshake.py`: `channel7[4] == 0x37 and channel7[5] == 0`),
which does not apply here (byte 4 is `0x72`; and a dedicated hint short-circuits it).

### What the 360's EGAM (PORT=0, no R-INT-*) produces

`theater/service.py _dispatch_enter_game`:

* `internal_ip = fields.get("R-INT-IP", fields.get("INT-IP", "0.0.0.0"))` -> `0.0.0.0`
* `internal_port = _positive_port(fields.get("R-INT-PORT", fields.get("PORT", "0")))` -> `0`
* `external_ip = connection.peer_ip` (the Theater TCP peer) -> stored on the participant by `games.enter`.

`_dedicated_handshake_hint(addr)` tries, in order: (1) `internal_ip == src ip and internal_port == src port`;
(2) a unique unbound participant with `external_ip == src ip`; (3) a unique `internal_port == src port`;
(4) loopback with exactly one pending participant. With `0.0.0.0:0`, (1) and (3) can never match. Only (2) can work, and
only if (a) the UDP source IP the server sees equals the IP of the Theater TCP connection and (b) exactly one unbound
participant uses that IP. If the hint is `None`, the endpoint is created as client-hosted with server id
`hash_sar_decimal(51)`, which a 360 in RC4 mode would reject (section 3).

So: a first datagram from the 360 is *not* matched by port, and is matched by public IP only in the simple single-player
case. It is bound for real only after the `0x0180` ticket.

### EGAM fields the translation should add or rewrite

PC retail EGAM, as the server reads it (`theater/service.py`): `GID`, `R-UID`/`UID` (invite flows only, can be omitted
for Play Now), `R-INT-IP`/`INT-IP`, `R-INT-PORT`/`PORT`. The comments in `_dedicated_handshake_hint` say the retail
client advertises its stock UDP port (1042) and that Theater's recorded internal endpoint is "preserved ... in the
official captures", i.e. it equals the UDP source. The 360 EGAM builder (`sub_829DAC08`) emits `LID`, `GID`, `R-U-*`,
`PORT`, `R-XNADDR`, `GROUP.[]`/`GROUP.<n>`; it has no R-INT-IP / R-INT-PORT / R-UID (matches stage 3).

Rewrite to:

| Field | Value |
|---|---|
| `R-INT-IP` | the IPv4 the shim's UDP socket sends from, as the server will see it (LAN or public; use the public IP if known, because rule 2 compares against the TCP peer IP) |
| `R-INT-PORT` | the UDP source port the server will see for the 360 tunnel socket (the shim's bound/NAT-mapped port) |
| `PORT` | same number (the server reads `R-INT-PORT` first, but keep the 360 field consistent; today it is 0) |
| `R-UID` | omit for Play Now. Only set it for invite entry (`requested_participant`). |

Why `PORT=0` on the 360 is **UNVERIFIED**: the EGAM builder takes PORT as a caller argument (r5); the caller is a virtual
call that was not located. The tunnel itself reports its real local port through `ProtoTunnelStatus 'lprt'`
(`sub_82A08928` stores it at `this+8`). The shim must therefore take the port from its own socket layer, not from EGAM.

## 3. How the 360 builds and expects the packets (code map)

All of this is DirtySDK `ProtoTunnel` + `CommUDP` from FESL SDK 2.9.5.1; the same layering as PC.

| Layer | 360 function | Role |
|---|---|---|
| Socket send hook | `sub_829F0340` (sendto wrapper, calls `NetDll_sendto` via `sub_82917458`) | If a send callback is installed (`SocketControl 'sdcb'`, `sub_829F0928` stores it at +3988) it runs first and, if it returns >0, nothing goes to `sendto`. |
| ProtoTunnel create | `sub_82A432B8` ('ptun' alloc, 124 + 2660*n bytes), `sub_82A431B0` (UDP socket + `bind`), called from `sub_82A08928` (string "Failed to create tunnel!", "Using Tunnel Service.") | `sub_82A432B8` sets **`tunnel+20 = 2`** (`li r9,2 ; stw r9,20(r31)`). `ProtoTunnelControl` (`sub_82A42990`) handles `clid`, `mode`, `flsh`, `rate`, `spam`. The only `'mode'` call in the image is in `sub_82A08928`, and it sets 2. |
| Peer add | `sub_82A42250` ProtoTunnelAdd, called from `sub_82A094C8` | Descriptor: id = hash(`%I64d`, u64 at `link+168`), ip/port from the endpoint, virtual ports 6000/6001/6002, channel flags `slot+29=1` (ch1), `+30=1` (ch2), `+35=1` (ch7) = exactly the server's `DEFAULT_ENCRYPTED_CHANNELS {1,2,7}`. RC4 send state initialised from the key string `link+185`. |
| Link ctor | `sub_82A09080` | `link+168` = 64-bit id (stored with `std`), `link+185` = 17-byte key copied from an argument, default literal `E_KEY` if none. 17 bytes matches the EGEG parser's EKEY buffer (`sub_829DC3A0`: EKEY -> `obj+388`, `li r6,17`; TICKET -> `obj+256`, 128 bytes). That the link's id and key arguments are the EGEG HUID and EKEY is **inferred** (the constructor is called through a vtable). |
| CommUDP | `sub_82A43FB0` (update/timers), `sub_82A43560` (control packet send), `sub_82A436A0` (control packet receive) | Control packet words: `[0]=size(0) [8]=kind [12]=id`. CommUDP sends through the tunnel hook; `sub_82A41E18` maps the destination virtual port to a channel. |
| Flush (**the builder of the 20 bytes**) | `sub_82A41A88` | Builds `[u16 prefix][2-byte table][bodies]` in a 1000-byte stack buffer, channel-7 identity first, then encrypted-flag channels (`sub_82A419B0` with r8=1), then clear channels (r8=0), `sendto(buf, total)` via `sub_829F0340`. Queueing is `sub_82A41D10` (table entry `len>>4`, `(len<<4)\|channel`). |
| Receive | `sub_82A43010` -> `sub_82A42CE0` -> `sub_82A42B60`/`sub_82A41F78` | Looks up the peer slot by source ip:port, else tries unconfirmed slots. |

**The decisive branch** (both directions test the same field):

* Send, `sub_82A41A88`: `lwz r10,20(r24)` (= tunnel mode) `; cmpwi 0 ; bne loc_82A41C70`. Mode 0: prefix = current RC4 word
  position (`slot+84`), table and encrypted bodies RC4-encrypted (`sub_82A493D0`) and the stream advanced
  (`sub_82A41858`). **Mode != 0: prefix = region byte length, no RC4.**
* Receive, `sub_82A42CE0`: `lwz r11,20(r29) ; cmpwi 0 ; bne` -> mode 0 uses `sub_82A41F78` (RC4 decode with offset
  sync), **mode != 0 walks the table in clear** (checks `sum(len)+2n == datagram-2`, ignores the prefix value).

The 360 tunnel is created with mode 2, so it sends clear and also **expects clear replies**. The PC server always
encrypts replies (`TunnelDatagram.encode(wire.tunnel_key or self.key)`), so even a server that accepted the CONNECT
would send something the 360 cannot parse. Probable reason for mode 2 on the console (**inferred**): XNet secure
transport already encrypts, so the SDK skips its own RC4. The PC build presumably creates mode 0 (**UNVERIFIED**; no PC
binary was inspected).

What the 360 needs back before proceeding (`sub_82A436A0`, kind in packet word 8, id in word 12):

* kind 2 (CONNECT_ACK) with `id == slot+156` (its own connection id) while `slot+152 == 2` (connecting) -> state 4
  (established). The server's dedicated reply (`connect_reply`) is `ch7 [00 srvid stage1] + ch1 CONNECT_ACK(id) + ch1
  CONNECT(id)` with the id echoed, which satisfies this; the 360 answers the trailing CONNECT with its own kind-2.
* The channel-7 stage byte 1 from the peer clears the 360's "send identity" flag (`slot+1656`) (`sub_82A42CE0`
  loc_82A42F64..F80).
* In RC4 mode `sub_82A42B60` also compares the received channel-7 id with the slot id `hash_sar_decimal(HUID)`. The
  server's `hash_sar_decimal(game.host.user_id)` matches that only if the dedicated hint resolved. In clear mode the id is
  not checked.
* After CONNECT_ACK the GameManager layer must send the `0x0180` ticket packet. Not examined, **UNVERIFIED** for the 360.

## 4. Ranked hypotheses for the silence

| # | Hypothesis | Likelihood | How to confirm on the next run | Fix |
|---|---|---|---|---|
| 1 | The 360 tunnel is in mode 2 (clear), the server only accepts RC4(EKEY), so the datagram is rejected in `_decode_client_datagram`. | Very high. Datagram decodes perfectly in clear, fails every RC4 candidate, code path ends in a silent drop. | Server log contains `Carbon UDP datagram rejected: src=<ip>:<port> bytes=20 error=invalid ProtoTunnel subpacket table prefix=00120067008100e149767200...` (the log prints the first 24 bytes). If this line is absent, it is #3 or #4 instead. | See fix A or B below. |
| 2 | Even after #1, replies are RC4 and the 360 in mode 2 reads in clear, so it would still time out. | Certain given the code (receive branch on `tunnel+20`). | After fixing #1: server logs `Carbon UDP handshake reply: ... raw=...` but the 360 keeps resending CONNECT. | Fix A (both directions at once) or the reply half of fix B. |
| 3 | Dedicated hint fails (no ticketing/ids): `internal_port=0`, `internal_ip=0.0.0.0` leave only the public-IP rule. A NAT/VPN difference between Theater TCP peer IP and UDP source, or two unbound participants on one IP, yields `None` -> server id `hash(51)`. | Medium, becomes relevant once #1 is fixed. | Server log `Carbon UDP endpoint started: ... profile=client-hosted ... egeg_huid=51 tunnel_id=00000691 hinted_gid=none` instead of `profile=dedicated ... egeg_huid=799270239 tunnel_id=2768fb60`. | Rewrite EGAM `R-INT-IP` / `R-INT-PORT` (section 2). |
| 4 | The datagram never reaches the server, or replies never reach the 360 (firewall, NAT, wrong shim socket, reply source address differs). | Low-medium; cannot be excluded without a server log. | Server log has no line at all for that source (neither "rejected" nor "endpoint started"). Capture on the server host. | Fix the path; also note the 360 accepts replies from a different source only for unconfirmed slots. |
| 5 | RC4 key mismatch (link key `link+185` is not `9181081919`, or the console uses the inverted key the server tries as `key ^ 0xFF`). | Low; only matters once encryption is on. | Server log `Carbon UDP accepted alternate EKEY after retry` means the inverted key was used. | Use that key in the transcoder. |

## 5. Concrete shim changes (server unchanged)

### Fix A (smallest, preferred if the recompiled game may be patched): force tunnel mode 0

After `sub_82A432B8` returns (or inside it, replacing `li r9,2` with `li r9,0` before `stw r9,20(r31)`), set the
tunnel's mode field to 0, i.e. `ProtoTunnelControl(tunnel, 'mode' /*0x6D6F6465*/, 0)` (`sub_82A42990`, which does
`stw r30,20(r31)`). Effects, all through the game's own code:

* Send: RC4 over table and channel-1/2/7 bodies with key `link+185`, prefix = RC4 word offset (`sub_82A41A88`).
* Receive: `sub_82A41F78` decodes the server's RC4 replies and tracks their offsets.
* No Theater or UDP frame rewriting is needed beyond the EGAM fix in section 2.

Risk: assumes `link+185` equals the EGEG EKEY (inferred) and that mode 0 is a supported path on this build (the code is
present and symmetric, but never exercised by the 360 title). If the key is wrong, expect the "alternate EKEY" log or
reject lines.

### Fix B (pure wire shim, no game patch): transcode the tunnel datagrams

Applies to every UDP datagram between the 360 tunnel socket and `<server ip>:19119`. The shim needs `EKEY` (from the
rewritten EGEG it already sees) and per-flow state.

* 360 -> server (clear to RC4). Parse the clear datagram: skip the prefix, read table entries `len=(b0<<4)|(b1>>4)`,
  `channel=b1&0xF`, check `sum(len)+2n == size-2`. Reorder as the server expects (channel 7 first, then channels
  {1,2,7} bodies, then others; the 360 already emits this order). Then `out = be16(W) + RC4_EKEY(table+bodies of channels
  {1,2,7} in wire order, skip=W*4)`, bodies of other channels (0) stay clear and do not advance the keystream.
  `W` can be any monotonically increasing word counter per flow starting at 0; the server uses the field as an absolute
  skip, so no contiguity is required. Advance `W += ceil(region/4)` (region = 2n + encrypted body bytes).
  Worked example, same CONNECT with W=0 and key `9181081919`:
  `00 00 5c c5 de 2e b7 4a 8b d7 ac a5 14 6d c0 38 0b 95 7b 62` (verified: it decodes back to ch7 `00e149767200` and
  ch1 `00000001b6ee592c` with the server's documented algorithm).
* server -> 360 (RC4 to clear). Decode with the server's own rules: offset = first u16, skip = offset*4, try header
  counts 1..9 until the table is consistent, RC4-decrypt table and channels {1,2,7}. Re-emit in the 360 clear format:
  `be16(region_len) + table + bodies` (the prefix value is ignored by the 360 in mode 2). Track the 16-bit wrap of the
  offset as `service.py _decode_client_datagram` does (the server's packets keep a running stream, e.g. the handshake
  reply uses `offset_words=5`).
* Keep the source address/port of the server unchanged: the 360 matches the peer slot by ip:port first.
* Rewrite EGAM as in section 2 (`R-INT-IP`, `R-INT-PORT`, `PORT`).
* Do **not** rewrite `PL` in EGEG (stage 3: `PL=PC` is the branch that builds a plain IP endpoint).

Fix A is less code and uses the game's own state machine; Fix B is needed if the game binary cannot be touched, and it
also doubles as a reference decoder for logging.

## 6. Not verified

* No server-side log for this packet; the reject is derived from code (hypothesis #1 is how to confirm it).
* Mode 0 on PC retail, and whether PC retail actually sends encrypted tables (the server and its captures say so; no PC
  binary was disassembled).
* The source of `clid` (`0xe1497672`): local XUID vs persona id; the value is not a hash of any EGEG field.
* That the link constructor `sub_82A09080` receives the EGEG HUID and EKEY (virtual call chain; only the buffer sizes
  and the `E_KEY` default are confirmed).
* Why the 360 EGAM carries `PORT=0`, and whether the tunnel's real local port is available at EGAM time.
* The generator of the CommUDP connection id and the 5-attempt give-up condition on the 360.
* The 360's GameManager `0x0180` ticket packet format versus the server's `parse_session_ticket`, and everything after
  the CONNECT handshake (ticket binding, host hello, ready/race messages).
* Whether the repo `main` I fetched matches the server version you are running (README says carbon race port 19119; the
  stage 3 note quoted 19118).
