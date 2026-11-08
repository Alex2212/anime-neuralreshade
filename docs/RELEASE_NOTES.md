# Anime Neural ReShade 0.1.0 preview 1

Native C++ anime styling with **two independent front ends**: the normal ReShade addon for launches that permit full addons, and an external Windows capture/viewer for use without injecting into a game. Both use locally prepared White-box Cartoonization or AnimeGANv2 profiles with the shared NVIDIA TensorRT backend.

The external `anime_viewer.exe` captures a selected window without installing a DLL into the game. It includes model selection, neural blend, DBZ finishing, split comparison, F10 bypass and an optional F9 click-through overlay. Keep it outside the game folder; ReShade is not required. Finishing-only mode needs no NVIDIA runtime. Use `scripts/setup_neural.ps1` to prepare the runtime and both models locally.

**IMPORTANT:** Xenoverse 2's EAC rejected the tested full-addon ReShade DLL. That local installation was rolled back and original files verified. The external viewer avoids that DLL-loading path, but **actual EAC-protected Xenoverse capture and manual overlay behavior remain unverified**. Windows or a game can block capture. This is a development preview, with no anti-cheat bypass or universal compatibility promise.

## Downloads

- `anime-neuralreshade-0.1.0-preview.1-windows-x64.zip`: external viewer, addon/backend binaries, compiled CUDA kernels, FX, DBZ preset, setup/test scripts, notices and documentation. **Recommended download.**
- `CelShade.addon64`: standalone x64 addon; neural processing additionally needs the matching backend, kernels, NVIDIA dependencies and local engines. No x86 binary is available.
- ZIP SHA-256 file; the ZIP also contains per-file `SHA256SUMS.txt`.

ReShade, NVIDIA runtime DLLs, model weights and serialized engines are **not bundled**. Follow [BUILDING.md](https://github.com/Alex2212/anime-neuralreshade/blob/main/BUILDING.md) / `scripts/setup_neural.ps1` to download pinned dependencies and prepare engines on your GPU. The model projects have noncommercial restrictions; review [THIRD_PARTY_NOTICES.md](https://github.com/Alex2212/anime-neuralreshade/blob/main/THIRD_PARTY_NOTICES.md). Runtime is C++ only; Python 3.11 is used for offline preparation.

## Evidence and limits

- External real Windows capture: finishing-only and both models each passed 100 frames, resize, 12 bypass checks, 88 styled checks, and shutdown. BGRA/RGBA color order was checked from GPU readback.
- Viewer frame-work p95 on an animated 640x360/800x448 source with RTX 5090: finishing-only 1.09 ms, White-box 720p FP16 3.57 ms, AnimeGAN 720p FP16 13.04 ms. These are not gameplay or end-to-end latency measurements.
- Exact RGBA GPU identity tests across sizes; native model output separately checked against ONNX CPU.
- ReShade/CUDA identity: 21 current-frame samples, zero mismatches, two resizes. Neural harness: 188 ready frames. Missing-engine harness: 21 original-image checks passed.
- RTX 5090 static 1440p output processing p95: White-box FP16 720p 3.82 ms, 1080p 5.69 ms; AnimeGAN FP16 720p 14.83 ms. Excludes game rendering, FX and presentation.
- Xenoverse combat comparison images and a red IMPORTANT system-requirements section are in the README.

Manual UI automation was stopped by the user; overlay acceptance remains pending. Live-game visual acceptance, input latency, 30-minute stability and broader hardware coverage remain pending. DLSS, Frame Generation, temporal stabilization and automatic HUD separation are not implemented.
