"""Summarize per-frame CP pacing samples without modifying hardware logs.

Reads `frame pacing v1` reports written by Renderer::Swap under carbon_gpu_stats.
"Present" is the command thread's swap completion, not the display flip.
"""
import argparse
from datetime import datetime, timedelta
import math
from pathlib import Path
import re
from statistics import median

VERSION = 1
CAPACITY = 1024
PER_LINE = 128
VBLANK_MS = 1000.0 / 60.0
BUDGET_MS = 1000.0 / 30.0
GRID_TOLERANCE_MS = 1.5
VBLANK_BINS = 6
SUSPEND_MS = 10_000.0
FIELDS = ("interval", "draw", "resolve", "wait", "idle", "swap")
PREFIX = r"^\[([^]]+)\].*\[carbon-gpu\] "
SUMMARY = re.compile(
    PREFIX + r"frame pacing v(\d+) window (\d+): frames (\d+) stored (\d+) t0 (\d+\.\d+) end (\d+\.\d+) "
    r"\| present interval ms p50/p95/p99/worst (\d+\.\d)/(\d+\.\d)/(\d+\.\d)/(\d+\.\d) mean (\d+\.\d+) "
    r"\| over 33\.3 ms (\d+) \((\d+\.\d)%\) \| vblanks 1:(\d+) 2:(\d+) 3:(\d+) 4:(\d+) 5:(\d+) 6\+:(\d+) "
    r"on-grid (\d+)$")
SAMPLES = re.compile(PREFIX + r"frame pacing samples v(\d+) window (\d+) part (\d+)/(\d+):((?: \d+/\d+/\d+/\d+/\d+/\d+)*)$")
ANY = re.compile(PREFIX + r"frame pacing (?:samples )?v(\d+) ")


def vblank_count(ms):
    """Nearest multiple of 16.667 ms, halves up, at least one (matches frame_pacing.h)."""
    return max(1, math.floor(ms / VBLANK_MS + 0.5))


def nearest_rank(sorted_values, permille):
    if not sorted_values:
        return 0.0
    return sorted_values[max(1, (permille * len(sorted_values) + 999) // 1000) - 1]


def _check_window(window, final):
    """Validate one window; returns False only for a truncated final window."""
    stored = window["stored"]
    expected_parts = (stored + PER_LINE - 1) // PER_LINE
    complete = (window["parts"] == list(range(1, expected_parts + 1)) and len(window["frames"]) == stored
                and window["part_total"] in (0, expected_parts) and bool(window["parts"]) == bool(stored))
    if not complete:
        prefix = window["parts"] == list(range(1, len(window["parts"]) + 1))
        if final and prefix and len(window["frames"]) <= stored:
            return False
        raise ValueError(f"Window {window['id']} samples are incomplete, out of order or miscounted")
    if stored == window["count"] and stored:
        intervals = sorted(frame["interval"] for frame in window["frames"])
        for permille, reported in zip((500, 950, 990), window["reported"]):
            if abs(nearest_rank(intervals, permille) - reported) > 0.1 + 1e-9:
                raise ValueError(f"Window {window['id']} percentile disagrees with its samples")
        if abs(intervals[-1] - window["worst"]) > 0.1 + 1e-9:
            raise ValueError(f"Window {window['id']} worst frame disagrees with its samples")
        rebuilt = window["t0"] + sum(frame["interval"] for frame in window["frames"]) / 1000.0
        if abs(rebuilt - window["end"]) > 0.00005 * stored + 0.0002:
            raise ValueError(f"Window {window['id']} present timestamps do not add up")
    return True


def parse_windows(text, source="log"):
    """Return validated windows; an incomplete final window (log truncation) is dropped."""
    windows = []
    current = None
    previous_time = None
    lines = text.splitlines()
    while lines and not lines[-1].strip():
        lines.pop()
    for index, line in enumerate(lines):
        if "frame pacing" not in line:
            continue
        version = ANY.match(line)
        if version and int(version[2]) != VERSION:
            raise ValueError(f"Unsupported frame pacing version {version[2]}")
        summary = SUMMARY.match(line)
        if summary:
            if current is not None:
                _check_window(current, False)
                windows.append(current)
            stamp = datetime.fromisoformat(summary[1])
            if previous_time is not None and stamp < previous_time:
                raise ValueError("Frame pacing timestamps are not chronological")
            previous_time = stamp
            window_id = int(summary[3])
            if windows and window_id <= windows[-1]["id"]:
                raise ValueError("Frame pacing window identifiers are not increasing")
            count, stored, over, on_grid = int(summary[4]), int(summary[5]), int(summary[13]), int(summary[21])
            bins = [int(summary[i]) for i in range(15, 21)]
            if stored > min(count, CAPACITY) or over > count or on_grid > count or sum(bins) != count:
                raise ValueError(f"Invalid frame pacing summary counters in window {window_id}")
            t0, end = float(summary[6]), float(summary[7])
            if count and end < t0:
                raise ValueError(f"Window {window_id} ends before it starts")
            current = {"source": source, "time": stamp, "id": window_id, "count": count, "stored": stored,
                       "t0": t0, "end": end, "over": over, "on_grid": on_grid, "bins": bins,
                       "reported": tuple(float(summary[i]) for i in (8, 9, 10)), "worst": float(summary[11]),
                       "parts": [], "part_total": 0, "frames": []}
            continue
        samples = SAMPLES.match(line)
        if samples:
            if current is None or int(samples[3]) != current["id"]:
                raise ValueError("Frame pacing samples without their window summary")
            part, total = int(samples[4]), int(samples[5])
            if not 1 <= part <= total or (current["part_total"] and total != current["part_total"]):
                raise ValueError(f"Invalid sample part {part}/{total} in window {current['id']}")
            current["part_total"] = total
            current["parts"].append(part)
            for token in samples[6].split():
                values = [int(value) / 10.0 for value in token.split("/")]
                current["frames"].append(dict(zip(FIELDS, values)))
            continue
        if not version:
            continue
        if index == len(lines) - 1:
            break  # the game log was cut off mid-line; the open window is checked as truncated
        raise ValueError(f"Malformed frame pacing line: {line[:160]}")
    if current is not None and _check_window(current, True):
        windows.append(current)
    return windows


def frames_of(windows):
    """Flatten stored samples with reconstructed present and wall-clock times."""
    rows = []
    for window in windows:
        present = window["t0"]
        for frame in window["frames"]:
            present += frame["interval"] / 1000.0
            row = dict(frame)
            row["present_s"] = present
            row["wall"] = window["time"] - timedelta(seconds=window["end"] - present)
            row["vblanks"] = vblank_count(frame["interval"])
            row["other"] = frame["interval"] - sum(frame[name] for name in FIELDS[1:])
            row["source"] = window["source"]
            rows.append(row)
    return rows


def _clock(value):
    return datetime.strptime(value, "%H:%M:%S").time() if len(value) <= 8 else datetime.fromisoformat(value)


def _in_range(stamp, start, stop):
    for bound, keep in ((start, lambda a, b: a >= b), (stop, lambda a, b: a <= b)):
        if bound is not None and not keep(stamp if isinstance(bound, datetime) else stamp.time(), bound):
            return False
    return True


def _breakdown(rows):
    names = FIELDS + ("other",)
    return " / ".join(f"{median(row[name] for row in rows):.1f}" for name in names)


def summarize(rows, windows, worst=10):
    lines = []
    count = sum(window["count"] for window in windows)
    lines.append(f"Windows: {len(windows)}; frames {count}; stored samples {len(rows)}"
                 f" (not stored after the {CAPACITY}-frame window cap: {count - len(rows)})")
    intervals = sorted(row["interval"] for row in rows)
    total_ms = sum(intervals)
    lines.append("Present interval = CP swap completion to completion, pooled over stored frames, nearest rank:")
    lines.append(f"  P50/P95/P99/worst {nearest_rank(intervals, 500):.1f}/{nearest_rank(intervals, 950):.1f}/"
                 f"{nearest_rank(intervals, 990):.1f}/{intervals[-1]:.1f} ms; mean {total_ms / len(rows):.2f} ms "
                 f"({1000.0 * len(rows) / total_ms:.1f} FPS)")
    over = sum(value > BUDGET_MS for value in intervals)
    missed = sum(row["vblanks"] >= 3 for row in rows)
    lines.append(f"  Over 33.3 ms: {over} ({100.0 * over / len(rows):.1f}%); missed the 2-vblank 30 FPS slot "
                 f"(>=3 vblanks): {missed} ({100.0 * missed / len(rows):.1f}%)")
    on_grid = sum(abs(row["interval"] - row["vblanks"] * VBLANK_MS) <= GRID_TOLERANCE_MS for row in rows)
    bins = [0] * VBLANK_BINS
    for row in rows:
        bins[min(row["vblanks"], VBLANK_BINS) - 1] += 1
    lines.append(f"16.7 ms multiples (nearest vblank count; within +/-{GRID_TOLERANCE_MS} ms: "
                 f"{100.0 * on_grid / len(rows):.1f}% of frames):")
    lines.append("  " + "  ".join(f"{i + 1}{'+' if i + 1 == VBLANK_BINS else ''}: {n} ({100.0 * n / len(rows):.1f}%)"
                                  for i, n in enumerate(bins)))
    changes = sum(a["vblanks"] != b["vblanks"] for a, b in zip(rows, rows[1:]))
    run = longest = 0
    for row in rows:
        run = run + 1 if row["vblanks"] >= 3 else 0
        longest = max(longest, run)
    lines.append(f"Pacing: vblank count changes between consecutive frames {changes} "
                 f"({100.0 * changes / max(len(rows) - 1, 1):.1f}%); longest run of >=3-vblank frames {longest}")
    lines.append("CP time per frame, median ms interval / draw / resolve / wait / idle / swap / other:")
    for label, subset in (("<=2 vblanks", [row for row in rows if row["vblanks"] <= 2]),
                          (">=3 vblanks", [row for row in rows if row["vblanks"] >= 3])):
        lines.append(f"  {label} ({len(subset)} frames): {_breakdown(subset) if subset else 'none'}")
    suspend = [row for row in rows if row["interval"] >= SUSPEND_MS]
    if suspend:
        lines.append(f"Intervals >= {SUSPEND_MS / 1000:.0f} s (possible HOME/suspend, NOT excluded): {len(suspend)}")
    lines.append(f"Worst {min(worst, len(rows))} frames (wall time rebuilt from the report timestamp):")
    for row in sorted(rows, key=lambda r: r["interval"], reverse=True)[:worst]:
        lines.append(f"  {row['wall']:%H:%M:%S.%f}"[:-3] + f" {row['interval']:.1f} ms: draw {row['draw']:.1f}, "
                     f"resolve {row['resolve']:.1f}, wait {row['wait']:.1f}, idle {row['idle']:.1f}, "
                     f"swap {row['swap']:.1f}, other {row['other']:.1f}")
    lines.append("Timers are wall time on the CP thread and include preemption; 'other' is PM4/register/"
                 "packet work outside the timed scopes. Not a display-flip or GPU measurement.")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_logs", type=Path, nargs="+")
    parser.add_argument("--from", dest="start", type=_clock, help="first report time (HH:MM:SS or ISO)")
    parser.add_argument("--to", dest="stop", type=_clock, help="last report time (HH:MM:SS or ISO)")
    parser.add_argument("--worst", type=int, default=10)
    args = parser.parse_args()
    windows = []
    for path in args.game_logs:
        windows += parse_windows(path.read_text(encoding="utf-8-sig", errors="replace"), path.name)
    if not windows:
        parser.error("No frame pacing v1 reports found; use logs from a frame-pacing NRO with carbon_gpu_stats enabled")
    windows = [window for window in windows if _in_range(window["time"], args.start, args.stop)]
    rows = frames_of(windows)
    if not rows:
        parser.error("No stored frame pacing samples in the selected time range")
    print("\n".join(summarize(rows, windows, args.worst)))


if __name__ == "__main__":
    main()
