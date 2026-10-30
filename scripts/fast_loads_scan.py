"""Find guest functions that must keep volatile guest loads when the FAST_LOADS option is on.

usage: fast_loads_scan.py <generated dir> [--out volatile_functions.txt]

Plain loads let the compiler merge, reorder and hoist guest memory reads. That is wrong where the game waits for
another thread to change memory. A function is kept volatile when it:
  - contains a loop with no call and no store whose load address does not change inside it (a possible spin-wait),
  - uses a barrier (sync, lwsync, isync, eieio), an atomic (lwarx, ldarx, stwcx., stdcx.) or the global lock.
Those are cheap, conservative rules: they keep every synchronisation-looking function exactly as it is today.
"""
import re
import sys
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\((\w+)\) \{")
LABEL = re.compile(r"^(loc_[0-9A-Fa-f]+):")
GOTO = re.compile(r"goto (loc_[0-9A-Fa-f]+);")
LOAD = re.compile(r"REX_LOAD_(?:U8|U16|U32|U64)\(([^;]*?)\)(?:\)|;|,)")
REG = re.compile(r"\b((?:ctx\.)?r\d+)\.(?:u32|s32|u64|s64)\b")
ASSIGN = re.compile(r"\b((?:ctx\.)?r\d+)\.(?:u64|s64|u32|s32|u16|s16|u8|s8)\s*=[^=]")
BARRIER = re.compile(r"//\s*(sync|lwsync|isync|eieio|lwarx|ldarx|stwcx\.|stdcx\.)\b")
ATOMIC = re.compile(r"REX_ENTER_GLOBAL_LOCK|REX_EXIT_GLOBAL_LOCK")
# Anything here is a side effect or a call the compiler cannot see through (guest functions are weak symbols, so they are
# never inlined), so a loop that contains one cannot have its loads hoisted.
OPAQUE = re.compile(r"\(ctx, base\)|REX_STORE_|REX_MM_STORE|REX_MM_LOAD|REX_RAW_ADDR|REX_ENTER|REX_EXIT|PPC_CALL|ppc_|rex::|\bctx\.lr\b|__builtin_trap|REX_TRAP")


def functions(text):
    name, body = None, []
    for line in text.splitlines():
        m = FUNC.match(line)
        if m:
            name, body = m.group(1), []
            continue
        if name is not None:
            if line == "}":
                yield name, body
                name = None
            else:
                body.append(line)


def reasons(body):
    out = []
    joined = "\n".join(body)
    if BARRIER.search(joined):
        out.append("barrier/atomic")
    if ATOMIC.search(joined):
        out.append("global lock")
    labels = {}
    for i, line in enumerate(body):
        m = LABEL.match(line)
        if m:
            labels[m.group(1)] = i
    for i, line in enumerate(body):
        for target in GOTO.findall(line):
            start = labels.get(target)
            if start is None or start > i:
                continue  # forward branch
            loop = body[start:i + 1]
            if OPAQUE.search("\n".join(loop)):
                continue  # a call or a store inside the loop: the compiler must re-read memory every pass
            written = {m.group(1) for l in loop for m in ASSIGN.finditer(l)}
            for l in loop:
                for lm in LOAD.finditer(l):
                    regs = {m.group(1) for m in REG.finditer(lm.group(1))}
                    if regs and not (regs & written):
                        out.append("loop with fixed load address")
                        return out
    return out


def main():
    root = Path(sys.argv[1])
    out_path = Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else None
    total, flagged, by_reason = 0, [], {}
    for path in sorted(root.glob("nfscarbon_recomp.*.cpp")):
        for name, body in functions(path.read_bytes().decode("utf-8", errors="replace")):
            total += 1
            why = reasons(body)
            if why:
                flagged.append(name)
                for w in set(why):
                    by_reason[w] = by_reason.get(w, 0) + 1
    print(f"{total} functions, {len(flagged)} keep volatile loads ({100.0 * len(flagged) / max(total, 1):.1f}%)")
    for w, n in sorted(by_reason.items(), key=lambda kv: -kv[1]):
        print(f"  {n:6d}  {w}")
    if out_path:
        out_path.write_text("\n".join(flagged) + "\n", encoding="ascii")
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()