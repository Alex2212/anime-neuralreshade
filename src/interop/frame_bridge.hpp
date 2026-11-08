#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>
namespace cel {
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
inline void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " HRESULT=" + std::to_string(static_cast<unsigned>(hr)));
}
// Owns capture/output independently of the swapchain. No backbuffer is retained.
class FrameBridge {
public:
    void initialize(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source);
    void capture(ID3D11DeviceContext* context, ID3D11Texture2D* source);
    void identity(ID3D11DeviceContext* context);
    bool matches(const D3D11_TEXTURE2D_DESC& desc) const;
    ID3D11Texture2D* input() const { return input_.Get(); }
    ID3D11Texture2D* output() const { return output_.Get(); }
    ID3D11ShaderResourceView* original_view() const { return original_view_.Get(); }
    ID3D11ShaderResourceView* output_view() const { return output_view_.Get(); }
    UINT width() const { return desc_.Width; }
    UINT height() const { return desc_.Height; }
    size_t bytes() const { return size_t(width()) * height() * 8; }
private:
    D3D11_TEXTURE2D_DESC desc_{};
    ComPtr<ID3D11Texture2D> input_, output_;
    ComPtr<ID3D11ShaderResourceView> original_view_, output_view_;
};
}
