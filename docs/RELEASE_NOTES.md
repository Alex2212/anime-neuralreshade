# Anime Neural ReShade 0.1.0 preview 2

Reduces diagonal pixelation in the external viewer and adds manual HUD preservation while retaining the existing DBZ shading, both model choices, and the normal ReShade addon.

## Changes

- **Smooth jagged edges**: optional spatial edge anti-aliasing after styling, enabled by default. Flat-region colors and axis-aligned edges are preserved. This is not temporal AA or super resolution.
- **Preserve HUD regions**: three editable percentage rectangles restore original captured pixels after finishing/AA. Starting layouts cover the top HUD, training stats and input panel. Only the top region is enabled initially; HUD preservation itself is opt-in. Edit and Apply to match your screen.
- **Native 1440p White-box**: `scripts/setup_neural.ps1 -Height 1440 -Model whitebox` prepares a 2560x1440 FP16 profile locally. Existing 720p/1080p profiles remain available. Models and GPU-specific engines are not bundled.
- Setup now handles TensorFlow stderr warnings using process exit status and skips copying already-identical runtime DLLs.
- The normal `.addon64`, its effects and the shared inference backend remain unchanged. Both addon and external viewer are included.

## Use

Close the previous viewer and extract this ZIP into its own working directory outside the game folder. Reuse matching local runtime DLLs and `cache` profiles, or run the setup script. Launch `anime_viewer.exe`. Keep Smooth jagged edges enabled. Enable Preserve HUD regions and adjust/apply masks as needed.

The viewer receives the finished game window; it cannot access a separate UI render pass. Masks preserve scenery behind the HUD too. Moving names, menus and subtitles are not identified automatically.

## Validation and limits

- Native build and both CTests passed. Actual viewer shaders were checked at 128x96, 512x288 and 2560x1440: 4,368 diagonal pixels softened, 3,820,513 flat pixels unchanged, exact bypass/disabled AA/split original half/straight edges.
- 3,365,376 HUD-pixel checks passed with AA on/off; masked pixels match the original and output outside masks remains unchanged.
- Native 1440p White-box passed export and GPU parity checks: maximum channel error 5.044, mean 0.255 versus ONNX CPU.
- Native 1440p static processing p95 was 18.78 ms under concurrent desktop/game load, before viewer finishing/presentation. It is a quality option, not a 60 FPS guarantee. Keep 1080p plus AA available for comparison.
- Static combat output was inspected through the actual inference and viewer finishing shaders. Live motion quality and the new UI controls still need in-game acceptance.

This remains a development preview. The external viewer installs no game DLL and does not bypass EAC; per-game capture compatibility still applies. The tested Xenoverse 2 EAC launch rejected full-addon ReShade. ReShade, NVIDIA runtime DLLs, model weights and engines are downloaded/prepared separately. See README, BUILDING and THIRD_PARTY_NOTICES for setup and model restrictions.
