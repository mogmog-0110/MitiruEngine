#pragma once

/// @file DX12DofShaders.hpp
/// @brief 遠景だけをぼかす被写界深度の PS (v44、ADR 0046)
/// @details tonemap 後の LDR 色を FXAA の intermediate へ写したもの (t0) と主パスの MSAA 深度 (t1) を読む。
///          ぼけ半径はビュー距離 (視線の長さ) で決め、視線軸の深度では決めない: 首を振ると画面の端の物が
///          中央より近く測られて、振るたびにぼけが変わるため。
///          集めるのは黄金角の螺旋 16 タップ。各タップは**自分の深度で決まる半径が中心まで届くときだけ**数えるので、
///          手前のくっきりした物 (半径 0) の色が奥のぼけの中へ滲まない。中心の半径が 0.5 px 未満なら何もしない。
///          色はタップの重みを決めた画素そのものから読む (双線形で読むと、除いたはずの手前の画素が隣から混ざる)。
///          平均は 2 乗した色 (ガンマ 2 の近似で線形に戻す) で取る。sRGB のまま平均すると明暗の境が暗く濁る。
///          root sig / VS は tonemap のもの (SRV t0..t2 + CBV b0) を共用する。

namespace mitiru::render
{

inline constexpr const char* DX12_DOF_PS = R"hlsl(
Texture2D<float4>     g_color : register(t0);
Texture2DMS<float, 4> g_depth : register(t1);

cbuffer CbDof : register(b0)
{
    float2 TexelSize;     // 1 / viewport
    float2 ViewRayScale;  // (tan(fovX/2), tan(fovY/2))。NDC の xy に掛けると視線の横ずれになる
    float  Start;         // ここより遠いとぼけ始める (ビュー距離)
    float  InvRange;      // 1 / (End - Start)
    float  MaxRadiusPx;   // 画面の画素でのぼけ半径の上限
    float  NearZ;
    float  FarZ;
    float3 _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// MSAA の 4 サンプルのうち一番手前で決める。物の縁の画素は色が手前と奥の混ざりなので、
// 奥のサンプルで決めると手前の物の縁までぼけて、縁に奥の色の輪が付く
float radiusAt(int2 pix)
{
    float d = min(min(g_depth.Load(pix, 0), g_depth.Load(pix, 1)),
                  min(g_depth.Load(pix, 2), g_depth.Load(pix, 3)));
    float z  = NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
    float2 uv  = (float2(pix) + 0.5) * TexelSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float dist = z * length(float3(ndc * ViewRayScale, 1.0));
    return saturate((dist - Start) * InvRange) * MaxRadiusPx;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2   pix    = int2(input.Position.xy);
    float4 center = g_color.Load(int3(pix, 0));
    float  rc     = radiusAt(pix);
    if (rc < 0.5) { return center; }

    int2   hi   = int2(round(1.0 / TexelSize)) - 1;
    float3 sum  = center.rgb * center.rgb;
    float  wsum = 1.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float  r   = rc * sqrt((i + 0.5) / 16.0);
        float  a   = i * 2.39996323;
        float2 off = float2(cos(a), sin(a)) * r;
        int2   q   = clamp(pix + int2(round(off)), int2(0, 0), hi);
        float  w   = saturate(radiusAt(q) - length(float2(q - pix)) + 0.5);
        float3 c   = g_color.Load(int3(q, 0)).rgb;
        sum  += c * c * w;
        wsum += w;
    }
    return float4(sqrt(sum / wsum), center.a);
}
)hlsl";

} // namespace mitiru::render
