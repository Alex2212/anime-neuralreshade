#include "frame_bridge.hpp"
namespace cel {
void FrameBridge::initialize(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source) {
    if (source.Format != DXGI_FORMAT_R8G8B8A8_UNORM && source.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        source.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS)
        throw std::runtime_error("Only SDR RGBA8 is supported; bypassing unsupported source format");
    if (!source.Width || !source.Height || source.ArraySize != 1 || source.MipLevels != 1)
        throw std::runtime_error("Unsupported source dimensions or array");
    FrameBridge next;
    next.desc_ = source;
    auto d = source;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc = {1, 0};
    d.Usage = D3D11_USAGE_DEFAULT; d.CPUAccessFlags = 0; d.MiscFlags = 0;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    check(device->CreateTexture2D(&d, nullptr, &next.input_), "Create capture texture");
    check(device->CreateTexture2D(&d, nullptr, &next.output_), "Create output texture");
    check(device->CreateShaderResourceView(next.input_.Get(), nullptr, &next.original_view_), "Create original SRV");
    check(device->CreateShaderResourceView(next.output_.Get(), nullptr, &next.output_view_), "Create output SRV");
    *this = std::move(next);
}
bool FrameBridge::matches(const D3D11_TEXTURE2D_DESC& d) const {
    return input_ && d.Width == desc_.Width && d.Height == desc_.Height && d.Format == desc_.Format &&
        d.SampleDesc.Count == desc_.SampleDesc.Count && d.SampleDesc.Quality == desc_.SampleDesc.Quality;
}
void FrameBridge::capture(ID3D11DeviceContext* context, ID3D11Texture2D* source) {
    if (desc_.SampleDesc.Count > 1)
        context->ResolveSubresource(input_.Get(), 0, source, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    else context->CopyResource(input_.Get(), source);
}
void FrameBridge::identity(ID3D11DeviceContext* context) { context->CopyResource(output_.Get(), input_.Get()); }
}
