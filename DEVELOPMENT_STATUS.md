# Development status

## Implemented

- External x64 Windows Graphics Capture viewer with bounded capture buffers, own D3D11 device/presentation, explicit BGRA-to-RGBA GPU conversion, both model profiles, live blend, fixed DBZ finishing, bypass, split and optional click-through overlay. No game process modification.
- C++20 x64 addon, pinned ReShade 6.1.1/API 12 and overlay.
- Owned RGBA8 D3D11 capture/output resources, same-frame CUDA interop and inverse-fit spatial upscaling.
- TensorRT backend with background loading/warm-up, cache integrity/GPU/shape/color validation, safe original-frame bypass.
- Both official model export paths, including White-box guided filtering, and reference/native accuracy checks.
- Selectable local model/resolution/precision profiles, bypass, split comparison, manual HUD rectangles, independent finishing passes, DBZ preset, reset controls and diagnostics.
- Build/setup/benchmark/harness scripts, source repository, release allowlist and dependency notices.

## Validation completed

Native compilation; exact RGBA GPU copy at multiple sizes; unsupported format rejection; CUDA identity with native textures; both official model conversions; White-box FP32/FP16 and 720p/1080p native execution; AnimeGAN FP32/FP16 execution; ReShade full-addon loading/FX compilation; same-frame identity readback, two resizes and clean destruction. External real Windows capture self-tests passed in finishing-only, White-box and AnimeGAN modes: each 100 frames, two resource sizes, 12 bypass checks and 88 styled checks. See BENCHMARKS.md for measurements and their limits.

## Pending before a stable release

- Actual neural rendering in Xenoverse 2 is blocked on the tested EAC-protected launch: EAC rejected the full-addon ReShade DLL. The attempted installation was fully rolled back and original DLL/configuration hashes verified. No protection bypass was performed.
- External viewer capture of actual Xenoverse 2/EAC gameplay is untested. Manual UI/overlay verification was interrupted by the user stopping Computer Use; no overlay acceptance claim is made.
- Controlled, end-to-end 30/60 FPS combat captures and input-latency measurements.
- Thirty-minute stability, alt-tab/minimize/fullscreen and additional game-specific lifecycle checks.
- Final visual tuning with both models, face/detail preservation, temporal stability and HUD readability during combat.
- Broader GPU/driver/ReShade compatibility and measured minimum memory/hardware requirements.

## Not implemented

DLSS Super Resolution, DLSS Frame Generation, motion-aware temporal stabilization, automatic/pre-HUD capture, other addon graphics APIs, x86, and non-NVIDIA inference. Viewer finishing has fixed parameters and no HUD masks yet. No new model training has begun.

Public release is a development preview. Public payload excludes weights, generated TensorRT engines, NVIDIA runtime binaries, ReShade binaries and game assets beyond the authorized README screenshots.
