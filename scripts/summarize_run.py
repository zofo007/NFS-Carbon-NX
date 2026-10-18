"""Summarize one play session from the game log and rex_perfil.log.

usage: summarize_run.py <logs folder> [--from HH:MM] [--to HH:MM]

Reads (read-only) nfscarbon_NNN.log files and rex/rex_perfil.log in the folder and prints: duration, clocks,
FPS spread of the busy windows, draws per frame, GPU time, audio drop-outs per 10 s and process memory. Meant for
bug reports and for the performance notes in the README; it does not need the game.
"""
import argparse
import glob
import os
import re
import statistics


def pct(values, p):
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int(p * len(values)))]


def parse_game_logs(folder, t0, t1):
    rows = []
    cur = None
    for path in sorted(glob.glob(os.path.join(folder, "nfscarbon_*.log"))):
        with open(path, errors="replace") as f:
            for line in f:
                m = re.search(r"\d{4}-\d\d-\d\d (\d\d:\d\d):\d\d\.\d+\].*\] ([\d.]+) fps \| per frame: draws (\d+) ", line)
                if m:
                    hm = m.group(1)
                    if (t0 and hm < t0) or (t1 and hm > t1):
                        cur = None
                        continue
                    cur = {"t": hm, "fps": float(m.group(2)), "draws": int(m.group(3)), "gpu": None}
                    rows.append(cur)
                    continue
                m = re.search(r"gpu time per frame: total ([\d.]+) ms", line)
                if m and cur is not None:
                    cur["gpu"] = float(m.group(1))
    return rows


def parse_perfil(folder):
    path = os.path.join(folder, "rex", "rex_perfil.log")
    out = []
    if not os.path.exists(path):
        return out
    text = open(path, errors="replace").read()
    for w in text.split("==== ")[1:]:
        m = re.match(r"([\d.]+) s \| modo: (\w+) \| juego ([\d.]+) fps \| CPU total (\d+)%", w)
        if not m or int(m.group(4)) < 5:
            continue
        clk = re.search(r"relojes: CPU ([\d.]+) MHz, GPU ([\d.]+) MHz, memoria ([\d.]+) MHz", w)
        nd = re.search(r"(\d+) solicitudes sin datos", w)
        mixed = re.search(r"(\d+) bloques de cliente mezclados", w)
        mem = re.search(r"proceso (\d+)/(\d+) MB", w)
        out.append({
            "fps": float(m.group(3)), "cpu": int(m.group(4)),
            "clock": tuple(float(x) for x in clk.groups()) if clk else None,
            "nodata": int(nd.group(1)) if nd else 0, "mixed": int(mixed.group(1)) if mixed else 0,
            "mem": tuple(int(x) for x in mem.groups()) if mem else None,
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--from", dest="t0")
    ap.add_argument("--to", dest="t1")
    args = ap.parse_args()
    rows = parse_game_logs(args.folder, args.t0, args.t1)
    play = [r for r in rows if r["draws"] > 300]
    print(f"stats windows: {len(rows)} ({len(rows) * 5 / 60:.1f} min), in-game windows (>300 draws): {len(play)}")
    if play:
        fps = [r["fps"] for r in play]
        draws = [r["draws"] for r in play]
        gpu = [r["gpu"] for r in play if r["gpu"]]
        print(f"FPS   median {statistics.median(fps):.1f}  p10 {pct(fps, .10):.1f}  p90 {pct(fps, .90):.1f}  min {min(fps):.1f}  max {max(fps):.1f}")
        print(f"draws median {statistics.median(draws):.0f}  p90 {pct(draws, .9):.0f}  max {max(draws)}")
        if gpu:
            print(f"GPU ms/frame median {statistics.median(gpu):.1f}  p90 {pct(gpu, .9):.1f}")
        low = sum(1 for v in fps if v < 25) / len(fps)
        high = sum(1 for v in fps if v >= 30) / len(fps)
        print(f"windows under 25 FPS: {low:.0%}   at or above 30 FPS: {high:.0%}")
    perfil = parse_perfil(args.folder)
    race = [p for p in perfil if p["cpu"] >= 150]
    if perfil:
        clocks = sorted({p["clock"] for p in perfil if p["clock"]})
        print("clocks seen (CPU/GPU/RAM MHz):", ", ".join("/".join(f"{c:g}" for c in cl) for cl in clocks))
        mem = [p["mem"] for p in perfil if p["mem"]]
        if mem:
            print(f"process memory: {max(m[0] for m in mem)} of {mem[-1][1]} MB at most")
        print(f"busy windows (process CPU >= 150% of 300%): {len(race)} of {len(perfil)}")
        if race:
            nd = [p["nodata"] for p in race]
            bad = sum(1 for v in nd if v > 50)
            print(f"audio requests without data per 10 s in busy windows: median {statistics.median(nd):.0f}  max {max(nd)}  "
                  f"windows over 50: {bad} of {len(race)}")
            print(f"CPU in busy windows: median {statistics.median(p['cpu'] for p in race):.0f}%")


if __name__ == "__main__":
    main()
