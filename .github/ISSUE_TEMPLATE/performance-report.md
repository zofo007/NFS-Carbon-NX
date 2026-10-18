---
name: Performance / audio report
about: Share a play session so the bottlenecks can be found
title: "[report] <console, clocks, scene>"
labels: report
---

**Console and setup**
- Model (Erista / Mariko / OLED / Lite):
- Handheld or docked:
- Clocks used (CPU / GPU / RAM MHz) and the tool that set them:
- Firmware / Atmosphere version:
- Game you held R on to launch (title override):
- NFS Carbon NX version (from the zip name):
- Game edition: `default.xex` SHA-256 (first 12 characters is enough):

**What you did** (race type, track, how long, where it stuttered)

**What you saw**
- FPS (roughly) and where it dipped:
- Audio: clean / short drop-outs / heavy stutter, and when:
- Anything else (freezes, black flashes, crashes):

**Numbers** (paste the output of `python scripts/summarize_run.py <your logs folder>`)

```
```

**Logs**: attach a zip of `sdmc:/switch/nfscarbon/logs/` (the game log `nfscarbon_001.log`, any higher numbered
ones and `rex/rex_perfil.log`). The logs contain your save profile names and SD paths; remove anything you do not
want to share. Do not attach game files or save files.
