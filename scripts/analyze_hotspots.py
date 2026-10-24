"""Rank the hottest functions in a hotspot-mode rex_perfil.log.

usage: analyze_hotspots.py <rex_perfil.log> <nm-output.txt> [--top N]

Make nm-output.txt from the ELF that matches the NRO that produced the log:
    aarch64-none-elf-nm -n nfscarbon.elf > nm-output.txt
The log lists samples as `imagen+0xOFFSET`; the NRO is linked at 0, so the offset is the ELF address. Guest functions
are named sub_XXXXXXXX (the Xbox address). Per thread it prints the leaf share (where the pc was) and the inclusive
share (functions anywhere on a sampled stack), averaged over the logged intervals.
"""
import bisect
import re
import sys
from collections import defaultdict

PC = re.compile(r"^\s+([\d.]+)%\s+pc imagen\+0x([0-9a-f]+)")
STACK = re.compile(r"^\s+([\d.]+)%\s+pila (.*)$")
FRAME = re.compile(r"imagen\+0x([0-9a-f]+)")
THREAD = re.compile(r'^-- hilo \d+ "([^"]*)": CPU ([\d.]+)%')


def load_symbols(text):
    addrs, names = [], []
    for line in text.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] in "tTwW":
            addrs.append(int(parts[0], 16))
            names.append(parts[2].removeprefix("__imp__"))
    order = sorted(range(len(addrs)), key=addrs.__getitem__)
    return [addrs[i] for i in order], [names[i] for i in order]


def analyze(log_text, symbols):
    addrs, names = symbols
    name_of = lambda off: names[max(bisect.bisect_right(addrs, off) - 1, 0)] if addrs else hex(off)
    leaf, incl, seen, cpu = (defaultdict(lambda: defaultdict(float)) for _ in range(4))
    intervals = defaultdict(int)
    thread = None
    for line in log_text.splitlines():
        m = THREAD.match(line)
        if m:
            thread = m.group(1) or "?"
            intervals[thread] += 1
            cpu[thread]["cpu"] += float(m.group(2))
            continue
        if line.startswith("==== ") or line.startswith("== "):
            thread = None
        if thread is None:
            continue
        m = PC.match(line)
        if m:
            leaf[thread][name_of(int(m.group(2), 16))] += float(m.group(1))
            continue
        m = STACK.match(line)
        if m:
            share = float(m.group(1))
            fns = {name_of(int(a, 16)) for a in FRAME.findall(m.group(2))}
            for fn in fns:
                incl[thread][fn] += share
    return leaf, incl, intervals, cpu


def neighbours(log_text, symbols, thread_name, target):
    """Frames printed just before and just after `target` in every sampled stack of the thread.

    The log does not say which end of a stack is the innermost frame, so both sides are reported; the
    side that lists the callees (the deeper frames) is the one whose leaf matches the `leaf` table.
    """
    addrs, names = symbols
    name_of = lambda off: names[max(bisect.bisect_right(addrs, off) - 1, 0)] if addrs else hex(off)
    before, after = defaultdict(float), defaultdict(float)
    intervals, thread = 0, None
    for line in log_text.splitlines():
        m = THREAD.match(line)
        if m:
            thread = m.group(1) or "?"
            if thread == thread_name:
                intervals += 1
            continue
        if line.startswith("==== ") or line.startswith("== "):
            thread = None
        if thread != thread_name:
            continue
        m = STACK.match(line)
        if not m:
            continue
        frames = [name_of(int(a, 16)) for a in FRAME.findall(m.group(2))]
        for i, fn in enumerate(frames):
            if fn == target:
                share = float(m.group(1))
                if i > 0:
                    before[frames[i - 1]] += share
                if i + 1 < len(frames):
                    after[frames[i + 1]] += share
                break
    return before, after, max(intervals, 1)


def main():
    if "--callees" in sys.argv:
        i = sys.argv.index("--callees")
        target, thread_name = sys.argv[i + 1], sys.argv[i + 2]
        rest = [a for a in sys.argv[1:] if not a.startswith("--") and a not in (target, thread_name)]
        log = open(rest[0], encoding="utf-8", errors="replace").read()
        symbols = load_symbols(open(rest[1], encoding="utf-8", errors="replace").read())
        before, after, n = neighbours(log, symbols, thread_name, target)
        for title, table in (("frame before", before), ("frame after", after)):
            print(f"{title} {target} in {thread_name}:")
            for fn, share in sorted(table.items(), key=lambda kv: -kv[1])[:25]:
                print(f"  {share / n:5.1f}%  {fn}")
        return
    top = 15
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
        args = [a for a in args if a != str(top)]
    log = open(args[0], encoding="utf-8", errors="replace").read()
    symbols = load_symbols(open(args[1], encoding="utf-8", errors="replace").read())
    leaf, incl, intervals, cpu = analyze(log, symbols)
    for thread in sorted(intervals, key=lambda t: -cpu[t]["cpu"] / intervals[t]):
        n = intervals[thread]
        if not leaf[thread] and not incl[thread]:
            continue
        print(f"== {thread}: average CPU {cpu[thread]['cpu'] / n:.1f}% over {n} intervals")
        for title, table in (("leaf", leaf[thread]), ("inclusive (on stack)", incl[thread])):
            print(f"  {title}:")
            for fn, share in sorted(table.items(), key=lambda kv: -kv[1])[:top]:
                print(f"    {share / n:5.1f}%  {fn}")


if __name__ == "__main__":
    main()