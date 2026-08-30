#pragma once

/// @file DX12SplatShaders.hpp
/// @brief 3D Gaussian Splatting 用 HLSL (インスタンス化ビルボード矩形 + EWA 楕円射影)。
/// @details v1: 各スプラットの 3D 共分散を 2D へ EWA 射影し、楕円ガウシアンとして描く。
///          行列は skybox と同じ row-vector 規約 (mul(v, gView) → mul(v, gProj))。
///          conic-build と power は aras-p 準拠でペア (符号一致)。バッファ構造は
///          SplatScene.hpp の SplatGPU (64 B) と一致。詳細: docs/splatting-dx12.md

namespace mitiru::render
{

/// @brief スプラット VS (EWA 楕円射影)。
inline constexpr const char* SPLAT_VS_HLSL = R"HLSL(
cbuffer SplatCam : register(b0)
{
    float4x4 gView;
    float4x4 gProj;
    float4   gParams;   // x=viewportW, y=viewportH, z=focalX(px), w=focalY(px)
};

struct Splat
{
    float3 pos;   float opacity;
    float3 scale; float _pad;
    float4 rot;   // xyzw 正規化
    float4 rgb;
};
StructuredBuffer<Splat> gSplats : register(t0);
StructuredBuffer<uint>  gOrder  : register(t1);   // 深度ソート済みインデックス (奥→手前)

struct VSOut
{
    float4 svpos : SV_Position;
    float3 conic : TEXCOORD0;   // 2D 逆共分散 (a, b, c)
    float2 d     : TEXCOORD1;   // 中心からのピクセル差分 (この隅)
    float4 col   : TEXCOORD2;   // rgb, opacity
};

float3x3 quatToMat(float4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return float3x3(
        1.0 - 2.0*(y*y + z*z), 2.0*(x*y - w*z),       2.0*(x*z + w*y),
        2.0*(x*y + w*z),       1.0 - 2.0*(x*x + z*z), 2.0*(y*z - w*x),
        2.0*(x*z - w*y),       2.0*(y*z + w*x),       1.0 - 2.0*(x*x + y*y));
}

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;
    Splat s = gSplats[gOrder[iid]];                  // 深度順に読む
    float4 vp   = mul(float4(s.pos, 1.0), gView);   // view 空間 (row-vector)
    float4 clip = mul(vp, gProj);
    if (clip.w <= 0.0) { o.svpos = float4(2,2,2,1); o.conic = 0; o.d = 0; o.col = 0; return o; }

    float Wv = gParams.x, Hv = gParams.y, fx = gParams.z, fy = gParams.w;

    // 3D 共分散 Sigma3 = R S S^T R^T (world)
    float3x3 Rq   = quatToMat(s.rot);
    float3x3 Sc   = float3x3(s.scale.x,0,0, 0,s.scale.y,0, 0,0,s.scale.z);
    float3x3 Mm   = mul(Rq, Sc);
    float3x3 Sig3 = mul(Mm, transpose(Mm));

    // world->view 回転 (column 規約) = row-vector view 3x3 の転置
    float3x3 Wrot = transpose((float3x3)gView);

    // 射影のヤコビアン (view->screen px)
    float z = vp.z; float iz = 1.0 / z;
    float3x3 J = float3x3(
        fx*iz, 0,     -fx*vp.x*iz*iz,
        0,     fy*iz, -fy*vp.y*iz*iz,
        0,     0,     0);
    float3x3 T   = mul(J, Wrot);
    float3x3 cov = mul(mul(T, Sig3), transpose(T));
    float a = cov[0][0] + 0.3;   // low-pass dilation
    float b = cov[0][1];
    float c = cov[1][1] + 0.3;
    float det = a*c - b*b;
    if (det <= 0.0) { o.svpos = float4(2,2,2,1); o.conic = 0; o.d = 0; o.col = 0; return o; }

    float3 conic = float3(c, -b, a) / det;           // 逆 2x2
    float  mid   = 0.5*(a + c);
    float  lam   = mid + sqrt(max(0.1, mid*mid - det));
    float  radius = min(ceil(3.0*sqrt(lam)), 256.0); // 3σ (px、上限で暴走防止)

    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float2 dpix   = corner * radius;
    o.svpos = clip;
    o.svpos.x += ( 2.0 * dpix.x / Wv) * clip.w;
    o.svpos.y += (-2.0 * dpix.y / Hv) * clip.w;       // 画面下向き → 反転
    o.conic = conic;
    o.d     = dpix;
    o.col   = float4(s.rgb.rgb, s.opacity);
    return o;
}
)HLSL";

/// @brief スプラット PS (conic ガウシアン減衰、プリマルチプライ合成)。
inline constexpr const char* SPLAT_PS_HLSL = R"HLSL(
struct VSOut
{
    float4 svpos : SV_Position;
    float3 conic : TEXCOORD0;
    float2 d     : TEXCOORD1;
    float4 col   : TEXCOORD2;
};

// engine の ACES filmic + gamma2.2 トーンマップを打ち消す逆補正。splat 色は写真から
// 焼き込み済みの完成 sRGB なので、本来トーンマップ不要。逆を掛けて忠実な色で出す。
// 出力 = pow(ACES(Cin), 1/2.2) なので Cin = ACES^-1(col^2.2) を渡せば最終的に col に戻る。
float3 acesInv(float3 y)
{
    y = clamp(y, 0.0, 0.98);                 // ACES 最大付近で発散するのでクランプ
    float3 a = 2.43*y - 2.51;
    float3 b = 0.59*y - 0.03;
    float3 c = 0.14*y;
    return (-b - sqrt(max(0.0, b*b - 4.0*a*c))) / (2.0*a);
}

float4 PSMain(VSOut i) : SV_Target
{
    float power = -0.5*(i.conic.x*i.d.x*i.d.x + i.conic.z*i.d.y*i.d.y) + i.conic.y*i.d.x*i.d.y;
    float alpha = min(0.99, i.col.a * exp(power));
    if (alpha < (1.0 / 255.0)) { discard; }
    float3 rgb = acesInv(pow(max(i.col.rgb, 0.0), 2.2));   // トーンマップ逆補正
    return float4(rgb * alpha, alpha);   // premultiplied over
}
)HLSL";

}  // namespace mitiru::render
