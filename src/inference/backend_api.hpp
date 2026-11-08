#pragma once
#include <d3d11.h>
#include <cstdint>
namespace cel {
inline constexpr uint32_t backend_abi = 1;
struct BackendConfig {
    uint32_t abi = backend_abi;
    ID3D11Device* device = nullptr;
    const wchar_t* engine_path = nullptr;
    const wchar_t* kernel_path = nullptr;
    int width = 1280, height = 720;
    bool bgr = false;
    bool identity = false;
};
struct Timings { float preprocess_ms, inference_ms, postprocess_ms, total_ms; uint64_t allocated_bytes; };
struct BackendApi {
    uint32_t abi;
    void* (*create)(const BackendConfig*, char*, uint32_t);
    void (*destroy)(void*);
    bool (*bind)(void*, ID3D11Texture2D*, ID3D11Texture2D*, char*, uint32_t);
    bool (*process)(void*, Timings*, char*, uint32_t);
};
using GetBackendApi = const BackendApi* (*)();
// Optional ABI-1 extension: export the model grid BEFORE display-size interpolation.
// Existing addons keep using bind/process unchanged. Rebind this after each base bind.
using BindModelOutput = bool (*)(void*, ID3D11Texture2D*, char*, uint32_t);
}
