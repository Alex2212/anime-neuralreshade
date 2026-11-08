# Local integration changes

Pinned NVIDIA Image Scaling SDK 1.0.3 revision is in REVISION.txt; upstream MIT terms are in notices/NVIDIA-Image-Scaling-LICENSE.txt.

NIS_Scaler.h: convert the signed tile origin and unsigned local coordinates to float separately before adding them. Under FXC cs_5_0, the original mixed signed/unsigned sum wraps negative support coordinates at the top/left image boundary before clamp sampling. This produced red=11 instead of red=20 in a flat test region. Explicit casts preserve negative coordinates for the clamp sampler. The rest of the algorithm is unchanged.

The viewer embeds the shader and uses FP32 coefficients, an SDR RGBA8 input/output, and the NVIDIA_Generic 32x24/128-thread layout. Regression coverage checks flat border colors, diagonal reconstruction, all output pixels, odd sizes, unsupported scales, and exact native copying.
