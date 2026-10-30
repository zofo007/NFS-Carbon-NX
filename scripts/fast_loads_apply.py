"""Apply or revert the FAST_LOADS transform on a generated edition folder (build products, never committed).

usage: fast_loads_apply.py apply|revert <generated dir>

apply : in nfscarbon_pch.h the REX_LOAD_U8..U64 macros become plain (alias-safe memcpy) loads when the translation unit is
        compiled with -DNFSCARBON_FAST_LOADS, and stay volatile otherwise; REX_LOADV_* are always the volatile ones. Every
        function that fast_loads_scan.py lists (possible spin-waits, barriers, atomics, the global lock) is rewritten to
        use REX_LOADV_*, so those functions are exactly as before.
revert: restores the files from the backups made by apply. A file the code generator rewrote in the meantime is left alone.
Hand-written hooks and native_leaf.h are compiled without NFSCARBON_FAST_LOADS and keep volatile loads.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fast_loads_scan as scan  # noqa: E402

MANIFEST = "fast_loads_manifest.json"
HELPER = """
// --- FAST_LOADS (scripts/fast_loads_apply.py): plain, alias-safe guest loads for generated guest code only.
// REX_LOADV_* are the original volatile loads; functions that wait on other threads keep using them.
#define REX_LOADV_U8(x) (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOADV_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOADV_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOADV_U64(x) __builtin_bswap64(*(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#if defined(NFSCARBON_FAST_LOADS)
template <class T> static inline __attribute__((always_inline)) T rex_plain_load(const u8* p) { T v; __builtin_memcpy(&v, p, sizeof(T)); return v; }
#define REX_LOAD_U8(x) rex_plain_load<u8>(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))
#define REX_LOAD_U16(x) __builtin_bswap16(rex_plain_load<u16>(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(rex_plain_load<u32>(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(rex_plain_load<u64>(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
#else
"""
ORIGINAL = [
    "#define REX_LOAD_U8(x) (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))",
    "#define REX_LOAD_U16(x) __builtin_bswap16(*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))",
    "#define REX_LOAD_U32(x) __builtin_bswap32(*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))",
    "#define REX_LOAD_U64(x) __builtin_bswap64(*(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))",
]


def rd(path):
    return path.read_bytes().decode("utf-8", errors="surrogateescape")  # no newline translation


def sha(data):
    return hashlib.sha256(data).hexdigest()


def revert(root):
    path = root / MANIFEST
    if not path.exists():
        return 0
    manifest = json.loads(path.read_text())
    restored = 0
    for name, entry in manifest.items():
        target, backup = root / name, root / (name + ".fastloads.bak")
        if backup.exists() and target.exists() and sha(target.read_bytes()) == entry["new"]:
            target.write_bytes(backup.read_bytes())
            restored += 1
        if backup.exists():
            backup.unlink()
    path.unlink()
    return restored


def apply(root):
    revert(root)  # always start from the original files
    flagged = set()
    manifest = {}

    def write(name, data):
        target = root / name
        original = target.read_bytes()
        (root / (name + ".fastloads.bak")).write_bytes(original)
        target.write_bytes(data)
        manifest[name] = {"orig": sha(original), "new": sha(data)}

    # 1. function list from the unmodified code
    for path in sorted(root.glob("nfscarbon_recomp.*.cpp")):
        for name, body in scan.functions(rd(path)):
            if scan.reasons(body):
                flagged.add(name)
    # 2. pch.h
    pch = rd(root / "nfscarbon_pch.h")
    for line in ORIGINAL:
        if line not in pch:
            raise SystemExit(f"nfscarbon_pch.h has no line: {line}")
    first, last = pch.index(ORIGINAL[0]), pch.index(ORIGINAL[3]) + len(ORIGINAL[3])
    block = pch[first:last]
    if block.replace("\r\n", "\n").split("\n") != ORIGINAL:
        raise SystemExit("the four REX_LOAD macros are not one contiguous block; refusing to patch")
    nl = "\r\n" if "\r\n" in pch else "\n"
    patched = pch[:first] + HELPER.replace("\n", nl).lstrip(nl) + nl.join(ORIGINAL) + nl + "#endif" + pch[last:]
    write("nfscarbon_pch.h", patched.encode("utf-8", errors="surrogateescape"))
    # 3. flagged functions use the volatile loads
    rewritten = 0
    for path in sorted(root.glob("nfscarbon_recomp.*.cpp")):
        text = rd(path)
        out, current, changed = [], None, False
        for line in text.splitlines(keepends=True):
            m = scan.FUNC.match(line)
            if m:
                current = m.group(1)
            elif line.startswith("}"):
                current = None
            if current in flagged and "REX_LOAD_U" in line:
                line = re.sub(r"REX_LOAD_(U8|U16|U32|U64)\b", r"REX_LOADV_\1", line)
                changed = True
            out.append(line)
        if changed:
            write(path.name, "".join(out).encode("utf-8", errors="surrogateescape"))
            rewritten += 1
    (root / MANIFEST).write_text(json.dumps(manifest, indent=1))
    print(f"fast loads applied: {len(flagged)} functions keep volatile loads, {rewritten} source files rewritten + nfscarbon_pch.h")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("apply", "revert"):
        raise SystemExit(__doc__)
    root = Path(sys.argv[2])
    if sys.argv[1] == "apply":
        apply(root)
    else:
        print(f"fast loads reverted: {revert(root)} files restored")


if __name__ == "__main__":
    main()