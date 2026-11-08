// Original companion effects. SDR display-encoded input; no implicit sRGB conversion.
texture CelBackBuffer : COLOR;
texture CelOriginal : CEL_ORIGINAL;
texture CelNeural : CEL_NEURAL;
sampler CelCurrent { Texture=CelBackBuffer; SRGBTexture=false; };
sampler CelOriginalSampler { Texture=CelOriginal; SRGBTexture=false; };
sampler CelNeuralSampler { Texture=CelNeural; SRGBTexture=false; };
uniform bool CelEnabled < hidden=true; > = false;
uniform bool CelBridgeReady < hidden=true; > = false;
uniform bool CelNeuralReady < hidden=true; > = false;
uniform bool CelSplit < ui_category="Comparison and HUD"; ui_label="Split comparison"; > = false;
uniform float CelSplitPosition < ui_category="Comparison and HUD"; ui_label="Split position"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.5;
uniform bool CelMask1 < ui_category="Comparison and HUD"; ui_label="HUD rectangle 1"; > = false;
uniform float4 CelRect1 < ui_category="Comparison and HUD"; ui_label="Rectangle 1 left/top/right/bottom"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = float4(0,0,0.4,0.2);
uniform bool CelMask2 < ui_category="Comparison and HUD"; ui_label="HUD rectangle 2"; > = false;
uniform float4 CelRect2 < ui_category="Comparison and HUD"; ui_label="Rectangle 2 left/top/right/bottom"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = float4(0.7,0.65,1,1);
uniform float CelStrength < ui_category="Neural"; ui_label="Neural blend"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.7;
uniform float CelSmoothing < ui_category="Surface smoothing"; ui_label="Edge-preserving smoothing"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.0;
uniform float CelToneAmount < ui_category="Two tone"; ui_label="Two-tone amount"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.0;
uniform float CelThreshold < ui_category="Two tone"; ui_label="Shadow threshold"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.4;
uniform float CelSoftness < ui_category="Two tone"; ui_label="Transition softness"; ui_type="slider"; ui_min=0.0; ui_max=0.25; > = 0.0;
uniform float CelShadowLevel < ui_category="Two tone"; ui_label="Shadow luminance"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.16;
uniform float CelLightLevel < ui_category="Two tone"; ui_label="Light luminance"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.65;
uniform bool CelLiteral < ui_category="Two tone"; ui_label="Literal two-color palette"; > = false;
uniform float3 CelLightTint < ui_category="Two tone"; ui_label="Light color"; ui_type="color"; > = float3(1.0,0.96,0.88);
uniform float3 CelShadowTint < ui_category="Two tone"; ui_label="Shadow color"; ui_type="color"; > = float3(0.24,0.22,0.34);
uniform float CelTintStrength < ui_category="Two tone"; ui_label="Tint strength"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.1;
uniform float CelOutlineStrength < ui_category="Outlines"; ui_label="Outline strength"; ui_type="slider"; ui_min=0.0; ui_max=1.0; > = 0.4;
uniform float CelOutlineWidth < ui_category="Outlines"; ui_label="Width in display pixels"; ui_type="slider"; ui_min=0.5; ui_max=3.0; > = 1.0;
uniform float CelOutlineThreshold < ui_category="Outlines"; ui_label="Edge threshold"; ui_type="slider"; ui_min=0.01; ui_max=1.0; > = 0.16;
uniform float CelSaturation < ui_category="Color"; ui_label="Saturation"; ui_type="slider"; ui_min=0.0; ui_max=2.0; > = 1.1;
uniform float CelExposure < ui_category="Color"; ui_label="Exposure stops"; ui_type="slider"; ui_min=-2.0; ui_max=2.0; > = 0.0;
void CelVS(uint id:SV_VertexID,out float4 pos:SV_Position,out float2 uv:TEXCOORD){
    uv=float2((id<<1)&2,id&2);pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
bool CelProtected(float2 uv){
    return (CelSplit && uv.x>=CelSplitPosition)||
        (CelMask1&&all(uv>=CelRect1.xy)&&all(uv<=CelRect1.zw))||
        (CelMask2&&all(uv>=CelRect2.xy)&&all(uv<=CelRect2.zw));
}
float CelLuma(float3 c){return dot(c,float3(0.2126,0.7152,0.0722));} // Rec.709 weights in display-encoded working space
float4 CelFinish(float2 uv,float4 value){
    float4 result=tex2D(CelCurrent,uv);
    if(CelEnabled&&CelBridgeReady){
        result=value;
        if(CelProtected(uv))result=tex2D(CelOriginalSampler,uv);
    }
    return result;
}
float4 CelCompositePS(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
    float4 original=tex2D(CelCurrent,uv),neural=tex2D(CelNeuralSampler,uv);
    return CelFinish(uv,float4(lerp(original.rgb,neural.rgb,CelNeuralReady?CelStrength:0),original.a));
}
float4 CelSmoothPS(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
    float4 c=tex2D(CelCurrent,uv);float3 sum=c.rgb;float weight=1;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
        float3 n=tex2D(CelCurrent,uv+float2(x*BUFFER_RCP_WIDTH,y*BUFFER_RCP_HEIGHT)).rgb;
        float w=exp(-dot(n-c.rgb,n-c.rgb)*80);sum+=n*w;weight+=w;
    }
    return CelFinish(uv,float4(lerp(c.rgb,sum/weight,CelSmoothing),c.a));
}
float4 CelTonePS(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
    float4 c=tex2D(CelCurrent,uv);float l=CelLuma(c.rgb);
    float mask=CelSoftness<=0?step(CelThreshold,l):smoothstep(CelThreshold-CelSoftness,CelThreshold+CelSoftness,l);
    float3 tint=lerp(CelShadowTint,CelLightTint,mask);
    float3 target=CelLiteral?lerp(CelShadowTint,CelLightTint,step(CelThreshold,l)):
        saturate(c.rgb*(lerp(CelShadowLevel,CelLightLevel,mask)/max(l,0.001)))*lerp(float3(1,1,1),tint,CelTintStrength);
    return CelFinish(uv,float4(lerp(c.rgb,target,CelToneAmount),c.a));
}
float4 CelOutlinePS(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
    float4 c=tex2D(CelCurrent,uv);float2 d=float2(BUFFER_RCP_WIDTH,BUFFER_RCP_HEIGHT)*CelOutlineWidth;
    float gx=CelLuma(tex2D(CelCurrent,uv+float2(d.x,0)).rgb)-CelLuma(tex2D(CelCurrent,uv-float2(d.x,0)).rgb);
    float gy=CelLuma(tex2D(CelCurrent,uv+float2(0,d.y)).rgb)-CelLuma(tex2D(CelCurrent,uv-float2(0,d.y)).rgb);
    float edge=smoothstep(CelOutlineThreshold,CelOutlineThreshold*2,length(float2(gx,gy)));
    return CelFinish(uv,float4(c.rgb*(1-edge*CelOutlineStrength),c.a));
}
float4 CelGradePS(float4 pos:SV_Position,float2 uv:TEXCOORD):SV_Target{
    float4 c=tex2D(CelCurrent,uv);float l=CelLuma(c.rgb);
    return CelFinish(uv,float4(saturate(lerp(l.xxx,c.rgb,CelSaturation)*exp2(CelExposure)),c.a));
}
technique Cel_Neural < ui_label="CelShade 1 - Neural composite"; > {pass{VertexShader=CelVS;PixelShader=CelCompositePS;}}
technique Cel_Smoothing < ui_label="CelShade 2 - Surface smoothing"; > {pass{VertexShader=CelVS;PixelShader=CelSmoothPS;}}
technique Cel_TwoTone < ui_label="CelShade 3 - Two tone"; > {pass{VertexShader=CelVS;PixelShader=CelTonePS;}}
technique Cel_Outlines < ui_label="CelShade 4 - Outlines"; > {pass{VertexShader=CelVS;PixelShader=CelOutlinePS;}}
technique Cel_Color < ui_label="CelShade 5 - Color"; > {pass{VertexShader=CelVS;PixelShader=CelGradePS;}}
