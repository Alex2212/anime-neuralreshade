#include "nis_scaler.hpp"
#include "NIS_Config.h"
#include "nis_shader.hpp"
#include <d3dcompiler.h>
namespace cel {
void NisScaler::initialize(ID3D11Device* device) {
    if(device_)return;device_=device;
    D3D_SHADER_MACRO macros[]={{"NIS_BLOCK_WIDTH","32"},{"NIS_BLOCK_HEIGHT","24"},{"NIS_THREAD_GROUP_SIZE","128"},{"NIS_VIEWPORT_SUPPORT","1"},{"NIS_CLAMP_OUTPUT","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,errors;
    HRESULT hr=D3DCompile(nis_shader_source,sizeof(nis_shader_source),"NVIDIA Image Scaling",macros,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr))throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):"NIS shader compilation failed");
    check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader_),"NIS compute shader");
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(NISConfig);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&cb,nullptr,&config_),"NIS constants");
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sampler,&sampler_),"NIS sampler");
    D3D11_TEXTURE2D_DESC d{};d.Width=UINT(kFilterSize/4);d.Height=UINT(kPhaseCount);d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    for(int i=0;i<2;++i){D3D11_SUBRESOURCE_DATA data{i==0?coef_scale:coef_usm,UINT(kFilterSize*sizeof(float)),0};
        check(device->CreateTexture2D(&d,&data,&coefficients_[i]),"NIS coefficients");check(device->CreateShaderResourceView(coefficients_[i].Get(),nullptr,&coefficient_views_[i]),"NIS coefficient view");}
}
void NisScaler::resize(UINT width,UINT height) {
    if(width==width_&&height==height_&&output_)return;
    output_uav_.Reset();output_view_.Reset();output_.Reset();width_=width;height_=height;
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
    check(device_->CreateTexture2D(&d,nullptr,&output_),"NIS output texture");check(device_->CreateShaderResourceView(output_.Get(),nullptr,&output_view_),"NIS output view");check(device_->CreateUnorderedAccessView(output_.Get(),nullptr,&output_uav_),"NIS output UAV");
}
bool NisScaler::process(ID3D11DeviceContext* c,ID3D11ShaderResourceView* input,UINT mw,UINT mh,float sharpness) {
    if(!input||!width_||!height_)return false;
    if(mw==width_&&mh==height_){ComPtr<ID3D11Resource> source;input->GetResource(&source);c->CopyResource(output_.Get(),source.Get());return true;}
    float fit=std::min(float(mw)/width_,float(mh)/height_);
    UINT crop_w=std::clamp(UINT(std::lround(width_*fit)),1u,mw),crop_h=std::clamp(UINT(std::lround(height_*fit)),1u,mh);
    NISConfig config{};
    if(!NVScalerUpdateConfig(config,sharpness,(mw-crop_w)/2,(mh-crop_h)/2,crop_w,crop_h,mw,mh,0,0,width_,height_,width_,height_))return false;
    c->UpdateSubresource(config_.Get(),0,nullptr,&config,0,0);
    ID3D11ShaderResourceView* views[]={input,coefficient_views_[0].Get(),coefficient_views_[1].Get()};ID3D11UnorderedAccessView* uav=output_uav_.Get();ID3D11Buffer* cb=config_.Get();ID3D11SamplerState* sampler=sampler_.Get();
    c->CSSetShader(shader_.Get(),nullptr,0);c->CSSetConstantBuffers(0,1,&cb);c->CSSetShaderResources(0,3,views);c->CSSetSamplers(0,1,&sampler);c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    c->Dispatch((width_+31)/32,(height_+23)/24,1);
    ID3D11ShaderResourceView* no_views[3]{};ID3D11UnorderedAccessView* no_uav{};ID3D11Buffer* no_cb{};ID3D11SamplerState* no_sampler{};
    c->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);c->CSSetShaderResources(0,3,no_views);c->CSSetConstantBuffers(0,1,&no_cb);c->CSSetSamplers(0,1,&no_sampler);c->CSSetShader(nullptr,nullptr,0);
    return true;
}
}
