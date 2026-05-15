#pragma once

/// @file ToonShaders3D.hpp
/// @brief 3D描画用トゥーン/セルシェーダー（HLSL SM5.0）
/// @details セルシェーディングによるカートゥーン調の描画を提供する。
///          NdotLの量子化による離散的な陰影バンド、リムライト、彩度ブーストを含む。

namespace mitiru::render
{

/// @brief トゥーン/セルシェーディング用 頂点シェーダー（HLSL SM5.0）
/// @details DEFAULT_VS_3Dと同一のCbTransformレイアウト・入出力構造体を使用する。
constexpr const char* TOON_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(World, float4(input.Position, 1.0));
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul((float3x3)World, input.Normal));

    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief トゥーン/セルシェーディング用 ピクセルシェーダー（HLSL SM5.0）
/// @details NdotLを3段階に量子化し、リムライトと彩度ブーストでカートゥーン調に仕上げる。
constexpr const char* TOON_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;

    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    // アンビエント
    float3 ambient = AmbientColor * MaterialDiffuse.rgb;

    // ディフューズ — NdotLを量子化
    float rawNdotL = max(dot(N, L), 0.0);
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * MaterialDiffuse.rgb * toon;

    // スペキュラー
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specFactor = pow(NdotH, MaterialShininess) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float3 finalColor = ambient + diffuse + specular;
    float alpha = MaterialDiffuse.a * input.Color.a;

    output.Color = float4(finalColor * input.Color.rgb, alpha);
    // 法線をRT1に出力（[0,1]にパック + NdotVをアルファに）
    float NdotV = max(dot(N, V), 0.0);
    output.Normal = float4(N * 0.5 + 0.5, NdotV);

    return output;
}
)hlsl";

/// @brief アウトライン用 頂点シェーダー（HLSL SM5.0）
/// @details 背面を法線方向に膨張させてアウトラインを描画する。
constexpr const char* OUTLINE_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float3 expandedPos = input.Position + input.Normal * 0.012;
    float4 worldPos = mul(World, float4(expandedPos, 1.0));
    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);
    // アウトラインをメインパスの奥に押し出す（深度バッファで隠れる）
    output.Position.z += 0.002 * output.Position.w;
    output.Color = float4(0.1, 0.08, 0.06, 1.0);
    return output;
}
)hlsl";

/// @brief アウトライン用 ピクセルシェーダー（HLSL SM5.0）
/// @details 単色のダークカラーを出力する。
constexpr const char* OUTLINE_PS_3D = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
)hlsl";

/// @brief ポストプロセスアウトライン用 頂点シェーダー（フルスクリーン三角形）
constexpr const char* OUTLINE_POST_VS = R"hlsl(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // フルスクリーン三角形（3頂点、頂点バッファ不要）
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
)hlsl";

/// @brief ポストプロセスアウトライン用 ピクセルシェーダー（Sobelエッジ検出）
constexpr const char* OUTLINE_POST_PS = R"hlsl(
// MSAA 4x (ENG-105 v2) — sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;   // 1.0 / viewport size
    float OutlineWidth; // アウトライン太さ（ピクセル）
    float Threshold;    // エッジ検出閾値
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float sampleDepth(int2 pos)
{
    return DepthTexture.Load(pos, 0);
}

float linearizeDepth(float d, float near, float far)
{
    return near * far / (far - d * (far - near));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);

    float nearZ = 0.1;
    float farZ = 100.0;

    // 中心基準の深度差分（二重線防止）+ NdotVフィルタ
    float dC = linearizeDepth(sampleDepth(pos), nearZ, farZ);
    float dL = linearizeDepth(sampleDepth(pos + int2(-w, 0)), nearZ, farZ);
    float dR = linearizeDepth(sampleDepth(pos + int2( w, 0)), nearZ, farZ);
    float dU = linearizeDepth(sampleDepth(pos + int2( 0,-w)), nearZ, farZ);
    float dD = linearizeDepth(sampleDepth(pos + int2( 0, w)), nearZ, farZ);
    float edge = max(max(abs(dC - dL), abs(dC - dR)),
                     max(abs(dC - dU), abs(dC - dD)));

    float4 normalData = NormalTexture.Load(pos, 0);
    float NdotV = normalData.a;

    if (edge > Threshold && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, 1.0);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード2: 法線不連続検出 ピクセルシェーダー
/// @details 法線バッファの隣接ピクセル間のdot積でエッジを検出。深度不要。
constexpr const char* OUTLINE_POST_PS_LAPLACIAN = R"hlsl(
// MSAA 4x (ENG-105 v2) — sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float3 unpackNormal(float4 data)
{
    return data.rgb * 2.0 - 1.0;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);

    // 中心と4近傍の法線を取得
    float3 nC = unpackNormal(NormalTexture.Load(pos, 0));
    float3 nL = unpackNormal(NormalTexture.Load(pos + int2(-w, 0), 0));
    float3 nR = unpackNormal(NormalTexture.Load(pos + int2( w, 0), 0));
    float3 nU = unpackNormal(NormalTexture.Load(pos + int2( 0,-w), 0));
    float3 nD = unpackNormal(NormalTexture.Load(pos + int2( 0, w), 0));

    // 法線の差異: 1 - dot(n1, n2) で角度差を測定
    float edge = max(max(1.0 - dot(nC, nL), 1.0 - dot(nC, nR)),
                     max(1.0 - dot(nC, nU), 1.0 - dot(nC, nD)));

    float NdotV = NormalTexture.Load(pos, 0).a;

    if (edge > Threshold && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, 1.0);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード3: 深度Sobel + NdotVフィルタ ピクセルシェーダー
/// @details 深度Sobelに加え、法線バッファのNdotVで凹面内部を抑制する強化版。
constexpr const char* OUTLINE_POST_PS_DEPTH_NDOTV = R"hlsl(
// MSAA 4x (ENG-105 v2) — sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float sampleDepth(int2 pos)
{
    return DepthTexture.Load(pos, 0);
}

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);

    float nearZ = 0.1;
    float farZ = 100.0;

    // 中心基準の深度差分（二重線防止）
    float dC = linearizeDepth(sampleDepth(pos), nearZ, farZ);
    float dL = linearizeDepth(sampleDepth(pos + int2(-w, 0)), nearZ, farZ);
    float dR = linearizeDepth(sampleDepth(pos + int2( w, 0)), nearZ, farZ);
    float dU = linearizeDepth(sampleDepth(pos + int2( 0,-w)), nearZ, farZ);
    float dD = linearizeDepth(sampleDepth(pos + int2( 0, w)), nearZ, farZ);
    float edge = max(max(abs(dC - dL), abs(dC - dR)),
                     max(abs(dC - dU), abs(dC - dD)));

    // NdotVフィルタ: 凹面内部(NdotV低)を抑制
    float4 normalData = NormalTexture.Load(pos, 0);
    float NdotV = normalData.a;
    float ndotVMask = smoothstep(0.1, 0.35, NdotV);

    if (edge * ndotVMask > Threshold)
    {
        return float4(0.1, 0.08, 0.06, 1.0);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード4: 深度+法線 複合エッジ ピクセルシェーダー
/// @details 深度差と法線差の両方を考慮。凹面の法線変化だけではアウトラインを引かない。
constexpr const char* OUTLINE_POST_PS_COLOR_EDGE = R"hlsl(
// MSAA 4x (ENG-105 v2) — sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float sampleDepth(int2 pos)
{
    return DepthTexture.Load(pos, 0);
}

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float3 unpackNormal(float4 data)
{
    return data.rgb * 2.0 - 1.0;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);
    float nearZ = 0.1;
    float farZ = 100.0;

    // 中心基準の深度差分
    float dC = linearizeDepth(sampleDepth(pos), nearZ, farZ);
    float dL = linearizeDepth(sampleDepth(pos + int2(-w, 0)), nearZ, farZ);
    float dR = linearizeDepth(sampleDepth(pos + int2( w, 0)), nearZ, farZ);
    float dU = linearizeDepth(sampleDepth(pos + int2( 0,-w)), nearZ, farZ);
    float dD = linearizeDepth(sampleDepth(pos + int2( 0, w)), nearZ, farZ);
    float depthEdge = max(max(abs(dC - dL), abs(dC - dR)),
                          max(abs(dC - dU), abs(dC - dD)));

    // 法線差分
    float3 nC = unpackNormal(NormalTexture.Load(pos, 0));
    float3 nL = unpackNormal(NormalTexture.Load(pos + int2(-w, 0), 0));
    float3 nR = unpackNormal(NormalTexture.Load(pos + int2( w, 0), 0));
    float3 nU = unpackNormal(NormalTexture.Load(pos + int2( 0,-w), 0));
    float3 nD = unpackNormal(NormalTexture.Load(pos + int2( 0, w), 0));
    float normalEdge = max(max(1.0 - dot(nC, nL), 1.0 - dot(nC, nR)),
                           max(1.0 - dot(nC, nU), 1.0 - dot(nC, nD)));

    float NdotV = NormalTexture.Load(pos, 0).a;

    // 深度エッジ OR (法線エッジ AND 深度エッジ弱)
    bool hasEdge = depthEdge > Threshold ||
                   (normalEdge > Threshold * 0.8 && depthEdge > Threshold * 0.2);

    if (hasEdge && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, 1.0);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード5: 深度+色 複合エッジ ピクセルシェーダー
/// @details 深度エッジと色エッジの両方が閾値を超えた場合のみアウトラインを描画。
///          偽エッジを大幅に低減する。t0に深度、t1に法線、t2に色バッファコピー。
constexpr const char* OUTLINE_POST_PS_DEPTH_COLOR = R"hlsl(
// MSAA 4x (ENG-105 v2) — sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);
Texture2D<float4> ColorTexture : register(t2);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float sampleDepth(int2 pos)
{
    return DepthTexture.Load(pos, 0);
}

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float luminance(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);

    float nearZ = 0.1;
    float farZ = 100.0;

    int w = max(int(OutlineWidth), 1);

    // 中心基準の4方向深度差分（中心と比較するので二重線にならない）
    float dC = linearizeDepth(sampleDepth(pos), nearZ, farZ);
    float dL = linearizeDepth(sampleDepth(pos + int2(-w, 0)), nearZ, farZ);
    float dR = linearizeDepth(sampleDepth(pos + int2( w, 0)), nearZ, farZ);
    float dU = linearizeDepth(sampleDepth(pos + int2( 0,-w)), nearZ, farZ);
    float dD = linearizeDepth(sampleDepth(pos + int2( 0, w)), nearZ, farZ);
    float depthEdge = max(max(abs(dC - dL), abs(dC - dR)),
                          max(abs(dC - dU), abs(dC - dD)));

    // 中心基準の色差分
    float lC = luminance(ColorTexture.Load(int3(pos, 0)).rgb);
    float lL = luminance(ColorTexture.Load(int3(pos + int2(-w, 0), 0)).rgb);
    float lR = luminance(ColorTexture.Load(int3(pos + int2( w, 0), 0)).rgb);
    float lU = luminance(ColorTexture.Load(int3(pos + int2( 0,-w), 0)).rgb);
    float lD = luminance(ColorTexture.Load(int3(pos + int2( 0, w), 0)).rgb);
    float colorEdge = max(max(abs(lC - lL), abs(lC - lR)),
                          max(abs(lC - lU), abs(lC - lD)));

    // NdotVフィルタ
    float4 normalData = NormalTexture.Load(pos, 0);
    float NdotV = normalData.a;

    // 深度エッジと色エッジの複合判定
    float depthThresh = Threshold;
    float colorThresh = Threshold * 0.3;
    bool hasDepthEdge = depthEdge > depthThresh;
    bool hasColorEdge = colorEdge > colorThresh;

    if ((hasDepthEdge || (hasColorEdge && depthEdge > depthThresh * 0.3)) && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, 1.0);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード6: Fresnel（N.Vリム効果）付きトゥーンPS
/// @details ポストプロセスではなく、メインのトゥーンPSにN.Vリムライト効果を追加。
///          シルエット付近を暗化してアウトラインとして機能させる。
constexpr const char* TOON_PS_3D_FRESNEL = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;

    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    // アンビエント
    float3 ambient = AmbientColor * MaterialDiffuse.rgb;

    // ディフューズ — NdotLを量子化
    float rawNdotL = max(dot(N, L), 0.0);
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * MaterialDiffuse.rgb * toon;

    // スペキュラー
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specFactor = pow(NdotH, MaterialShininess) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    // Fresnel: シルエット付近（NdotV小）を暗化してアウトラインに
    float NdotV = max(dot(N, V), 0.0);
    float fresnelEdge = 1.0 - smoothstep(0.0, 0.4, NdotV);
    float3 outlineColor = float3(0.08, 0.06, 0.04);

    float3 finalColor = ambient + diffuse + specular;
    finalColor = lerp(finalColor, outlineColor, fresnelEdge * 0.95);
    float alpha = MaterialDiffuse.a * input.Color.a;

    output.Color = float4(finalColor * input.Color.rgb, alpha);
    output.Normal = float4(N * 0.5 + 0.5, NdotV);

    return output;
}
)hlsl";

} // namespace mitiru::render
