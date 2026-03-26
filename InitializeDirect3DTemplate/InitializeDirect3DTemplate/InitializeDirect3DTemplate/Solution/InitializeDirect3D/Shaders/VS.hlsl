cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3   gEyePosW;
    float    cbPerObjectPad1;
    float2   gRenderTargetSize;
    float2   gInvRenderTargetSize;
    float    gNearZ;
    float    gFarZ;
    float    gTotalTime;
    float    gDeltaTime;
    float4   gAmbientLight;
    float3   gDirectionalLightDirection; float pad1;
    float3   gDirectionalLightColor;     float pad2;
    float4   gPointLightPositions[10];
    float4   gPointLightColors[10];
    float    gPointLightRanges[10];
    int      gNumPointLights;            float3 pad3;
};

struct VertexIn
{
    float3 PosL   : POSITION;
    float4 Color  : COLOR;
    float2 Tex    : TEXCOORD;
    float3 Normal : NORMAL;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION0;
    float4 Color   : COLOR0;
    float2 Tex     : TEXCOORD0;
    float3 NormalW : NORMAL0;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW  = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW    = posW.xyz;
    vout.PosH    = mul(posW, gViewProj);
    vout.Color   = vin.Color;
    vout.Tex     = vin.Tex;
    vout.NormalW = mul(vin.Normal, (float3x3)gWorld);

    return vout;
}