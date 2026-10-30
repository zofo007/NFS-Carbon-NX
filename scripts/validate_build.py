"""Check NRO structure and the companion ELF's ARM64 target; never claim gameplay."""
from pathlib import Path
import struct
import sys


def validate_nro(data):
    if len(data) < 0x80 or data[0x10:0x14] != b"NRO0":
        raise ValueError("Missing or truncated NRO0 header")
    if data[3] & 0xFC != 0x14:  # the first instruction of crt0 is an unconditional B; anything else means code was placed ahead of it
        raise ValueError("NRO does not start with the crt0 entry branch (a section was linked before .crt0)")
    size = struct.unpack_from("<I", data, 0x18)[0]
    if size < 0x80 or size > len(data):
        raise ValueError("NRO image size exceeds the file or omits its header")
    previous = 0
    for offset in (0x20, 0x28, 0x30):
        start, length = struct.unpack_from("<II", data, offset)
        if start % 0x1000 or length % 0x1000 or start < previous or start + length > size:
            raise ValueError("NRO segments are unaligned, overlapping, or outside the image")
        previous = start + length
    text_start, text_size = struct.unpack_from("<II", data, 0x20)
    if text_start != 0 or text_size < 0x1000:
        raise ValueError("NRO has no valid executable segment")
    return size


def validate_elf(data):
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4:7] != b"\x02\x01\x01":
        raise ValueError("Expected a little-endian ELF64 version 1 executable")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if kind not in (2, 3) or machine != 183:
        raise ValueError("ELF is not an ARM64 executable/PIE")
    version, entry, phoff = struct.unpack_from("<IQQ", data, 20)
    ehsize, phsize, phcount = struct.unpack_from("<HHH", data, 52)
    if version != 1 or ehsize != 64 or phsize != 56 or not 0 < phcount < 0xFFFF:
        raise ValueError("Invalid ELF executable/program-header layout")
    if phoff < ehsize or phoff + phcount * phsize > len(data):
        raise ValueError("ELF program headers extend outside the file")
    executable_entry = False
    for index in range(phcount):
        segment, flags, offset, address, _, size, memory_size, alignment = struct.unpack_from(
            "<II6Q", data, phoff + index * phsize
        )
        if offset + size > len(data):
            raise ValueError("ELF segment extends outside the file")
        if segment != 1:  # PT_LOAD
            continue
        if memory_size < size or (alignment > 1 and (
            alignment & (alignment - 1) or (offset - address) % alignment
        )):
            raise ValueError("Invalid ELF load-segment size/alignment")
        executable_entry |= bool(flags & 1 and address <= entry < address + size)
    if not executable_entry:
        raise ValueError("ELF entry point is not in a file-backed executable segment")


if __name__ == "__main__":
    try:
        size = validate_nro(Path(sys.argv[1]).read_bytes())
        validate_elf(Path(sys.argv[2]).read_bytes())
        print(f"PASS: ARM64 ELF and NRO structure ({size:,} image bytes). Hardware validation not performed.")
    except (OSError, ValueError, IndexError) as error:
        sys.exit(str(error))
