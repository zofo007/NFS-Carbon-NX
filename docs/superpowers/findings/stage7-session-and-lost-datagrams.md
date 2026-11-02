# Stage 7: what the UDP session does after the handshake (hardware run of 2026-11-02 02:05)

Source: `nfscarbon_001.log` (two Play Now attempts), decoded with `scripts/decode_tunnel_log.py` plus a partial decoder
for the datagrams the log cuts at 128 bytes, and the public server source (`server/carbon/...`, read as data).
Tags: **V** = read in the log or the server code, **I** = inferred, **U** = unknown.

## What works (V)

* Ticket accepted, ack, HostHello `01 82`, roster `01 83`, PlayerJoined `01 85`: the lobby ("Game Setup") shows.
* The game's local car/session object goes out as three `0x1E` BIG_MESSAGE chunks (offsets 0, 0x1E4, 0x3C8, total
  0x457 bytes, seq 0x106-0x108 at +2.2 s). The server's parser accepts that layout (`session_object.py`).
* The game's ClockSyncStart `0x02` (token `42 bd ac 8c`, seq 0x110) is answered at +15 s by the server with its reflected
  object (`0x1E`, object id 2) and the `0x03` confirmation, in the 520 B and 721 B datagrams; the game acknowledges them
  (ack 0x114 at +15.7 s). The session is established from the game's side.

## `01 8c` is not a disconnect (V)

GameManager message ids are `0x80 | type`; `0x8c` = type `0x0C` = `HOST_PROPERTIES` (`protocol.py`). The server sends it
(`append_initial_hostprops`) once the room is finalised. The OLMSG records the two sides exchange are `0x12` LATENCY_INFO,
`0x1C` ACTIVE_GAME_MESSAGE, `0x01`/`0x02` clock sync, `0x15` INVITES_ON, `0x17` ENABLE_JOINS, `0x1D` GAME_ATTRIBUTES.
The Game Setup timer is an OLMSG `0x1B` START_TIMER; none arrived, so it stayed at 10:00.

## Why the lobby stalls: datagrams from the server are lost (V)

The server numbers its datagrams by their position in one RC4 stream (the be16 prefix, in 4-byte words). A datagram that
does not arrive leaves a hole in that numbering. In session 1 the numbering is gapless until +25 s, then 14 holes
appear (24-312 bytes each; the size is estimated from the numbering), and the server's records are missing in the
sequence from the first hole on: 0x118, 0x11C-0x122, 0x124-0x126, 0x129, 0x130-0x13A never reached the game. The server
sends LATENCY_INFO and similar records once, without retransmission or bundled history, so a single lost record
leaves a permanent hole in the game's in-order receive window: the game's ack stays at 0x117 from then on, whatever the
server sends after it. The session then times out on the game's side ("You have been disconnected from the game"),
about 35 s after the join, and it sends ECNL.

Not yet known (**U**): whether the datagrams are lost on the network, or dropped on the Switch before the game reads
them. Hints for the second: the log shows 75-90 lines per second during the session against about 12 outside it, and
the lines are 8-12 ms apart (each warn line is flushed to the SD card by the thread that writes it). Two sources made
most of it: the `NetDll_getsockname` stub (22 lines/s) and `XLIVEBASE` message `0x5800E` (33 lines/s). The user also
saw 18-20 fps in the lobby. A game thread that is held up like this reads its UDP socket late, and the Switch's 9 KB
UDP receive buffer can then overflow.

## Changes made for the next run

* Stub and unimplemented-message lines are logged for the first calls only, then one in a thousand; the per-call trace is
  one line per ten seconds after the first 40 calls.
* The receive path counts the holes in the server's numbering (`fake Live: the server's datagram numbering skips ...`)
  and reports every 5 s how often the game polled its socket and the longest gap between polls
  (`fake Live: UDP receive polled ...`). Many holes with short poll gaps point at the network, long gaps at the Switch.

## Next, depending on the result

* Holes remain with short poll gaps: loss on the network. A healer in the shim could fill a hole that stays open for
  more than about 1.5 s with an empty record of the missing sequence number (the server's own empty ack record), after
  the server's reliable retransmissions (0.25 s doubling) had their chance.
* Poll gaps long: drain the socket on a thread of its own into a queue.
* Ask the server owner for the server-side lines "Carbon GM confirmation retried" and the datagrams sent to our endpoint
  between +25 s and +50 s of a session, to see whether the server really sent the missing numbers.
