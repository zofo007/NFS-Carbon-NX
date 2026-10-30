"""Turn hotspot-mode rex_perfil.log files into a linker section-ordering file (hot functions first).

usage: hotspot_order.py --nm nm-output.txt --out order.txt [--coverage 0.95] [--max 6000] LOG [LOG...]

Each function's weight is its share of the on-CPU leaf samples of a thread, times that thread's average CPU use,
summed over threads and logs. Idle leaves (svc* waits, sleeps) are dropped. The output lists the sections of the
hottest functions until `coverage` of the total weight (or `max` functions) is reached, hottest first:
    .text.__imp__sub_82586908     (guest function body)
    .text.sub_82586908            (the weak entry / hook wrapper, harmless if absent)
    .text._ZN6carbon3gpu...       (host code: its section carries the mangled name)
GNU ld takes it with --section-ordering-file (file format: ".text : { *(.text.a) *(.text.b) }"). Functions are in their own sections because of -ffunction-sections.
"""
import argparse
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_hotspots as ah  # noqa: E402

IDLE_PREFIXES = ("svc", "__syscall", "_ZN3rex6thread5Sleep", "_ZN3rex6thread4Wait")


def is_idle(name):
    return name.startswith(IDLE_PREFIXES) or name in ("__thread_entry", "_EntryWrap", "xstart")


def sections_for(name):
    if name.startswith("sub_"):
        return [f".text.__imp__{name}", f".text.{name}"]
    return [f".text.{name}"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--coverage", type=float, default=0.95)
    ap.add_argument("--max", type=int, default=6000)
    ap.add_argument("logs", nargs="+")
    a = ap.parse_args()

    symbols = ah.load_symbols(open(a.nm, encoding="utf-8", errors="replace").read())
    addrs, names = symbols
    # size of a function = distance to the next symbol (includes padding; good enough for a footprint figure)
    size_of = {}
    for i, n in enumerate(names):
        if i + 1 < len(addrs):
            size_of[n] = max(size_of.get(n, 0), addrs[i + 1] - addrs[i])

    weight = defaultdict(float)
    per_thread = defaultdict(lambda: defaultdict(float))
    for path in a.logs:
        leaf, _incl, intervals, cpu = ah.analyze(open(path, encoding="utf-8", errors="replace").read(), symbols)
        for thread, table in leaf.items():
            n = max(intervals[thread], 1)
            thread_cpu = cpu[thread]["cpu"] / n / 100.0
            busy = {fn: s / n for fn, s in table.items() if not is_idle(fn)}
            total = sum(busy.values())
            if total <= 0:
                continue
            for fn, s in busy.items():
                w = s / total * thread_cpu
                weight[fn] += w
                per_thread[thread][fn] += w

    ranked = sorted(weight.items(), key=lambda kv: -kv[1])
    total = sum(weight.values())
    print(f"{len(ranked)} distinct on-CPU leaf functions, total weight {total:.2f} cores")
    cum, byte_sum = 0.0, 0
    marks = [0.5, 0.8, 0.9, 0.95, 0.99]
    seen_marks = {}
    for i, (fn, w) in enumerate(ranked, 1):
        cum += w
        byte_sum += size_of.get(fn, 0)
        for m in marks:
            if m not in seen_marks and cum / total >= m:
                seen_marks[m] = (i, byte_sum)
    for m in marks:
        if m in seen_marks:
            i, b = seen_marks[m]
            print(f"  {int(m * 100):3d}% of on-CPU time: {i:5d} functions, about {b / 1024:.0f} KiB of code")
    for thread in sorted(per_thread, key=lambda t: -sum(per_thread[t].values()))[:6]:
        tw = sum(per_thread[thread].values())
        top = sorted(per_thread[thread].items(), key=lambda kv: -kv[1])[:3]
        print(f"  {thread[:28]:28s} {tw:.2f} cores; top: " + ", ".join(f"{f} {w / tw * 100:.0f}%" for f, w in top))

    out, cum, count = [], 0.0, 0
    for fn, w in ranked:
        if count >= a.max or cum / total >= a.coverage:
            break
        out.extend(sections_for(fn))
        cum += w
        count += 1
    with open(a.out, "w", encoding="ascii", errors="replace", newline="\n") as f:
        # GNU ld reads the file as output-section statements: the listed input sections are placed first, in order.
        # .crt0 (the NRO header and entry branch) must stay at offset 0, so it is named first.
        f.write(".text : {\n  KEEP(*(.crt0))\n" + "".join(f"  *({s})\n" for s in out) + "}\n")
    print(f"wrote {count} functions ({len(out)} section lines), {cum / total * 100:.1f}% of weight, to {a.out}")


if __name__ == "__main__":
    main()