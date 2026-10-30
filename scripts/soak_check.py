"""Check soak-test logs for the signs of a hang, a crash or a leak.

usage: soak_check.py LOG_FOLDER [LOG_FOLDER ...]      (each folder holds nfscarbon_*.log and rex/rex_perfil.log)

Reports per folder:
  - the build line ([build] ...) so you know which NRO produced the log,
  - wall-clock span and active play time (stats windows),
  - silent gaps longer than 30 s: a gap with NO lines at all means the process was suspended (HOME menu / sleep) or frozen
    hard - tell me which; a gap WITH lines but 0 fps is a stall,
  - stalls: 2 or more windows in a row with 0 fps while the scene was loaded,
  - crash / exception lines,
  - process memory growth (malloc in use, first vs last) when the full-logging config was used,
  - audio windows with many missing-data requests.
"""
import glob
import os
import re
import statistics as st
import sys
from datetime import datetime


def ts(line):
    m = re.match(r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]", line)
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f") if m else None


def check(folder):
    print(f"\n=== {folder}")
    lines = []
    for path in sorted(glob.glob(os.path.join(folder, "nfscarbon_*.log"))):
        with open(path, errors="replace") as f:
            lines.extend((ts(l), l) for l in f if ts(l))
    if not lines:
        print("  no log lines found")
        return False
    ok = True
    first, last = lines[0][0], lines[-1][0]
    builds = sorted({re.search(r"\[build\] (.*)$", l).group(1).strip() for _, l in lines if "[build]" in l})
    print(f"  build: {', '.join(builds) if builds else 'no [build] line (older build)'}")
    print(f"  span {first:%H:%M:%S} -> {last:%H:%M:%S} = {(last - first).total_seconds() / 60:.1f} min wall clock")
    gaps = [(a[0], b[0]) for a, b in zip(lines, lines[1:]) if (b[0] - a[0]).total_seconds() > 30]
    for a, b in gaps:
        print(f"  SILENT GAP {a:%H:%M:%S} -> {b:%H:%M:%S} ({(b - a).total_seconds() / 60:.1f} min): was the console asleep / in HOME? if not, this is a freeze")
    play = []
    for t, l in lines:
        m = re.search(r"([\d.]+) fps \| per frame: draws (\d+)", l)
        if m:
            play.append((t, float(m.group(1)), int(m.group(2))))
    active = [p for p in play if p[2] > 300]
    print(f"  active play: {len(active) * 5 / 60:.1f} min ({len(active)} windows)")
    zero_run, worst_run = 0, 0
    for i, (t, fps, draws) in enumerate(play):
        gap_before = i > 0 and (t - play[i - 1][0]).total_seconds() > 30
        zero_run = zero_run + 1 if (fps == 0.0 and draws > 300 and not gap_before) else 0
        worst_run = max(worst_run, zero_run)
    if worst_run >= 2:
        ok = False
        print(f"  STALL: {worst_run} 0-fps windows in a row with a loaded scene")
    bad = [l for _, l in lines if re.search(r"crash|fatal|exception|abort|unhandled|terminate", l, re.I)]
    if bad:
        ok = False
        print(f"  CRASH-LIKE LINES: {len(bad)}; first: {bad[0].strip()[:160]}")
    mem = [(t, int(m.group(1))) for t, l in lines for m in [re.search(r"malloc: en uso (\d+) MB", l)] if m]
    if len(mem) >= 4:
        print(f"  malloc in use: {mem[0][1]} MB -> {mem[-1][1]} MB (max {max(v for _, v in mem)} MB)")
    perf = os.path.join(folder, "rex", "rex_perfil.log")
    if os.path.exists(perf):
        text = open(perf, errors="replace").read()
        nd = [int(x) for x in re.findall(r"(\d+) solicitudes sin datos", text)]
        if nd:
            print(f"  audio: {sum(1 for v in nd if v > 50)} of {len(nd)} windows with more than 50 missing-data requests (max {max(nd)})")
    fps = [p[1] for p in active if p[1] > 0]
    if fps:
        fps.sort()
        print(f"  FPS median {st.median(fps):.1f}, p10 {fps[len(fps) // 10]:.1f}, min {fps[0]:.1f}")
    print("  RESULT:", "no hang/crash signs" if ok else "LOOK AT THIS RUN")
    return ok


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    results = [check(f) for f in sys.argv[1:]]
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()