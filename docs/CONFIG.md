# Configuration

Settings live in `nfscarbon.toml` next to the NRO. A line that is missing uses the default. Restart the game after
editing. The release file ships the values below under "Shipped"; everything else is at its default.

## Shipped settings

| Setting | Shipped | Meaning |
|---|---|---|
| `gpu_plugin` | `"carbon"` | The native Vulkan renderer (the other value, `"xenos"`, is the SDK's slower emulation) |
| `input_backend` | `"switch"` | Controllers through libnx |
| `input_xbox_layout` | `false` | `true` swaps A/B and X/Y by position, as on an Xbox pad |
| `log_level` | `"warn"` | Quiet: only warnings and errors. `"info"` logs everything (use `nfscarbon.report.toml` for a report) |
| `carbon_gpu_vsync` | `true` | |
| `carbon_gpu_render_scale` | `80` | Percent of 1280x720 the scene is drawn at (100 = native) |
| `present_effect` | `"fsr"` | How the scene is scaled to the screen: `"fsr"` (AMD FSR upscale plus sharpening), `"cas"` (sharpen only), `"bilinear"` (plain stretch) |
| `input_vibracion` | `true` | Controller rumble. `false` turns it off |
| `carbon_gpu_stats`, `carbon_audio_mixer_diag`, `audio_xma_profile` | `false` | Statistics and diagnostics, off for speed; on in `nfscarbon.report.toml` |
| `carbon_gpu_max_anisotropy` | `4` | 1 = off, up to 16 |
| `audio_switch_tramas_en_cola` | `32` | Audio frames queued before output (about 170 ms of cushion) |
| `carbon_handoff_priority` | `58` | Horizon priority of the two frame hand-off threads |
| `carbon_debug_skip_passes` | `4` | Skips the car cube-reflection pass, whose faces the game never samples |

## Quality and speed

| Setting | Default | Effect |
|---|---|---|
| `carbon_gpu_render_scale` | 100 | Lower = faster, softer image (the shipped file uses 80 with FSR) |
| `carbon_gpu_texture_budget_mb` | 704 on Switch | Texture memory above which the least recently used textures are freed. Without a cap the cache grew until memory ran out in long sessions |
| `thread_host_stack_mib` | 8 | Host stack per game thread. Smaller leaves more memory free for new threads |
| `carbon_gpu_max_anisotropy` | 4 on Switch | 1 = off |
| `carbon_gpu_texture_lod_bias` | 0.0 | 0 to 2. +1 uses one mip level lower on scene textures: blurrier, less memory traffic |
| `carbon_gpu_mip_nearest` | false | Bilinear instead of trilinear mip filtering (visible mip steps) |
| `carbon_gpu_bloom` | true | `false` removes the glow passes (about 5 ms of GPU in some scenes) |
| `carbon_gpu_reflection_faces` | game default | Car reflection cube faces redrawn per frame (1 to 6) |
| `carbon_gpu_mirror_half_rate` | on Switch | Rear-view mirror redrawn every other frame |
| `carbon_lod_bias` | 1.0 | 0.3 to 1.0. Lower hides small distant objects sooner. Had no visible effect on the heaviest scenes |

## Performance helpers (all on by default; set false to compare)

| Setting | What it does |
|---|---|
| `carbon_gpu_texture_fast_untile` | Word-wise endian-swapping texture copy. Keeps cutscene frames cheap: leave on |
| `carbon_gpu_texture_sets_across_frames` | Keeps texture descriptor sets between frames |
| `carbon_gpu_constant_blocks` | Uploads only the 1 KB constant blocks that changed |
| `carbon_gpu_vertex_memo` | Checks each vertex buffer once per frame |
| `carbon_gpu_index_cache`, `carbon_gpu_texture_hash_thread`, `carbon_gpu_record_thread`, `carbon_gpu_async_pipelines` | Renderer threading and caching |
| `carbon_video_wmv3_native` | FFmpeg decodes cutscene frames. Each movie starts verified against the game's own decoder and falls back to it if the pictures differ |
| `carbon_audio_native_*`, `carbon_audio_neon_*` | Register-local and NEON versions of the game's audio mixer helpers. Same results as the original code (tested bit-exact); falls back to the original code for unusual inputs |
| `carbon_audio_mixer_priority` | Priority of the game's mixer thread (default 0x2D). Changing it made things worse in testing |
| `carbon_audio_wait_server_ms` | The host waits up to this many ms for the mixer's packet instead of playing silence (default 30) |

## Logging and diagnostics

| Setting | Default | Effect |
|---|---|---|
| `carbon_gpu_stats` | true (shipped false) | Statistics lines in the log (FPS, draws, GPU time). The short summary is printed every 5 s, the detailed one once a minute. `false` also skips two clock reads per draw |
| `carbon_audio_mixer_diag` | true (shipped false) | `[mixer-diag]` timing of the audio mixer every 10 s, plus a memory and thread line |
| `carbon_video_wmv3_shadow` | false | The game decodes every cutscene frame and FFmpeg only compares |
| `switch_log_console_shortcut` | false | L+R+Down log console. Off because opening it froze the console |

Logs go to `sdmc:/switch/nfscarbon/logs/`. If you report a problem, attach `nfscarbon_001.log` (and the newest
numbered one) and say what you did.
