"""Score a paired speed test: A1 B1 A2 B2 (A = baseline, B = new build), each a folder with the game's nfscarbon_*.log files.

usage: ab_report.py A1 B1 A2 B2        (or just: A1 B1 for a single pair)

Everything is compared at equal draws per frame, because the scene decides most of the frame rate. For each pair it prints,
per draw-count bucket, the median FPS and the median game render-thread ms/frame, then a verdict:
  pair PASS = weighted gain of 3 percent or more in median FPS or in render-thread ms/frame, and no bucket with 8 or more
              windows more than 5 percent worse in FPS;  the scenes must be alike (median draws within 10 percent).
Needs full logging (nfscarbon.report.toml): the [render-pass] and "N fps | per frame: draws" lines.
"""
import glob
import os
import re
import statistics as st
import sys
from datetime import datetime

BUCKETS = ((300, 1000), (1000, 1500), (1500, 2000), (2000, 2500), (2500, 99999))
MIN_WINDOWS = 8


def ts(line):
    m = re.match(r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]", line)
    return datetime.strptime(m[1], "%Y-%m-%d %H:%M:%S.%f").timestamp() if m else None


def load(folder):
    stats, passes, cur = [], [], None
    for path in sorted(glob.glob(os.path.join(folder, "nfscarbon_*.log"))):
        for line in open(path, errors="replace"):
            m = re.search(r"([\d.]+) fps \| per frame: draws (\d+) ", line)
            if m:
                cur = {"t": ts(line), "fps": float(m[1]), "draws": int(m[2])}
                if cur["draws"] > 300 and cur["fps"] > 0:
                    stats.append(cur)
                continue
            m = re.search(r"\[render-pass\] game render thread ms/frame over (\d+) frames: .*\| frame ([\d.]+)", line)
            if m and int(m[1]) > 100:
                passes.append({"t": ts(line), "ms": float(m[2])})
    for p in passes:
        near = [s["draws"] for s in stats if 0 <= p["t"] - s["t"] <= 10]
        p["draws"] = st.mean(near) if near else None
    return stats, [p for p in passes if p["draws"]]


def bucket_of(d):
    for lo, hi in BUCKETS:
        if lo <= d < hi:
            return (lo, hi)


def label(b):
    return f"{b[0]}-{b[1] if b[1] < 99999 else '+'}"


def compare(a_folder, b_folder, name):
    (sa, pa), (sb, pb) = load(a_folder), load(b_folder)
    if not sa or not sb:
        print(f"{name}: no usable windows (a log folder is empty or lacks full logging)")
        return False
    da, db = st.median(s["draws"] for s in sa), st.median(s["draws"] for s in sb)
    print(f"\n== {name}: A {len(sa)} windows, median {st.median(s['fps'] for s in sa):.1f} FPS, {da:.0f} draws | "
          f"B {len(sb)} windows, median {st.median(s['fps'] for s in sb):.1f} FPS, {db:.0f} draws")
    print(f"   {'draws':10s} {'windows A/B':>12s} {'FPS A':>7s} {'FPS B':>7s} {'gain':>7s} | {'render ms A':>11s} {'B':>6s} {'gain':>7s}")
    fps_gain = ms_gain = weight = 0.0
    worst = 0.0
    for b in BUCKETS:
        wa = [s["fps"] for s in sa if bucket_of(s["draws"]) == b]
        wb = [s["fps"] for s in sb if bucket_of(s["draws"]) == b]
        ra = [p["ms"] for p in pa if bucket_of(p["draws"]) == b]
        rb = [p["ms"] for p in pb if bucket_of(p["draws"]) == b]
        if not wa or not wb:
            continue
        fa, fb = st.median(wa), st.median(wb)
        g = 100.0 * (fb - fa) / fa
        line = f"   {label(b):10s} {len(wa):5d}/{len(wb):<5d} {fa:7.1f} {fb:7.1f} {g:+6.1f}% |"
        if ra and rb:
            ma, mb = st.median(ra), st.median(rb)
            mg = 100.0 * (ma - mb) / ma
            line += f" {ma:11.2f} {mb:6.2f} {mg:+6.1f}%"
        else:
            mg = None
            line += "           -      -       -"
        print(line)
        if min(len(wa), len(wb)) >= MIN_WINDOWS:
            w = min(len(wa), len(wb))
            fps_gain += g * w
            weight += w
            if mg is not None:
                ms_gain += mg * w
            worst = min(worst, g)
    if not weight:
        print("   not enough windows per bucket to judge")
        return False
    fps_gain, ms_gain = fps_gain / weight, ms_gain / weight
    alike = abs(db - da) / da <= 0.10
    ok = (fps_gain >= 3.0 or ms_gain >= 3.0) and worst > -5.0 and alike
    print(f"   weighted gain: FPS {fps_gain:+.1f}%, render-thread ms {ms_gain:+.1f}%; worst bucket FPS {worst:+.1f}%; "
          f"scenes alike (draws within 10%): {'yes' if alike else 'NO'}  ->  {'PASS' if ok else 'NOT PROVEN'}")
    return ok


def main():
    folders = sys.argv[1:]
    if len(folders) not in (2, 4):
        raise SystemExit(__doc__)
    results = [compare(folders[0], folders[1], "pair 1 (A1 vs B1)")]
    if len(folders) == 4:
        results.append(compare(folders[2], folders[3], "pair 2 (A2 vs B2)"))
    print("\nVERDICT:", "PASS in every pair" if all(results) else "NOT PROVEN (see the pairs above)")


if __name__ == "__main__":
    main()