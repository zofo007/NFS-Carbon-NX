"""Validate Carbon's Xbox 360 executable and archive layout without changing it."""
import hashlib
import json
from pathlib import Path
import struct
import sys

EXPECTED_XEX_SHA256 = "947f95c7306eabf5bc72972938df57a922798bafbc02edf1a9107f0462c721e1"


def xex_identity(data):
    if len(data) < 24 or data[:4] != b"XEX2":
        raise ValueError("Expected an Xbox 360 XEX2 executable")
    count = struct.unpack_from(">I", data, 20)[0]
    if count > 4096 or 24 + count * 8 > len(data):
        raise ValueError("Truncated or invalid optional-header table")
    headers = dict(struct.unpack_from(">II", data, 24 + i * 8) for i in range(count))
    execution = headers.get(0x00040006)
    if execution is None or execution + 24 > len(data):
        raise ValueError("Missing or truncated execution identity")
    media, version, _, title = struct.unpack_from(">IIII", data, execution)
    if title != 0x454107EC:
        raise ValueError(f"Expected Xbox 360 Carbon title 454107EC, found {title:08X}")
    base = headers.get(0x00010201)
    entry = headers.get(0x00010100)
    if base != 0x82000000 or entry != 0x829446E0:
        raise ValueError("Executable edition does not match the pinned Carbon address manifest")
    return {"title_id": f"{title:08X}", "media_id": f"{media:08X}", "version": version,
            "image_base": f"{base:08X}", "entry_point": f"{entry:08X}"}


def check_game(root):
    root = Path(root)
    data = (root / "default.xex").read_bytes()
    result = xex_identity(data)
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_XEX_SHA256:
        raise ValueError("XEX differs from the audited edition; revalidate manifest addresses before building")
    required = [root / "NFS/ZDIR.BIN"] + [root / f"NFS/ZZDATA{i}.BIN" for i in range(11)]
    missing = [p.name for p in required if not p.is_file()]
    if missing:
        raise ValueError("Missing Carbon resources: " + ", ".join(missing))
    index = (root / "NFS/ZDIR.BIN").read_bytes()
    sizes = {i: (root / f"NFS/ZZDATA{i}.BIN").stat().st_size for i in range(11)}
    entries = check_archive_index(index, sizes)
    movies = list((root / "Movies").glob("*.wmv"))
    if not movies:
        raise ValueError("Carbon movies are missing")
    result.update(sha256=digest, archives=len(required), archive_entries=entries, movies=len(movies),
                  resource_bytes=sum(p.stat().st_size for p in required))
    return result


def check_archive_index(index, sizes):
    if not index or len(index) % 24:
        raise ValueError("ZDIR.BIN has an empty or truncated 24-byte entry table")
    entries = list(struct.iter_unpack("<6I", index))
    for _, archive, sector, _, size, _ in entries:
        if archive not in sizes or sector * 2048 + size > sizes[archive]:
            raise ValueError(f"ZDIR.BIN references missing/truncated archive {archive}")
    return len(entries)


if __name__ == "__main__":
    try:
        print(json.dumps(check_game(sys.argv[1] if len(sys.argv) > 1 else "game-data"), indent=2))
    except (OSError, ValueError) as error:
        sys.exit(str(error))
