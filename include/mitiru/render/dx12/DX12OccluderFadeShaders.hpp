#pragma once

/// @file DX12OccluderFadeShaders.hpp
/// @brief 的を隠す面を網点で抜く判定 (ADR 0077)。前方の描画の PS と clod のラスタが同じ式を使う
/// @details OCCLUDER_FADE_FIELDS は C++ の OccluderFadeGpu (OccluderFade.hpp) と同じ並びで、前方の描画では CbCluster の末尾、
///          clod では CB の末尾に置く。clod 側は tools/clod_shaders/generate_blobs.py がこの 2 本を取り出して .hlsli にする。

#include <string>

namespace mitiru::render
{

inline constexpr const char* DX12_OCCLUDER_FADE_FIELDS_HLSL = R"hlsl(
#define OCCLUDER_FADE_FIELDS float4 FadeCamera; float4 FadeParams; float4 FadeTargets[4]; float4 FadeCones[4];
)hlsl";

inline constexpr const char* DX12_OCCLUDER_FADE_HLSL = R"hlsl(
// 0..1。円錐の中 (縁はぼかす) × 的の中心より手前 × 的が面の裏にある、の積を的ごとに取って一番大きいもの。
// 面の裏の判定があるので、的の足元の床と、的が貼り付いた壁は抜けない
float occluderFadeAmount(float3 W, float3 N)
{
    uint count = (uint)FadeCamera.w;
    float3 v = W - FadeCamera.xyz;
    float3 n = dot(N, v) > 0.0 ? -N : N;
    float amount = 0.0;
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        float3 T = FadeTargets[i].xyz;
        float r = FadeTargets[i].w;
        float3 axis = FadeCones[i].xyz;
        float s = dot(v, axis);
        float L = dot(T - FadeCamera.xyz, axis);
        float k = length(v - axis * s) / max(s * FadeCones[i].w, 1e-5);
        float radial = 1.0 - smoothstep(1.0 - FadeParams.y, 1.0, k);
        float front = saturate((L - s) / r);
        float behind = smoothstep(0.25 * r, -0.25 * r, dot(T - W, n));
        amount = max(amount, radial * front * behind);
    }
    return amount * FadeParams.x;
}

static const float kOccluderBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// 4x4 の Bayer の閾値で抜くか決める。閾値のずらしは TAA の間だけ動き、止めた絵では毎フレーム同じ網点になる
bool occluderFaded(float2 pixel, float3 W, float3 N)
{
    if (FadeCamera.w < 0.5) { return false; }
    uint2 p = uint2(pixel) & 3u;
    float t = frac((kOccluderBayer[p.y * 4 + p.x] + 0.5) / 16.0 + FadeParams.z);
    return occluderFadeAmount(W, N * rsqrt(max(dot(N, N), 1e-20))) > t;
}
)hlsl";

/// @brief 前方の描画の PS が先頭で呼ぶ。透かしの的として描いた物 (DrawFlags.x) は抜かない
inline constexpr const char* DX12_OCCLUDER_FADE_FORWARD_HLSL = R"hlsl(
void clipOccluded(float4 svPos, float3 worldPos, float3 worldNorm)
{
    if (DrawFlags.x < 0.5 && occluderFaded(svPos.xy, worldPos, worldNorm)) { discard; }
}
)hlsl";

/// @brief 的の影絵。的を目の側へ寄せて描き、深度を GREATER で比べるので、寄せた距離より手前に別の物がある所にだけ色が付く。
///        寄せる距離は的の半径で、腕が胴の前に出たような的どうしの重なりは塗らない
inline constexpr const char* DX12_OCCLUDER_SILHOUETTE_HLSL = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

cbuffer CbSilhouette : register(b1)
{
    float4 SilhouetteColor;   // rgb = 線形の色 a = 濃さ
    float4 SilhouetteParams;  // x = 目の側へ寄せる距離
};

float4 VSMain(float3 position : POSITION) : SV_POSITION
{
    float4 viewPos = mul(View, mul(World, float4(position, 1.0)));
    float d = length(viewPos.xyz);
    viewPos.xyz *= max(d - SilhouetteParams.x, 1e-3) / max(d, 1e-6);
    return mul(Projection, viewPos);
}

Texture2DMS<float> NearestTarget : register(t0);

// 的のいちばん手前の面 (寄せた後の深度) より奥の面は、的どうしの重なりなので塗らない。
// 縁の画素で手前の部品が標本の一部だけを覆う所も取りこぼさないよう、標本ごとに比べる
float4 PSMain(float4 pos : SV_POSITION, uint sample : SV_SampleIndex) : SV_TARGET0
{
    float nearest = NearestTarget.Load(int2(pos.xy), sample);
    if (pos.z > nearest + 1e-6) { discard; }
    return SilhouetteColor;
}

// 的の寄せた後の深度を、標本ごとにいちばん手前だけ残す (書き先は MIN で混ぜる)。影絵と同じ標本の位置の深度にする
float PSNearest(float4 pos : SV_POSITION, uint sample : SV_SampleIndex) : SV_TARGET0
{
    return (sample < 64u) ? pos.z : 1.0;
}
)hlsl";

/// @brief CbCluster を宣言しない PS (Unlit / Flat / Fresnel) が透かしの定数だけを読む前置き。先頭 960 byte は CbCluster の本体
[[nodiscard]] inline std::string dx12OccluderFadePrelude()
{
	std::string src = DX12_OCCLUDER_FADE_FIELDS_HLSL;
	src += "cbuffer CbCluster : register(b4) { float4 _ClusterBody[60]; OCCLUDER_FADE_FIELDS };\n";
	src += DX12_OCCLUDER_FADE_HLSL;
	return src;
}

} // namespace mitiru::render
