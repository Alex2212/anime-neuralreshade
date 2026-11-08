# Building Anime Neural ReShade

## Native addon and external viewer

Windows x64 with MSVC C++20, Windows SDK including C++/WinRT headers (tested 10.0.26100.0), CMake and Ninja (Visual Studio Build Tools' bundled versions work). Python 3.11 and Git are needed for offline setup, not during gameplay. The viewer links the Windows system libraries and embeds its D3D11 finishing shader; it requires no WinUI, .NET, or ReShade runtime.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
powershell -ExecutionPolicy Bypass -File scripts/stage_local.ps1
```

`bootstrap.ps1` uses pinned revisions and SHA-256-verified packages. It keeps SDKs under `external` and conversion dependencies under `.venv`; it does not downgrade a system Python/CUDA installation or install into a game. The TensorRT SDK download is about 1.74 GB. MSVC is located with `vswhere`. Configure/build/test presets are in `CMakePresets.json`.

The staged `dist/local` is a **private local test package** containing separately downloaded runtime dependencies. It is not the public release payload.

## Prepare models and engines

For a downloaded binary release, use `scripts/setup_neural.ps1 -Python <Python311.exe>`. This fetches dependencies locally, converts both official models, builds mixed-FP16 720p engine profiles for the local GPU, and places the runtime DLLs and cache beside the addon. It needs no C++ compiler. First-time preparation is expensive and should be done while the game is closed. Source builds may use the individual steps:

The setup script also accepts `-Height 1080` or `-Height 1440` and `-Model whitebox`, `-Model animegan`, or `-Model both` (default). For a 2560x1440 source, a 2560x1440 model avoids the neural downsample/upscale step. More pixels cost more GPU work; this is a quality option, not a guaranteed 60 FPS mode. To prepare only White-box at native 1440p:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup_neural.ps1 -Height 1440 -Model whitebox -Python <Python311.exe>
```

Keep existing lower-resolution profiles for comparison. In the running viewer, click Refresh, reselect the game window, choose `whitebox | 2560x1440 | fp16`, then Start. Refresh repopulates the selectors, so verify the source and profile before starting. The same cached engine can be selected in the normal addon's model controls.

```powershell
.venv\Scripts\python.exe scripts/prepare_models.py whitebox --width 1280 --height 720 --image docs/images/original.png
.venv\Scripts\python.exe scripts/prepare_models.py animegan --width 1280 --height 720 --image docs/images/original.png
powershell -ExecutionPolicy Bypass -File scripts/build_engine.ps1 -Model models/whitebox-1280x720.onnx -Precision fp32
powershell -ExecutionPolicy Bypass -File scripts/build_engine.ps1 -Model models/whitebox-1280x720.onnx -Precision fp16
powershell -ExecutionPolicy Bypass -File scripts/build_engine.ps1 -Model models/animegan-1280x720.onnx -Precision fp16
```

White-box export restores the official checkpoint and includes the guided filter (`r=1`, `eps=0.005`). TensorFlow 1.x symbols are mapped to `compat.v1` and `tf_slim` in memory; downloaded upstream files are not modified. AnimeGAN exports the official frozen Shinkai model, with optional brightness adjustment disabled as supported by its official inference script.

Each export compares against official TensorFlow inference. Normalized-value tolerances are max 0.01 and mean 0.002 (1.275 and 0.255 8-bit channel levels respectively). The original tighter mean threshold of 0.001 flagged the Shinkai conversion at ~0.0011; that result and the adopted explicit image-level tolerance are documented rather than hidden. Actual metrics are in `models/*.json`.

TensorRT 10.13.3 supports the used weakly typed `--fp16` build option; external I/O stays FP32. FP32 builds disable TF32. Cache identity includes ONNX hash (which includes fixed shape), precision, runtime, GPU UUID/name/compute capability/driver, and build flags. Runtime additionally validates engine SHA-256, matching GPU, dimensions, color order, and runtime. Never rename a foreign serialized engine into place and expect portability.

## Validation

`build.ps1 -Test` runs exact RGBA GPU capture/copy comparisons at changing resolutions and unsupported-format rejection.

It also runs `anime_viewer.exe --test-quality` without displaying windows. This compiles and executes the actual viewer shaders at 128x96, 512x288 and 2560x1440. It verifies softened diagonal edges, unchanged flat colors/straight edges, exact bypass, unchanged original split half, and exact original-pixel restoration inside HUD masks with AA on and off (unchanged output outside). Reports are under `build/viewer-edge-quality.json`.

`anime_viewer.exe --test-image input.png --engine cache/<engine>.plan --report evaluation/viewer-image.json` uses the same GPU inference/composite shaders on a static image and writes PNG comparisons with AA off, AA on and the default top HUD region restored. It is an offline diagnostic, not a live capture or latency measurement. Runtime dependencies must be beside that executable.

Preview 3 also tests NIS 720p/1080p to 1440p, odd dimensions, flat border colors, full output coverage, unsupported scale rejection and exact native copying. Image diagnostics additionally save the raw model grid and a bilinear reference with the same finishing settings. For a native-sized source/model pair, the new grid must exactly match the legacy output, checking channel order and quantization. Keep the viewer, backend and PTX from the same release; the new viewer explicitly rejects a backend/PTX lacking native-grid support.

Preview 4 adds ramp/colored-row checks for two HSV-value bands, threshold movement, near-black ink preservation and exact bypass. Image diagnostics write a `-legacy-tones.png` comparison as well as the hard-tone output. AA can introduce intermediate values at boundaries; the band test measures the finishing pass before AA.

`cel_benchmark.exe input.png output.png engine.plan width height rgb|bgr iterations backend_directory` runs a native GPU-resident capture/inference/output path. Use `identity` instead of an engine to test CUDA preprocessing/upscaling without a model. CPU input loading and final PNG readback are outside timing; this is not a game benchmark. A JSON report is written beside the PNG.

`scripts/test_harness.ps1` uses a separately downloaded full-addon ReShade DLL in an isolated hidden D3D11 process. It verifies the addon is actually loaded, FX compilation, same-frame alternating-color output, two resizes, and clean shutdown. Diagnostic readbacks occur only in the test executable.

`scripts/test_viewer.ps1` launches `dist/local/anime_viewer.exe --self-test`. It creates its own visible animated source and output windows, captures with real Windows Graphics Capture, checks red/blue channel ordering and exact bypass patches, resizes the source, verifies styling, then exits after 100 frames. Add `-Engine cache/<engine>.plan` and `-Report build/viewer-model.json` to exercise either TensorRT model. It needs an interactive desktop and access to Windows capture services; a restricted execution sandbox may reject WinRT activation. Test-only CPU readbacks occur after frame-work timing. This is not a gameplay or input-latency test.

`anime_viewer.exe --demo` runs the same animated source interactively without exiting automatically. Capture runs on a dedicated worker; the UI remains responsive during model initialization. Stop/restart waits for the worker to finish its current operation. Runtime profiles are scanned from `cache` beside the executable. Both model paths share the same backend/contract checks as the addon.

Use `scripts/package.ps1` to assemble the public preview ZIP. Its allowlist excludes NVIDIA/ReShade binaries, weights, serialized engines, logs, personal local paths, and private evaluation data. The release contains setup scripts and documentation so dependencies can be acquired locally.

`package.ps1 -RuntimeStage dist/local-preview3` and `test_harness.ps1 -RuntimeStage dist/local-preview3` can use a separate private staging directory while an older viewer is running. Compile the current `kernels.cu` into that directory and keep all staged components consistent. Never overwrite a loaded backend DLL.
