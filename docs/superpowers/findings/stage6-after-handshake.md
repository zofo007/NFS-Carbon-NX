# Stage 6: after the CONNECT reply, why the server stays silent

Scope: read-only. Server source = `moio9/NFS-Online-Server` `main` @ b218ea78 (2026-10-07), `server/carbon/...`, fetched
as data and read by hand; line numbers below are that snapshot. No server was contacted, no fetched code was executed
(a decode-check script was refused by the sandbox, so every "traced" result is a hand trace). 360 side: recompiled code.
Tags: **V** = read in code and/or arithmetic-checked against your bytes; **I** = inferred; **U** = unknown.

## 0. Headline

The ticket packets of the 360 are CommUDP **history aggregates** (sequence top nibble = number of older records
appended). The server's `parse_session_ticket` accepts only a record whose GameManager body is exactly the 18-byte
ticket message, so it returns `None` for every 360 ticket datagram: no bind, no log line, no reply (**V**). Independently,
the 360 now sends to `<ip>:44874`; whether those datagrams reach the server is **U** (server code cannot be the cause of
the odd source port, section A). Fix both in the shim: de-aggregate client-to-server CommUDP records and pin the peer
port (section D).

## A. connect_reply and the source port

`handshake.py connect_reply` (L457-470) = TunnelDatagram(offset 5): ch7 `00 | srv_tunnel_id(4) | 01`, ch1
`00000002 <connid>` (CONNECT_ACK), ch1 `00000001 <connid>` (CONNECT). Sizes 2 prefix + 3x2 table + 6+8+8 = 30 (**V**,
matches your 30 B). RC4: table at skip 20 B, bodies after it; stream advance = ceil(28/4) = 7 so the server's
`wire.next_offset_words` becomes 12 (`outbound.py append_datagram` L261-265).

* `srv_tunnel_id` = `hash_sar_decimal(game.host.user_id)` if `_dedicated_handshake_hint` found the participant
  (`service.py` L402-478, L624-634; public-IP rule L472), else `hash_sar_decimal(51)` = 0x691. The id is created once at
  endpoint creation, promoted later (L660-681).
* 360 behaviour: **V** by recomp `sub_82A436A0` (CONNECT_ACK with own id in state 2 -> state 4; CONNECT with own id ->
  answers kind 2) and by your log (datagram 2 contains CONNECT_ACK `00000002 b6ce592c`, and its table has no ch7 entry,
  so the stage-1 ch7 cleared the identity flag, `sub_82A42CE0` L-1656 branch). Because the 360 mode-0 receive path
  compares ch7 with `hash(HUID)` (stage 5, **I**), its reply suggests the dedicated hint resolved (id 0x2768fb60).
* Peer address: `sub_82A42CE0` loops the slots comparing `slot+4` (ip) / `slot+8` (port); with no match an unconfirmed
  slot (`slot+1652 == 0`) adopts the sender (`stw r25,4(r31); sth r26,8(r31); stb 1,1652(r31)`). That is exactly why
  the game moved to port 44874 (**V**). A later server datagram from any other port would match no confirmed slot and
  be dropped by the game (returns -2).
* Source port: **the server has exactly one UDP socket for Carbon**: `app.py` L240 `UDPListener(race_listen...)`, all
  sends go through `core/udp.py _send_response` L185-189 `self._socket.sendto(...)`; `isolate_reply_targets=True`
  (app.py L245) only creates per-target send *threads/queues* (`_enqueue_send`/`_send_loop`), not sockets. No other
  `sendto` exists in the package (**V**). So 44874 was created by something between the server and the Switch (NAT SNAT
  of a flow that is not conntrack-linked to the inbound one, or a UDP relay/tunnel replying from its upstream socket)
  (**I**). If it is a relay, datagrams sent to 44874 hit the relay's upstream socket, not the server (**I**, plausible).

## B. Server path for datagrams 2..N

Order in `handle_datagram` (L1268-1326): `_decode_ingress_datagram` -> `_handle_transport_controls` ->
`_accept_endpoint_handshake` -> `_ingest_active_packets` -> bind -> bound dispatch.

1. **Decode** (`_decode_ingress_datagram` L521-569, `_decode_client_datagram` L480-519, `prototunnel.decode_datagram`
   L211-249). The prefix is an *absolute* word offset into one RC4(EKEY) stream: `skip = prefix*4`; table bytes at
   `skip`, each encrypted body at `skip + 2n + earlier encrypted bytes`. First datagram from an address tries
   `low, low+0x10000, low+0x20000`; afterwards `(prev & ~0xFFFF)|low` with +-0x10000 wrap fixes. There is **no
   contiguity or restart requirement**: your prefixes 5, 16, 27, 38, 49, 60, 168, 278... are accepted as-is (**V**;
   45 B = 2+6+37 -> 11 words, 5+11 = 16 etc. matches the stream accounting). Per-address state is keyed on the
   *source (ip, port) the server sees*; a new source simply restarts as "first datagram" (works too). Silent/logged
   rejects: >1000 B, <4 B, no consistent table among header counts 1..9 (positive lengths, sum == size), wrong key
   (also tries key^0xFF); logged as WARNING "Carbon UDP datagram rejected ..." (L547). None applies to your bytes.
2. **Controls** (L580-612): DISCONNECT drops; a CONNECT from a *bound* addr resets it. Your datagrams carry CONNECT_ACK
   only -> nothing.
3. **Handshake object** (L614-713, `EndpointHandshake.accept` L394-444): with a CONNECT_ACK present and role HOST (set by
   the first CONNECT; ch7[4]=0x72 != 0x37) it sets stage ESTABLISHED and returns `None`; with no control it returns
   `None`. No reply, as the code intends (HostHello is deferred to the bind path).
4. **Active parse** (`commudp.py parse_channel_one` L78-93): ch1 body of exactly 8 bytes whose first u32 is 1..6 is a
   control, any other body >= 8 B is `CommUDPActive(seq=u32, ack=u32, payload)`. Header fields: u32 #1 = `[n:4 | seq:28]`
   where **n = number of older records appended** (`outbound.py commudp_aggregate_payload` L106-128,
   `append_active_record_batch` L130-164; `sequence = ((len-1)<<28) | latest`), u32 #2 = cumulative ack (28-bit,
   `_SEQUENCE_MASK 0x0FFFFFFF`). Record layout: `R0(newest, ends with its own terminator) | R1 | len(R1) | R2 | len(R2)...`
   (**V**: server HostHello = `body(...04) + footer12 + 40 0d` in `session_codec.encode_host_hello` L112 is
   `R0 + R1(footer+40) + 0x0d`).
5. **Your packets decode exactly under that layout** (**V**, arithmetic):
   * Datagram 1/2 third body (21 B): `00 00 01 00 | 00 00 00 ff | footer12 | 40` = the "empty active ACK" record,
     seq 0x100 (`session_codec.encode_empty_active_ack` L63-77, 13-byte record). The 8-byte `00 00 01 00 00 00 00 ff` is
     a bare ack-only packet (no record). Both parse as `CommUDPActive`, `game_manager_body` -> `b""`/no message. Harmless.
   * Ticket datagrams, 41 B payload: `10 00 01 01` = n=1, seq 0x101; ack 0xff; R0 = `01 80 | 80 04 80 00 00 | 0a |
     "1952405471" | 04` (19 B); R1 = footer + `40` (13 B, the seq-0x100 record); trailing byte `0d` = len(R1).
     8+19+13+1 = 41; your decoded `... 31 04 00 ...` is the terminator then footer byte 0.
   * 59 B (seq 0x102, n=2): R0 = footer+40 (the new footer-only record), R1 = ticket+04, R2 = footer+40.
     73 B (n=3) and 67 B (seq 0x105/0x106, n=3) follow the same shape. Each retransmit tick adds a footer-only
     record and nothing is acked, so the history grows.
6. **Why the bind fails (the silent drop).** `_ingest_active_packets` (L943-974) -> `_bind_active_ticket` (L902-941) ->
   `parse_session_ticket(active.game_manager)` (`commudp.py` L41-55). `game_manager_body` (L58-68) takes `payload[8:]`,
   `final = last byte`, `body = ngl[:-1]`, strips 12 footer bytes only if `final & 0x40`. For the 360 ticket
   `final = 0x0d` (len byte, bit 0x40 clear) so `body` = 18-byte message + `04` + footer12 + `40` = **32 bytes**.
   `parse_game_manager` accepts it (`01 80`, type 0), the tag/prefix check `01 80 80 04 80 00 00` passes, `size =
   body[7] = 10`, but `len(body) != 8 + size` (32 != 18) -> `return None` (L49-50). `_bind_active_ticket` returns
   `None` *before* `resolve_ticket`, so there is no "ticket rejected" log either (**V**, hand trace). In the 59/73/67 B
   forms `body[0]` is the footer's first byte (0x00), so `parse_game_manager` (L73) returns `None` as well.
   The server's own tests/fixture (`tests/test_carbon_transport.py::ticket_confirmation`) feed only the
   single-record form `CONNECT_ACK | 00000100000000ff | 00000100000000ff + 01808004800000 len ticket + 04`.
   So the 360 differs from the PC retail client in one structural way: its seq-0x100 slot is occupied by a footer-only
   record, so the ticket is seq 0x101 and rides in an aggregate.
7. **Other conditions that would also stay silent** (all **V** in code, none observed to apply): ticket not decimal /
   size 0 / length mismatch (L46-55); `games.resolve_ticket` (`theater/directory.py` L1159-1167) needs a participant
   still entered whose `md5("carbon-ticket:gid:pid:user_id")`-derived ticket equals the string (your 1952405471 is in the
   generator's range 1e9..1e10; the participant must not have expired: `join_timeout_seconds` default 45 s,
   `endpoint_lifecycle.expire_stale_rooms` L549-593) -> WARNING "ticket rejected ... UNKNOWN_TICKET";
   `SessionBootstrapCoordinator.bind` (`session_bootstrap.py` L96-132) refuses an address already bound to a different
   user/game -> "BIND_FAILED". Unbound endpoints never get a transport ACK (`_finish_bound_datagram` only runs when
   `binding` exists, L1300-1324). Packets are never rejected for sequence number, ack value, header nibble or size (only
   the 9-header/1000 B decode limits).

## C. After a successful bind

`handle_datagram` L1291-1298: `bootstrap_pending = True`, `bootstrap_acknowledgement = last_client_sequence`; if the
datagram produced no handshake reply, `_append_ticket_ack` (`session_bootstrap.py` L229-255) sends **one ch1 packet**:
`encode_empty_active_ack(0x100, ack = client seq, footer)` = `00 00 01 00 | ack | footer12 | 40` (datagram = 2+2+21 =
**25 B**), RC4 at `wire.next_offset_words`. Footer = `client_tick(4, echoed from the 360 footer bytes 4..8) |
server_tick(4, server monotonic ms) | 00000000` (`service.py _server_footer_from_client` L1796-1815).
Then the **next inbound datagram that contains any active packet** triggers `_append_bootstrap`
(`service.py` L1095-1102, `session_bootstrap.py` L134-227): one datagram, ch1 only, with HostHello `01 82` (sequence
`0x10000000 | 0x101`, payload `...04 + footer + 40 0d`, `encode_host_hello`) followed by one PlayerRoster `01 83` per bound
participant (seq 0x102...), all with ack = the ticket sequence. Registered for retry (`session-host-bootstrap`,
`confirmations.py`, 0.25 s doubling). After the 360's PlayerPublish `01 84` the server sends PlayerJoined `01 85`
(`_append_join_publication` L257-330, 12 ms spacing via `reply_spacing_seconds_for`), then session-object / host
property exchange (`invite_session`, `room_commit`, outside this note). Encryption: same RC4(EKEY), absolute stream
word offset per datagram = running `wire.next_offset_words`, ch1 encrypted, no ch7 after the handshake.

What the 360 needs (**I**, same DirtySDK/NFSC netcode as PC; the 360 ticket bytes are byte-identical to the PC format,
which supports a shared GameManager layer, but `01 82/83/84/85` handling on the 360 is not verified in recomp):
1. A CommUDP ack covering its seq 0x101 (the 25 B ticket ACK carries ack = 0x101) so it stops aggregating.
2. HostHello + roster as above, decodable by `sub_82A41F78` (mode 0 offset sync) and handed to CommUDP
   (`sub_82A43B50`/`sub_82A43C38` handle `n = seq>>28` history exactly as the server encodes it: `rlwinm r29,r11,4,28,31`
   and `clrlwi r10,r10,4` in `sub_82A43FB0`).
3. All server datagrams must come from the **confirmed slot address** (ip:44874 now), see A.

## D. Ranked hypotheses and fixes (server unchanged)

| # | Hypothesis | Likelihood | Confirm in next log | Shim fix |
|---|---|---|---|---|
| 1 | Aggregated ticket record defeats `parse_session_ticket` (B6). | Certain blocker if datagrams reach the server (**V** by code). | Decode the first post-handshake 45 B datagram: ch1 payload = 41 B, first u32 `0x1000_0101`, last byte `0d`, bytes 27..40 = `footer12 40`. After the fix: a **25 B** server datagram within ~100 ms of the first normalized ticket, then a ~51+ B HostHello datagram after the next client active packet, and the 360 stops adding footer-only history. | Section D1. |
| 2 | Datagrams to the new port 44874 never reach the server (relay/NAT). | Medium; unknowable from the client. | With fix 1 in place and still no reply: duplicate-send each datagram to `:19119` for one run; reply only to the 19119 copy means this. Log `sendto` rc and `recvfrom` source for both. | Pin the peer (D2). |
| 3 | Server replies arrive but the 360 drops them: wrong source port after the slot adoption, or mode-0 offset sync rejects offset 12 / 0 (new server wire after a new source tuple starts at 0). | Low-medium. | Full bytes of every received datagram + RC4 decode with offset prefix; check `recvfrom` source port equals 44874, and whether the prefix is 12 (same tuple) or 0 (new tuple). The 360 tunnel stats/CONNECT retransmit do not change. | D2 (source rewrite) and, if offsets are the issue, re-prefix server datagrams (RC4 re-encode with the 360's expected next offset). |
| 4 | Ticket lookup fails (participant expired/not in EGAM'd game, wrong TICKET). | Low (ticket is server-issued, format matches). | Only server-side WARNING; client evidence = fix 1 applied, a reply never comes and 2/3 excluded. Check time EGEG -> UDP < 45 s. | Keep EGEG TICKET untouched; send the ticket promptly. |
| 5 | Server ack/seq semantics: server acks using the *normalized* sequence; if the shim rewrites the seq to 0x100 instead of 0x101 the 360 will not prune and keeps resending. | Self-inflicted risk. | 25 B ack's ack field must equal the 360's ticket seq (0x101). | Never renumber; keep real seq. |

### D1. Client-to-server normalization (stateful, in the shim's sendto path, mode-0 datagram)

Per datagram: RC4-decode with EKEY (`skip = prefix*4`, table then channels {1,2,7} bodies), then for every ch1 packet
`P` with `len(P) > 8`, `n = P[0] >> 4 > 0`:
```
pos = len(P); old = []
for _ in range(n): L = P[pos-1]; old.append(P[pos-1-L:pos-1]); pos -= L+1     # old = oldest last
R0 = P[8:pos]                                  # newest record incl. its 04 / 40 terminator
latest = be32(P[0:4]) & 0x0FFFFFFF; ack = P[4:8]
# oldest-first list: old[n-1] (seq latest-n), ..., old[0] (seq latest-1), then R0 (seq latest)
emit, oldest first, only if seq is newer than the last forwarded seq for this flow:
    be32(seq) + ack + record          # nibble 0
```
Result for your 41 B packet: `00000100 000000ff footer 40` then `00000101 000000ff 01 80 80 04 80 00 00 0a <ticket> 04`
(27 B), which `parse_session_ticket` accepts (`body` = 18 B, size 10). Re-encode the datagram with the same prefix using
the server's rule (`encode_datagram`: table at `prefix*4`, encrypted bodies at `prefix*4 + 2n + consumed`; order ch7,
then ch1/2, then clear). New lengths are fine because the server uses the absolute prefix; stay within 8 virtual
packets / 1000 B (drop already-forwarded old records). Keep the 8-byte ack-only packets and controls unchanged.
Apply it to every client ch1 packet, not only the ticket; the server tolerates leading-record plus history for later
messages (`service.py` L1439-1452 comment) but needs a clean leading record, which this guarantees.

### D2. Peer pinning

In the shim `sendto`: if destination ip == server ip, force port 19119 (what the server listens on); in `recvfrom`/the
receive completion: if source ip == server ip, report port 44874 (the value the 360 slot adopted) or, better, report
19119 for the whole flow so the slot never changes (set it from the first reply). Keep one consistent value so
`sub_82A42CE0` always matches the confirmed slot.

### Next-run logging checklist
Full bytes (up to 128) both directions with timestamps; for each datagram the decoded view (prefix, table, each
ch1 header n/seq/ack and record lengths); `sendto` destination port actually used and `recvfrom` raw source port; any
`recvfrom` error; for outgoing normalized datagrams log before and after bytes.

## E. Not verified
Whether the home/relay in front of the server forwards port 44874 to the server; the server's actual log (not
available); `01 82-85` handling in the 360 binary; whether the 360 mode-0 decoder accepts an arbitrary first server
offset (5 worked for the CONNECT reply, so non-zero starts are accepted at least once); whether the retail PC client
ever sends the ticket aggregated (the fixture suggests not).
