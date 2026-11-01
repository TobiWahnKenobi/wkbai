# AMD FidelityFX FSR 3.1 upscaler shaders

The shader sources of FSR 3.1's upscaler, from the [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)
1.1.4 (tag `v1.1.4`, commit `c6efa6b`; the same shaders as SDK 2.4.0's FSR 3.1.5), under AMD's MIT licence (`LICENSE.txt`).
`gfx/DiligentFsr3.cpp` runs them on Diligent in place of the SDK's host code and backends.

- `passes/`: the SDK's DX12 pass files (`sdk/src/backends/dx12/shaders/fsr3upscaler/`), all but the
  debug view and the generated reactive mask (wkbai makes its own, `gfx/DiligentPostProcess.cpp`).
- `gpu/`: every header they include (`sdk/include/FidelityFX/gpu/`), without the CPU and GLSL
  cores, which the HLSL build never includes.
- CMake embeds both folders in the exe (`FidelityFXShaders.inc`).

Changed for FXC (Diligent's D3D12 shader compiler, shader model 5), marked `wkbai:`:

- `gpu/fsr3upscaler/ffx_fsr3upscaler_accumulate.h`: `Accumulate` zero-initialises its two
  structures, as FXC refuses `inout` parameters it can't see fully initialised.

The rest is as in the SDK; `FFX_SPD_NO_WAVE_OPERATIONS` and `FFX_HALF 0` keep the passes within
shader model 5.
