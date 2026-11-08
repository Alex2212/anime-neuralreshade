# Development status

## Implemented

- Adjustable hard two-tone finishing: two HSV-value bands, preserved near-black ink, live threshold/shadow/light controls and a legacy shading toggle. The DBZ addon preset applies full-strength value quantization after color grading, while custom luminance/literal modes remain available.
- Added outline darkening is optional and defaults off in the viewer, addon FX and DBZ preset. The viewer exposes an Extra outlines checkbox while keeping color/shadow styling independent.
- Viewer NIS upscaling from a native model-grid texture, native-resolution exact copy, explicit bilinear fallback diagnostics, and a quality dropdown restricted to installed resolutions for the same model/precision. The optional backend export preserves the normal addon's existing ABI and output path.
- External x64 Windows Graphics Capture viewer with bounded capture buffers, own D3D11 device/presentation, explicit BGRA-to-RGBA GPU conversion, both model profiles, live blend, fixed DBZ finishing, bypass, split and optional click-through overlay. No game process modification.
- Viewer diagonal-edge anti-aliasing and three editable HUD preservation regions, with original pixels restored after styling/AA. Native 2560x1440 White-box FP16 preparation is supported alongside the existing profiles.
- C++20 x64 addon, pinned ReShade 6.1.1/API 12 and overlay.
- Owned RGBA8 D3D11 capture/output resources, same-frame CUDA interop and inverse-fit spatial upscaling.
- TensorRT backend with background loading/warm-up, cache integrity/GPU/shape/color validation, safe original-frame bypass.
- Both official model export paths, including White-box guided filtering, and reference/native accuracy checks.
- Selectable local model/resolution/precision profiles, bypass, split comparison, manual HUD rectangles, independent finishing passes, DBZ preset, reset controls and diagnostics.
- Build/setup/benchmark/harness scripts, source repository, release allowlist and dependency notices.

## Validation completed

Native compilation; exact RGBA GPU copy at multiple sizes; unsupported format rejection; CUDA identity with native textures; both official model conversions; White-box FP32/FP16 and 720p/1080p native execution; AnimeGAN FP32/FP16 execution; ReShade full-addon loading/FX compilation; same-frame identity readback, two resizes and clean destruction. External real Windows capture self-tests passed in finishing-only, White-box and AnimeGAN modes: each 100 frames, two resource sizes, 12 bypass checks and 88 styled checks. See BENCHMARKS.md for measurements and their limits.

Preview 2 GPU checks passed at three resource sizes: 4,368 diagonal pixels softened, 3,820,513 flat-region pixels unchanged, and 3,365,376 exact HUD-pixel checks with AA on/off. Native 1440p White-box passed official-export and native-versus-ONNX parity checks. The same viewer shaders rendered static combat comparisons with AA and HUD restoration. These checks do not establish motion quality or live UI-control acceptance.

Preview 3 checks: NIS scaling at 720p/1080p to 1440p and odd dimensions passed flat-border, output-coverage and non-replicated filtering tests. Native copying is exact. Static combat frames passed through White-box 1080p/1440p and AnimeGAN 720p; native-grid versus legacy output matched exactly for both color orders. White-box 1080p versus ONNX CPU remained within tolerance (max 6.1784, mean 0.2552 channel levels). The isolated ReShade identity harness passed 21 samples / 226 ready frames / zero failures using the new backend/PTX. These are pipeline checks; the quality dropdown, live NIS motion and performance need manual acceptance.

## Pending before a stable release

- Preview 4 passed native compilation, the GPU ramp/colored-row band checks, threshold movement, preserved ink and exact bypass; existing NIS/AA/HUD tests passed. Static White-box combat output was inspected. Per-material lighting separation and live temporal acceptance are not established by these tests.
- Actual neural rendering in Xenoverse 2 is blocked on the tested EAC-protected launch: EAC rejected the full-addon ReShade DLL. The attempted installation was fully rolled back and original DLL/configuration hashes verified. No protection bypass was performed.
- The user supplied gameplay styling screenshots and reported 1080p inference / 1440p capture. Controlled EAC-launch, overlay and live quality acceptance remain unverified; manual Computer Use was previously stopped by the user.
- Controlled, end-to-end 30/60 FPS combat captures and input-latency measurements.
- Thirty-minute stability, alt-tab/minimize/fullscreen and additional game-specific lifecycle checks.
- Final visual tuning with both models, face/detail preservation, temporal stability and HUD readability during combat.
- Broader GPU/driver/ReShade compatibility and measured minimum memory/hardware requirements.

## Not implemented

DLSS Super Resolution, DLSS Frame Generation, motion-aware temporal stabilization, automatic/pre-HUD capture, separate game UI-pass access, other addon graphics APIs, x86, and non-NVIDIA inference. Viewer tone levels are adjustable; its saturation remains fixed. No new model training has begun.

Public release is a development preview. Public payload excludes weights, generated TensorRT engines, NVIDIA runtime binaries, ReShade binaries and game assets beyond the authorized README screenshots.
