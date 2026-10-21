// 的を隠す面を網点で抜く判定と、隠れた的の影絵 (forward.slang から include される)。
// DX12 の DX12OccluderFadeShaders.hpp の occluderFadeAmount / occluderFaded と DX12_OCCLUDER_SILHOUETTE_HLSL を移した物。
// 影のパス (shadow.slang) はこの定数を読まないので、抜いた面も影を落とす。

float occluderFadeAmount(float3 W, float3 N)
{
    uint count = uint(frame.fadeCamera.w);
    float3 v = W - frame.fadeCamera.xyz;
    float3 n = dot(N, v) > 0.0 ? -N : N;
    float amount = 0.0;
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= count) { break; }
        float3 T = frame.fadeTargets[i].xyz;
        float r = frame.fadeTargets[i].w;
        float3 axis = frame.fadeCones[i].xyz;
        float s = dot(v, axis);
        float L = dot(T - frame.fadeCamera.xyz, axis);
        float k = length(v - axis * s) / max(s * frame.fadeCones[i].w, 1e-5);
        float radial = 1.0 - smoothstep(1.0 - frame.fadeParams.y, 1.0, k);
        float front = saturate((L - s) / r);
        float behind = smoothstep(0.25 * r, -0.25 * r, dot(T - W, n));
        amount = max(amount, radial * front * behind);
    }
    return amount * frame.fadeParams.x;
}

static const float kOccluderBayer[16] = { 0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0 };

// 4x4 の Bayer の閾値で抜くか決める。Dawn には TAA が無いので、閾値のずらしは 0 のまま毎フレーム同じ網点になる
bool occluderFaded(float2 pixel, float3 W, float3 N)
{
    if (frame.fadeCamera.w < 0.5) { return false; }
    uint2 p = uint2(pixel) & uint2(3u, 3u);
    float t = frac((kOccluderBayer[p.y * 4u + p.x] + 0.5) / 16.0 + frame.fadeParams.z);
    return occluderFadeAmount(W, N * rsqrt(max(dot(N, N), 1e-20))) > t;
}

struct SilhouetteOut
{
    float4 pos       : SV_Position;
    float  viewDepth : TEXCOORD0;
};

// 的を目の側へ寄せて描く。寄せる距離は的の半径で、腕が胴の前に出たような的どうしの重なりは影絵にならない
[shader("vertex")]
SilhouetteOut vsSilhouette(VSIn i)
{
    SilhouetteOut o;
    float3 wp = mul(draw.world, float4(i.pos, 1.0)).xyz;
    float3 rel = wp - frame.cameraPos.xyz;
    float d = length(rel);
    float3 moved = frame.cameraPos.xyz + rel * (max(d - frame.fadeSilParams.x, 1e-3) / max(d, 1e-6));
    o.pos = mul(frame.viewProj, float4(moved, 1.0));
    o.viewDepth = o.pos.w;
    return o;
}

// 寄せた的の手前に、場面の別の面 (輪郭線の下地の距離) があるときだけ色を重ねる。標本ごとに比べるので縁も取りこぼさない
[shader("fragment")]
float4 fsSilhouette(SilhouetteOut i, uint sampleIndex : SV_SampleIndex) : SV_Target
{
    float4 e = fogLineDepth.Load(int2(i.pos.xy), int(sampleIndex));
    uint v = (uint(e.r * 255.0 + 0.5) << 16u) | (uint(e.g * 255.0 + 0.5) << 8u) | uint(e.b * 255.0 + 0.5);
    if (v >= 16777215u) { discard; }
    float sceneDepth = float(v) / 16777215.0 * frame.viewInfo.x;
    if (sceneDepth >= i.viewDepth) { discard; }
    return float4(storeHdr(frame.fadeSilColor.rgb, frame.lightInfo.y, i.pos.xy), frame.fadeSilColor.a);
}

// 互換レベルは sample_index を使えない。1 画素の 4 標本の場面の距離を全部読み、手前にある標本の割合で濃さを掛けて、画素の全標本へ重ねる。
// 的の縁の画素 (的が標本の一部しか覆わない所) では、覆わない標本の分も数えるので、影絵の縁が通常のレベルより少し滑らかになる
[shader("fragment")]
float4 fsSilhouetteCompat(SilhouetteOut i) : SV_Target
{
    int hit = 0;
    [unroll]
    for (int s = 0; s < 4; ++s)
    {
        float4 e = fogLineDepth.Load(int2(i.pos.xy), s);
        uint v = (uint(e.r * 255.0 + 0.5) << 16u) | (uint(e.g * 255.0 + 0.5) << 8u) | uint(e.b * 255.0 + 0.5);
        float sceneDepth = float(v) / 16777215.0 * frame.viewInfo.x;
        hit += (v < 16777215u && sceneDepth < i.viewDepth) ? 1 : 0;
    }
    if (hit == 0) { discard; }
    return float4(storeHdr(frame.fadeSilColor.rgb, frame.lightInfo.y, i.pos.xy), frame.fadeSilColor.a * float(hit) * 0.25);
}
