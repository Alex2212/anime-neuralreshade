# Anime Neural ReShade 0.1.0 preview 4

Adds enforceable hard brightness bands for the White-box workflow. The previous viewer shading multiplied the original brightness, allowing gradients to survive inside both regions. The new optional hard-tone pass quantizes color brightness while retaining hue and saturation ratios.

## Changes

- Hard two-tone is enabled by default with DBZ finishing. Defaults: Threshold 60%, Shadow 35%, Light 90%. Edit values and click Apply tones; Shadow must be below Light. Changes apply live.
- The tone pass quantizes HSV value into two bands. Near-black ink is preserved. Extra outlines remain off by default. Disable Hard two-tone to compare the previous shading.
- The supplied addon DBZ preset uses full-strength value bands after color grading. Custom luminance and literal two-color modes remain available. The addon reset controls include the new options.
- Both model profiles, NIS scaling, quality selection, HUD masks and the normal addon remain available.

## Use

Close the older viewer and launch this version outside the game folder. Select White-box FP16 and the desired prepared resolution; for a 1440p capture, 1080p Quality with NIS is a useful comparison path. Keep DBZ finishing and Hard two-tone on, Extra outlines off. Raise Threshold for more shadow pixels. Adjust Shadow and Light to control contrast.

This is final-image quantization, not material-aware relighting. Dark material colors can be classified as shadows. Hue/saturation variation can remain; it does not guarantee exactly two RGB colors per object. Original ink, preserved HUD and anti-aliased boundaries are exceptions to the two brightness bands. The model itself has not been retrained.

## Validation

- Native build and both CTests passed, including existing NIS/AA/HUD checks.
- A continuous grayscale ramp and colored rows produced two configured value bands for 2,039 non-ink pixels. Nine near-black pixels stayed unchanged. Threshold movement and exact bypass passed.
- Actual White-box 1080p-to-1440p NIS combat output was rendered with the final shader and inspected against the previous shading.
- The isolated ReShade harness checks addon loading, FX compilation, identity output, resizes and shutdown. Live combat motion quality, threshold stability, sustained performance and manual control acceptance remain pending.

Development preview only. No DLSS, frame generation, material buffers or EAC bypass. NVIDIA runtimes, model weights and GPU-specific engines are acquired separately. See README and THIRD_PARTY_NOTICES.
