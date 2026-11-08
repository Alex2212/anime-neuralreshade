#include "interop/frame_bridge.hpp"
#include <vector>
#include <iostream>
#include <cstring>
int main(){try{
    cel::ComPtr<ID3D11Device> d;cel::ComPtr<ID3D11DeviceContext> c;
    cel::check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c),"Create GPU device");
    cel::FrameBridge bridge;
    for(auto width:{16u,257u,1280u,64u}){
        UINT height=width/2;std::vector<uint32_t> pixels(size_t(width)*height);
        for(size_t i=0;i<pixels.size();++i)pixels[i]=uint32_t((i*1664525u+1013904223u));
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};cel::ComPtr<ID3D11Texture2D> src,read;
        cel::check(d->CreateTexture2D(&desc,&data,&src),"Create source");
        bridge.initialize(d.Get(),desc);bridge.capture(c.Get(),src.Get());bridge.identity(c.Get());
        desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        cel::check(d->CreateTexture2D(&desc,nullptr,&read),"Create readback");c->CopyResource(read.Get(),bridge.output());
        D3D11_MAPPED_SUBRESOURCE map{};cel::check(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map),"Readback");
        bool same=true;for(UINT y=0;y<height;++y)same&=memcmp(static_cast<char*>(map.pData)+y*map.RowPitch,pixels.data()+size_t(y)*width,width*4)==0;
        c->Unmap(read.Get(),0);if(!same)throw std::runtime_error("RGBA identity mismatch");
        std::cout<<"PASS exact RGBA identity "<<width<<'x'<<height<<" after resource recreation\n";
    }
    auto bad=D3D11_TEXTURE2D_DESC{};bad.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    bool rejected=false;try{bridge.initialize(d.Get(),bad);}catch(...){rejected=true;}
    if(!rejected)throw std::runtime_error("Unsupported HDR format accepted");
    std::cout<<"PASS unsupported format rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
