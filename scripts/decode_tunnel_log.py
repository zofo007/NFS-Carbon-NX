#!/usr/bin/env python3
"""Decode the UDP datagrams in a hardware log written with online_fake_live on.

The game's UDP tunnel is RC4-encrypted with the key the server gives in its Theater entry reply
(EGEG, field EKEY). The log keeps up to 128 bytes of every datagram; this prints, for each one, the
stream prefix, its virtual packets and, for the CommUDP "active" packets on channel 1, the header
(older records bundled, sequence, ack) and the first bytes of the record.

    python scripts/decode_tunnel_log.py HWLOG/Standard/logs/nfscarbon_001.log 9181081919

Datagrams longer than the 128 logged bytes can not be decoded fully and are shown as such.
"""
import re
import sys


def keystream(key: bytes, skip: int, count: int) -> bytes:
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 255
        state[i], state[j] = state[j], state[i]
    a = b = 0
    out = bytearray()
    for _ in range(skip + count):
        a = (a + 1) & 255
        b = (b + state[a]) & 255
        state[a], state[b] = state[b], state[a]
        out.append(state[(state[a] + state[b]) & 255])
    return bytes(out[skip:])


def decode(data: bytes, key: bytes):
    """(prefix, [(channel, body)]) for a datagram, or (prefix, None) if it does not decode."""
    prefix = int.from_bytes(data[:2], "big")
    stream = keystream(key, prefix * 4, len(data) - 2)
    for count in range(1, 10):
        if 2 + 2 * count > len(data):
            break
        entries, total, valid = [], 0, True
        for i in range(count):
            high = data[2 + 2 * i] ^ stream[2 * i]
            low = data[3 + 2 * i] ^ stream[2 * i + 1]
            length = (high << 4) | (low >> 4)
            if length == 0:
                valid = False
                break
            entries.append((length, low & 15))
            total += length
        if valid and 2 + 2 * count + total == len(data):
            position, used, packets = 2 + 2 * count, 2 * count, []
            for length, channel in entries:
                body = bytearray(data[position:position + length])
                if channel in (1, 2, 7):
                    for k in range(length):
                        body[k] ^= stream[used + k]
                    used += length
                packets.append((channel, bytes(body)))
                position += length
            return prefix, packets
    return prefix, None


def describe(channel: int, body: bytes) -> str:
    if channel == 1 and len(body) > 8:
        head = int.from_bytes(body[:4], "big")
        ack = int.from_bytes(body[4:8], "big")
        return (f"ch1 older={head >> 28} seq={head & 0xFFFFFFF:#x} ack={ack:#x} {len(body)}B "
                f"rec={body[8:18].hex(' ')}")
    return f"ch{channel} {len(body)}B {body[:10].hex(' ')}"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    key = sys.argv[2].encode()
    pattern = re.compile(
        r"\[\d{4}-\d\d-\d\d ([0-9:.]+)\].*fake Live: "
        r"(UDP datagram of (\d+) bytes from \S+ given to the game|"
        r"UDP datagram split: \d+ bytes became (\d+) bytes|"
        r"sendto socket \d+ (\d+) bytes to \S+): ((?:[0-9a-f]{2} )+)")
    with open(sys.argv[1], encoding="utf-8", errors="ignore") as log:
        for line in log:
            match = pattern.search(line)
            if not match:
                continue
            kind = ("RECV" if match.group(2).startswith("UDP datagram of") else
                    "SPLIT" if match.group(2).startswith("UDP datagram split") else "SEND")
            data = bytes.fromhex(match.group(6).replace(" ", ""))
            full = int(match.group(3) or match.group(4) or match.group(5))
            if len(data) < full:
                print(f"{match.group(1)} {kind} {full}B (only {len(data)} logged) {data[:8].hex(' ')} ...")
                continue
            prefix, packets = decode(data, key)
            body = " ; ".join(describe(c, b) for c, b in packets) if packets else "(does not decode)"
            print(f"{match.group(1)} {kind} {full}B prefix {prefix}: {body}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
