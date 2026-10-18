"""Summarize sampled CPU draw stages without modifying hardware logs."""
import argparse
from datetime import datetime
from pathlib import Path
import re
from statistics import median

STAGES = ("setup+targets", "pipeline", "prepare", "textures", "indices", "vertices", "constants", "record")
HEADER = re.compile(r"^\[([^]]+)\].*cpu draw sampled 1/64: (\d+)/(\d+) requests \| estimated ms/frame (.+)$")


def parse_profiles(text):
    reports = []
    previous = None
    for line in text.splitlines():
        match = HEADER.match(line)
        if not match:
            continue
        stamp = datetime.fromisoformat(match[1])
        samples, requests = int(match[2]), int(match[3])
        fields = match[4].split(", ")
        values = {}
        for field in fields:
            name, value = field.rsplit(" ", 1)
            if name in values:
                raise ValueError("Duplicate draw stage")
            values[name] = float(value)
        if tuple(values) != STAGES or not 0 < samples <= requests:
            raise ValueError("Invalid draw-stage/counter contract")
        if any(not 0 <= value < float("inf") for value in values.values()):
            raise ValueError("Invalid draw-stage duration")
        if previous is not None and stamp < previous:
            raise ValueError("Draw-profile timestamps are not chronological")
        reports.append({"time": stamp, "samples": samples, "requests": requests,
                        "stages": values, "long_gap": previous is not None and (stamp - previous).total_seconds() > 30})
        previous = stamp
    return reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_log", type=Path)
    args = parser.parse_args()
    reports = parse_profiles(args.game_log.read_text(encoding="utf-8-sig"))
    ordinary = [report for report in reports if not report["long_gap"]]
    if not ordinary:
        parser.error("No sampled draw profiles found; use logs from the draw-profile NRO with carbon_gpu_stats enabled")
    print(f"Reports: {len(reports)}; excluded long-gap reports: {len(reports) - len(ordinary)}")
    print("Median estimated wall ms/frame across ordinary reports (not pure CPU time or a racing benchmark):")
    costs = {name: median(report["stages"][name] for report in ordinary) for name in STAGES}
    for name in sorted(costs, key=costs.get, reverse=True):
        print(f"  {name}: {costs[name]:.2f}")
    print("Sampling may miss rare stalls; check full draw/frame timing and scene notes. Long gaps may be real stalls, not only suspend.")


if __name__ == "__main__":
    main()
