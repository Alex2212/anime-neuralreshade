#include "interop/frame_bridge.hpp"
#include "inference/backend_api.hpp"
#include <wincodec.h>
#include <filesystem>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <numeric>
int wmain(int argc,wchar_t** argv){
    if(argc!=9){std::cerr<<"Usage: cel_benchmark input_image output.png engine.plan|identity model_width model_height rgb|bgr iterations backend_directory\n";return 2;}
    HMODULE dll{};void* state{};const cel::BackendApi* api{};
    try{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        cel::ComPtr<IWICImagingFactory> factory;cel::check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
        cel::ComPtr<IWICBitmapDecoder> decoder;cel::check(factory->CreateDecoderFromFilename(argv[1],nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Decode image");
        cel::ComPtr<IWICBitmapFrameDecode> frame;cel::check(decoder->GetFrame(0,&frame),"Decode frame");
        cel::ComPtr<IWICFormatConverter> conv;cel::check(factory->CreateFormatConverter(&conv),"Create converter");
        cel::check(conv->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert RGBA");
        UINT width=0,height=0;conv->GetSize(&width,&height);std::vector<uint8_t> pixels(size_t(width)*height*4);
        cel::check(conv->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Read image");
        cel::ComPtr<ID3D11Device> d;cel::ComPtr<ID3D11DeviceContext> c;
        cel::check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c),"Create device");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.ArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA initial{pixels.data(),width*4,0};cel::ComPtr<ID3D11Texture2D> source,readback;
        cel::check(d->CreateTexture2D(&desc,&initial,&source),"Source texture");
        cel::FrameBridge bridge;bridge.initialize(d.Get(),desc);
        auto dir=std::filesystem::absolute(argv[8]);auto lib=dir/L"cel_nvidia.dll",kernels=dir/L"kernels.ptx";
        dll=LoadLibraryExW(lib.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!dll)throw std::runtime_error("Load backend failed: "+std::to_string(GetLastError()));
        auto get=reinterpret_cast<cel::GetBackendApi>(GetProcAddress(dll,"CelGetBackendApi"));
        if(!get||(api=get())->abi!=cel::backend_abi)throw std::runtime_error("Backend ABI mismatch");
        cel::BackendConfig cfg;cfg.device=d.Get();cfg.engine_path=argv[3];cfg.kernel_path=kernels.c_str();
        cfg.width=_wtoi(argv[4]);cfg.height=_wtoi(argv[5]);cfg.bgr=wcscmp(argv[6],L"bgr")==0;cfg.identity=wcscmp(argv[3],L"identity")==0;
        char err[2048]{};state=api->create(&cfg,err,sizeof(err));if(!state)throw std::runtime_error(err);
        if(!api->bind(state,bridge.input(),bridge.output(),err,sizeof(err)))throw std::runtime_error(err);
        int count=std::clamp(_wtoi(argv[7]),1,10000);std::vector<double> times,inference;
        cel::Timings t{};
        for(int i=-5;i<count;++i){
            auto start=std::chrono::steady_clock::now();bridge.capture(c.Get(),source.Get());
            if(!api->process(state,&t,err,sizeof(err)))throw std::runtime_error(err);
            auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(i>=0){times.push_back(elapsed);inference.push_back(t.inference_ms);}
        }
        desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        cel::check(d->CreateTexture2D(&desc,nullptr,&readback),"Readback texture");c->CopyResource(readback.Get(),bridge.output());
        D3D11_MAPPED_SUBRESOURCE map{};cel::check(c->Map(readback.Get(),0,D3D11_MAP_READ,0,&map),"Read output");
        for(UINT y=0;y<height;++y)memcpy(pixels.data()+size_t(y)*width*4,static_cast<uint8_t*>(map.pData)+size_t(y)*map.RowPitch,width*4);
        c->Unmap(readback.Get(),0);
        cel::ComPtr<IWICStream> stream;factory->CreateStream(&stream);cel::check(stream->InitializeFromFilename(argv[2],GENERIC_WRITE),"Output file");
        cel::ComPtr<IWICBitmapEncoder> encoder;factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);
        cel::check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Encoder init");
        cel::ComPtr<IWICBitmapFrameEncode> out;encoder->CreateNewFrame(&out,nullptr);out->Initialize(nullptr);out->SetSize(width,height);
        WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;out->SetPixelFormat(&fmt);
        if(fmt!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder rejected BGRA");
        for(size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
        cel::check(out->WritePixels(height,width*4,UINT(pixels.size()),pixels.data()),"Write pixels");out->Commit();encoder->Commit();
        std::sort(times.begin(),times.end());
        auto percentile=[&](double p){return times[std::min(times.size()-1,size_t(std::ceil(p*times.size())-1))];};
        std::ostringstream report;
        report<<"{\"iterations\":"<<count<<",\"display_width\":"<<width<<",\"display_height\":"<<height
                 <<",\"model_width\":"<<cfg.width<<",\"model_height\":"<<cfg.height
                 <<",\"pipeline_p50_ms\":"<<percentile(.5)<<",\"pipeline_p95_ms\":"<<percentile(.95)<<",\"pipeline_p99_ms\":"<<percentile(.99)
                 <<",\"inference_mean_ms\":"<<std::accumulate(inference.begin(),inference.end(),0.0)/count
                 <<",\"last_preprocess_ms\":"<<t.preprocess_ms<<",\"last_postprocess_ms\":"<<t.postprocess_ms
                 <<",\"note\":\"Static-image GPU path; excludes game, ReShade FX and presentation; diagnostic CPU readback occurs after timing\"}\n";
        std::ofstream(std::filesystem::path(argv[2]).replace_extension(L".json"))<<report.str();std::cout<<report.str();
        api->destroy(state);state=nullptr;FreeLibrary(dll);dll=nullptr;return 0;
    }catch(const std::exception& e){if(state&&api)api->destroy(state);if(dll)FreeLibrary(dll);std::cerr<<e.what()<<'\n';return 1;}
}
