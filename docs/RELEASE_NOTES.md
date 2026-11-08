# Anime Neural ReShade 0.1.0 preview 3

Adds NVIDIA Image Scaling directly from the model's native output grid and a Processing quality dropdown to the external viewer. Both neural models, the approved DBZ finishing, HUD masks, and the normal ReShade addon remain available.

## Changes

- Extra outlines now default off in the viewer and supplied addon preset, avoiding additional black lines over Xenoverse 2's cel shading. The viewer has an optional Extra outlines checkbox. Color and shadow settings are retained; lines already produced by the game or neural model are not removed.
- NIS spatial edge-adaptive scaling from 720p/1080p to the 1440p capture, before finishing and HUD restoration. Native-sized inference uses an exact copy. Unsupported ratios and NIS-off explicitly report a bilinear fallback.
- Processing quality lists locally prepared resolutions for the same model and precision: Performance (720p), Quality (1080p), High detail (1440p), when installed. Select one and click Start. These are not DLSS presets.
- Status separates capture, model and display dimensions and identifies the active scaling path.
- Optional backend export exposes the actual model grid. The normal addon retains its ABI and existing output path. Keep the viewer, backend DLL and kernels.ptx from the same release.
- Pinned MIT-licensed NIS SDK sources and license are included. A documented FXC signed-coordinate correction fixes flat-color errors at the top/left image boundary.

## Try it

Close the previous viewer and extract the ZIP outside the game folder. Reuse matching local runtime DLLs and cache profiles, or run scripts/setup_neural.ps1. For a 1440p capture, select White-box FP16 at 1080p, keep NVIDIA upscale (NIS) enabled, and Start. Maximize or overlay the output to view at the intended display size. Compare NIS on/off with the same model, blend and finishing settings. Native 1440p remains available at higher GPU cost.

The viewer receives the finished game image; manual HUD masks also preserve scenery behind the HUD. It cannot access a separate UI pass. NIS is spatial reconstruction, not AI super-resolution, DLSS or frame generation, and cannot recover all detail removed by the model.

## Validation and limits

- Native build and both CTests passed, including NIS 720p/1080p to 1440p, odd dimensions, flat border colors, output coverage, exact native copying and unsupported-ratio rejection.
- Existing edge-AA, original HUD restoration, split and bypass GPU checks still pass.
- Actual static combat frames processed with White-box 1080p/1440p and AnimeGAN 720p. Native grid and legacy native output match exactly for both RGB/BGR model paths.
- White-box 1080p FP16 versus ONNX remains within tolerance: max 6.1784, mean 0.2552 channel levels.
- Isolated ReShade identity harness passed with the new backend/PTX: 21 samples, 226 ready frames, zero failures, including resizes and shutdown.
- Live NIS motion quality, sustained combat performance and manual quality-dropdown acceptance remain pending. This is not a verified 60 FPS or complete pixelation fix.

This is a development preview. The external viewer installs no game DLL and does not bypass EAC. Per-game capture compatibility still applies. NVIDIA runtime DLLs, ReShade, model weights and GPU-specific engines are acquired separately; see README and THIRD_PARTY_NOTICES.
