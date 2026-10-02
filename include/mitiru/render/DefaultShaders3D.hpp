#pragma once

/// @file DefaultShaders3D.hpp
/// @brief 3D 描画用デフォルトシェーダー（HLSL SM5.0）
/// @details Phong シェーディングモデルによる頂点・ピクセルシェーダーを提供する。
///          ディレクショナルライト 1 灯によるアンビエント+ディフューズ+スペキュラー照明。

namespace mitiru::render
{

/// @brief 3D Phong 照明用 頂点シェーダー（HLSL SM5.0）
/// @details ワールド・ビュー・プロジェクション行列による座標変換と、
///          ワールド空間での法線・位置をピクセルシェーダーに渡す。
constexpr const char* DEFAULT_VS_3D = R"hlsl(
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

    float4 worldPos = mul(float4(input.Position, 1.0), World);
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul(input.Normal, (float3x3)World));

    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief 3D Phong 照明用 インスタンシング頂点シェーダー（HLSL SM5.0）
/// @details `DEFAULT_VS_3D` の instanced 版。ワールド行列は CbTransform ではなく
///          per-instance 頂点属性 (InstRow0..3、`D3D11_INPUT_PER_INSTANCE_DATA`) から
///          読む。PS は `DEFAULT_PS_3D` を共用する（VSOutput の形が同じため）。
constexpr const char* INSTANCED_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;      // インスタンシング経路では未使用（View/Projectionのみ使う）
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
    float4 InstRow0 : TEXCOORD3;
    float4 InstRow1 : TEXCOORD4;
    float4 InstRow2 : TEXCOORD5;
    float4 InstRow3 : TEXCOORD6;
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

    float4x4 instWorld = float4x4(input.InstRow0, input.InstRow1, input.InstRow2, input.InstRow3);

    float4 worldPos = mul(float4(input.Position, 1.0), instWorld);
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul(input.Normal, (float3x3)instWorld));

    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief 3D Phong 照明用 ピクセルシェーダー（HLSL SM5.0）
/// @details ディレクショナルライト 1 灯による Phong シェーディング。
///          アンビエント + ディフューズ + スペキュラー成分を計算する。
constexpr const char* DEFAULT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;
    float  _pad0;
    float3 LightColor;
    float  _pad1;
    float3 AmbientColor;
    float  _pad2;
    float3 CameraPos;
    float  _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
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

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 R = reflect(-L, N);

    float3 ambient = AmbientColor * MaterialDiffuse.rgb;

    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = LightColor * MaterialDiffuse.rgb * NdotL;

    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specFactor = pow(NdotH, MaterialShininess);
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float3 finalColor = ambient + diffuse + specular;
    float alpha = MaterialDiffuse.a * input.Color.a;

    return float4(finalColor * input.Color.rgb, alpha);
}
)hlsl";

/// @brief アンライト（照明なし）頂点シェーダー
/// @details テクスチャカラーと頂点カラーのみで描画する 3D シェーダー。
constexpr const char* UNLIT_VS_3D = R"hlsl(
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
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
)hlsl";

/// @brief アンライト（照明なし）ピクセルシェーダー
/// @details 頂点カラーをそのまま出力する。
constexpr const char* UNLIT_PS_3D = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
)hlsl";

/// @brief オクルージョン用 min-depth resolve ピクセルシェーダー（HLSL SM5.0）
/// @details MSAA 深度 (`Texture2DMS<float>`) の全サンプルを `Load` し、最小値
///          （カメラに最も近い＝最も保守的な遮蔽物候補）を単一サンプル R32_FLOAT
///          へ書く。サンプル数は実行時に `GetDimensions` で取得する
///          （DX11 は `Dx11SwapChain` の MSAA プローブ結果次第でサンプル数が
///          固定でないため）。頂点シェーダーは呼び出し側 (`Renderer3D`)
///          が独自に用意するフルスクリーン三角形 VS を使う。
constexpr const char* OCCLUSION_MIN_DEPTH_RESOLVE_PS_3D = R"hlsl(
Texture2DMS<float> DepthTexture : register(t0);

struct PSInput
{
    float4 Position : SV_POSITION;
};

float PSMain(PSInput input) : SV_TARGET
{
    uint w, h, samples;
    DepthTexture.GetDimensions(w, h, samples);

    const uint2 coord = uint2(input.Position.xy);
    float minDepth = 1.0;
    for (uint i = 0; i < samples; ++i)
    {
        minDepth = min(minDepth, DepthTexture.Load(coord, i));
    }
    return minDepth;
}
)hlsl";

} // namespace mitiru::render
