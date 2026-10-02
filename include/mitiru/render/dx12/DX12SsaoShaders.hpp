#pragma once

/// @file DX12SsaoShaders.hpp
/// @brief SSAO (画面空間の環境遮蔽) の生成 PS と深度重み付きの分離ぼかし PS (v40、ADR 0042)
/// @details 主パスの MSAA 深度 (t0) を sample 0 で読み、深度から組み直した視空間の面の向きの半球に
///          16 本サンプルして遮蔽率を R8 に書く。ぼかしは横→縦の 2 回で、深度差が Radius を超える
///          隣は重みを落として物の縁の外へ暗さが滲まないようにする。結果は tonemap PS が t1 で読み、
///          HDR 色に掛ける。root sig / VS は outline post のもの (SRV t0..t2 + CBV b0、フルスクリーン三角形) を共用。

namespace mitiru::render
{

/// @brief SSAO 共通の cbuffer と入力。生成・ぼかしの両 PS が同じ並びで読む
inline constexpr const char* DX12_SSAO_COMMON_HLSL = R"hlsl(
Texture2DMS<float, 4>  g_depth  : register(t0);
Texture2DMS<float4, 4> g_normal : register(t1);
Texture2D<float>       g_aoIn   : register(t2);

cbuffer CbSsao : register(b0)
{
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 View;
    float2   TexelSize;   // 1 / viewport
    float2   ScreenSize;  // viewport (px)
    float    Radius;      // 遮蔽を探す半径 (ワールド単位 = 視空間単位)
    float    Strength;
    float    NearZ;
    float    FarZ;
    float2   BlurDir;     // ぼかしの向き (1,0) / (0,1)。生成パスでは未使用
    float2   _pad;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float3 viewPosAt(int2 pix, float d)
{
    float2 uv  = (float2(pix) + 0.5) * TexelSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 p   = mul(InvProj, float4(ndc, d, 1.0));
    return p.xyz / p.w;
}

float linearizeDepth(float d)
{
    return NearZ * FarZ / (FarZ - d * (FarZ - NearZ));
}
)hlsl";

/// @brief 遮蔽率の生成。出力 1 = 遮られていない
inline constexpr const char* DX12_SSAO_PS_BODY = R"hlsl(
// 法線向きの半球 (z > 0)。原点寄りに詰めてあるので細い隙間も拾う
static const float3 kKernel[16] =
{
    float3( 0.5381,  0.1856,  0.4319), float3( 0.1379,  0.2486,  0.4430),
    float3( 0.3371,  0.5679,  0.0057), float3(-0.6999, -0.0451,  0.0019),
    float3( 0.0689, -0.1598,  0.8547), float3( 0.0560,  0.0069,  0.1843),
    float3(-0.0146,  0.1402,  0.0762), float3( 0.0100, -0.1924,  0.0344),
    float3(-0.3577, -0.5301,  0.4358), float3(-0.3169,  0.1063,  0.0158),
    float3( 0.0103, -0.5869,  0.0046), float3(-0.0897, -0.4940,  0.3287),
    float3( 0.7119, -0.0154,  0.0918), float3(-0.0533,  0.0596,  0.5411),
    float3( 0.0352, -0.0631,  0.5460), float3(-0.4776,  0.2847,  0.0271),
};

// 面の向きは法線 RT ではなく深度から組み直す。法線 RT は頂点法線の補間なので、角を丸めた大きな面
// (台の天板) では三角形ごとに数度ずつ傾き、平らな面が自分の隣を「上にある」と数えてまだらに暗くなる。
// 左右・上下は深度の差が小さい側を取る (物の縁の向こうの画素で向きが折れないように)。
// 画面の端では外側の隣が無いので内側だけを使う (端で自分自身を隣にすると差が 0 になり、向きが NaN になる)
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
    float4 nrm = g_normal.Load(pix, 0);
    // outlineCaster=false の印 (1,1,1) は法線として読めない
    if (all(nrm.rgb > 0.99)) { return 1.0; }

    float3 P = viewPosAt(pix, d);
    float3 N = faceNormal(pix, P);

    // 画素ごとに半球を回す (interleaved gradient noise)。縞はぼかしで消える
    float  n   = frac(52.9829189 * frac(dot(float2(pix), float2(0.06711056, 0.00583715))));
    float  ang = n * 6.2831853;
    float3 rnd = float3(cos(ang), sin(ang), 0.3);
    float3 T   = normalize(rnd - N * dot(rnd, N));
    float3 B   = cross(N, T);

    // 面の自己遮蔽 (acne) を避ける最小段差
    float bias = Radius * 0.04;
    float occ  = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float3 k = kKernel[i];
        float3 s = P + (T * k.x + B * k.y + N * k.z) * Radius;
        float4 clip = mul(Proj, float4(s, 1.0));
        if (clip.w <= 1e-4) { continue; }
        float2 suv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (any(suv < 0.0) || any(suv > 1.0)) { continue; }
        int2   spix  = clamp(int2(suv * ScreenSize), int2(0, 0), int2(ScreenSize) - 1);
        float3 scene = viewPosAt(spix, g_depth.Load(spix, 0));
        float3 toScene = scene - P;
        // 視線は -Z。見つかった面がサンプル点より手前 (z が大きい) なら遮られている。
        // ただし自分の接平面より上にある面だけ数える (すれすれの床は画素の量子化で手前に見えて acne になる)。
        // 遠く離れた面 (物の縁の向こう) は半径の 2 倍で 0 になるよう減衰させ、輪郭の外に halo を出さない。
        // 見上げる角 (高さ / 距離) が浅い面も数えない: 核の半分は接平面すれすれなので、壁から 1.5 cm 浮いた
        // 暦の脇が半径いっぱいに暗くなった (KaeruCrepe の事務所で輝度 216 → 201)。入隅や器の足元は角が深いので残る
        float dist    = length(toScene);
        float rise    = dot(toScene, N);
        float steep   = saturate((rise / max(dist, 1e-4) - 0.15) * 4.0);
        bool  inFront = scene.z >= s.z + bias;
        float range   = saturate(2.0 - dist / Radius);
        occ += (inFront && rise > bias) ? range * steep : 0.0;
    }
    // 強さは指数で掛ける。直線 (1 - 強さ × 遮蔽率) だと強さ 2 で半分遮られた隙間が真っ黒になり、
    // 箱と箱の間のような深い隙間が墨の線になる。強さ 1 は直線と同じ
    return pow(saturate(1.0 - occ / 16.0), Strength);
}
)hlsl";

/// @brief 深度重み付きの 9 タップ分離ぼかし。BlurDir の向きに 1 回、2 回呼んで縦横。
///        タップは 2 px おき (幅 17 px): 1 px おきでは半球を回す模様が 4〜5 px の斑に残った
inline constexpr const char* DX12_SSAO_BLUR_PS_BODY = R"hlsl(
float PSMain(PSInput input) : SV_TARGET
{
    int2  pix = int2(input.Position.xy);
    float dC  = linearizeDepth(g_depth.Load(pix, 0));
    static const float kW[5] = { 0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162 };
    int2  dir = int2(BlurDir);
    int2  hi  = int2(ScreenSize) - 1;

    float sum = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int k = -4; k <= 4; ++k)
    {
        int2  q  = clamp(pix + dir * (k * 2), int2(0, 0), hi);
        float dq = linearizeDepth(g_depth.Load(q, 0));
        float t  = (dq - dC) / max(Radius, 1e-3);
        float w  = kW[abs(k)] * exp(-4.0 * t * t);
        sum  += g_aoIn.Load(int3(q, 0)) * w;
        wsum += w;
    }
    return sum / max(wsum, 1e-4);
}
)hlsl";

} // namespace mitiru::render
