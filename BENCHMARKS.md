# Measured results

These measurements are from **8 November 2026**, RTX 5090, NVIDIA driver 621.14, TensorRT 10.13.3.9, CUDA runtime 12.9.79, Windows x64. They are development measurements, not a promised frame rate or minimum hardware specification.

## Native static-image processing

Preview 3 validation adds NVIDIA Image Scaling directly from the raw model grid. GPU tests passed 1280x720 and 1920x1080 to 2560x1440, odd 129x73 to 257x145, and exact native copying. Across the three scaled synthetic cases, 44,804 pixels differed from bilinear and 44,897 differed from nearest-neighbor replication. Flat border colors stayed within one channel level and every output pixel had valid alpha. These checks distinguish the implemented filter from basic enlargement; they are not an image-quality score.

The shared backend still passed White-box 1080p FP16 versus ONNX (max channel error 6.1784, mean 0.2552, p99 0.5769). Both White-box BGR and AnimeGAN RGB native grids matched legacy native output exactly. Static combat diagnostics rendered White-box 1080p NIS, White-box 1440p native, and AnimeGAN 720p NIS. Live NIS frame-time and motion-quality acceptance remain pending; the earlier timings below exclude this new NIS pass.

The C++ benchmark loads one 2560×1440 Xenoverse 2 combat screenshot, uploads it before timing, and repeatedly measures D3D11 copy → CUDA preprocessing → TensorRT → CUDA output/upscale → graphics ownership handoff. Five iterations are discarded for warm-up. CPU source loading and final PNG readback are outside timing. Runs below used 200 iterations unless noted.

| Model / precision | Processing resolution | Pipeline p50 | Pipeline p95 | Pipeline p99 | Mean inference |
|---|---:|---:|---:|---:|---:|
| White-box FP32 (100 iterations) | 1280×720 | 9.94 ms | 11.61 ms | 14.07 ms | 9.08 ms |
| White-box mixed FP16 | 1280×720 | 2.88 ms | 3.82 ms | 4.01 ms | 1.98 ms |
| White-box mixed FP16 | 1920×1080 | 5.09 ms | 5.69 ms | 6.21 ms | 4.31 ms |
| AnimeGAN FP32 | 1280×720 | 28.08 ms | 29.31 ms | 29.68 ms | 26.90 ms |
| AnimeGAN mixed FP16 | 1280×720 | 13.85 ms | 14.83 ms | 15.56 ms | 12.89 ms |

CUDA identity processing at 720p with 1440p output measured 0.44 ms p50 and 0.98 ms p95 in an earlier 100-iteration run. This is a data-path check, not neural throughput.

**These numbers exclude the game renderer, companion ReShade FX, presentation and input latency.** Other desktop/game GPU activity was not controlled. They are not a 60 FPS acceptance test. A capped game still needs its own work plus all addon work to fit within 16.7 ms (or 33.3 ms for genuine 30 FPS). Generated frames/repeated stale output do not count as processed frames.

## Accuracy checks

- White-box exported ONNX versus official TensorFlow (including guided filter): max normalized error `4.29e-6` at 720p, `5.25e-6` at 1080p on the recorded reference frames.
- AnimeGAN exported ONNX versus official frozen TensorFlow graph: max `0.003121`, mean `0.001080` normalized error on the combat frame (about 0.398 max / 0.138 mean 8-bit channel levels). Optional brightness adjustment is disabled.
- Native White-box mixed-FP16 versus ONNX CPU at 720p: max channel error 4.85, mean 0.256, p99 0.583 on a same-resolution test input; this includes quantization and the complete native image path.
- Native White-box mixed-FP16 at 1080p: max channel error 6.18, mean 0.255, p99 0.577.
- White-box FP16 versus native FP32 on the full combat image: max 4, mean 0.030, p99 1 channel level.
- Native AnimeGAN mixed-FP16 versus ONNX CPU: max channel error 2.22, mean 0.307, p99 0.893. TensorRT reported a generic FP16 layer-normalization overflow warning during build; this captured-frame parity check passed. Wider scene/motion coverage is still needed.

The PNG diagnostic writer explicitly uses BGRA encoding. An early channel-swapped diagnostic image was corrected before selecting/publishing the README previews. Exact RGBA GPU copy tests include alpha.

## ReShade harness

The hidden Direct3D 11 harness verified actual addon registration with full-addon ReShade 6.1.1, FX compilation, 240 presents, two resizes, and device/runtime destruction. The CUDA identity run checked 21 pixel samples against alternating current-frame input colors, with 227 ready frames and zero mismatches. This checks same-frame correspondence rather than relying only on a load log.

A subsequent White-box neural harness run recorded 188 neural-ready frames and clean resizing/destruction. A missing-engine run recorded zero neural-ready frames and passed 21 original-frame pixel checks. Neural image accuracy is checked separately by the native-versus-ONNX image comparison above.

## External Windows capture viewer

Real Windows Graphics Capture tests on an animated built-in source, resized from 640x360 to 800x448. Each mode completed 100 frames, two resource sizes, 12 original-image bypass checks and 88 styled-output checks. Both neural runs submitted 88 inference frames. Red/blue patches verify BGRA-to-RGBA conversion; the moving source forces fresh capture frames. The GPU identity CTest also passed after viewer integration.

| Viewer mode | Model shape | Frame-work p50 | Frame-work p95 |
|---|---:|---:|---:|
| Finishing only | None | 0.77 ms | 1.09 ms |
| White-box FP16 | 1280x720 | 2.97 ms | 3.57 ms |
| AnimeGAN FP16 | 1280x720 | 11.83 ms | 13.04 ms |

Frame-work timing starts after acquiring a capture frame and includes conversion, optional inference, finishing, viewer rendering and the Present call. Diagnostic readbacks happen afterward. It excludes capture delivery delay and display scan-out. The source/output size is smaller than the static-image benchmark above; these rows are not directly comparable to the 1440p results or a loaded game.

The signed QPC-minus-capture-timestamp measurement sometimes went negative (finishing p50 -3.66 ms, White-box -1.72 ms), consistent with compositor timestamps ahead of the sampled local clock. It is reported as **capture timestamp delta**, never treated as end-to-end latency. No input-latency result is established.

The restricted tool sandbox initially could not activate Windows capture services; the same executable passed when run normally on the interactive desktop. Manual UI/overlay inspection was stopped by the user before completion. Actual game capture, overlay input/focus, minimize/restart, long-duration memory behavior and protected-game compatibility remain unverified.

## Preview 2 quality checks

- Native White-box FP16 at 2560x1440: official TensorFlow versus ONNX max normalized error `4.41e-6`; native GPU versus ONNX max 5.044, mean 0.255 and p99 0.575 channel levels, within the existing tolerances.
- 200 static 1440p iterations under the current concurrent desktop/game load: p50 15.48 ms, p95 18.78 ms, p99 19.02 ms; mean inference 9.64 ms. This run is not controlled against the earlier 1080p results and excludes viewer finishing/AA/presentation. It is not a 60 FPS result.
- Real D3D11 viewer shader tests at 128x96, 512x288 and 2560x1440: 4,368 diagonal pixels softened; 3,820,513 flat pixels unchanged. Straight edges, bypass, disabled AA and the original half of split comparison remained exact.
- 3,365,376 HUD-region pixel checks passed with AA enabled and disabled, with original capture pixels restored exactly inside masks and no differences outside compared with the corresponding unmasked rendering.
- Full 2560x1440 static combat output was rendered through the actual viewer shaders with 1080p and 1440p neural profiles, including AA off/on and a restored top HUD. Motion quality, HUD layout coverage and live UI controls still require in-game acceptance.

## Required live acceptance

Capture baseline, pass-through and neural modes at identical display settings and frame caps. For standing, camera orbit, close combat and particle-heavy attack scenes, collect three 60-second captures after warm-up, report p50/p95/p99 total frame time, present/processed FPS, stalls, allocation growth and frame age. Review faces, attacks, HUD, camera cuts and motion. Complete a 30-minute mixed gameplay run plus alt-tab, resize, bypass, profile-switch and shutdown checks. These checks remain pending.
