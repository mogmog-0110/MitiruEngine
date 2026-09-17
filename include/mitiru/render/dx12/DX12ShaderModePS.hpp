#pragma once

/// @file DX12ShaderModePS.hpp
/// @brief DX12 メインパス用の ShaderMode3D 別ピクセルシェーダー（MRT 出力）
/// @details DX12 のメインパスは MRT (RT0=color + RT1=worldNormal) なので、
///          DX11 用の `DefaultShaders3D.hpp` の単一 SV_TARGET PS をそのまま使えず、
///          バリアントごとに MRT 対応版を用意する。
///
///          全モード共通の頂点シェーダー: `TOON_VS_3D` (出力 VSOutput が同形)。
///          ライティングは `CbLighting (b1)` から material + cameraPos + 単一光源を読む。
///          マルチライト経路は `DX12MultiLightShaders.hpp` (b2=CbLightArray) に分離。

namespace mitiru::render
{

/// @brief MRT 用 Toon PS。量子化 Lambert + リム + albedo テクスチャ
///        DX11 の TOON_PS_3D は単一 SV_TARGET。DX12 メインパスは MRT (color+normal)
///        + t0 albedo を扱うため、ここで DX12 専用の変種を用意する。
inline constexpr const char* DX12_TOON_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 ShadowTint;   // 影部でアルベドに掛ける係数
    float4 FogColor;
    float4 FogParams;    // x=開始距離 y=完全に染まる距離 z=有効
    float4 MaterialParams;
};

Texture2D                g_albedo : register(t0);
Texture2D                g_shadow : register(t1);
Texture2D                g_shadowFar : register(t2);
SamplerState             g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}

SamplerComparisonState   g_pcf    : register(s1);

// B13 カスケードシャドウ。LightViewProj (カスケード0) は VS が読んで LightSpacePos を
// 計算済みなのでここでは未使用、LightViewProjFar (カスケード1) は WorldPos から
// このシェーダー自身が light-space 座標を組む。
cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float2   _padCascade;
    float4x4 LightViewProjFar2;
};

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

float samplePCFTex(Texture2D shadowTex, float3 ndc)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;
    // 光の錐台の外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    float shadow = 0.0;
    const float texelSize = 1.0 / 1024.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            shadow += shadowTex.SampleCmpLevelZero(g_pcf, uv + float2(x, y) * texelSize, depthRef);
        }
    }
    return shadow / 9.0;
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。u を列の中へ写し、PCF の
// タップが隣の列へはみ出さないよう列の内側に収める。
float samplePCFAtlas(float3 ndc, float column)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelV = 1.0 / 1024.0;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    float shadow = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 tap = float2(clamp((uv.x + column) * 0.5 + x * texelU, uMin, uMax), uv.y + y * texelV);
            shadow += g_shadowFar.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return shadow / 9.0;
}

// B13: カメラ距離が CascadeSplitDistance 未満ならカスケード0 (VS 計算済みの
// LightSpacePos)、それ以上ならカスケード1 (WorldPos から自前で計算) を使う。
// カスケード無効時は CPU 側が CascadeSplitDistance に非常に大きい値を入れるため
// 常にカスケード0 を通る (従来と同じ結果)。
float sampleCascadedShadow(float3 worldPos, float4 lightSpacePos0, float distanceFromCamera)
{
    if (distanceFromCamera < CascadeSplitDistance)
    {
        float3 ndc = lightSpacePos0.xyz / max(lightSpacePos0.w, 1e-4);
        return samplePCFTex(g_shadow, ndc);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        float4 lsFar = mul(LightViewProjFar, float4(worldPos, 1.0));
        return samplePCFAtlas(lsFar.xyz / max(lsFar.w, 1e-4), 0.0);
    }
    float4 lsFar2 = mul(LightViewProjFar2, float4(worldPos, 1.0));
    return samplePCFAtlas(lsFar2.xyz / max(lsFar2.w, 1e-4), 1.0);
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    float distToCamera = length(CameraPos - input.WorldPos);
    float castShadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera);

    float lambert = saturate(dot(N, L)) * castShadow;
    float band = smoothstep(0.44, 0.56, lambert);

    float3 tone = lerp(ShadowTint, float3(1.0, 1.0, 1.0), band);
    float3 color = albedo * tone * LightColor + AmbientColor * albedo * 0.30;

    if (FogParams.z > 0.5)
    {
        float dist = length(CameraPos - input.WorldPos);
        float f = saturate((dist - FogParams.x) / max(FogParams.y - FogParams.x, 0.001));
        color = lerp(color, FogColor.rgb, f);
    }

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(color, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)hlsl";

/// @brief MRT 用 Fresnel Toon PS。OutlineMode::Fresnel でメイン PS を差し替える
///        DX12_TOON_PS_3D にシルエット付近 (NdotV 小) の暗化を加えた変種。
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

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1)
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

    float3 finalColor = ambient + diffuse + specular;
    finalColor = lerp(finalColor, outlineColor, fresnelEdge * 0.95);
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(finalColor, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    return o;
}
)hlsl";

/// @brief MRT 用 Phong PS。単一光源 Lambert + Phong + albedo テクスチャ + shadow PCF
inline constexpr const char* DX12_PHONG_PS_3D = R"hlsl(
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

Texture2D                g_albedo  : register(t0);
Texture2D                g_shadow  : register(t1);
Texture2D                g_shadowFar : register(t2);
SamplerState             g_samp    : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}

SamplerComparisonState   g_pcf     : register(s1);

// B13 カスケードシャドウ。DX12_TOON_PS_3D と同じ規約 (LightViewProj はここでは未使用、
// LightViewProjFar + CascadeSplitDistance だけカスケード1 の判定/サンプルに使う)。
cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float2   _padCascade;
    float4x4 LightViewProjFar2;
};

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

// 3x3 PCF (depth bias 込み)
float samplePCFTex(Texture2D shadowTex, float3 ndc)
{
    // ndc: [-1,1] xy → UV [0,1], z → DX [0,1] そのまま
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;  // shadow acne 抑制
    // light frustum 外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    float shadow = 0.0;
    const float texelSize = 1.0 / 1024.0;  // map size に整合させること
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowTex.SampleCmpLevelZero(g_pcf, uv + offset, depthRef);
        }
    }
    return shadow / 9.0;
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。u を列の中へ写し、PCF の
// タップが隣の列へはみ出さないよう列の内側に収める。
float samplePCFAtlas(float3 ndc, float column)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - 0.001;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelV = 1.0 / 1024.0;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    float shadow = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 tap = float2(clamp((uv.x + column) * 0.5 + x * texelU, uMin, uMax), uv.y + y * texelV);
            shadow += g_shadowFar.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return shadow / 9.0;
}

// B13: DX12_TOON_PS_3D の sampleCascadedShadow と同じ判定 (距離 < CascadeSplitDistance
// でカスケード0、それ以外はカスケード1)。カスケード無効時は CPU が分割距離を
// 非常に大きくするため常にカスケード0 (従来と同じ結果)。
float sampleCascadedShadow(float3 worldPos, float4 lightSpacePos0, float distanceFromCamera)
{
    if (distanceFromCamera < CascadeSplitDistance)
    {
        float3 ndc = lightSpacePos0.xyz / max(lightSpacePos0.w, 1e-4);
        return samplePCFTex(g_shadow, ndc);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        float4 lsFar = mul(LightViewProjFar, float4(worldPos, 1.0));
        return samplePCFAtlas(lsFar.xyz / max(lsFar.w, 1e-4), 0.0);
    }
    float4 lsFar2 = mul(LightViewProjFar2, float4(worldPos, 1.0));
    return samplePCFAtlas(lsFar2.xyz / max(lsFar2.w, 1e-4), 1.0);
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // shadow factor (1.0 = 影なし)
    float distToCamera = length(CameraPos - input.WorldPos);
    float shadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera);

    float3 ambient = AmbientColor * albedo;

    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL * shadow;

    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specPow = max(MaterialShininess, 1.0);
    float specFactor = pow(NdotH, specPow) * NdotL * shadow;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    float3 lit = ambient + diffuse + specular;
    if (FogParams.z > 0.5)
    {
        float dist = length(CameraPos - input.WorldPos);
        float f = saturate((dist - FogParams.x) / max(FogParams.y - FogParams.x, 0.001));
        lit = lerp(lit, FogColor.rgb, f);
    }

    PSOutput o;
    o.Color  = float4(lit, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
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

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1)
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
    o.Color  = float4(albedo, alpha);
    o.Normal = float4(N * 0.5 + 0.5, 1.0);
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

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1)
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
    o.Color  = float4(ambient + diffuse, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotL);
    return o;
}
)hlsl";

} // namespace mitiru::render
