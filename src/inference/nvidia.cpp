#include "backend_api.hpp"
#include <NvInfer.h>
#include <cuda_runtime_api.h>
#include <cuda_d3d11_interop.h>
#include <cuda.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <wrl/client.h>
#include <dxgi.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void ck(cudaError_t r){if(r!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(r));}
void dk(CUresult r){if(r!=CUDA_SUCCESS){const char* s=nullptr;cuGetErrorString(r,&s);throw std::runtime_error(s?s:"CUDA driver failure");}}
void copy_error(char* dst,uint32_t size,const char* s){if(dst&&size)strncpy_s(dst,size,s,_TRUNCATE);}
struct Logger:nvinfer1::ILogger {
    std::string last_error;
    void log(Severity s,const char* m) noexcept override {if(s<=Severity::kERROR){try{last_error=m;}catch(...){}}}
};
struct State {
    Logger logger;
    int device=0,w=0,h=0,sw=0,sh=0; bool bgr=false,identity=false;
    cudaStream_t stream{}; std::array<cudaEvent_t,4> events{};
    cudaGraphicsResource* graphics[2]{};
    CUmodule module{}; CUfunction pre{},post{};
    float *input{},*output{};
    std::unique_ptr<nvinfer1::IRuntime> runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> engine;
    std::unique_ptr<nvinfer1::IExecutionContext> context;
    std::string in_name,out_name;
    void unbind() noexcept {
        cudaSetDevice(device); if(stream)cudaStreamSynchronize(stream);
        for(auto& r:graphics){if(r)cudaGraphicsUnregisterResource(r);r=nullptr;}
    }
    ~State(){
        cudaSetDevice(device); unbind(); context.reset(); engine.reset(); runtime.reset();
        if(input)cudaFree(input);if(output)cudaFree(output);
        if(module)cuModuleUnload(module);
        for(auto e:events)if(e)cudaEventDestroy(e);
        if(stream)cudaStreamDestroy(stream);
    }
};
std::vector<char> read(const wchar_t* path){
    std::ifstream f(std::filesystem::path(path),std::ios::binary);
    if(!f)throw std::runtime_error("Cannot open engine or kernels.ptx; prepare a local engine first");
    return std::vector<char>((std::istreambuf_iterator<char>(f)),{});
}
std::string digest(const std::vector<char>& bytes){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};std::array<unsigned char,32> out{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 provider failed");
    auto a=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(a>=0)a=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),0);
    if(a>=0)a=BCryptFinishHash(hash,out.data(),ULONG(out.size()),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(a<0)throw std::runtime_error("Engine checksum failed");
    std::ostringstream os;os<<std::hex<<std::setfill('0');for(auto b:out)os<<std::setw(2)<<int(b);return os.str();
}
void validate_contract(const cel::BackendConfig& config,const std::vector<char>& bytes,int device){
    auto file=std::filesystem::absolute(config.engine_path);file+=L".ini";
    auto get=[&](const wchar_t* key){wchar_t value[256]{};GetPrivateProfileStringW(L"Engine",key,L"",value,256,file.c_str());return std::wstring(value);};
    auto expected=digest(bytes);std::wstring checksum(expected.begin(),expected.end());
    if(get(L"Schema")!=L"1"||get(L"SHA256")!=checksum)throw std::runtime_error("Missing engine contract or checksum mismatch; rebuild with scripts/build_engine.ps1");
    if(get(L"TensorRT")!=L"10.13.3.9"||get(L"Width")!=std::to_wstring(config.width)||get(L"Height")!=std::to_wstring(config.height)||get(L"Color")!=(config.bgr?L"BGR":L"RGB"))
        throw std::runtime_error("Engine contract does not match runtime, dimensions or color order");
    cudaDeviceProp p{};ck(cudaGetDeviceProperties(&p,device));std::ostringstream uuid;uuid<<"GPU-"<<std::hex<<std::setfill('0');
    for(int i=0;i<16;++i){if(i==4||i==6||i==8||i==10)uuid<<'-';uuid<<std::setw(2)<<int(static_cast<unsigned char>(p.uuid.bytes[i]));}
    auto id=uuid.str();if(get(L"GPU")!=std::wstring(id.begin(),id.end()))throw std::runtime_error("Engine belongs to another GPU; rebuild locally");
}
void* create(const cel::BackendConfig* config,char* err,uint32_t cap){
    try {
        if(!config||config->abi!=cel::backend_abi||!config->device)throw std::runtime_error("Backend ABI/device mismatch");
        auto s=std::make_unique<State>();s->w=config->width;s->h=config->height;s->bgr=config->bgr;s->identity=config->identity;
        if(s->w<8||s->h<8||s->w>3840||s->h>2160)throw std::runtime_error("Invalid processing dimensions");
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if(FAILED(config->device->QueryInterface(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->GetAdapter(&adapter)))throw std::runtime_error("Cannot identify graphics adapter");
        ck(cudaD3D11GetDevice(&s->device,adapter.Get()));ck(cudaSetDevice(s->device));
        ck(cudaStreamCreateWithFlags(&s->stream,cudaStreamNonBlocking));
        for(auto& e:s->events)ck(cudaEventCreate(&e));
        auto ptx=read(config->kernel_path);ptx.push_back(0);dk(cuModuleLoadData(&s->module,ptx.data()));
        dk(cuModuleGetFunction(&s->pre,s->module,"preprocess"));dk(cuModuleGetFunction(&s->post,s->module,"postprocess"));
        size_t bytes=size_t(s->w)*s->h*3*sizeof(float);
        ck(cudaMalloc(reinterpret_cast<void**>(&s->input),bytes));ck(cudaMalloc(reinterpret_cast<void**>(&s->output),bytes));
        if(!s->identity){
            auto data=read(config->engine_path);
            validate_contract(*config,data,s->device);
            s->runtime.reset(nvinfer1::createInferRuntime(s->logger));
            if(!s->runtime)throw std::runtime_error("TensorRT runtime creation failed");
            s->engine.reset(s->runtime->deserializeCudaEngine(data.data(),data.size()));
            if(!s->engine)throw std::runtime_error("TensorRT engine rejected: "+s->logger.last_error);
            if(s->engine->getNbIOTensors()!=2)throw std::runtime_error("Expected one input and one output");
            for(int i=0;i<2;++i){
                const char* name=s->engine->getIOTensorName(i);auto d=s->engine->getTensorShape(name);
                if(d.nbDims!=4||d.d[0]!=1||d.d[1]!=s->h||d.d[2]!=s->w||d.d[3]!=3||
                   s->engine->getTensorDataType(name)!=nvinfer1::DataType::kFLOAT||
                   s->engine->getTensorLocation(name)!=nvinfer1::TensorLocation::kDEVICE)
                    throw std::runtime_error("Engine contract must be fixed FP32 NHWC [1,H,W,3] I/O (internal FP16 allowed)");
                if(s->engine->getTensorIOMode(name)==nvinfer1::TensorIOMode::kINPUT)s->in_name=name;else s->out_name=name;
            }
            if(s->in_name.empty()||s->out_name.empty())throw std::runtime_error("Invalid input/output tensor modes");
            s->context.reset(s->engine->createExecutionContext());
            if(!s->context||!s->context->setTensorAddress(s->in_name.c_str(),s->input)||!s->context->setTensorAddress(s->out_name.c_str(),s->output))
                throw std::runtime_error("TensorRT context/tensor binding failed");
            ck(cudaMemsetAsync(s->input,0,bytes,s->stream));
            // Warm up before exposing the backend to the render thread.
            for(int i=0;i<3;++i)if(!s->context->enqueueV3(s->stream))throw std::runtime_error("TensorRT warm-up failed: "+s->logger.last_error);
            ck(cudaStreamSynchronize(s->stream));
        }
        return s.release();
    }catch(const std::exception& e){copy_error(err,cap,e.what());return nullptr;}
}
void destroy(void* state){delete static_cast<State*>(state);}
bool bind(void* state,ID3D11Texture2D* input,ID3D11Texture2D* output,char* err,uint32_t cap){
    auto& s=*static_cast<State*>(state);
    try {
        s.unbind();D3D11_TEXTURE2D_DESC a{},b{};input->GetDesc(&a);output->GetDesc(&b);
        if(a.Width!=b.Width||a.Height!=b.Height||a.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||a.SampleDesc.Count!=1)
            throw std::runtime_error("CUDA interop requires matching owned RGBA8 textures");
        s.sw=int(a.Width);s.sh=int(a.Height);
        ck(cudaGraphicsD3D11RegisterResource(&s.graphics[0],input,cudaGraphicsRegisterFlagsNone));
        ck(cudaGraphicsResourceSetMapFlags(s.graphics[0],cudaGraphicsMapFlagsReadOnly));
        ck(cudaGraphicsD3D11RegisterResource(&s.graphics[1],output,cudaGraphicsRegisterFlagsSurfaceLoadStore));
        ck(cudaGraphicsResourceSetMapFlags(s.graphics[1],cudaGraphicsMapFlagsWriteDiscard));
        return true;
    }catch(const std::exception& e){s.unbind();copy_error(err,cap,e.what());return false;}
}
bool process(void* state,cel::Timings* timing,char* err,uint32_t cap){
    auto& s=*static_cast<State*>(state);bool mapped=false;cudaTextureObject_t tex{};cudaSurfaceObject_t surf{};
    try {
        ck(cudaSetDevice(s.device));
        if(!s.graphics[0]||!s.graphics[1])throw std::runtime_error("Textures are not bound");
        // Map/unmap provide the graphics-compute ownership handoff on this same adapter.
        ck(cudaGraphicsMapResources(2,s.graphics,s.stream));mapped=true;
        cudaArray_t src{},dst{};
        ck(cudaGraphicsSubResourceGetMappedArray(&src,s.graphics[0],0,0));
        ck(cudaGraphicsSubResourceGetMappedArray(&dst,s.graphics[1],0,0));
        cudaResourceDesc rd{};rd.resType=cudaResourceTypeArray;rd.res.array.array=src;
        cudaTextureDesc td{};td.addressMode[0]=td.addressMode[1]=cudaAddressModeClamp;
        td.filterMode=cudaFilterModeLinear;td.readMode=cudaReadModeNormalizedFloat;
        ck(cudaCreateTextureObject(&tex,&rd,&td,nullptr));rd.res.array.array=dst;ck(cudaCreateSurfaceObject(&surf,&rd));
        ck(cudaEventRecord(s.events[0],s.stream));int bgr=s.bgr?1:0;
        void* preArgs[]={&tex,&s.input,&s.sw,&s.sh,&s.w,&s.h,&bgr};
        dk(cuLaunchKernel(s.pre,(s.w+15)/16,(s.h+15)/16,1,16,16,1,0,s.stream,preArgs,nullptr));
        ck(cudaEventRecord(s.events[1],s.stream));
        if(s.identity)ck(cudaMemcpyAsync(s.output,s.input,size_t(s.w)*s.h*3*sizeof(float),cudaMemcpyDeviceToDevice,s.stream));
        else if(!s.context->enqueueV3(s.stream))throw std::runtime_error("TensorRT enqueue failed: "+s.logger.last_error);
        ck(cudaEventRecord(s.events[2],s.stream));
        void* postArgs[]={&s.output,&surf,&tex,&s.sw,&s.sh,&s.w,&s.h,&bgr};
        dk(cuLaunchKernel(s.post,(s.sw+15)/16,(s.sh+15)/16,1,16,16,1,0,s.stream,postArgs,nullptr));
        ck(cudaEventRecord(s.events[3],s.stream));
        ck(cudaStreamSynchronize(s.stream)); // Correct same-frame output before graphics consumes it.
        ck(cudaDestroyTextureObject(tex));tex=0;ck(cudaDestroySurfaceObject(surf));surf=0;
        ck(cudaGraphicsUnmapResources(2,s.graphics,s.stream));mapped=false;ck(cudaStreamSynchronize(s.stream));
        if(timing){
            ck(cudaEventElapsedTime(&timing->preprocess_ms,s.events[0],s.events[1]));
            ck(cudaEventElapsedTime(&timing->inference_ms,s.events[1],s.events[2]));
            ck(cudaEventElapsedTime(&timing->postprocess_ms,s.events[2],s.events[3]));
            ck(cudaEventElapsedTime(&timing->total_ms,s.events[0],s.events[3]));
            timing->allocated_bytes=uint64_t(s.w)*s.h*3*4*2+(s.engine?s.engine->getDeviceMemorySizeV2():0);
        }
        return true;
    }catch(const std::exception& e){
        cudaStreamSynchronize(s.stream);if(tex)cudaDestroyTextureObject(tex);if(surf)cudaDestroySurfaceObject(surf);
        if(mapped){cudaGraphicsUnmapResources(2,s.graphics,s.stream);cudaStreamSynchronize(s.stream);}
        copy_error(err,cap,e.what());return false;
    }
}
const cel::BackendApi api{cel::backend_abi,create,destroy,bind,process};
}
extern "C" __declspec(dllexport) const cel::BackendApi* CelGetBackendApi(){return &api;}
