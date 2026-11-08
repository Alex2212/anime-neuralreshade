# Third-party components and model terms

This repository's original addon/FX code is separate from upstream SDKs, models, and generated engines. No ownership of Dragon Ball or Xenoverse imagery is claimed. README game screenshots were authorized by the project owner for project documentation.

| Component | Source / revision | Handling |
|---|---|---|
| NVIDIA Image Scaling SDK 1.0.3 | [NVIDIAGameWorks/NVIDIAImageScaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling), `35e13ba316c98eeecf16f37eae70ce88019911f6` | MIT. Shader/config sources are vendored in `third_party/nis`, with documented local signed-coordinate correction. The included NVIDIA-Image-Scaling-LICENSE notice accompanies binaries. No DLSS SDK/runtime is included. |
| ReShade API | [crosire/reshade](https://github.com/crosire/reshade), v6.1.1, `a59d611903db84355d562a2cb6da20d4d2761619` | Header notices state BSD-3-Clause OR MIT. Included notice text accompanies compiled addon releases. ReShade itself is downloaded separately. |
| Dear ImGui | [ocornut/imgui](https://github.com/ocornut/imgui), `c6aa051629753f0ef0d26bf775a8b6a92aa213b2` | MIT. Matching header is used through ReShade's function table. Included notice accompanies binaries. |
| AnimeGANv2 | [official repository](https://github.com/TachibanaYoshino/AnimeGANv2), `724b5ce75185c2e838d30c9077dbb967ab034254` | Upstream README restricts use to non-commercial purposes and gives a separate commercial-authorization route. This is not blanket approval to redistribute weights/conversions. Download for local evaluation; do not bundle weights or engines in public releases. |
| White-box Cartoonization | [official repository](https://github.com/SystemErrorWang/White-box-Cartoonization), `ee97277d7fd873b89e9b8e1341efe74e10dd4124` | Upstream states CC BY-NC-SA 4.0 and prohibits commercial application. Preserve upstream terms. The export uses the official generator AND guided filter. Weights and converted engines are excluded from public releases. |
| NVIDIA TensorRT 10.13.3.9 | [NVIDIA documentation](https://docs.nvidia.com/deeplearning/tensorrt/) | SDK/runtime governed by NVIDIA terms. Download from NVIDIA; runtime binaries are not redistributed in the release. |
| NVIDIA CUDA runtime/NVRTC | [CUDA documentation](https://docs.nvidia.com/cuda/) | Pinned CUDA 12.9 packages from NVIDIA's PyPI distribution. Libraries are downloaded separately; public release contains only this project's compiled kernels. |
| Offline conversion dependencies | TensorFlow CPU, tf2onnx, ONNX Runtime CPU, tf_slim, NumPy, Pillow, OpenCV | Used locally for model export/reference checks and documentation images. No Python service is used in game. Their package licenses remain with their distributions. |

Model manifests record upstream revisions, checkpoint and ONNX SHA-256 values, tensor/color contracts, and conversion parity. Training is not part of this release. Separate model downloads do not remove model license obligations. Review terms again before commercial use or any future weight/engine redistribution.
