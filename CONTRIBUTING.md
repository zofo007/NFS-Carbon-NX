# Contributing

Thanks for looking. This is a small project; the most useful contribution is a good report.

## Reporting a problem

Use the **Performance / audio report** or **Bug report** issue template. For performance, run
`scripts/pack-logs.ps1` on your logs folder and attach the zip: the numbers in a report are what lets us find what
limits real setups.

Do not attach game files, saves or screenshots of the game's copyrighted content beyond what is needed to show a bug.

## Rules that never change

- **No game data in this repository.** No `default.xex`, archives, movies, shaders, generated code, NRO builds or
  logs. A pull request that adds any of them is closed.
- No code, addresses or profile data taken from other games' projects. Studying how another project solved a problem
  is fine; copying it is not, unless its license allows it and the file keeps its notice.
- Keep upstream license headers and add new credits to `THIRD_PARTY_NOTICES.md`.

## Changes to the code

- Build and test instructions are in [docs/BUILD.md](docs/BUILD.md). Run `python -m unittest discover -s tests` and the
  PowerShell checks in `tests/` that touch what you changed.
- A hand-written replacement for a game function must keep the original as a fallback, have a setting to turn it off,
  and come with a test that compares it with the generated original (see `tests/native_leaf_test.cpp`).
- Do not claim a speed-up without a measurement from a console: say what you ran, the clocks, and use
  `scripts/summarize_run.py` for the numbers.
- Small, focused commits with a message that says why.

## Questions

Open an issue. Please do not ask where to get the game.
