#include <nvrtc.h>
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=4) { std::cerr<<"Usage: cel_compile_kernels source.cu output.ptx cuda_include\n"; return 2; }
    auto dll=LoadLibraryW(L"nvrtc64_120_0.dll");if(!dll){std::cerr<<"NVRTC DLL unavailable\n";return 1;}
#define LOAD(name) auto name=reinterpret_cast<decltype(&::name)>(GetProcAddress(dll,#name));if(!name)return 1;
    LOAD(nvrtcCreateProgram) LOAD(nvrtcCompileProgram) LOAD(nvrtcGetProgramLogSize) LOAD(nvrtcGetProgramLog)
    LOAD(nvrtcDestroyProgram) LOAD(nvrtcGetPTXSize) LOAD(nvrtcGetPTX)
#undef LOAD
    std::ifstream f(argv[1]); std::string source((std::istreambuf_iterator<char>(f)),{});
    if(source.empty())return 2;
    nvrtcProgram p{};
    if(nvrtcCreateProgram(&p,source.c_str(),argv[1],0,nullptr,nullptr)!=NVRTC_SUCCESS)return 1;
    std::string inc=std::string("--include-path=")+argv[3];
    const char* opts[]={"--gpu-architecture=compute_75","--std=c++17",inc.c_str()};
    auto result=nvrtcCompileProgram(p,3,opts);
    size_t n=0; nvrtcGetProgramLogSize(p,&n); std::vector<char> log(n); nvrtcGetProgramLog(p,log.data());
    if(n>1)std::cerr<<log.data();
    if(result!=NVRTC_SUCCESS){ nvrtcDestroyProgram(&p);return 1; }
    nvrtcGetPTXSize(p,&n);std::vector<char> ptx(n);nvrtcGetPTX(p,ptx.data());nvrtcDestroyProgram(&p);
    std::ofstream out(argv[2],std::ios::binary);out.write(ptx.data(),std::streamsize(n));
    return out?0:1;
}
