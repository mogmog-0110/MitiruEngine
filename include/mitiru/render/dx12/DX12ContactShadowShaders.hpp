#pragma once

/// @file DX12ContactShadowShaders.hpp
/// @brief 接触影（ADR 0109）用シェーダーの宣言。主パスの MSAA 深度（t0）の sample 0 を読み、各画素から光の向きへ短いレイを画面空間で進め、手前の面に遮られたときの影の濃さを R8 に乗算する。
/// @details root sig と VS は SSAO と同じ outline post 用のものを使う。出力は AO テクスチャに乗算ブレンドし、tonemap が t1 で読む。遮られた位置がレイの始点に近いほど影を濃くし、遠いほど薄くする。遮る面との深度差が厚み（レイの長さの半分）を超えた場合は、レイが面の裏を通ったものとして数えない。これにより、遠い壁の手前を通る細い物の影が、その物の後ろに回り込むのを防ぐ。面の向きと光の向きの内積が 0 以下の面には、遮られる直接光がないため影を付けない。

namespace mitiru::render
{

inline constexpr const char* DX12_CONTACT_SHADOW_PS = R"hlsl(
Texture2DMS<float, 4> g_depth : register(t0);

cbuffer CbContactShadow : register(b0)
{
    float4x4 Proj;
    float4x4 InvProj;
    float3   LightView;    // 視空間で画素から光へ向かう単位ベクトル
    float    Strength;     // 0..1
    float2   ScreenSize;
    float    RayLength;    // 視空間 (= ワールド) の単位
    float    Bias;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

static const int kSteps = 24;

float3 viewPosAt(int2 pix, float d)
{
    float2 uv  = (float2(pix) + 0.5) / ScreenSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p   = mul(InvProj, float4(ndc, d, 1.0));
    return p.xyz / p.w;
}

// 面の向きは深度から組み直す (SSAO と同じ。法線 RT は頂点法線の補間で、角を丸めた面が傾く)。
// 左右・上下は深度の差が小さい側を取り、画面の端では内側だけを使う
float3 faceNormal(int2 pix, float3 P)
{
    int2 hi = int2(ScreenSize) - 1;
    int2 l = max(pix - int2(1, 0), int2(0, 0));
    int2 r = min(pix + int2(1, 0), hi);
    int2 u = max(pix - int2(0, 1), int2(0, 0));
    int2 b = min(pix + int2(0, 1), hi);
    float3 Pl = viewPosAt(l, g_depth.Load(l, 0));
    float3 Pr = viewPosAt(r, g_depth.Load(r, 0));
    float3 Pu = viewPosAt(u, g_depth.Load(u, 0));
    float3 Pb = viewPosAt(b, g_depth.Load(b, 0));
    bool useR = (pix.x == 0) || (pix.x < hi.x && abs(Pr.z - P.z) < abs(P.z - Pl.z));
    bool useB = (pix.y == 0) || (pix.y < hi.y && abs(Pb.z - P.z) < abs(P.z - Pu.z));
    float3 dx = useR ? Pr - P : P - Pl;
    float3 dy = useB ? Pb - P : P - Pu;
    float3 N = normalize(cross(dx, dy));
    return dot(N, P) > 0.0 ? -N : N;
}

float PSMain(PSInput input) : SV_TARGET
{
    int2  pix = int2(input.Position.xy);
    float d   = g_depth.Load(pix, 0);
    if (d >= 0.99999) { return 1.0; }

    float3 P = viewPosAt(pix, d);
    // 光に背を向けた面には、遮る直接光がない。レイが自分の面の内側へ入って全部が影になるのを避け、境は少し幅を取って切り替える
    float facing = smoothstep(0.0, 0.25, dot(faceNormal(pix, P), LightView));
    if (facing <= 0.0) { return 1.0; }

    float  stepLen  = RayLength / float(kSteps);
    float  thick    = max(RayLength * 0.5, 0.05);
    float  bias     = Bias + 0.003 * abs(P.z);
    float  occluded = 0.0;

    [loop] for (int s = 1; s <= kSteps; ++s)
    {
        float3 Q    = P + LightView * (stepLen * float(s));
        float4 clip = mul(Proj, float4(Q, 1.0));
        if (clip.w <= 0.0) { break; }
        float2 ndc = clip.xy / clip.w;
        float2 px  = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * ScreenSize;
        if (px.x < 0.0 || px.y < 0.0 || px.x >= ScreenSize.x || px.y >= ScreenSize.y) { break; }
        int2  qp = int2(px);
        float sd = g_depth.Load(qp, 0);
        if (sd >= 0.99999) { continue; }
        float3 S     = viewPosAt(qp, sd);
        float  delta = abs(Q.z) - abs(S.z);
        if (delta > bias && delta < thick)
        {
            occluded = 1.0 - float(s - 1) / float(kSteps);
            break;
        }
    }
    return 1.0 - Strength * occluded * facing;
}
)hlsl";

}  // namespace mitiru::render
