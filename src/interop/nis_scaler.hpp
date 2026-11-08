#pragma once
#include "frame_bridge.hpp"
namespace cel {
// Own-context D3D11 integration for the external viewer. It unbinds its CS resources.
class NisScaler {
public:
    void initialize(ID3D11Device* device);
    void resize(UINT width,UINT height);
    // Input is the actual model grid, not a previously enlarged texture.
    // False means outside NIS's supported 1x-2x scale range; caller labels fallback.
    bool process(ID3D11DeviceContext* context,ID3D11ShaderResourceView* input,UINT model_width,UINT model_height,float sharpness=0.f);
    ID3D11ShaderResourceView* view() const {return output_view_.Get();}
    ID3D11Texture2D* texture() const {return output_.Get();}
private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11ComputeShader> shader_;
    ComPtr<ID3D11Buffer> config_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Texture2D> coefficients_[2],output_;
    ComPtr<ID3D11ShaderResourceView> coefficient_views_[2],output_view_;
    ComPtr<ID3D11UnorderedAccessView> output_uav_;
    UINT width_{},height_{};
};
}
