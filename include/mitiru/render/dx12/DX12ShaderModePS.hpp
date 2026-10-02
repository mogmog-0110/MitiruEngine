#pragma once

/// @file DX12ShaderModePS.hpp
/// @brief DX12 メインパスの Fresnel / Unlit / Flat の PS (MRT 出力)
/// @details Toon / Phong / PBR は材質のマップと局所光を共有するので DX12LitShaders.hpp にある。
///          ここの PS は同じ VS 出力とルートシグネチャで、CbLighting (b1) の先頭だけを読む。

namespace mitiru::render
{

/// @brief MRT 用 Fresnel Toon PS。OutlineMode::Fresnel でメイン PS を差し替える
///        Toon にシルエット付近 (NdotV 小) の暗化を加えた変種。
///        DX11 世代 TOON_PS_3D_FRESNEL は LightSpacePos 無しで VS-PS linkage が
///        不成立だったため、DX12 VS の出力 signature に合わせてここへ移植。
inline constexpr const char* DX12_TOON_PS_3D_FRESNEL = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

// 描画ごとの色の調整 (DX12LitShaders.hpp の CbDrawEx と同じ並び)。ここでは照明の後に足す色だけを読む
cbuffer CbDrawEx : register(b2)
{
    float4 _unusedBaseColor;
    float4 _unusedEmissive;
    float4 _unusedMapFlags;
    float4 _unusedPbr;
    float4 TintAdd;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


// LightSpacePos は VS が出力するため PSInput でも宣言してレジスタ整合を取る。
struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // アンビエント
    float3 ambient = AmbientColor * albedo;

    // ディフューズ。NdotL を 3 段階に量子化（toon 帯）
    float rawNdotL = saturate(dot(N, L));
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * albedo * toon;

    // ハイライト
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specFactor = pow(NdotH, max(MaterialShininess, 1.0)) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    // Fresnel リム。シルエット付近 (NdotV 小) を暗化してアウトラインに
    float NdotV = saturate(dot(N, V));
    float fresnelEdge = 1.0 - smoothstep(0.0, 0.4, NdotV);
    float3 outlineColor = float3(0.08, 0.06, 0.04);

    float3 finalColor = ambient + diffuse + specular + TintAdd.rgb;
    finalColor = lerp(finalColor, outlineColor, fresnelEdge * 0.95);
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(finalColor, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Unlit PS。頂点色 × material diffuse × albedo テクスチャ
inline constexpr const char* DX12_UNLIT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 _unusedLightDir;    float _pad0;
    float3 _unusedLightColor;  float _pad1;
    float3 _unusedAmbient;     float _pad2;
    float3 _unusedCameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 _unusedSpecular;
    float  _unusedShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

// 描画ごとの色の調整 (DX12LitShaders.hpp の CbDrawEx と同じ並び)。ここでは照明の後に足す色だけを読む
cbuffer CbDrawEx : register(b2)
{
    float4 _unusedBaseColor;
    float4 _unusedEmissive;
    float4 _unusedMapFlags;
    float4 _unusedPbr;
    float4 TintAdd;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(albedo + TintAdd.rgb, alpha);
    o.Normal = float4(N * 0.5 + 0.5, 1.0);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Flat PS。面ごと一様陰影 + albedo テクスチャ
inline constexpr const char* DX12_FLAT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

// 描画ごとの色の調整 (DX12LitShaders.hpp の CbDrawEx と同じ並び)。ここでは照明の後に足す色だけを読む
cbuffer CbDrawEx : register(b2)
{
    float4 _unusedBaseColor;
    float4 _unusedEmissive;
    float4 _unusedMapFlags;
    float4 _unusedPbr;
    float4 TintAdd;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


struct PSInput
{
    float4 Position                  : SV_POSITION;
    float3 WorldPos                  : TEXCOORD0;
    nointerpolation float3 WorldNorm : TEXCOORD1;
    float2 TexCoord                  : TEXCOORD2;
    float4 LightSpacePos             : TEXCOORD3;
    float4 Color                     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    float3 ambient = AmbientColor * albedo;
    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(ambient + diffuse + TintAdd.rgb, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotL);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

} // namespace mitiru::render
