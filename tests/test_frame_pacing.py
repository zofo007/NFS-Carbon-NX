import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("frame_pacing_analysis", ROOT / "scripts/analyze_frame_pacing.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def window(window_id=0, frames=((500, 300, 9, 103, 18, 24),) * 3, time="12:00:05.000", t0=100.0,
           count=None, version=1, overrides=None, per_line=analysis.PER_LINE):
    """Mirror frame_pacing.h formatting (0.1 ms integer sample fields)."""
    prefix = f"[2026-10-15 {time}] [info] [gpu] [t1] [carbon-gpu] "
    intervals = sorted(frame[0] / 10.0 for frame in frames)
    count = len(frames) if count is None else count
    bins = [0] * 6
    for frame in frames:
        bins[min(analysis.vblank_count(frame[0] / 10.0), 6) - 1] += 1
    fields = {"p50": analysis.nearest_rank(intervals, 500), "p95": analysis.nearest_rank(intervals, 950),
              "p99": analysis.nearest_rank(intervals, 990), "worst": intervals[-1] if intervals else 0.0,
              "over": sum(value > analysis.BUDGET_MS for value in intervals), "bins": bins,
              "end": t0 + sum(frame[0] for frame in frames) / 10000.0, "stored": len(frames), "count": count,
              "on_grid": sum(abs(v - analysis.vblank_count(v) * analysis.VBLANK_MS) <= 1.5 for v in intervals)}
    fields.update(overrides or {})
    b = fields["bins"]
    lines = [prefix + f"frame pacing v{version} window {window_id}: frames {fields['count']} stored {fields['stored']} "
             f"t0 {t0:.4f} end {fields['end']:.4f} | present interval ms p50/p95/p99/worst "
             f"{fields['p50']:.1f}/{fields['p95']:.1f}/{fields['p99']:.1f}/{fields['worst']:.1f} mean 50.00 | "
             f"over 33.3 ms {fields['over']} ({100.0 * fields['over'] / max(fields['count'], 1):.1f}%) | "
             f"vblanks 1:{b[0]} 2:{b[1]} 3:{b[2]} 4:{b[3]} 5:{b[4]} 6+:{b[5]} on-grid {fields['on_grid']}"]
    parts = [frames[i:i + per_line] for i in range(0, len(frames), per_line)]
    for index, part in enumerate(parts):
        lines.append(prefix + f"frame pacing samples v{version} window {window_id} part {index + 1}/{len(parts)}:"
                     + "".join(" " + "/".join(str(v) for v in frame) for frame in part))
    return "\n".join(lines)


SLOW = (500, 330, 9, 80, 5, 24)
FAST = (333, 200, 9, 80, 5, 24)


class FramePacingTests(unittest.TestCase):
    def test_cli_summary_and_legacy_log_rejection(self):
        text = "\n".join((window(0, (FAST, SLOW, SLOW, (667, 400, 9, 150, 5, 24))),
                          window(1, (FAST,) * 4, "12:00:10.000", t0=100.2034)))
        output = io.StringIO()
        with patch("sys.argv", ["analyze_frame_pacing.py", "synthetic.log"]), \
                patch.object(Path, "read_text", return_value=text), contextlib.redirect_stdout(output):
            analysis.main()
        report = output.getvalue()
        self.assertIn("Windows: 2; frames 8; stored samples 8", report)
        self.assertIn("P50/P95/P99/worst 33.3/66.7/66.7/66.7 ms", report)
        self.assertIn("Over 33.3 ms: 3 (37.5%)", report)
        self.assertIn("(>=3 vblanks): 3 (37.5%)", report)
        self.assertIn("1: 0 (0.0%)  2: 5 (62.5%)  3: 2 (25.0%)  4: 1 (12.5%)", report)
        self.assertIn(">=3 vblanks (3 frames): 50.0 / 33.0 / 0.9 / 8.0 / 0.5 / 2.4 / 5.2", report)
        for legacy in ("old build", "[2026-10-15 12:00:05.000] [carbon-gpu] pacing(dbg mode 0): avg 52.1 ms"):
            with patch("sys.argv", ["analyze_frame_pacing.py", "legacy.log"]), \
                    patch.object(Path, "read_text", return_value=legacy), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                analysis.main()
            self.assertEqual(error.exception.code, 2)

    def test_reconstruction_and_time_filter(self):
        windows = analysis.parse_windows(window(0, (FAST, SLOW)) + "\n" + window(1, (SLOW,), "12:00:10.000", 100.0833))
        rows = analysis.frames_of(windows)
        self.assertEqual([row["vblanks"] for row in rows], [2, 3, 3])
        self.assertAlmostEqual(rows[1]["present_s"], 100.0833)
        self.assertEqual(rows[1]["wall"].strftime("%H:%M:%S.%f"), "12:00:05.000000")
        self.assertAlmostEqual(rows[0]["other"], 33.3 - 31.8)
        kept = [w for w in windows if analysis._in_range(w["time"], analysis._clock("12:00:06"), None)]
        self.assertEqual([w["id"] for w in kept], [1])
        self.assertEqual(analysis.vblank_count(0.0), 1)
        self.assertEqual(analysis.vblank_count(41.6), 2)
        self.assertEqual(analysis.vblank_count(41.7), 3)

    def test_multi_part_capacity_and_truncation(self):
        many = tuple((500 if i % 3 else 333, 300, 9, 80, 5, 24) for i in range(300))
        rows = analysis.frames_of(analysis.parse_windows(window(0, many)))
        self.assertEqual(len(rows), 300)
        capped = window(0, many, count=1500, overrides={"worst": 1808.3, "bins": [0, 100, 1400, 0, 0, 0]})
        self.assertEqual(analysis.parse_windows(capped)[0]["count"], 1500)
        complete = window(0, (FAST,)) + "\n"
        # Only the final line/window may be cut off by a truncated log.
        for truncated in (window(1, many)[:-7], "\n".join(window(1, many).splitlines()[:2])):
            rows = analysis.parse_windows(complete + truncated)
            self.assertEqual([w["id"] for w in rows], [0])
        with self.assertRaises(ValueError):
            analysis.parse_windows("\n".join(window(0, many).splitlines()[:2]) + "\n" + window(1, (FAST,)))
        with self.assertRaises(ValueError):
            analysis.parse_windows(window(0, many)[:-7] + "\n" + window(1, (FAST,)))

    def test_reject_malformed_contract(self):
        good = window(0, (FAST, SLOW, SLOW))
        lines = good.splitlines()
        cases = {
            "version": window(0, (FAST,), version=2),
            "bins": window(0, (FAST,), overrides={"bins": [0, 2, 0, 0, 0, 0]}),
            "stored": window(0, (FAST,), overrides={"stored": 2}),
            "over": window(0, (FAST,), overrides={"over": 2}),
            "percentile": window(0, (FAST, SLOW, SLOW), overrides={"p50": 33.3}),
            "worst": window(0, (FAST,), overrides={"worst": 40.0}),
            "timestamps": window(0, (FAST, SLOW), overrides={"end": 100.2}),
            "orphan samples": lines[1],
            "duplicate part": "\n".join(lines + lines[1:]),
            "reused id": good + "\n" + window(0, (FAST,), "12:00:10.000"),
            "chronology": good + "\n" + window(1, (FAST,), "12:00:01.000"),
            "malformed middle": good.replace("mean 50.00", "mean x") + "\n" + window(1, (FAST,), "12:00:10.000"),
        }
        for name, text in cases.items():
            with self.subTest(name), self.assertRaises(ValueError):
                analysis.parse_windows(text)
        self.assertEqual(analysis.parse_windows("unrelated\n[carbon-gpu] pacing(dbg mode 0): avg 1 ms"), [])

    @unittest.skipUnless((ROOT / "carbon-source/renderer/src/renderer.cpp").is_file(), "Carbon source checkout unavailable")
    def test_renderer_binding(self):
        source = (ROOT / "carbon-source/renderer/src/renderer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "carbon-source/renderer/src/renderer.h").read_text(encoding="utf-8")
        self.assertTrue('#include "frame_pacing.h"' in header, "renderer.h lacks frame_pacing.h")
        self.assertTrue("FramePacingWindow frame_pacing_;" in header, "renderer.h lacks the pacing window")
        swap = source[source.index("void Renderer::Swap("):]
        add = swap.index("frame_pacing_.Add(")
        # Samples are taken before the per-frame accumulators are cleared, only with stats enabled.
        self.assertLess(add, swap.index("frame_idle_s_ = frame_wait_s_ = frame_texture_s_ = frame_draw_s_ = 0;"))
        self.assertIn("if (overlay_last_swap_ > 0.0 && REXCVAR_GET(carbon_gpu_stats)) {", swap[:add])
        self.assertIn("(now - swap_timer.start) * 1000.0", swap[add:add + 600])
        report = swap[swap.index("if (now - stats_start_ >= 5.0) {"):]
        self.assertLess(report.index("frame_pacing_.FormatSummary()"), report.index("frame_pacing_.Reset();"))
        self.assertIn("for (size_t part = 0; part < frame_pacing_.SampleLineCount(); ++part)", report)
        self.assertLess(report.index("stats_ = Stats();"), report.index("pacing_resolve_mark_ = 0;"))
        pacing = (ROOT / "carbon-source/renderer/src/frame_pacing.h").read_text(encoding="utf-8")
        self.assertIn("frame pacing v{} window {}: frames {} stored {} t0 {:.4f} end {:.4f}", pacing)
        self.assertIn("frame pacing samples v{} window {} part {}/{}:", pacing)
        self.assertIn(f"kCapacity = {analysis.CAPACITY};", pacing)
        self.assertIn(f"kSamplesPerLine = {analysis.PER_LINE};", pacing)


if __name__ == "__main__":
    unittest.main()
