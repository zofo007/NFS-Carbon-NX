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
| `carbon_cube_interval` | `30` | Runs the car cube-reflection pass once every 30 frames instead of every frame (about 0.1 ms instead of 2.4 ms per frame) |
| `carbon_gpu_bloom` | `true` | Bloom on. `false` saves about 4 ms of GPU time per frame in a crowded race |

## Quality and speed

| Setting | Default | Effect |
|---|---|---|
| `carbon_gpu_render_scale` | 100 | Lower = faster, softer image (the shipped file uses 80 with FSR) |
| `carbon_gpu_texture_budget_mb` | 704 on Switch | Texture memory above which the least recently used textures are freed. Without a cap the cache grew until memory ran out in long sessions |
| `thread_host_stack_mib` | 8 | Host stack per game thread. Smaller leaves more memory free for new threads |
| `carbon_gpu_max_anisotropy` | 4 on Switch | 1 = off |
| `carbon_gpu_texture_lod_bias` | 0.0 | 0 to 2. +1 uses one mip level lower on scene textures: blurrier, less memory traffic |
| `carbon_gpu_mip_nearest` | false | Bilinear instead of trilinear mip filtering (visible mip steps) |
| `carbon_cube_interval` | 1 (shipped 30) | Run the car cube-reflection pass once every N frames, 1 to 240 (1 = every frame, what the game does). Cheaper than skipping it for good (`carbon_debug_skip_passes = 4`), which is a suspect for the flashing shadows |
| `carbon_gpu_bloom` | true | The game's bloom blur passes (about 4 ms of GPU time per frame in a crowded race). `false` draws the quarter-resolution (pitch 320) blur chain as black, so there is no glow; the same shader at pitch 640 and 1280 builds the pause menu background and keeps running. `carbon_gpu_bloom_skip = true` skips all blur draws instead: cheaper, but the suspected cause of dark intros and black textures in cutscenes. `carbon_gpu_bloom_black_f84b = true` (experiment) also blacks out the 640-pitch blur of the race, about 3 ms more |
| `carbon_gpu_reflection_faces` | game default | Car reflection cube faces redrawn per frame (1 to 6) |
| `carbon_gpu_mirror_half_rate` | on Switch | Rear-view mirror redrawn every other frame |
| `carbon_ai_car_lod_bias` | 0 | 0 to 3. In a race the game pins every other racer and traffic car at model detail level 1; 1 or more moves them to level 2 (the lowest level that still draws the body; level 3 and 4 draw only lights and shadows). The player's car and showroom cars are never touched. Only applies while 5 or more cars are on screen (`carbon_ai_car_lod_min_cars`). Little FPS effect measured on Switch. Log: `[car-lod]` |
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
| `carbon_audio_mixer_priority` | Priority of the game's mixer thread (default 0x2A, above the XMA worker 0x2B and the GPU ring 0x2C; it was 0x2D). In race wars 0x2A cut the starved audio requests on hardware; go back to 0x2D only if render FPS suffers |
| `carbon_audio_wait_server_ms` | The host waits up to this many ms for the mixer's packet instead of playing silence (default 30) |
| `audio_switch_recuperar` | While the output queue holds fewer than `audio_switch_tramas_en_cola - 2` frames, ask the game for this many extra frames per 5.33 ms tick (default 1, 0 to 3; 0 = the old behaviour, which never refilled the queue after a gap) |
| `carbon_audio_gain_cull_below` | Skip the mixing of voices whose gain is below this (default 0 = only an exact zero gain, which changes nothing). Experiment: only about 7% of the calls are that quiet |
| `carbon_audio_mixer_rate` | Experiment, default 0 (off). Setting it to 44100 changes the rate fields the game keeps for the mixer; it did not reduce the mixer's work, so leave it off |
| `content_max_marketplace_items` | How many DLC packs the game is shown (default 30, 0 = all). 30 packs ran a long session; 36 and 52 crashed the profile load |

## Logging and diagnostics

| Setting | Default | Effect |
|---|---|---|
| `carbon_gpu_stats` | true (shipped false) | Statistics lines in the log (FPS, draws, GPU time). The short summary is printed every 5 s, the detailed one once a minute. `false` also skips two clock reads per draw |
| `carbon_audio_mixer_diag` | true (shipped false) | `[mixer-diag]` timing of the audio mixer every 10 s, plus a memory and thread line |
| `carbon_video_wmv3_shadow` | false | The game decodes every cutscene frame and FFmpeg only compares |
| `switch_log_console_shortcut` | false | L+R+Down log console. Off because opening it froze the console |

Logs go to `sdmc:/switch/nfscarbon/logs/`. If you report a problem, attach `nfscarbon_001.log` (and the newest
numbered one) and say what you did.
