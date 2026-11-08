# Compatibility and troubleshooting

## Verified environment

Windows 11 x64, RTX 5090 (32 GB), NVIDIA 621.14; native Direct3D 11. The installed Xenoverse 2 Steam build inspected was 25345532, executable version 1.26.01.0, at 2560×1440, `DXGI_FORMAT_R8G8B8A8_UNORM`, one sample. Its original ReShade 6.1.1.1848 DLL was the limited-addon edition. That build explicitly refused to load `CelShade.addon64` in an isolated harness.

The official **6.1.1 full-addon** build successfully loaded the addon in the harness. SDK revision `a59d611903db84355d562a2cb6da20d4d2761619`, API 12, ImGui 1.90.4. This is a pinned compatibility target; a newer ReShade GUI/API version is not assumed compatible.

The subsequent game installation was rejected by **Easy Anti-Cheat**, which identified the full-addon `bin/dxgi.dll` as an untrusted system file before successful neural game startup. The original DLL and ReShade.ini were restored and verified byte-for-byte using SHA-256; the task's added files were removed only when their hashes matched the install manifest. The game installation is therefore **rolled back**, not active. EAC-protected launch compatibility is a known blocker, not merely untested.

## Troubleshooting

### External viewer

The viewer uses Windows Graphics Capture without loading code into a game. Real capture of its own animated source passes with finishing-only, White-box and AnimeGAN on the verified machine. This establishes the external pipeline, **not EAC-protected game compatibility**. The game has not been tested through the viewer yet.

- Use SDR and a non-minimized windowed/borderless source. HDR tone mapping and exclusive fullscreen are unsupported/unvalidated.
- Keep the viewer outside the game directory. It does not require ReShade. If an earlier addon installation caused EAC's untrusted-DLL error, restore that installation separately; starting a viewer cannot remove an already rejected game DLL.
- A blocked capture, invalid/closed window, missing dependency or device failure stops the worker and hides its output. Restore/reselect the window and Start again. No attempt is made to override content protection.
- Profiles must be beside the viewer under `cache`; click Refresh after local setup. Invalid/missing dimensions are omitted; backend integrity checks run on Start.
- F9/F10 global hotkeys can conflict with another app. Visible controls still work. Manual overlay, DPI/multi-monitor, focus switching, and sustained game testing remain pending.
- The optional overlay is non-activating and click-through; source focus determines visibility. It captures the selected source window rather than the desktop to avoid feeding its own output back into inference. It hides after two seconds without a fresh frame; static sources may therefore make it disappear until they update.
- The viewer chooses an NVIDIA adapter when present. Same-machine cross-adapter behavior and non-NVIDIA finishing-only performance have not been tested.

### ReShade addon

| Symptom | Action |
|---|---|
| Addon absent from the overlay | Read ReShade.log. If it reports limited addon functionality, install the full-addon edition. Check x64 and file placement. |
| EAC reports untrusted `dxgi.dll` | Restore the previously working DLL/configuration. The original limited-addon ReShade build cannot host the neural backend. The separate viewer avoids installing that DLL, but game capture compatibility still needs testing. |
| DLL load error 126 | Keep `cel_nvidia.dll`, `nvinfer_10.dll`, and `cudart64_12.dll` beside the addon. Install the Microsoft VC++ x64 runtime. Missing transitive dependencies also produce 126. |
| No prepared profiles | Place each `.plan` and matching `.plan.ini` in the addon directory's `cache` folder, then Refresh model profiles. |
| Checksum/GPU/shape/runtime mismatch | Rebuild the engine using the pinned local setup. Do not edit the manifest to conceal a mismatch. |
| Original image while preparing | Expected. Engine deserialization and warm-up occur off the rendering callback. |
| Original image after a failure | Read the CelShade diagnostic and ReShade.log; fix the dependency/configuration and use Prepare / reload engine. |
| No visual change in finishing mode | Enable the `Cel_` techniques and adjust their amounts. Two-tone defaults off outside the DBZ preset. |
| HUD stylized | Use exclusion rectangles or F10. A pre-HUD capture is not currently available. |
| Colors incorrect | Choose a prepared profile, which selects RGB for AnimeGAN and BGR for White-box. Avoid implicit sRGB sampling or double gamma conversions. |
| Slow frame times | Start with 720p FP16, disable optional FX, compare against a game-only baseline at identical settings. GPU load from other applications affects results. |

## Boundaries

- The addon does not support D3D12, Vulkan, OpenGL, D3D9 or BGRA sources. The external viewer instead receives Windows BGRA8 capture and explicitly converts it to RGBA8. HDR, multi-GPU transfers, x86 and AMD/Intel neural inference remain unsupported/unvalidated.
- RGBA8 MSAA resolve is implemented; the tested game source is single-sample. MSAA game coverage remains pending.
- Ordinary ReShade effects can be reordered after the companion techniques. Other effects that change semantics/color space or render before capture require separate compatibility tests.
- Live-game installation, restart, frame pacing, alt-tab/minimize, 30-minute gameplay, and input latency remain release-readiness checks. The hidden harness verifies resize and clean shutdown but does not substitute for those checks.
- No online or anti-cheat compatibility promise. No game executable patches or protection bypasses are part of this project.
