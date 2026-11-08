// External window capture. This executable never opens or modifies a game process.
#include "interop/frame_bridge.hpp"
#include "inference/backend_api.hpp"
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using cel::ComPtr;
using cel::check;
using Clock = std::chrono::steady_clock;

namespace {
constexpr UINT refresh_timer = 1;
constexpr int source_id=101, profile_id=102, refresh_id=103, start_id=104, stop_id=105,
    bypass_id=106, finish_id=107, split_id=108, overlay_id=109, strength_id=110, aa_id=111, hud_id=112, hud_apply_id=113;
const wchar_t* window_class = L"AnimeNeuralViewer";
fs::path base;
HWND controls{}, viewer{}, source_combo{}, profile_combo{}, status_label{}, strength_combo{}, test_source{};
HWND bypass_box{}, finish_box{}, split_box{}, overlay_box{}, aa_box{};
HWND hud_box{}, hud_region_boxes[3]{}, hud_edits[3][4]{};
std::atomic<bool> stop_requested{false}, bypass{false}, finishing{true}, split{false}, running{false};
std::atomic<float> strength{0.85f};
std::atomic<bool> edge_aa{true};
std::atomic<bool> preserve_hud{false};
using Rectangles=std::array<std::array<float,4>,3>;
std::mutex hud_mutex;
Rectangles hud_regions{{{0,0,1,.19f},{0,0,0,0},{0,0,0,0}}};
bool overlay=false, self_test=false, demo=false, quality_test=false;
fs::path report_path=L"viewer-self-test.json";
fs::path image_test_path;
std::thread worker;
HWND selected_source{};
unsigned test_tick=0;
std::mutex status_mutex;
std::wstring status_text=L"Select a window and a prepared model, then Start. F10 bypasses; F9 hides the overlay.";
std::atomic<unsigned> tested_frames{0};
std::atomic<ULONGLONG> last_capture_tick{0};
std::atomic<int> test_result{1};

std::wstring widen(const std::string& s) { return winrt::to_hstring(s).c_str(); }
void status(std::wstring s) { std::lock_guard lock(status_mutex); status_text=std::move(s); }
std::wstring ini(const fs::path& p,const wchar_t* key,const wchar_t* fallback=L"") {
    wchar_t value[1024]{}; GetPrivateProfileStringW(L"Engine",key,fallback,value,1024,p.c_str());return value;
}
struct Profile { fs::path engine; std::wstring label; int width=1280,height=720; bool bgr=false; };
std::vector<Profile> profiles;
Profile load_profile(const fs::path& path) {
    Profile p; p.engine=fs::absolute(path); auto contract=fs::path(p.engine.wstring()+L".ini");
    p.width=GetPrivateProfileIntW(L"Engine",L"Width",0,contract.c_str());
    p.height=GetPrivateProfileIntW(L"Engine",L"Height",0,contract.c_str());
    if(p.width<16||p.height<16||p.width>8192||p.height>8192) throw std::runtime_error("Invalid or missing engine dimensions in .plan.ini");
    p.bgr=ini(contract,L"Color")==L"BGR";
    p.label=ini(contract,L"Name",L"Model")+L" | "+std::to_wstring(p.width)+L"x"+std::to_wstring(p.height)+L" | "+ini(contract,L"Precision");
    return p;
}
void refresh_profiles() {
    SendMessageW(profile_combo,CB_RESETCONTENT,0,0); profiles.clear(); profiles.push_back({{},L"Finishing only (no NVIDIA runtime needed)"});
    try { if(fs::exists(base/L"cache")) for(const auto& entry:fs::directory_iterator(base/L"cache")) {
        if(entry.path().extension()!=L".plan") continue;
        try {profiles.push_back(load_profile(entry.path()));} catch(const std::exception&) {}
    }} catch(const std::exception& e) {status(L"Profile scan: "+widen(e.what()));}
    for(const auto& p:profiles) SendMessageW(profile_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.label.c_str()));
    SendMessageW(profile_combo,CB_SETCURSEL,0,0);
}
BOOL CALLBACK enumerate_window(HWND h,LPARAM) {
    DWORD pid{};GetWindowThreadProcessId(h,&pid);
    if(!IsWindowVisible(h)||pid==GetCurrentProcessId()||GetWindow(h,GW_OWNER)) return TRUE;
    DWORD cloaked{};DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked) return TRUE;
    wchar_t title[512]{};if(!GetWindowTextW(h,title,512)) return TRUE;
    auto index=SendMessageW(source_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title));
    SendMessageW(source_combo,CB_SETITEMDATA,index,reinterpret_cast<LPARAM>(h));return TRUE;
}
void refresh_sources() {
    SendMessageW(source_combo,CB_RESETCONTENT,0,0);EnumWindows(enumerate_window,0);
    if(IsWindow(test_source)){auto index=SendMessageW(source_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Built-in animated demo"));SendMessageW(source_combo,CB_SETITEMDATA,index,reinterpret_cast<LPARAM>(test_source));SendMessageW(source_combo,CB_SETCURSEL,index,0);}
    else SendMessageW(source_combo,CB_SETCURSEL,0,0);
}

struct Signal { HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr); ~Signal(){if(handle)CloseHandle(handle);} };
struct Capture {
    IDirect3DDevice device{nullptr}; GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr}; GraphicsCaptureSession session{nullptr};
    std::shared_ptr<Signal> signal=std::make_shared<Signal>();
    std::shared_ptr<std::atomic<bool>> closed=std::make_shared<std::atomic<bool>>(false);
    winrt::event_token arrived{},closing{};
    ~Capture() {
        try { if(item)item.Closed(closing);if(pool)pool.FrameArrived(arrived);if(session)session.Close();if(pool)pool.Close(); } catch(...) {}
    }
    void open(HWND source,ID3D11Device* native) {
        if(!GraphicsCaptureSession::IsSupported()) throw std::runtime_error("Windows Graphics Capture is unavailable on this system");
        ComPtr<IDXGIDevice> dxgi;check(native->QueryInterface(IID_PPV_ARGS(&dxgi)),"DXGI device");
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put())); device=inspectable.as<IDirect3DDevice>();
        auto interop=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(source,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(item)));
        pool=Direct3D11CaptureFramePool::CreateFreeThreaded(device,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        arrived=pool.FrameArrived([event=signal](auto&&,auto&&){SetEvent(event->handle);});
        closing=item.Closed([flag=closed,event=signal](auto&&,auto&&){*flag=true;SetEvent(event->handle);});
        session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);session.StartCapture();
    }
};

// BGRA capture is sampled through a typed SRV, then rendered into owned RGBA8.
// No copy between incompatible BGRA/RGBA formats and no CPU readback in normal use.
constexpr char shader[]=R"(
cbuffer Settings : register(b0) { float2 texel; float blend; float stylize; float compare; float aa; float hud; float padding; float4 hudRects[3]; };
Texture2D original : register(t0); Texture2D neural : register(t1); SamplerState linearClamp : register(s0);
struct V { float4 position:SV_Position; float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) { V o; o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o; }
float4 copy(V i):SV_Target { return float4(original.SampleLevel(linearClamp,i.uv,0).rgb,1); }
float luma(float3 c) { return dot(c,float3(.2126,.7152,.0722)); }
float4 finish(V i):SV_Target {
 float3 raw=original.SampleLevel(linearClamp,i.uv,0).rgb;
 if(compare>0.5 && i.uv.x<.5) return float4(raw,1);
 float3 c=lerp(raw,neural.SampleLevel(linearClamp,i.uv,0).rgb,blend);
 if(stylize>0.5) {
  float y=luma(c);float shade=lerp(.66,1.08,smoothstep(.40,.43,y));c*=shade;
  c=lerp(luma(c).xxx,c,1.18);
  float edge=abs(luma(original.SampleLevel(linearClamp,i.uv+float2(texel.x,0),0).rgb)-luma(original.SampleLevel(linearClamp,i.uv-float2(texel.x,0),0).rgb));
  edge+=abs(luma(original.SampleLevel(linearClamp,i.uv+float2(0,texel.y),0).rgb)-luma(original.SampleLevel(linearClamp,i.uv-float2(0,texel.y),0).rgb));
  c*=1-.55*smoothstep(.09,.25,edge);
 }
 return float4(saturate(c),1);
}
// Original conservative spatial edge filter. Only diagonal high-contrast edges
// are averaged along their tangent; flat areas and axis-aligned lines stay intact.
// This is not temporal AA, super resolution, or reconstruction of missing detail.
float4 antialias(V i):SV_Target {
 if(hud>.5) {
  [unroll] for(int r=0;r<3;++r) {
   float4 box=hudRects[r];
   if(box.z>box.x && box.w>box.y && all(i.uv>=box.xy) && all(i.uv<box.zw))
    return float4(neural.SampleLevel(linearClamp,i.uv,0).rgb,1);
  }
 }
 float3 c=original.SampleLevel(linearClamp,i.uv,0).rgb;
 if(aa<.5) return float4(c,1);
 if(compare>0.5 && i.uv.x<.5+texel.x*2) return float4(c,1);
 float nw=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(-1,-1),0).rgb);
 float n=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(0,-1),0).rgb);
 float ne=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(1,-1),0).rgb);
 float w=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(-1,0),0).rgb);
 float e=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(1,0),0).rgb);
 float sw=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(-1,1),0).rgb);
 float s=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(0,1),0).rgb);
 float se=luma(original.SampleLevel(linearClamp,i.uv+texel*float2(1,1),0).rgb);
 float lo=min(luma(c),min(min(n,s),min(e,w))),hi=max(luma(c),max(max(n,s),max(e,w)));
 if(hi-lo<max(.03,hi*.10)) return float4(c,1);
 float2 gradient=float2(ne+2*e+se-nw-2*w-sw,sw+2*s+se-nw-2*n-ne);
 float largest=max(abs(gradient.x),abs(gradient.y));
 if(largest<.001) return float4(c,1);
 float diagonal=min(abs(gradient.x),abs(gradient.y))/largest;
 float amount=.85*smoothstep(.10,.50,diagonal);
 if(amount<.001) return float4(c,1);
 float2 tangent=normalize(float2(-gradient.y,gradient.x))*texel;
 float3 filtered=.35*(original.SampleLevel(linearClamp,i.uv-tangent*.5,0).rgb+original.SampleLevel(linearClamp,i.uv+tangent*.5,0).rgb)
                 +.15*(original.SampleLevel(linearClamp,i.uv-tangent,0).rgb+original.SampleLevel(linearClamp,i.uv+tangent,0).rgb);
 return float4(lerp(c,filtered,amount),1);
})";
struct Constants {float x{},y{},blend{},stylize{},compare{},aa{},hud{},pad{};Rectangles rectangles{};};
struct Graphics {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> copy,finish,antialias;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> captured,composite;ComPtr<ID3D11ShaderResourceView> capture_view,composite_view;
    ComPtr<ID3D11Texture2D> antialiased;ComPtr<ID3D11ShaderResourceView> aa_view;ComPtr<ID3D11RenderTargetView> aa_target;bool aa_active=false;
    ComPtr<ID3D11RenderTargetView> input_target,final_target,back_target;
    cel::FrameBridge bridge;UINT swap_width=0,swap_height=0;
    void initialize(HWND target) {
        ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i) {ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x10de){adapter=a;break;}}
        check(D3D11CreateDevice(adapter.Get(),adapter?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11 device");
        ComPtr<IDXGIFactory2> f2;check(factory.As(&f2),"DXGI factory2");DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width=640;desc.Height=360;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(f2->CreateSwapChainForHwnd(device.Get(),target,&desc,nullptr,nullptr,&swap),"Viewer swapchain");
        factory->MakeWindowAssociation(target,DXGI_MWA_NO_ALT_ENTER);
        ComPtr<IDXGIDevice1> latency;if(SUCCEEDED(device.As(&latency)))latency->SetMaximumFrameLatency(1);
        auto compile=[](const char* entry,const char* model) {ComPtr<ID3DBlob> code,errors;HRESULT hr=D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,entry,model,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);if(FAILED(hr))throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):"Shader compile failed");return code;};
        auto v=compile("vs","vs_5_0"),p=compile("copy","ps_5_0"),f=compile("finish","ps_5_0"),a=compile("antialias","ps_5_0");
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"Vertex shader");
        check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&copy),"Copy shader");
        check(device->CreatePixelShader(f->GetBufferPointer(),f->GetBufferSize(),nullptr,&finish),"Finish shader");
        check(device->CreatePixelShader(a->GetBufferPointer(),a->GetBufferSize(),nullptr,&antialias),"Antialias shader");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd,&sampler),"Sampler");D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(Constants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&bd,nullptr,&constants),"Constants");
    }
    void resize(UINT width,UINT height) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        bridge.initialize(device.Get(),desc);input_target.Reset();final_target.Reset();composite.Reset();composite_view.Reset();captured.Reset();capture_view.Reset();
        check(device->CreateRenderTargetView(bridge.input(),nullptr,&input_target),"Input RTV");
        check(device->CreateTexture2D(&desc,nullptr,&composite),"Composite texture");check(device->CreateRenderTargetView(composite.Get(),nullptr,&final_target),"Composite RTV");
        check(device->CreateShaderResourceView(composite.Get(),nullptr,&composite_view),"Composite SRV");
        aa_target.Reset();aa_view.Reset();antialiased.Reset();aa_active=false;
        check(device->CreateTexture2D(&desc,nullptr,&antialiased),"AA texture");check(device->CreateRenderTargetView(antialiased.Get(),nullptr,&aa_target),"AA RTV");
        check(device->CreateShaderResourceView(antialiased.Get(),nullptr,&aa_view),"AA SRV");
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&desc,nullptr,&captured),"Capture texture");check(device->CreateShaderResourceView(captured.Get(),nullptr,&capture_view),"Capture SRV");
    }
    void draw(ID3D11RenderTargetView* target,ID3D11ShaderResourceView* a,ID3D11ShaderResourceView* b,ID3D11PixelShader* ps,D3D11_VIEWPORT viewport,const Constants& values) {
        context->UpdateSubresource(constants.Get(),0,nullptr,&values,0,0);ID3D11Buffer* cb=constants.Get();context->PSSetConstantBuffers(0,1,&cb);
        context->OMSetRenderTargets(1,&target,nullptr);context->RSSetViewports(1,&viewport);context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps,nullptr,0);ID3D11ShaderResourceView* views[]={a,b};context->PSSetShaderResources(0,2,views);
        ID3D11SamplerState* s=sampler.Get();context->PSSetSamplers(0,1,&s);context->Draw(3,0);
        ID3D11ShaderResourceView* empty[2]{};context->PSSetShaderResources(0,2,empty);context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    D3D11_VIEWPORT full() const {return {0,0,float(bridge.width()),float(bridge.height()),0,1};}
    void capture(ID3D11Texture2D* source) {
        D3D11_BOX box{0,0,0,bridge.width(),bridge.height(),1};context->CopySubresourceRegion(captured.Get(),0,0,0,0,source,0,&box);
        draw(input_target.Get(),capture_view.Get(),nullptr,copy.Get(),full(),{});
    }
    void compose(bool neural,bool raw) {
        Constants c{1.f/bridge.width(),1.f/bridge.height(),neural&&!raw?strength.load():0.f,!raw&&finishing?1.f:0.f,!raw&&split?1.f:0.f};
        c.aa=edge_aa?1.f:0.f;c.hud=preserve_hud&&!raw?1.f:0.f;
        if(c.hud>.5f){std::lock_guard lock(hud_mutex);c.rectangles=hud_regions;}
        draw(final_target.Get(),bridge.original_view(),neural?bridge.output_view():bridge.original_view(),finish.Get(),full(),c);
        aa_active=!raw&&(edge_aa||preserve_hud);
        // Restore native captured HUD pixels after all styling and edge filtering.
        if(aa_active)draw(aa_target.Get(),composite_view.Get(),bridge.original_view(),antialias.Get(),full(),c);
    }
    ID3D11Texture2D* result_texture() const {return aa_active?antialiased.Get():composite.Get();}
    void present(HWND target) {
        RECT rect{};GetClientRect(target,&rect);UINT w=std::max(1L,rect.right),h=std::max(1L,rect.bottom);
        if(w!=swap_width||h!=swap_height) {back_target.Reset();check(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"Resize viewer");ComPtr<ID3D11Texture2D> back;check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Backbuffer");check(device->CreateRenderTargetView(back.Get(),nullptr,&back_target),"Backbuffer RTV");swap_width=w;swap_height=h;}
        const float black[4]={0,0,0,1};context->ClearRenderTargetView(back_target.Get(),black);
        float scale=std::min(float(w)/bridge.width(),float(h)/bridge.height());float rw=bridge.width()*scale,rh=bridge.height()*scale;
        draw(back_target.Get(),aa_active?aa_view.Get():composite_view.Get(),nullptr,copy.Get(),{(w-rw)/2,(h-rh)/2,rw,rh,0,1},{});
        check(swap->Present(0,0),"Present viewer");
    }
    std::vector<uint8_t> read_pixels(ID3D11Texture2D* texture) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"Test staging");context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Test readback");std::vector<uint8_t> pixels(size_t(desc.Width)*desc.Height*4);
        for(UINT y=0;y<desc.Height;++y)memcpy(pixels.data()+size_t(y)*desc.Width*4,static_cast<uint8_t*>(m.pData)+size_t(y)*m.RowPitch,size_t(desc.Width)*4);
        context->Unmap(staging.Get(),0);return pixels;
    }
};
struct Backend {
    HMODULE library{};const cel::BackendApi* api{};void* state{};
    ~Backend(){if(state)api->destroy(state);if(library)FreeLibrary(library);}
    void open(const Profile& p,ID3D11Device* device) {
        if(p.engine.empty())return;
        library=LoadLibraryExW((base/L"cel_nvidia.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library)throw std::runtime_error("NVIDIA backend unavailable (Windows error "+std::to_string(GetLastError())+"). Run scripts/setup_neural.ps1, or select Finishing only.");
        auto get=reinterpret_cast<cel::GetBackendApi>(GetProcAddress(library,"CelGetBackendApi"));
        if(!get||!(api=get())||api->abi!=cel::backend_abi)throw std::runtime_error("NVIDIA backend ABI mismatch");
        auto kernel=base/L"kernels.ptx";cel::BackendConfig config;config.device=device;config.engine_path=p.engine.c_str();config.kernel_path=kernel.c_str();config.width=p.width;config.height=p.height;config.bgr=p.bgr;
        char error[2048]{};state=api->create(&config,error,sizeof(error));if(!state)throw std::runtime_error(error);
    }
    void bind(cel::FrameBridge& bridge) {if(!state)return;char error[2048]{};if(!api->bind(state,bridge.input(),bridge.output(),error,sizeof(error)))throw std::runtime_error(error);}
    cel::Timings process() {cel::Timings t{};if(state){char error[2048]{};if(!api->process(state,&t,error,sizeof(error)))throw std::runtime_error(error);}return t;}
};
void run_quality_test() {
    Graphics g;g.initialize(viewer);finishing=false;edge_aa=true;split=false;
    size_t softened=0,flat_checked=0,hud_checked=0;unsigned sizes=0;
    for(auto dimensions:{std::pair<UINT,UINT>{128,96},{512,288},{2560,1440}}) {
        auto [w,h]=dimensions;g.resize(w,h);++sizes;
        std::vector<uint8_t> input(size_t(w)*h*4);
        auto paint=[&](bool diagonal) {for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){
            bool light=diagonal?float(y)>.15f*h+float(x)*.6f*h/w:x>=w/2;
            size_t p=(size_t(y)*w+x)*4;input[p]=light?220:20;input[p+1]=light?180:40;input[p+2]=light?140:60;input[p+3]=255;
        }g.context->UpdateSubresource(g.bridge.input(),0,nullptr,input.data(),w*4,0);};
        paint(true);g.compose(false,false);auto smooth=g.read_pixels(g.result_texture());
        size_t changed=0;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x) {
            size_t p=(size_t(y)*w+x)*4;bool different=memcmp(input.data()+p,smooth.data()+p,4)!=0;
            if(different)++changed;
            if(std::abs(float(y)-(.15f*h+float(x)*.6f*h/w))>4) {if(different)throw std::runtime_error("AA changed a flat-color pixel");++flat_checked;}
        }
        if(changed<w/4)throw std::runtime_error("AA did not soften the diagonal test edge");softened+=changed;
        split=true;g.compose(false,false);auto comparison=g.read_pixels(g.result_texture());split=false;
        for(UINT y=0;y<h;++y)if(memcmp(input.data()+size_t(y)*w*4,comparison.data()+size_t(y)*w*4,size_t(w/2)*4)!=0)throw std::runtime_error("AA changed the original side of split comparison");
        g.compose(false,true);if(g.read_pixels(g.result_texture())!=input)throw std::runtime_error("AA bypass is not exact");
        edge_aa=false;g.compose(false,false);if(g.read_pixels(g.result_texture())!=input)throw std::runtime_error("Disabled AA is not exact");edge_aa=true;
        paint(false);g.compose(false,false);if(g.read_pixels(g.result_texture())!=input)throw std::runtime_error("AA blurred an axis-aligned edge");
        // Re-enable the real style pass and verify that AA leaves both flat color plateaus intact.
        paint(true);finishing=true;edge_aa=false;g.compose(false,false);auto styled=g.read_pixels(g.result_texture());
        edge_aa=true;g.compose(false,false);auto styled_aa=g.read_pixels(g.result_texture());finishing=false;
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)if(std::abs(float(y)-(.15f*h+float(x)*.6f*h/w))>5){
            size_t p=(size_t(y)*w+x)*4;if(memcmp(styled.data()+p,styled_aa.data()+p,4)!=0)throw std::runtime_error("AA changed the DBZ flat-region colors");
        }
        finishing=true;preserve_hud=true;hud_regions={{{0,0,1,.25f},{.625f,.5f,1,1},{0,0,0,0}}};
        for(bool aa_on:{false,true}) {
            edge_aa=aa_on;g.compose(false,false);auto masked=g.read_pixels(g.result_texture());
            auto& unmasked=aa_on?styled_aa:styled;
            for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x) {
                bool inside=y<h/4||(x>=w*5/8&&y>=h/2);size_t p=(size_t(y)*w+x)*4;
                const auto& expected=inside?input:unmasked;
                if(memcmp(expected.data()+p,masked.data()+p,4)!=0)throw std::runtime_error(inside?"HUD mask did not restore exact original pixels":"HUD mask changed pixels outside its region");
                if(inside)++hud_checked;
            }
        }
        g.compose(false,true);if(g.read_pixels(g.result_texture())!=input)throw std::runtime_error("HUD bypass is not exact");
        preserve_hud=false;edge_aa=true;finishing=false;
    }
    std::ofstream(report_path)<<"{\"passed\":true,\"resource_sizes\":"<<sizes<<",\"diagonal_pixels_softened\":"<<softened<<",\"flat_pixels_unchanged\":"<<flat_checked<<",\"hud_pixels_restored\":"<<hud_checked<<",\"bypass_exact\":true,\"disabled_exact\":true,\"split_original_exact\":true,\"axis_aligned_edges_exact\":true,\"styled_flat_colors_exact\":true,\"hud_outside_exact\":true}\n";
}
void run_image_test(const Profile& profile) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Image test WIC");
    ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromFilename(image_test_path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Image test decode");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"Image test frame");ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter),"Image test converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Image test RGBA");
    UINT w{},h{};check(converter->GetSize(&w,&h),"Image test dimensions");std::vector<uint8_t> pixels(size_t(w)*h*4);check(converter->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()),"Image test pixels");
    Graphics g;g.initialize(viewer);g.resize(w,h);Backend backend;backend.open(profile,g.device.Get());backend.bind(g.bridge);
    g.context->UpdateSubresource(g.bridge.input(),0,nullptr,pixels.data(),w*4,0);if(backend.state)backend.process();
    auto save=[&](const fs::path& path,std::vector<uint8_t> rgba) {
        for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
        ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"Image test stream");check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Image test output");
        ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Image test encoder");check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Image test encoder init");
        ComPtr<IWICBitmapFrameEncode> output;check(encoder->CreateNewFrame(&output,nullptr),"Image test output frame");check(output->Initialize(nullptr),"Image test frame init");check(output->SetSize(w,h),"Image test size");
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check(output->SetPixelFormat(&format),"Image test format");if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder rejected BGRA");
        check(output->WritePixels(h,w*4,UINT(rgba.size()),rgba.data()),"Image test write");check(output->Commit(),"Image test frame commit");check(encoder->Commit(),"Image test encoder commit");
    };
    auto prefix=report_path.parent_path()/report_path.stem();
    edge_aa=false;g.compose(backend.state!=nullptr,false);auto before=g.read_pixels(g.result_texture());save(prefix.wstring()+L"-aa-off.png",before);
    edge_aa=true;g.compose(backend.state!=nullptr,false);auto after=g.read_pixels(g.result_texture());save(prefix.wstring()+L"-aa-on.png",after);
    preserve_hud=true;g.compose(backend.state!=nullptr,false);save(prefix.wstring()+L"-aa-on-hud.png",g.read_pixels(g.result_texture()));preserve_hud=false;
    size_t changed=0;for(size_t p=0;p<before.size();p+=4)if(memcmp(before.data()+p,after.data()+p,4)!=0)++changed;
    std::ofstream(report_path)<<"{\"passed\":true,\"width\":"<<w<<",\"height\":"<<h<<",\"aa_changed_pixels\":"<<changed<<",\"total_pixels\":"<<size_t(w)*h<<",\"neural\":"<<(backend.state?"true":"false")<<"}\n";
}
double percentile(std::vector<double> values,double p) {if(values.empty())return 0;std::sort(values.begin(),values.end());return values[std::min(values.size()-1,size_t(p*values.size()))];}
void capture_worker(HWND source,Profile profile) {
    unsigned frames=0,resizes=0,verified=0,neural_frames=0,styled_checks=0;std::vector<double> durations,ages;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        // Destruction order: capture closes before CUDA unregisters and before D3D resources die.
        Graphics graphics;graphics.initialize(viewer);Backend backend;
        status(L"Preparing "+profile.label+L"...");backend.open(profile,graphics.device.Get());
        Capture capture;capture.open(source,graphics.device.Get());auto pool_size=capture.item.Size();
        auto last_frame=Clock::now(),last_status=last_frame,next_frame=last_frame;unsigned status_frames=0;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        while(!stop_requested&&!capture.closed->load()) {
            if(!IsWindow(source))break;
            if(Clock::now()<next_frame) {WaitForSingleObject(capture.signal->handle,2);continue;}
            // At most two queued frames. Discard the older one before submitting GPU work.
            auto frame=capture.pool.TryGetNextFrame();
            if(!frame) {
                if(Clock::now()-last_frame>std::chrono::seconds(2))status(L"Waiting for frames: restore the source window. Capture may be unavailable or blocked.");
                if(self_test&&Clock::now()-last_frame>std::chrono::seconds(15))throw std::runtime_error("Capture self-test timed out");
                WaitForSingleObject(capture.signal->handle,10);continue;
            }
            auto newer=capture.pool.TryGetNextFrame();if(newer){frame.Close();frame=std::move(newer);}
            auto size=frame.ContentSize();
            if(size.Width<=0||size.Height<=0){frame.Close();continue;}
            if(size.Width!=pool_size.Width||size.Height!=pool_size.Height) {
                frame.Close();capture.pool.Recreate(capture.device,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);pool_size=size;continue;
            }
            auto start=Clock::now();last_frame=start;next_frame=start+std::chrono::microseconds(16667);
            auto surface=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;winrt::check_hresult(surface->GetInterface(IID_PPV_ARGS(&texture)));
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            if(UINT(size.Width)>desc.Width||UINT(size.Height)>desc.Height) {frame.Close();continue;}
            if(UINT(size.Width)!=graphics.bridge.width()||UINT(size.Height)!=graphics.bridge.height()) {graphics.resize(size.Width,size.Height);backend.bind(graphics.bridge);++resizes;}
            graphics.capture(texture.Get());texture.Reset();auto stamp=frame.SystemRelativeTime().count();frame.Close();
            bool raw=self_test?(frames<12):bypass.load();
            bool neural=backend.state&&!raw;cel::Timings timing{};
            if(neural){timing=backend.process();++neural_frames;}
            graphics.compose(neural,raw);
            LARGE_INTEGER now{};QueryPerformanceCounter(&now);double age=double(now.QuadPart)*1000.0/double(frequency.QuadPart)-double(stamp)/10000.0;
            graphics.present(viewer);double duration=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            if(frames>=12){
                // Keep diagnostics bounded during multi-hour sessions.
                if(durations.size()==600){durations.erase(durations.begin());ages.erase(ages.begin());}
                durations.push_back(duration);ages.push_back(age);
            }
            if(self_test) {
                auto pixels=graphics.read_pixels(graphics.bridge.input());auto result=graphics.read_pixels(graphics.result_texture());
                // Stable patches are away from the animated square. A red/blue swap, blank frame,
                // wrong dimensions or failure to return the original during bypass fails the test.
                size_t left=(size_t(graphics.bridge.height()/2)*graphics.bridge.width()+graphics.bridge.width()/8)*4;
                size_t right=(size_t(graphics.bridge.height()/2)*graphics.bridge.width()+graphics.bridge.width()*7/8)*4;
                if(pixels[left]<170||pixels[left+2]>60||pixels[right]>60||pixels[right+2]<170)throw std::runtime_error("WGC BGRA/RGBA color or resize test failed");
                if(raw) {for(auto index:{left,right})for(size_t c=0;c<3;++c)if(abs(int(pixels[index+c])-int(result[index+c]))>1)throw std::runtime_error("Viewer bypass mismatch");++verified;}
                else {int difference=0;for(auto index:{left,right})for(size_t c=0;c<3;++c)difference+=abs(int(pixels[index+c])-int(result[index+c]));if(difference>10)++styled_checks;}
            }
            ++frames;tested_frames=frames;last_capture_tick=GetTickCount64();
            if(Clock::now()-last_status>std::chrono::milliseconds(500)) {
                double fps=(frames-status_frames)/std::chrono::duration<double>(Clock::now()-last_status).count();
                std::wostringstream s;s.precision(2);s<<std::fixed<<profile.label<<L"\r\n"<<graphics.bridge.width()<<L" x "<<graphics.bridge.height()<<L" | "<<fps<<L" processed FPS | frame work "<<duration<<L" ms\r\n";
                s<<L"Inference GPU "<<timing.inference_ms<<L" ms | capture timestamp delta "<<age<<L" ms\r\n"<<(raw?L"BYPASS":L"Styling")<<L" | capped at 60 processed FPS. Timestamp delta is not input latency.";status(s.str());last_status=Clock::now();status_frames=frames;
            }
            if(self_test&&frames>=100) {
                if(resizes<2||verified<10||styled_checks<50||(!profile.engine.empty()&&neural_frames<50))throw std::runtime_error("Self-test did not cover resize, bypass and styling");
                test_result=0;break;
            }
        }
        if(!self_test)status(capture.closed->load()?L"Source window closed. Select another window and Start.":L"Capture stopped.");
    } catch(const winrt::hresult_error& e) {status(L"Capture stopped: "+std::wstring(e.message().c_str()));}
      catch(const std::exception& e) {status(L"Capture stopped: "+widen(e.what()));}
    if(self_test) {
        std::wstring message;{std::lock_guard lock(status_mutex);message=status_text;}
        std::ofstream report(report_path);report<<"{\"passed\":"<<(test_result==0?"true":"false")<<",\"frames\":"<<frames<<",\"resource_sizes\":"<<resizes<<",\"bypass_checks\":"<<verified<<",\"styled_checks\":"<<styled_checks<<",\"neural_frames\":"<<neural_frames
            <<",\"work_p50_ms\":"<<percentile(durations,.5)<<",\"work_p95_ms\":"<<percentile(durations,.95)<<",\"capture_timestamp_delta_p50_ms\":"<<percentile(ages,.5)<<",\"capture_timestamp_delta_p95_ms\":"<<percentile(ages,.95)<<"}\n";
        if(test_result!=0)std::ofstream(report_path.wstring()+L".error.txt")<<winrt::to_string(message);
    }
    running=false;
}

void stop_capture() {stop_requested=true;if(worker.joinable())worker.join();running=false;ShowWindow(viewer,SW_HIDE);}
void start_capture(HWND source,const Profile& profile) {
    stop_capture();selected_source=source;stop_requested=false;running=true;tested_frames=0;last_capture_tick=0;
    ShowWindow(viewer,SW_SHOWNOACTIVATE);worker=std::thread(capture_worker,source,profile);
}
void update_overlay() {
    if(!overlay||!running)return;
    HWND foreground=GetForegroundWindow();
    if(IsIconic(selected_source)||!IsWindowVisible(selected_source)||foreground!=selected_source||last_capture_tick==0||GetTickCount64()-last_capture_tick>2000){ShowWindow(viewer,SW_HIDE);return;}
    RECT rect{};
    if(FAILED(DwmGetWindowAttribute(selected_source,DWMWA_EXTENDED_FRAME_BOUNDS,&rect,sizeof(rect))))GetWindowRect(selected_source,&rect);
    SetWindowPos(viewer,HWND_TOPMOST,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void set_overlay(bool value) {
    overlay=value;SendMessageW(overlay_box,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);
    SetWindowLongPtrW(viewer,GWL_STYLE,value?WS_POPUP:WS_OVERLAPPEDWINDOW);
    SetWindowLongPtrW(viewer,GWL_EXSTYLE,value?(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW):WS_EX_APPWINDOW);
    if(value)SetLayeredWindowAttributes(viewer,0,255,LWA_ALPHA);
    SetWindowPos(viewer,value?HWND_TOPMOST:HWND_NOTOPMOST,760,180,960,580,SWP_NOACTIVATE|SWP_FRAMECHANGED);
    if(!value&&running)ShowWindow(viewer,SW_SHOWNOACTIVATE);
    update_overlay();
}
HWND control(const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id=0) {
    auto window=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,controls,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return window;
}
void apply_hud_regions() {
    Rectangles next{};
    for(int r=0;r<3;++r) {
        if(SendMessageW(hud_region_boxes[r],BM_GETCHECK,0,0)!=BST_CHECKED)continue;
        for(int c=0;c<4;++c) {
            wchar_t value[64]{};GetWindowTextW(hud_edits[r][c],value,64);wchar_t* end{};double number=wcstod(value,&end);
            if(end==value||*end||!std::isfinite(number)||number<0||number>100){status(L"HUD values must be numbers from 0 to 100 percent.");return;}
            next[r][c]=float(number/100.0);
        }
        if(next[r][0]>=next[r][2]||next[r][1]>=next[r][3]){status(L"HUD rectangle needs Left < Right and Top < Bottom.");return;}
    }
    {std::lock_guard lock(hud_mutex);hud_regions=next;}
    status(L"HUD regions applied. Enable Preserve HUD regions to show original pixels there.");
}
LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    if(h==test_source) {
        if(message==WM_PAINT) {
            PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);RECT left=r;left.right=r.right/2;RECT right=r;right.left=left.right;
            HBRUSH red=CreateSolidBrush(RGB(204,51,26)),blue=CreateSolidBrush(RGB(26,51,204)),white=CreateSolidBrush(RGB(240,240,240));
            FillRect(dc,&left,red);FillRect(dc,&right,blue);RECT square{LONG(r.right/2-20),LONG(test_tick%200),LONG(r.right/2+20),LONG(test_tick%200+30)};FillRect(dc,&square,white);
            DeleteObject(red);DeleteObject(blue);DeleteObject(white);EndPaint(h,&p);return 0;
        }
        if(message==WM_ERASEBKGND)return 1;
    }
    if(h==viewer) {
        if(message==WM_CLOSE){stop_capture();return 0;}
        if(message==WM_NCHITTEST&&overlay)return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE&&overlay)return MA_NOACTIVATE;
        if(message==WM_ERASEBKGND)return 1;
    }
    if(h==controls) {
        if(message==WM_COMMAND) {
            switch(LOWORD(wp)) {
            case refresh_id:refresh_sources();refresh_profiles();break;
            case start_id: {
                auto index=SendMessageW(source_combo,CB_GETCURSEL,0,0),p=SendMessageW(profile_combo,CB_GETCURSEL,0,0);
                if(index==CB_ERR||p==CB_ERR) {status(L"Select a source window and profile.");break;}
                HWND source=reinterpret_cast<HWND>(SendMessageW(source_combo,CB_GETITEMDATA,index,0));
                if(!IsWindow(source)){status(L"Source window no longer exists. Refresh the list.");break;}
                start_capture(source,profiles.at(size_t(p)));break;
            }
            case stop_id:stop_capture();break;
            case bypass_id:bypass=SendMessageW(bypass_box,BM_GETCHECK,0,0)==BST_CHECKED;break;
            case finish_id:finishing=SendMessageW(finish_box,BM_GETCHECK,0,0)==BST_CHECKED;break;
            case aa_id:edge_aa=SendMessageW(aa_box,BM_GETCHECK,0,0)==BST_CHECKED;break;
            case hud_id:preserve_hud=SendMessageW(hud_box,BM_GETCHECK,0,0)==BST_CHECKED;break;
            case hud_apply_id:apply_hud_regions();break;
            case split_id:split=SendMessageW(split_box,BM_GETCHECK,0,0)==BST_CHECKED;break;
            case overlay_id:set_overlay(SendMessageW(overlay_box,BM_GETCHECK,0,0)==BST_CHECKED);break;
            case strength_id:if(HIWORD(wp)==CBN_SELCHANGE){constexpr float values[]={0,.25f,.5f,.75f,.85f,1};auto index=SendMessageW(strength_combo,CB_GETCURSEL,0,0);if(index>=0&&index<6)strength=values[index];}break;
            }
            return 0;
        }
        if(message==WM_HOTKEY) {
            if(wp==1){bypass=!bypass;SendMessageW(bypass_box,BM_SETCHECK,bypass?BST_CHECKED:BST_UNCHECKED,0);}
            if(wp==2)set_overlay(!overlay);return 0;
        }
        if(message==WM_TIMER) {
            {std::lock_guard lock(status_mutex);SetWindowTextW(status_label,status_text.c_str());}
            // A failed/closed capture must never leave an old opaque overlay covering the game.
            if(!running&&worker.joinable()){worker.join();ShowWindow(viewer,SW_HIDE);}
            update_overlay();
            if(self_test||demo) {
                ++test_tick;InvalidateRect(test_source,nullptr,FALSE);
                if(self_test&&tested_frames>=35&&test_tick<1000){SetWindowPos(test_source,nullptr,0,0,800,448,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);test_tick=1000;}
                if(self_test&&!running)PostMessageW(controls,WM_CLOSE,0,0);
            }
            return 0;
        }
        if(message==WM_CLOSE){stop_capture();DestroyWindow(h);return 0;}
        if(message==WM_DESTROY){UnregisterHotKey(h,1);UnregisterHotKey(h,2);PostQuitMessage(0);return 0;}
    }
    return DefWindowProcW(h,message,wp,lp);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);base=fs::path(exe).parent_path();
        int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);fs::path test_engine;
        for(int i=1;i<count;++i) {std::wstring arg=args[i];if(arg==L"--self-test")self_test=true;else if(arg==L"--test-quality")quality_test=true;else if(arg==L"--test-image"&&i+1<count)image_test_path=args[++i];else if(arg==L"--demo")demo=true;else if(arg==L"--engine"&&i+1<count)test_engine=args[++i];else if(arg==L"--report"&&i+1<count)report_path=args[++i];else {LocalFree(args);throw std::runtime_error("Options: --demo | --self-test | --test-quality | --test-image input.png [--engine engine.plan] [--report result.json]");}}LocalFree(args);
        WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=window_class;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassW(&wc);
        controls=CreateWindowExW(0,window_class,L"Anime Neural Viewer - external capture",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,60,60,680,632,nullptr,nullptr,instance,nullptr);
        viewer=CreateWindowExW(WS_EX_APPWINDOW,window_class,L"Anime Neural Viewer - styled output",WS_OVERLAPPEDWINDOW,760,180,960,580,nullptr,nullptr,instance,nullptr);
        if(quality_test){run_quality_test();DestroyWindow(viewer);DestroyWindow(controls);return 0;}
        if(!image_test_path.empty()){run_image_test(test_engine.empty()?Profile{}:load_profile(test_engine));DestroyWindow(viewer);DestroyWindow(controls);return 0;}
        // Only specific source windows are captured; our own viewer is excluded from selection.
        // Keep the output capturable so users can record or stream their styled view.
        control(L"STATIC",L"Source window (windowed / borderless, SDR)",0,16,14,600,20);
        source_combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,16,38,530,240,source_id);
        control(L"BUTTON",L"Refresh",WS_TABSTOP,556,36,90,26,refresh_id);
        control(L"STATIC",L"Local model profile - select another profile and Start to reload",0,16,76,625,20);
        profile_combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,16,100,630,240,profile_id);
        control(L"BUTTON",L"Start",WS_TABSTOP,16,140,90,28,start_id);control(L"BUTTON",L"Stop",WS_TABSTOP,116,140,90,28,stop_id);
        bypass_box=control(L"BUTTON",L"Bypass (F10)",BS_AUTOCHECKBOX|WS_TABSTOP,226,140,140,28,bypass_id);
        finish_box=control(L"BUTTON",L"DBZ finishing",BS_AUTOCHECKBOX|WS_TABSTOP,390,140,160,28,finish_id);SendMessageW(finish_box,BM_SETCHECK,BST_CHECKED,0);
        split_box=control(L"BUTTON",L"Split comparison",BS_AUTOCHECKBOX|WS_TABSTOP,16,183,160,24,split_id);
        overlay_box=control(L"BUTTON",L"Click-through overlay (F9)",BS_AUTOCHECKBOX|WS_TABSTOP,200,183,230,24,overlay_id);
        control(L"STATIC",L"Neural blend",0,450,186,90,20);strength_combo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,548,181,98,180,strength_id);
        for(auto value:{L"0%",L"25%",L"50%",L"75%",L"85%",L"100%"})SendMessageW(strength_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));SendMessageW(strength_combo,CB_SETCURSEL,4,0);
        aa_box=control(L"BUTTON",L"Smooth jagged edges (spatial anti-aliasing)",BS_AUTOCHECKBOX|WS_TABSTOP,16,218,430,24,aa_id);SendMessageW(aa_box,BM_SETCHECK,BST_CHECKED,0);
        hud_box=control(L"BUTTON",L"Preserve HUD regions (original captured pixels)",BS_AUTOCHECKBOX|WS_TABSTOP,16,252,440,24,hud_id);
        control(L"BUTTON",L"Apply HUD regions",WS_TABSTOP,480,250,166,26,hud_apply_id);
        control(L"STATIC",L"Enabled region",0,16,287,180,18);
        const wchar_t* columns[]={L"Left %",L"Top %",L"Right %",L"Bottom %"};
        for(int c=0;c<4;++c)control(L"STATIC",columns[c],0,210+c*108,287,100,18);
        const wchar_t* names[]={L"Top health / HUD",L"Right training stats",L"Left input panel"};
        const wchar_t* defaults[3][4]={{L"0",L"0",L"100",L"19"},{L"75",L"23",L"100",L"49"},{L"0",L"24",L"15",L"85"}};
        for(int r=0;r<3;++r) {
            hud_region_boxes[r]=control(L"BUTTON",names[r],BS_AUTOCHECKBOX|WS_TABSTOP,16,309+r*28,188,24);
            SendMessageW(hud_region_boxes[r],BM_SETCHECK,r==0?BST_CHECKED:BST_UNCHECKED,0);
            for(int c=0;c<4;++c)hud_edits[r][c]=control(L"EDIT",defaults[r][c],WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,210+c*108,309+r*28,90,23);
        }
        control(L"STATIC",L"Masks also preserve scenery behind the HUD. Edit bounds, then Apply HUD regions.",0,16,400,630,20);
        status_label=control(L"STATIC",L"",0,16,440,630,84);
        control(L"STATIC",L"Runs outside the game. Capture can be restricted by the game or Windows.\r\nOverlay appears only while the source has focus. F9 returns to a separate viewer.\r\nNo DLSS / frame generation. Neural models need local setup; see README.",0,16,532,630,58);
        refresh_sources();refresh_profiles();
        bool hotkeys=RegisterHotKey(controls,1,MOD_NOREPEAT,VK_F10)!=0;hotkeys=RegisterHotKey(controls,2,MOD_NOREPEAT,VK_F9)!=0&&hotkeys;
        if(!hotkeys)status(L"A global F9/F10 shortcut is in use. Use the visible bypass/overlay controls.");
        ShowWindow(controls,SW_SHOW);SetTimer(controls,refresh_timer,self_test||demo?16:100,nullptr);
        if(self_test||demo) {
            test_source=CreateWindowExW(0,window_class,L"Anime viewer test source",WS_POPUP,60,520,640,360,nullptr,nullptr,instance,nullptr);ShowWindow(test_source,SW_SHOWNOACTIVATE);UpdateWindow(test_source);
            refresh_sources();
            start_capture(test_source,test_engine.empty()?profiles.front():load_profile(test_engine));
        }
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(controls,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        if(test_source)DestroyWindow(test_source);DestroyWindow(viewer);return self_test?test_result.load():0;
    } catch(const std::exception& e) {stop_capture();if(quality_test||self_test||!image_test_path.empty()){std::ofstream(report_path)<<"{\"passed\":false}\n";std::ofstream(report_path.wstring()+L".error.txt")<<e.what();}else MessageBoxW(nullptr,widen(e.what()).c_str(),L"Anime Neural Viewer",MB_ICONERROR);return 1;}
}
