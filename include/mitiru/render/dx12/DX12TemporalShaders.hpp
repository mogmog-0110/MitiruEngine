#pragma once

/// @file DX12TemporalShaders.hpp
/// @brief 動きベクトル (動いた物の描き直し + 画面全体の解決) と TAA の HLSL
/// @details 動きベクトルは「前フレームの UV − 今フレームの UV」を R16G16_FLOAT に入れる。
///          どちらの UV も射影のずらし (jitter) を抜いた行列で求めるので、止まったカメラと
///          止まった物では TAA が動いていても 0 になる。
///          全画面の 3 本は outline post の VS (フルスクリーン三角形) と、temporal 用の
///          root sig (SRV t0..t3 + CBV b0、s0 = 線形 clamp、s1 = 最近傍 clamp) を共用する。

namespace mitiru::render
{

/// @brief 前フレームから動いた物だけを描き直し、物の動きを書く
/// @details 深度は専用の single-sample バッファで物同士だけを比べる。主パスの深度と比べないのは、
///          主パスと同じ式でも別シェーダーでは深度が 1 ulp ずれて EQUAL の比較が当てにならないため。
///          主パスに隠された所は解決のパスで深度を比べて捨てる。
inline constexpr const char* DX12_VELOCITY_OBJECT_HLSL = R"hlsl(
cbuffer CbVelocityDraw : register(b0)
{
    float4x4 World;
    float4x4 PrevWorld;
};

cbuffer CbVelocityFrame : register(b1)
{
    float4x4 ViewProjJittered;   // 主パスと同じ位置に描くため
    float4x4 ViewProj;           // ずらしを抜いた今フレーム
    float4x4 PrevViewProj;       // ずらしを抜いた前フレーム
};

struct VSInput
{
    float3 Position     : POSITION;
    float3 PrevPosition : TEXCOORD7;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Cur      : TEXCOORD0;
    float4 Prev     : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float4 w  = mul(World, float4(input.Position, 1.0));
    float4 pw = mul(PrevWorld, float4(input.PrevPosition, 1.0));
    o.Position = mul(ViewProjJittered, w);
    o.Cur      = mul(ViewProj, w);
    o.Prev     = mul(PrevViewProj, pw);
    return o;
}

float2 PSMain(VSOutput input) : SV_TARGET
{
    // 前フレームでカメラの後ろにあった頂点は射影できない。動きなしとして扱う
    if (input.Prev.w <= 1e-4 || input.Cur.w <= 1e-4) { return float2(0.0, 0.0); }
    float2 cur  = input.Cur.xy / input.Cur.w;
    float2 prev = input.Prev.xy / input.Prev.w;
    return (prev - cur) * float2(0.5, -0.5);
}
)hlsl";

/// @brief 画面全体の動きベクトル。物の描き直しが主パスの見えている面と合う所はそれを、
///        それ以外 (止まった物、clod、空) はカメラの動きを深度から求める
inline constexpr const char* DX12_VELOCITY_RESOLVE_PS = R"hlsl(
Texture2DMS<float, 4> g_depth     : register(t0);
Texture2D<float2>     g_objVel    : register(t1);
Texture2D<float>      g_objDepth  : register(t2);

cbuffer CbVelocityResolve : register(b0)
{
    float4x4 InvViewProj;    // ずらしを抜いた今フレームの clip → world
    float4x4 PrevViewProj;   // ずらしを抜いた前フレーム
    float2   TexelSize;
    float    NearZ;
    float    FarZ;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float linearDepth(float d) { return NearZ * FarZ / (FarZ - d * (FarZ - NearZ)); }

float2 PSMain(PSInput input) : SV_TARGET
{
    int2  pix = int2(input.Position.xy);
    float d = min(min(g_depth.Load(pix, 0), g_depth.Load(pix, 1)),
                  min(g_depth.Load(pix, 2), g_depth.Load(pix, 3)));
    float od = g_objDepth.Load(int3(pix, 0));
    if (od < 1.0)
    {
        // 物が主パスで見えている面と同じ距離にあるときだけ物の動きを使う。手前に別の物があれば
        // 主パスの方が近く、抜き (alpha mask) の穴なら主パスの方が遠い
        float lz = linearDepth(d);
        float lo = linearDepth(od);
        if (abs(lz - lo) <= lz * 0.02 + 0.01) { return g_objVel.Load(int3(pix, 0)); }
    }

    float2 uv  = (float2(pix) + 0.5) * TexelSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, d, 1.0));
    world /= world.w;
    float4 prevClip = mul(PrevViewProj, world);
    if (prevClip.w <= 1e-4) { return float2(0.0, 0.0); }
    float2 prevNdc = prevClip.xy / prevClip.w;
    float2 prevUv  = float2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
    return prevUv - uv;
}
)hlsl";

/// @brief TAA。tonemap・輪郭線・被写界深度の後の LDR 色に重ねる
/// @details 輪郭線は解決後の画素単位で描かれ MSAA が効かないので、その後に置くと輪郭線も滑らかになる。
///          履歴は 3x3 近傍の YCoCg の分散で決めた箱へ切り詰めてから混ぜる (動いた物の残像を消す)。
///          動きベクトルは近傍で一番手前の画素のものを使う (物の縁で背景の動きを拾わない)。
///          履歴は Catmull-Rom で読む。双線形だと毎フレーム少しずつぼけが溜まる。
inline constexpr const char* DX12_TAA_PS = R"hlsl(
Texture2D<float4>     g_color    : register(t0);
Texture2D<float4>     g_history  : register(t1);
Texture2D<float2>     g_velocity : register(t2);
Texture2DMS<float, 4> g_depth    : register(t3);
SamplerState          s_linear   : register(s0);

cbuffer CbTaa : register(b0)
{
    float2 TexelSize;
    float2 ScreenSize;
    float  BlendStill;       // 止まっている画素で今フレームを混ぜる割合
    float  BlendMoving;      // 速く動く画素の割合 (履歴の当てにならなさに合わせて上げる)
    float  MovingPixels;     // この画素数動くと BlendMoving になる
    float  HistoryValid;     // 0 なら履歴を捨てて今フレームだけを出す
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PSOutput
{
    float4 Color   : SV_TARGET0;
    float4 History : SV_TARGET1;
};

float3 toYCoCg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 fromYCoCg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float3 sampleHistory(float2 uv)
{
    float2 samplePos = uv * ScreenSize;
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    float2 f   = samplePos - tc1;
    float2 w0  = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1  = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2  = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3  = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc0  = (tc1 - 1.0) * TexelSize;
    float2 tc3  = (tc1 + 2.0) * TexelSize;
    float2 tc12 = (tc1 + w2 / w12) * TexelSize;
    float3 r = g_history.SampleLevel(s_linear, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y)
             + g_history.SampleLevel(s_linear, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y)
             + g_history.SampleLevel(s_linear, tc12, 0).rgb * (w12.x * w12.y)
             + g_history.SampleLevel(s_linear, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y)
             + g_history.SampleLevel(s_linear, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, 0.0);
}

float3 clipToBox(float3 boxMin, float3 boxMax, float3 h)
{
    float3 c = 0.5 * (boxMax + boxMin);
    float3 e = 0.5 * (boxMax - boxMin) + 1e-4;
    float3 v = h - c;
    float3 a = abs(v / e);
    float  m = max(a.x, max(a.y, a.z));
    return (m > 1.0) ? c + v / m : h;
}

PSOutput PSMain(PSInput input)
{
    int2 pix = int2(input.Position.xy);
    int2 hi  = int2(ScreenSize) - 1;

    float3 center = 0.0;
    float3 m1 = 0.0;
    float3 m2 = 0.0;
    float3 cmin = 1e9;
    float3 cmax = -1e9;
    float  closest = 2.0;
    int2   closestPix = pix;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2   q = clamp(pix + int2(dx, dy), int2(0, 0), hi);
            float3 c = toYCoCg(g_color.Load(int3(q, 0)).rgb);
            m1 += c;
            m2 += c * c;
            cmin = min(cmin, c);
            cmax = max(cmax, c);
            float d = g_depth.Load(q, 0);
            if (d < closest) { closest = d; closestPix = q; }
            if (dx == 0 && dy == 0) { center = c; }
        }
    }

    float alpha = g_color.Load(int3(pix, 0)).a;
    PSOutput o;
    float2 vel    = g_velocity.Load(int3(closestPix, 0));
    float2 uv     = (float2(pix) + 0.5) * TexelSize;
    float2 prevUv = uv + vel;
    if (HistoryValid < 0.5 || any(prevUv < 0.0) || any(prevUv > 1.0))
    {
        o.Color   = float4(fromYCoCg(center), alpha);
        o.History = o.Color;
        return o;
    }

    float3 mean  = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    float3 hist  = clipToBox(max(cmin, mean - sigma), min(cmax, mean + sigma), toYCoCg(sampleHistory(prevUv)));

    float  movedPx = length(vel * ScreenSize);
    float  blend   = lerp(BlendStill, BlendMoving, saturate(movedPx / MovingPixels));
    // 明るさで重みを割る: 明るい 1 画素が履歴の平均を引っ張ってちらつくのを抑える
    float  wc = blend / (1.0 + center.x);
    float  wh = (1.0 - blend) / (1.0 + hist.x);
    float3 result = (center * wc + hist * wh) / (wc + wh);
    o.Color   = float4(saturate(fromYCoCg(result)), alpha);
    o.History = o.Color;
    return o;
}
)hlsl";

} // namespace mitiru::render
