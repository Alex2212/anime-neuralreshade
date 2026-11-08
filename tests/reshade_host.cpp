#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <reshade.hpp>
#include <cmath>
using Microsoft::WRL::ComPtr;
int current_frame=0,verified=0,failures=0,ready_frames=0;
bool require_identity=true,require_bypass=false;
void verify(reshade::api::effect_runtime* runtime,reshade::api::command_list* command,reshade::api::resource_view rtv,reshade::api::resource_view){
    auto flag=runtime->find_uniform_variable("CelShade.fx","CelNeuralReady");bool ready=false;
    if(flag.handle)runtime->get_uniform_value_bool(flag,&ready,1);
    if(ready)++ready_frames;
    if(!ready&&!require_bypass)return;
    if(ready&&require_bypass){++failures;return;}
    if(current_frame%10!=0)return;
    auto* tex=reinterpret_cast<ID3D11Texture2D*>(runtime->get_device()->get_resource_from_view(rtv).handle);
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(command->get_native());
    ComPtr<ID3D11Device> device;tex->GetDevice(&device);
    D3D11_TEXTURE2D_DESC desc{};tex->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))){++failures;return;}
    context->CopyResource(staging.Get(),tex);D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))){++failures;return;}
    auto* p=static_cast<unsigned char*>(mapped.pData)+mapped.RowPitch*(desc.Height/2)+(desc.Width/2)*4;
    const int expected[]={current_frame%2?179:51,64,26,255};bool match=true;
    if(require_identity||require_bypass)for(int k=0;k<4;++k)if(std::abs(int(p[k])-expected[k])>1)match=false;
    if(!match){++failures;std::cerr<<"Frame "<<current_frame<<" pixel "<<int(p[0])<<','<<int(p[1])<<','<<int(p[2])<<','<<int(p[3])<<" mismatch\n";}
    context->Unmap(staging.Get(),0);++verified;
}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
int wmain(int argc,wchar_t** argv){
    // Dedicated hidden test application, never injects into the running game.
    auto reshade=LoadLibraryW((std::filesystem::current_path()/L"dxgi.dll").c_str());
    if(!reshade){std::cerr<<"Local ReShade dxgi.dll unavailable\n";return 1;}
    bool identity_check=argc>1;
    require_identity=argc>1&&wcscmp(argv[1],L"--verify-identity")==0;
    require_bypass=argc>1&&wcscmp(argv[1],L"--verify-bypass")==0;
    if(identity_check){if(!reshade::register_addon(GetModuleHandleW(nullptr),reshade))return 4;
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(verify);}
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"CelShadeGpuHost";
    RegisterClassW(&wc);HWND window=CreateWindowW(wc.lpszClassName,L"CelShade test host",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,wc.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=640;desc.BufferDesc.Height=360;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;ComPtr<IDXGISwapChain> swap;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&ctx);
    if(FAILED(hr)){std::cerr<<"Create swapchain failed\n";return 1;}
    for(int i=0;i<240;++i){
        current_frame=i;
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(i==90||i==160){ctx->ClearState();hr=swap->ResizeBuffers(1,i==90?800:640,i==90?448:360,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return 2;}
        ComPtr<ID3D11Texture2D> buffer;swap->GetBuffer(0,IID_PPV_ARGS(&buffer));ComPtr<ID3D11RenderTargetView> rtv;
        device->CreateRenderTargetView(buffer.Get(),nullptr,&rtv);float color[4]={i%2?.7f:.2f,.25f,.1f,1};
        ctx->ClearRenderTargetView(rtv.Get(),color);hr=swap->Present(0,0);if(FAILED(hr))return 3;
        Sleep(16);
    }
    ctx->ClearState();ctx->Flush();swap.Reset();ctx.Reset();device.Reset();DestroyWindow(window);
    if(identity_check){reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(verify);reshade::unregister_addon(GetModuleHandleW(nullptr),reshade);
        std::cout<<"Verified "<<verified<<" same-frame output samples; "<<ready_frames<<" ready frames; failures "<<failures<<'\n';
        std::ofstream("harness-counts.txt")<<"samples="<<verified<<" ready_frames="<<ready_frames<<" failures="<<failures<<"\n";
        if(failures||verified<10)return 5;}
    std::cout<<"PASS 240 ReShade presents, two resizes and clean device destruction\n";return 0;
}
