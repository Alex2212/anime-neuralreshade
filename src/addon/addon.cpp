#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>
#include "interop/frame_bridge.hpp"
#include "inference/backend_api.hpp"
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <array>
#include <vector>
using namespace reshade::api;
namespace {
std::filesystem::path base;
using Clock=std::chrono::steady_clock;
struct Profile {std::string path,label;int width,height;bool bgr;};
std::vector<Profile> profiles(){
    std::vector<Profile> result;auto directory=base/L"cache";
    if(!std::filesystem::exists(directory))return result;
    for(const auto& f:std::filesystem::directory_iterator(directory)){
        if(f.path().extension()!=L".plan")continue;auto ini=f.path();ini+=L".ini";
        if(!std::filesystem::exists(ini))continue;
        auto get=[&](const wchar_t* k){wchar_t b[128]{};GetPrivateProfileStringW(L"Engine",k,L"",b,128,ini.c_str());char text[512]{};WideCharToMultiByte(CP_UTF8,0,b,-1,text,512,nullptr,nullptr);return std::string(text);};
        Profile p;p.path=std::filesystem::relative(f.path(),base).string();p.width=GetPrivateProfileIntW(L"Engine",L"Width",0,ini.c_str());p.height=GetPrivateProfileIntW(L"Engine",L"Height",0,ini.c_str());p.bgr=get(L"Color")=="BGR";
        auto name=get(L"Name");if(name.empty())continue;
        p.label=(name=="whitebox"?"White-box Cartoonization":"AnimeGANv2 Shinkai")+std::string(" | ")+std::to_string(p.width)+"x"+std::to_string(p.height)+" | "+get(L"Precision");
        result.push_back(std::move(p));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.label<b.label;});return result;
}
struct Backend {
    HMODULE dll{}; const cel::BackendApi* api{};void* state{};std::string error;
    ~Backend(){if(state)api->destroy(state);if(dll)FreeLibrary(dll);}
};
struct __declspec(uuid("48478639-2360-4B17-B158-782FAD55122A")) Runtime {
    cel::FrameBridge bridge;
    std::unique_ptr<Backend> backend;
    std::future<std::unique_ptr<Backend>> pending;
    cel::Timings timing{};
    bool enabled=true,ready=false,failed=false,bgr=false,split=false;
    int mode=0,width=1280,height=720,key=VK_F10;
    char engine[260]="cache/active.plan";
    std::string status="Original capture / finishing layers only";
    std::vector<Profile> profiles;
    uint64_t frame=0,processed=0;
    float cpu_ms=0,present_fps=0,processed_fps=0;
    Clock::time_point last=Clock::now(),sample=last;
    uint64_t sample_frames=0,sample_processed=0;
};
Runtime* state_of(effect_runtime* r){uint64_t value=0;r->get_private_data(reinterpret_cast<const uint8_t*>(&__uuidof(Runtime)),&value);return reinterpret_cast<Runtime*>(value);}
void log_error(const std::string& s){reshade::log_message(reshade::log_level::error,s.c_str());}
void uniform(effect_runtime* r,const char* name,bool v){auto h=r->find_uniform_variable("CelShade.fx",name);if(h.handle)r->set_uniform_value_bool(h,&v,1);}
void bindings(effect_runtime* r,Runtime& s){
    r->update_texture_bindings("CEL_ORIGINAL",{reinterpret_cast<uint64_t>(s.bridge.original_view())},{reinterpret_cast<uint64_t>(s.bridge.original_view())});
    r->update_texture_bindings("CEL_NEURAL",{reinterpret_cast<uint64_t>(s.bridge.output_view())},{reinterpret_cast<uint64_t>(s.bridge.output_view())});
}
void prepare(effect_runtime* r,Runtime& s){
    if(s.pending.valid())return;
    s.backend.reset();s.failed=false;s.ready=false;
    if(s.mode==0){s.status="Original capture / finishing layers only";return;}
    cel::ComPtr<ID3D11Device> device=reinterpret_cast<ID3D11Device*>(r->get_device()->get_native());
    const auto path=base/std::filesystem::path(s.engine);const auto kernel=base/L"kernels.ptx";
    const int width=s.width,height=s.height;const bool identity=s.mode==1,bgr=s.bgr;
    s.status="Preparing NVIDIA backend in background...";
    s.pending=std::async(std::launch::async,[device,path,kernel,width,height,identity,bgr](){
        auto b=std::make_unique<Backend>();
        auto dll=base/L"cel_nvidia.dll";
        b->dll=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!b->dll){b->error="NVIDIA runtime unavailable (DLL load error "+std::to_string(GetLastError())+"). Original-frame bypass.";return b;}
        auto get=reinterpret_cast<cel::GetBackendApi>(GetProcAddress(b->dll,"CelGetBackendApi"));
        if(!get||(b->api=get())->abi!=cel::backend_abi){b->error="NVIDIA backend ABI mismatch";return b;}
        cel::BackendConfig config;config.device=device.Get();config.engine_path=path.c_str();config.kernel_path=kernel.c_str();
        config.width=width;config.height=height;config.bgr=bgr;config.identity=identity;
        char error[1024]{};b->state=b->api->create(&config,error,sizeof(error));if(!b->state)b->error=error;
        return b;
    });
}
void save(effect_runtime* r,const Runtime& s){
    reshade::set_config_value(r,"CelShade","Schema",1);
    reshade::set_config_value(r,"CelShade","Mode",s.mode);
    reshade::set_config_value(r,"CelShade","Width",s.width);reshade::set_config_value(r,"CelShade","Height",s.height);
    reshade::set_config_value(r,"CelShade","BGR",s.bgr);reshade::set_config_value(r,"CelShade","BypassKey",s.key);
    reshade::set_config_value(r,"CelShade","Engine",s.engine);
}
void init(effect_runtime* r){
    if(r->get_device()->get_api()!=device_api::d3d11){log_error("CelShade supports Direct3D 11 only; bypassing.");return;}
    auto& s=r->create_private_data<Runtime>();
    reshade::get_config_value(r,"CelShade","Mode",s.mode);
    reshade::get_config_value(r,"CelShade","Width",s.width);reshade::get_config_value(r,"CelShade","Height",s.height);
    reshade::get_config_value(r,"CelShade","BGR",s.bgr);reshade::get_config_value(r,"CelShade","BypassKey",s.key);
    size_t n=sizeof(s.engine);reshade::get_config_value(r,"CelShade","Engine",s.engine,&n);
    s.mode=std::clamp(s.mode,0,2);s.width=std::clamp(s.width,8,3840);s.height=std::clamp(s.height,8,2160);
    s.key=std::clamp(s.key,1,255);s.engine[sizeof(s.engine)-1]=0;
    try{s.profiles=profiles();}catch(const std::exception& e){log_error(e.what());}
    try{prepare(r,s);}catch(const std::exception& e){s.status=e.what();s.failed=true;log_error(s.status);}
}
void destroy(effect_runtime* r){
    if(auto* s=state_of(r)){
        uniform(r,"CelBridgeReady",false);uniform(r,"CelNeuralReady",false);
        r->update_texture_bindings("CEL_ORIGINAL",{},{});r->update_texture_bindings("CEL_NEURAL",{},{});
        if(s->pending.valid())s->pending.get().reset();
        s->backend.reset();r->destroy_private_data<Runtime>();
    }
}
void begin(effect_runtime* r,command_list* commands,resource_view rtv,resource_view){
    auto* s=state_of(r);if(!s)return;
    auto start=Clock::now();++s->frame;
    s->ready=false;
    if(r->is_key_pressed(s->key))s->enabled=!s->enabled;
    uniform(r,"CelEnabled",s->enabled);uniform(r,"CelNeuralReady",false);uniform(r,"CelBridgeReady",false);
    if(!s->enabled||s->failed)return;
    try {
        auto handle=r->get_device()->get_resource_from_view(rtv);
        auto* source=reinterpret_cast<ID3D11Texture2D*>(handle.handle);
        auto* context=reinterpret_cast<ID3D11DeviceContext*>(commands->get_native());
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if(!s->bridge.matches(desc)){
            // Clear all references before releasing/re-registering on resize.
            r->update_texture_bindings("CEL_ORIGINAL",{},{});r->update_texture_bindings("CEL_NEURAL",{},{});
            // Backend owns registered resources until replaced; bind() safely unregisters them.
            s->bridge.initialize(reinterpret_cast<ID3D11Device*>(r->get_device()->get_native()),desc);
            if(s->backend&&s->backend->state){char err[1024]{};
                if(!s->backend->api->bind(s->backend->state,s->bridge.input(),s->bridge.output(),err,sizeof(err)))throw std::runtime_error(err);
            }
            bindings(r,*s);
        }
        if(s->pending.valid()&&s->pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            s->backend=s->pending.get();
            if(!s->backend->state)throw std::runtime_error(s->backend->error);
            char err[1024]{};
            if(!s->backend->api->bind(s->backend->state,s->bridge.input(),s->bridge.output(),err,sizeof(err)))throw std::runtime_error(err);
            s->status=s->mode==1?"CUDA identity diagnostic (no neural model)":"TensorRT same-frame inference";
        }
        s->bridge.capture(context,source);
        if(s->backend&&s->backend->state){
            char err[1024]{};
            if(!s->backend->api->process(s->backend->state,&s->timing,err,sizeof(err)))throw std::runtime_error(err);
            s->ready=true;++s->processed;uniform(r,"CelNeuralReady",true);
        }else s->bridge.identity(context);
        uniform(r,"CelBridgeReady",s->mode==0||s->ready);
        s->cpu_ms=std::chrono::duration<float,std::milli>(Clock::now()-start).count();
    }catch(const std::exception& e){
        s->failed=true;s->ready=false;s->status=e.what();log_error(s->status);
        uniform(r,"CelBridgeReady",false);uniform(r,"CelNeuralReady",false);
    }
}
void present(effect_runtime* r){
    auto* s=state_of(r);if(!s)return;
    auto now=Clock::now();float secs=std::chrono::duration<float>(now-s->sample).count();
    ++s->sample_frames;
    if(secs>=1){s->present_fps=float(s->sample_frames)/secs;s->processed_fps=float(s->processed-s->sample_processed)/secs;
        s->sample=now;s->sample_frames=0;s->sample_processed=s->processed;}
}
void reload(effect_runtime* r){if(auto* s=state_of(r))bindings(r,*s);}
void overlay(effect_runtime* r){
    auto* s=state_of(r);if(!s){ImGui::TextUnformatted("CelShade requires Direct3D 11.");return;}
    ImGui::Checkbox("Enable CelShade",&s->enabled);ImGui::TextWrapped("%s",s->status.c_str());
    if(ImGui::CollapsingHeader("Neural",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::BeginDisabled(s->pending.valid());
        const char* selected="Choose a prepared model profile";
        for(const auto& p:s->profiles)if(p.path==s->engine)selected=p.label.c_str();
        if(ImGui::BeginCombo("Model / resolution / precision",selected)){
            for(const auto& p:s->profiles)if(ImGui::Selectable(p.label.c_str(),p.path==s->engine)){
                strcpy_s(s->engine,p.path.c_str());s->width=p.width;s->height=p.height;s->bgr=p.bgr;s->mode=2;save(r,*s);
                try{prepare(r,*s);}catch(const std::exception& e){s->status=e.what();s->failed=true;}
            }
            ImGui::EndCombo();
        }
        if(ImGui::Button("Refresh model profiles")){try{s->profiles=profiles();}catch(const std::exception& e){s->status=e.what();}}
        ImGui::TextWrapped("Model profile changes prepare in the background. Each profile validates its engine hash and GPU before use.");
        ImGui::Combo("Backend",&s->mode,"Finishing layers only\0CUDA identity diagnostic\0TensorRT NVIDIA\0");
        ImGui::InputText("Local engine",s->engine,sizeof(s->engine));
        ImGui::InputInt("Model width",&s->width);ImGui::InputInt("Model height",&s->height);
        ImGui::Checkbox("BGR model (White-box)",&s->bgr);
        if(ImGui::Button("Prepare / reload engine")){save(r,*s);try{prepare(r,*s);}catch(const std::exception& e){s->status=e.what();s->failed=true;}}
        ImGui::EndDisabled();
        ImGui::TextWrapped("Use matching fixed engine dimensions. TensorRT preparation runs in the background; processing stays on the current frame. Finishing controls and presets are in CelShade.fx on ReShade's Home tab.");
    }
    if(ImGui::CollapsingHeader("Bypass and controls")){
        if(ImGui::InputInt("Bypass key (Windows VK)",&s->key)){s->key=std::clamp(s->key,1,255);save(r,*s);}
        ImGui::TextUnformatted("Default: F10. Split comparison and HUD rectangles: CelShade.fx.");
        if(ImGui::Button("Reset addon settings")){s->mode=0;s->width=1280;s->height=720;s->bgr=false;s->key=VK_F10;strcpy_s(s->engine,"cache/active.plan");save(r,*s);if(!s->pending.valid())prepare(r,*s);}
        auto reset=[&](std::initializer_list<const char*> names){for(auto name:names){auto u=r->find_uniform_variable("CelShade.fx",name);if(u.handle)r->reset_uniform_value(u);}};
        if(ImGui::Button("Reset Neural"))reset({"CelStrength"});ImGui::SameLine();
        if(ImGui::Button("Reset Two tone"))reset({"CelToneAmount","CelThreshold","CelSoftness","CelShadowLevel","CelLightLevel","CelLiteral","CelLightTint","CelShadowTint","CelTintStrength"});
        if(ImGui::Button("Reset Outlines"))reset({"CelOutlineStrength","CelOutlineWidth","CelOutlineThreshold"});ImGui::SameLine();
        if(ImGui::Button("Reset Color"))reset({"CelSaturation","CelExposure"});
        if(ImGui::Button("Reset all finishing controls"))r->enumerate_uniform_variables("CelShade.fx",[](effect_runtime* runtime,effect_uniform_variable v,void*){runtime->reset_uniform_value(v);},nullptr);
    }
    if(ImGui::CollapsingHeader("Diagnostics",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Text("Display: %u x %u   Model: %d x %d",s->bridge.width(),s->bridge.height(),s->width,s->height);
        ImGui::Text("Present %.1f FPS   Processed %.1f FPS",s->present_fps,s->processed_fps);
        ImGui::Text("Input frame: %llu   Output frame: %llu",s->frame,s->ready?s->frame:0);
        if(s->backend&&s->ready){
            ImGui::Text("GPU preprocess %.3f ms / inference %.3f ms / output %.3f ms",s->timing.preprocess_ms,s->timing.inference_ms,s->timing.postprocess_ms);
            ImGui::Text("GPU kernels %.3f ms   CPU callback incl. synchronization %.3f ms",s->timing.total_ms,s->cpu_ms);
            ImGui::Text("Tracked GPU allocations %.1f MiB",double(s->bridge.bytes()+s->timing.allocated_bytes)/1048576.0);
            ImGui::TextUnformatted("Processing age: 0 frames. Driver/model weight memory not included above.");
        }else ImGui::TextUnformatted("Neural timings / processing age: unavailable");
        ImGui::TextUnformatted("End-to-end latency: unmeasured. DLSS / frame generation: unavailable.");
    }
}
}
extern "C" __declspec(dllexport) const char* NAME="CelShade";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Native NVIDIA cartoonization and independent cel finishing layers. Development build.";
extern "C" __declspec(dllexport) bool AddonInit(HMODULE module,HMODULE reshade_module){
    if(!reshade::register_addon(module,reshade_module))return false;
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);base=std::filesystem::path(path).parent_path();
    reshade::register_event<reshade::addon_event::init_effect_runtime>(init);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(destroy);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::register_event<reshade::addon_event::reshade_present>(present);
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(reload);
    reshade::register_overlay("CelShade",overlay);return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE module,HMODULE reshade_module){
    reshade::unregister_event<reshade::addon_event::init_effect_runtime>(init);
    reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(destroy);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::unregister_event<reshade::addon_event::reshade_present>(present);
    reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(reload);
    reshade::unregister_overlay("CelShade",overlay);reshade::unregister_addon(module,reshade_module);
}
