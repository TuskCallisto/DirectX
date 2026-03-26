Texture2DArray gTreeMapArray : register(t0);

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

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
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float3 gDirectionalLightDirection; float pad1;
    float3 gDirectionalLightColor;     float pad2;
    float4 gPointLightPositions[10];
    float4 gPointLightColors[10];
    float  gPointLightRanges[10];
    int    gNumPointLights; float3 pad3;
};

struct VertexIn
{
    float3 PosW  : POSITION;
    float2 SizeW : SIZE;
};

struct VertexOut
{
    float3 CenterW : POSITION;
    float2 SizeW   : SIZE;
};

struct GeoOut
{
    float4 PosH   : SV_POSITION;
    float3 PosW   : POSITION;
    float3 NormalW: NORMAL;
    float2 TexC   : TEXCOORD;
    uint   PrimID : SV_PrimitiveID;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.CenterW = vin.PosW;
    vout.SizeW   = vin.SizeW;
    return vout;
}

[maxvertexcount(4)]
void GS(point VertexOut gin[1],
        uint primID : SV_PrimitiveID,
        inout TriangleStream<GeoOut> triStream)
{
    float3 up   = float3(0.0f, 1.0f, 0.0f);
    float3 look = gEyePosW - gin[0].CenterW;
    look.y = 0.0f;
    look   = normalize(look);
    float3 right = cross(up, look);

    float halfWidth  = 0.5f * gin[0].SizeW.x;
    float halfHeight = 0.5f * gin[0].SizeW.y;

    float4 v[4];
    v[0] = float4(gin[0].CenterW + halfWidth*right - halfHeight*up, 1.0f);
    v[1] = float4(gin[0].CenterW + halfWidth*right + halfHeight*up, 1.0f);
    v[2] = float4(gin[0].CenterW - halfWidth*right - halfHeight*up, 1.0f);
    v[3] = float4(gin[0].CenterW - halfWidth*right + halfHeight*up, 1.0f);

    float2 texC[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    GeoOut gout;
    [unroll]
    for(int i = 0; i < 4; ++i)
    {
        gout.PosH    = mul(v[i], gViewProj);
        gout.PosW    = v[i].xyz;
        gout.NormalW = look;
        gout.TexC    = texC[i];
        gout.PrimID  = primID;
        triStream.Append(gout);
    }
}

float4 PS(GeoOut pin) : SV_Target
{
    float3 uvw = float3(pin.TexC, pin.PrimID % 3);
    float4 diffuseAlbedo = gTreeMapArray.Sample(gsamAnisotropicWrap, uvw);

#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif

    pin.NormalW = normalize(pin.NormalW);

    float3 normal = pin.NormalW;
    float3 dirLightDir = normalize(-gDirectionalLightDirection);
    float dirAmount = max(dot(normal, dirLightDir), 0.0f);
    float3 lightColor = gAmbientLight.rgb + gDirectionalLightColor * dirAmount;

    float4 litColor = float4(diffuseAlbedo.rgb * lightColor, diffuseAlbedo.a);
    return litColor;
}