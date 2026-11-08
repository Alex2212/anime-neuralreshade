// Runtime-compiled once during background preparation. Display-encoded RGB is intentional.
// Half-pixel bilinear sampling; aspect-preserving edge padding, reversed on output.
#include <cuda_runtime.h>
extern "C" __global__ void preprocess(cudaTextureObject_t src, float* tensor,
    int sw, int sh, int mw, int mh, int bgr) {
    int x = blockIdx.x*blockDim.x+threadIdx.x, y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=mw || y>=mh) return;
    float scale=fminf(float(mw)/sw,float(mh)/sh);
    float ox=(mw-sw*scale)*0.5f, oy=(mh-sh*scale)*0.5f;
    float4 p=tex2D<float4>(src,(x+0.5f-ox)/scale,(y+0.5f-oy)/scale);
    int i=(y*mw+x)*3;
    tensor[i]=(bgr?p.z:p.x)*2-1; tensor[i+1]=p.y*2-1; tensor[i+2]=(bgr?p.x:p.z)*2-1;
}
__device__ float sample(const float* t,float x,float y,int w,int h,int c) {
    x=fminf(fmaxf(x,0),float(w-1)); y=fminf(fmaxf(y,0),float(h-1));
    int x0=int(x),y0=int(y),x1=min(x0+1,w-1),y1=min(y0+1,h-1);
    float a=x-x0,b=y-y0;
    return (t[(y0*w+x0)*3+c]*(1-a)+t[(y0*w+x1)*3+c]*a)*(1-b)+
           (t[(y1*w+x0)*3+c]*(1-a)+t[(y1*w+x1)*3+c]*a)*b;
}
__device__ unsigned char quantize(float x) { return (unsigned char)(fminf(fmaxf((x+1)*127.5f,0),255)+0.5f); }
extern "C" __global__ void postprocess(const float* tensor,cudaSurfaceObject_t dst,cudaTextureObject_t src,
    int sw,int sh,int mw,int mh,int bgr) {
    int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=sw||y>=sh)return;
    float scale=fminf(float(mw)/sw,float(mh)/sh);
    float sx=(x+0.5f)*scale+(mw-sw*scale)*0.5f-0.5f;
    float sy=(y+0.5f)*scale+(mh-sh*scale)*0.5f-0.5f;
    float4 original=tex2D<float4>(src,x+0.5f,y+0.5f);
    uchar4 p=make_uchar4(quantize(sample(tensor,sx,sy,mw,mh,bgr?2:0)),
        quantize(sample(tensor,sx,sy,mw,mh,1)),quantize(sample(tensor,sx,sy,mw,mh,bgr?0:2)),
        (unsigned char)(original.w*255+0.5f));
    surf2Dwrite(p,dst,x*4,y);
}
