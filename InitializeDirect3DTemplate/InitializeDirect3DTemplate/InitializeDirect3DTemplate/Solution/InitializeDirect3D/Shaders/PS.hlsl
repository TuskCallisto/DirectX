#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif
#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 10
#endif
#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

Texture2D    gDiffuseMap   : register(t0);
SamplerState gsamPointWrap : register(s0);

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3   gEyePosW;          float pad0;
    float2   gRenderTargetSize;
    float2   gInvRenderTargetSize;
    float    gNearZ;
    float    gFarZ;
    float    gTotalTime;
    float    gDeltaTime;
    float4   gAmbientLight;
    float3   gDirectionalLightDirection; float pad1;
    float3   gDirectionalLightColor;     float pad2;
    float4   gPointLightPositions[NUM_POINT_LIGHTS];
    float4   gPointLightColors[NUM_POINT_LIGHTS];
    float    gPointLightRanges[NUM_POINT_LIGHTS];
    int      gNumPointLights;            float3 pad3;
};
struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION0;
    float4 Color   : COLOR0;
    float2 Tex     : TEXCOORD0;
    float3 NormalW : NORMAL0;
};

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    return R0 + (1.0f - R0) * (f0*f0*f0*f0*f0);
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye)
{
    const float shininess     = 64.0f;
    float3 halfVec            = normalize(toEye + lightVec);
    float  roughnessFactor    = (shininess + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), shininess) / 8.0f;
    float3 fresnelFactor      = SchlickFresnel(float3(0.02f, 0.02f, 0.02f), halfVec, lightVec);
    float3 specAlbedo         = fresnelFactor * roughnessFactor;
    specAlbedo                = specAlbedo / (specAlbedo + 1.0f);
    return lightStrength + specAlbedo * lightStrength;
}

float3 ComputeDirectionalLight(float3 normal, float3 toEye)
{
    float3 lightVec      = normalize(-gDirectionalLightDirection);
    float  ndotl         = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = gDirectionalLightColor * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye);
}

float3 ComputePointLights(float3 posW, float3 normal, float3 toEye)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 10; ++i)
    {
        float3 toLight = gPointLightPositions[i].xyz - posW;
        float  dist    = length(toLight);
        float  range   = gPointLightRanges[i];
        if (dist < range && range > 0.0f)
        {
            toLight         = normalize(toLight);
            float  ndotl    = max(dot(normal, toLight), 0.0f);
            float  atten    = saturate(1.0f - (dist / range));
            atten           = atten * atten;
            float3 lightStr = gPointLightColors[i].rgb * ndotl * atten;
            result         += BlinnPhong(lightStr, toLight, normal, toEye);
        }
    }
    return result;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 toEye = normalize(gEyePosW - pin.PosW);

    if (pin.PosW.y > 0.25f && pin.PosW.y < 0.35f)
    {
        float2 scrolledTex = pin.Tex + float2(gTotalTime * 0.05f, gTotalTime * 0.02f);
        float4 waterColor  = gDiffuseMap.Sample(gsamPointWrap, scrolledTex);
        float4 blueTint    = float4(0.0f, 0.3f, 0.7f, 1.0f);
        float4 finalColor  = lerp(waterColor, blueTint, 0.4f);
        float3 normal      = float3(0.0f, 1.0f, 0.0f);
        float3 totalLight  = gAmbientLight.rgb
                           + ComputeDirectionalLight(normal, toEye)
                           + ComputePointLights(pin.PosW, normal, toEye);
        return float4(finalColor.rgb * totalLight, 0.5f);
    }

    float4 texColor   = gDiffuseMap.Sample(gsamPointWrap, pin.Tex);
    float3 normal     = normalize(pin.NormalW);
    float3 totalLight = gAmbientLight.rgb
                      + ComputeDirectionalLight(normal, toEye)
                      + ComputePointLights(pin.PosW, normal, toEye);
    return float4(texColor.rgb * totalLight, 0.35f);
}