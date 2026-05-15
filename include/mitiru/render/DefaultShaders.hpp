#pragma once

/// @file DefaultShaders.hpp
/// @brief デフォルト2Dシェーダー定義
/// @details 2D描画パイプラインで使用するHLSLシェーダーソースを
///          constexpr文字列として提供する。

#include <string_view>

namespace mitiru::render
{

/// @brief デフォルト2D頂点シェーダーのHLSLソース
/// @details 入力: float2 position, float2 texCoord, float4 color
///          定数バッファ: float4x4 projection（正射影行列）
///          出力: 変換後の位置とテクスチャ座標・色をパススルー
constexpr std::string_view DEFAULT_VS_2D = R"hlsl(
cbuffer Constants : register(b0)
{
	float4x4 projection;
};

struct VSInput
{
	float2 position : POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.position = mul(projection, float4(input.position, 0.0f, 1.0f));
	output.texCoord = input.texCoord;
	output.color = input.color;
	return output;
}
)hlsl";

/// @brief デフォルト2DピクセルシェーダーのHLSLソース
/// @details uUseTexture=0: 頂点色をそのまま出力
///          uUseTexture=1: テクスチャ×頂点色を出力
constexpr std::string_view DEFAULT_PS_2D = R"hlsl(
Texture2D    tex0      : register(t0);
SamplerState sampler0  : register(s0);

cbuffer PSConstants : register(b0)
{
	float uUseTexture;
	float3 _pad;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
	float4 color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
	if (uUseTexture > 0.5f)
	{
		float4 texColor = tex0.Sample(sampler0, input.texCoord);
		return texColor * input.color;
	}
	return input.color;
}
)hlsl";

// ── SDF Styled Rectangle Shaders ───────────────────────

/// @brief SDF角丸矩形用の頂点シェーダーHLSLソース
/// @details 入力: float2 position, float2 localUV, float4 color, float4 shapeRect
///          定数バッファ b0: float4x4 projection（正射影行列）
///          出力: 変換後の位置と localUV, color, shapeRect をパススルー
constexpr std::string_view SDF_RECT_VS = R"hlsl(
cbuffer Constants : register(b0)
{
    float4x4 projection;
};

struct VSInput
{
    float2 position  : POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position  = mul(projection, float4(input.position, 0.0f, 1.0f));
    output.localUV   = input.localUV;
    output.color     = input.color;
    output.shapeRect = input.shapeRect;
    return output;
}
)hlsl";

/// @brief SDF角丸矩形用のピクセルシェーダーHLSLソース
/// @details 角丸SDF、最大8ストップグラデーション、ストローク、シャドウ、AA対応。
///          定数バッファ b1 にスタイル定数を受け取る。
constexpr std::string_view SDF_RECT_PS = R"hlsl(
cbuffer StyleConstants : register(b1)
{
    float4 cornerRadii;              // tl, tr, br, bl
    float4 gradientStops[8];         // up to 8 stop colors (RGBA each)
    float4 gradientOffsetsPacked[2]; // 8 stop offsets packed into 2 float4s
    float4 gradientParams;           // type(0=solid,1=linear,2=radial), cos(angle), sin(angle), stopCount
    float4 strokeColor;
    float  strokeWidth;
    float3 _pad1;
    float4 shadowColor;
    float  shadowBlur;
    float  shadowOffsetX;
    float  shadowOffsetY;
    float  opacity;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

/// @brief グラデーションオフセット配列から値を取得する
float getGradientOffset(int idx)
{
    if (idx < 4)
    {
        if (idx == 0) return gradientOffsetsPacked[0].x;
        if (idx == 1) return gradientOffsetsPacked[0].y;
        if (idx == 2) return gradientOffsetsPacked[0].z;
        return gradientOffsetsPacked[0].w;
    }
    if (idx == 4) return gradientOffsetsPacked[1].x;
    if (idx == 5) return gradientOffsetsPacked[1].y;
    if (idx == 6) return gradientOffsetsPacked[1].z;
    return gradientOffsetsPacked[1].w;
}

/// @brief マルチストップグラデーションを評価する
float4 sampleGradient(float t, int stopCount)
{
    if (stopCount <= 1) return gradientStops[0];

    float firstOff = getGradientOffset(0);
    if (t <= firstOff) return gradientStops[0];

    float lastOff = getGradientOffset(stopCount - 1);
    if (t >= lastOff) return gradientStops[stopCount - 1];

    [unroll(7)]
    for (int i = 0; i < stopCount - 1; i++)
    {
        float off0 = getGradientOffset(i);
        float off1 = getGradientOffset(i + 1);
        if (t >= off0 && t <= off1)
        {
            float range = off1 - off0;
            float local = (range > 0.001f) ? (t - off0) / range : 0.0f;
            return lerp(gradientStops[i], gradientStops[i + 1], local);
        }
    }
    return gradientStops[stopCount - 1];
}

/// @brief 角丸矩形のSDF（符号付き距離関数）
float roundedBoxSDF(float2 p, float2 halfSize, float4 radii)
{
    float r = (p.x > 0.0f)
        ? ((p.y > 0.0f) ? radii.z : radii.y)
        : ((p.y > 0.0f) ? radii.w : radii.x);

    float maxR = min(halfSize.x, halfSize.y);
    r = min(r, maxR);

    float2 q = abs(p) - halfSize + float2(r, r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 rectPos  = input.shapeRect.xy;
    float2 rectSize = input.shapeRect.zw;
    float2 halfSize = rectSize * 0.5f;
    float2 center   = rectPos + halfSize;

    float  expand = max(shadowBlur + abs(shadowOffsetX),
                        shadowBlur + abs(shadowOffsetY));
    float2 expandedSize = rectSize + float2(expand * 2.0f, expand * 2.0f);
    float2 expandedOrigin = rectPos - float2(expand, expand);
    float2 pixelPos = expandedOrigin + input.localUV * expandedSize;
    float2 p = pixelPos - center;

    // ── SDF距離計算 ──
    float dist = roundedBoxSDF(p, halfSize, cornerRadii);

    // ── Shadow（シャドウ） ──
    float4 shadowOut = float4(0, 0, 0, 0);
    if (shadowBlur > 0.0f || shadowOffsetX != 0.0f || shadowOffsetY != 0.0f)
    {
        float2 shadowP = p - float2(shadowOffsetX, shadowOffsetY);
        float shadowDist = roundedBoxSDF(shadowP, halfSize, cornerRadii);
        float shadowAlpha = 1.0f - smoothstep(-shadowBlur * 0.5f, shadowBlur * 0.5f, shadowDist);
        shadowOut = float4(shadowColor.rgb, shadowColor.a * shadowAlpha);
    }

    // ── Fill（塗りつぶし） ──
    float gradType  = gradientParams.x;
    int   stopCount = (int)gradientParams.w;

    float4 fillCol = gradientStops[0];

    if (gradType > 0.5f && gradType < 1.5f)
    {
        // Linear gradient
        float cosA = gradientParams.y;
        float sinA = gradientParams.z;
        float2 normP = (pixelPos - rectPos) / max(rectSize, float2(1, 1));
        float t = saturate(dot(normP - 0.5f, float2(cosA, sinA)) + 0.5f);
        fillCol = sampleGradient(t, stopCount);
    }
    else if (gradType > 1.5f)
    {
        // Radial gradient
        float2 normP = (pixelPos - rectPos) / max(rectSize, float2(1, 1));
        float t = saturate(length(normP - 0.5f) * 2.0f);
        fillCol = sampleGradient(t, stopCount);
    }

    // ── Stroke（ストローク/ボーダー） ──
    float4 shapeColor;
    if (strokeWidth > 0.0f)
    {
        float strokeOuter = smoothstep(0.5f, -0.5f, dist);
        float strokeInner = smoothstep(-strokeWidth + 0.5f, -strokeWidth - 0.5f, dist);
        float strokeMask = strokeOuter - strokeInner;
        shapeColor = lerp(fillCol * strokeInner, strokeColor, strokeMask);
        shapeColor.a *= strokeOuter;
    }
    else
    {
        float alpha = smoothstep(0.5f, -0.5f, dist);
        shapeColor = fillCol;
        shapeColor.a *= alpha;
    }

    // ── Shadow合成 ──
    float4 result;
    result.rgb = lerp(shadowOut.rgb, shapeColor.rgb, shapeColor.a);
    result.a   = shadowOut.a * (1.0f - shapeColor.a) + shapeColor.a;

    // ── 頂点カラー乗算 ──
    result *= input.color;

    // ── Opacity ──
    result.a *= opacity;

    return result;
}
)hlsl";

// ── SDF Styled Circle/Ellipse Shaders ──────────────────

/// @brief SDF円/楕円用の頂点シェーダーHLSLソース
/// @details SDF_RECT_VS と同じ入力レイアウトを使用する。
///          shapeRect には (centerX, centerY, rx, ry) を格納する。
constexpr std::string_view SDF_CIRCLE_VS = R"hlsl(
cbuffer Constants : register(b0)
{
    float4x4 projection;
};

struct VSInput
{
    float2 position  : POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

struct VSOutput
{
    float4 position  : SV_POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position  = mul(projection, float4(input.position, 0.0f, 1.0f));
    output.localUV   = input.localUV;
    output.color     = input.color;
    output.shapeRect = input.shapeRect;
    return output;
}
)hlsl";

/// @brief SDF円/楕円用のピクセルシェーダーHLSLソース
/// @details 楕円SDF、グラデーション、ストローク、シャドウ、アンチエイリアシング対応。
///          shapeRect = (centerX, centerY, rx, ry)
///          定数バッファ b1 にスタイル定数を受け取る（StyleConstants と同一レイアウト）。
///          cornerRadii は無視される（円/楕円に角丸は無い）。
constexpr std::string_view SDF_CIRCLE_PS = R"hlsl(
cbuffer StyleConstants : register(b1)
{
    float4 cornerRadii;              // 未使用（互換性のため保持）
    float4 gradientStops[8];         // up to 8 stop colors (RGBA each)
    float4 gradientOffsetsPacked[2]; // 8 stop offsets packed into 2 float4s
    float4 gradientParams;           // type(0=solid,1=linear,2=radial), cos(angle), sin(angle), stopCount
    float4 strokeColor;
    float  strokeWidth;
    float3 _pad1;
    float4 shadowColor;
    float  shadowBlur;
    float  shadowOffsetX;
    float  shadowOffsetY;
    float  opacity;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float2 localUV   : TEXCOORD0;
    float4 color     : COLOR0;
    float4 shapeRect : TEXCOORD1;
};

/// @brief グラデーションオフセット配列から値を取得する
float getGradientOffset(int idx)
{
    if (idx < 4)
    {
        if (idx == 0) return gradientOffsetsPacked[0].x;
        if (idx == 1) return gradientOffsetsPacked[0].y;
        if (idx == 2) return gradientOffsetsPacked[0].z;
        return gradientOffsetsPacked[0].w;
    }
    if (idx == 4) return gradientOffsetsPacked[1].x;
    if (idx == 5) return gradientOffsetsPacked[1].y;
    if (idx == 6) return gradientOffsetsPacked[1].z;
    return gradientOffsetsPacked[1].w;
}

/// @brief マルチストップグラデーションを評価する
float4 sampleGradient(float t, int stopCount)
{
    if (stopCount <= 1) return gradientStops[0];

    float firstOff = getGradientOffset(0);
    if (t <= firstOff) return gradientStops[0];

    float lastOff = getGradientOffset(stopCount - 1);
    if (t >= lastOff) return gradientStops[stopCount - 1];

    [unroll(7)]
    for (int i = 0; i < stopCount - 1; i++)
    {
        float off0 = getGradientOffset(i);
        float off1 = getGradientOffset(i + 1);
        if (t >= off0 && t <= off1)
        {
            float range = off1 - off0;
            float local = (range > 0.001f) ? (t - off0) / range : 0.0f;
            return lerp(gradientStops[i], gradientStops[i + 1], local);
        }
    }
    return gradientStops[stopCount - 1];
}

/// @brief 楕円の近似SDF
float ellipseSDF(float2 p, float2 halfSize)
{
    float minR = min(halfSize.x, halfSize.y);
    float2 normalized = p / max(halfSize, float2(0.0001f, 0.0001f));
    return (length(normalized) - 1.0f) * minR;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 center   = input.shapeRect.xy;
    float2 halfSize = input.shapeRect.zw;

    float  expand = max(shadowBlur + abs(shadowOffsetX),
                        shadowBlur + abs(shadowOffsetY));
    float2 expandedSize = halfSize * 2.0f + float2(expand * 2.0f, expand * 2.0f);
    float2 expandedOrigin = center - halfSize - float2(expand, expand);
    float2 pixelPos = expandedOrigin + input.localUV * expandedSize;
    float2 p = pixelPos - center;

    // ── SDF距離計算 ──
    float dist = ellipseSDF(p, halfSize);

    // ── Shadow（シャドウ） ──
    float4 shadowOut = float4(0, 0, 0, 0);
    if (shadowBlur > 0.0f || shadowOffsetX != 0.0f || shadowOffsetY != 0.0f)
    {
        float2 shadowP = p - float2(shadowOffsetX, shadowOffsetY);
        float shadowDist = ellipseSDF(shadowP, halfSize);
        float shadowAlpha = 1.0f - smoothstep(-shadowBlur * 0.5f, shadowBlur * 0.5f, shadowDist);
        shadowOut = float4(shadowColor.rgb, shadowColor.a * shadowAlpha);
    }

    // ── Fill（塗りつぶし） ──
    float gradType  = gradientParams.x;
    int   stopCount = (int)gradientParams.w;

    float4 fillCol = gradientStops[0];

    float2 rectPos  = center - halfSize;
    float2 rectSize = halfSize * 2.0f;

    if (gradType > 0.5f && gradType < 1.5f)
    {
        // Linear gradient
        float cosA = gradientParams.y;
        float sinA = gradientParams.z;
        float2 normP = (pixelPos - rectPos) / max(rectSize, float2(1, 1));
        float t = saturate(dot(normP - 0.5f, float2(cosA, sinA)) + 0.5f);
        fillCol = sampleGradient(t, stopCount);
    }
    else if (gradType > 1.5f)
    {
        // Radial gradient
        float2 normP = (pixelPos - rectPos) / max(rectSize, float2(1, 1));
        float t = saturate(length(normP - 0.5f) * 2.0f);
        fillCol = sampleGradient(t, stopCount);
    }

    // ── Stroke（ストローク/ボーダー） ──
    float4 shapeColor;
    if (strokeWidth > 0.0f)
    {
        float strokeOuter = smoothstep(0.5f, -0.5f, dist);
        float strokeInner = smoothstep(-strokeWidth + 0.5f, -strokeWidth - 0.5f, dist);
        float strokeMask = strokeOuter - strokeInner;
        shapeColor = lerp(fillCol * strokeInner, strokeColor, strokeMask);
        shapeColor.a *= strokeOuter;
    }
    else
    {
        float alpha = smoothstep(0.5f, -0.5f, dist);
        shapeColor = fillCol;
        shapeColor.a *= alpha;
    }

    // ── Shadow合成 ──
    float4 result;
    result.rgb = lerp(shadowOut.rgb, shapeColor.rgb, shapeColor.a);
    result.a   = shadowOut.a * (1.0f - shapeColor.a) + shapeColor.a;

    // ── 頂点カラー乗算 ──
    result *= input.color;

    // ── Opacity ──
    result.a *= opacity;

    return result;
}
)hlsl";

} // namespace mitiru::render
