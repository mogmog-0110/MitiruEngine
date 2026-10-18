#pragma once

/// @file DX12ToonShaders.hpp
/// @brief 描画ごとのトゥーンの段・線・止め絵の HLSL (ADR 0079)
/// @details 法線 RT の a は「描画の印 × 32 + N·V を 31 段」を 255 で割った値で、印は ToonStyleGpu.hpp の表の番号。
///          段の式 (DX12_TOON_RAMP_HLSL) は前方の描画と clod で同じ文字列を使う (clod は generate_blobs.py が取り出す)。

namespace mitiru::render
{

/// @brief 法線 RT の a に描画の印と N·V を詰める・取り出す
inline constexpr const char* DX12_TOON_SLOT_HLSL = R"hlsl(
float packSlotNdv(float slot, float ndv)
{
    return (slot * 32.0 + round(saturate(ndv) * 31.0)) / 255.0;
}

uint slotOfNormalA(float a)
{
    return ((uint)round(a * 255.0)) >> 5;
}

float ndvOfNormalA(float a)
{
    return (float)(((uint)round(a * 255.0)) & 31u) / 31.0;
}
)hlsl";

/// @brief 段の位置と色。p = (段の数, 境の半幅, 落ちた影で下げる段, 印)、th.xyz = 段の境
inline constexpr const char* DX12_TOON_RAMP_HLSL = R"hlsl(
// 段の位置 0 .. 段の数 - 1。境の半幅は画素の lambert の傾きまで広げる (細い境は斜めの面で点々に砕ける)。
// 上限 0.25 は、段そのものが溶けて平らにならないため
float toonBandLevel(float lambert, float4 p, float4 th, float grad)
{
    float h = max(p.y, min(grad, 0.25));
    float level = 0.0;
    [unroll]
    for (int k = 0; k < 3; ++k)
    {
        level += ((float)(k + 1) < p.x) ? smoothstep(th[k] - h, th[k] + h, lambert) : 0.0;
    }
    return level;
}

// 落ちた影の中 (lit 0) は p.z 段だけ下げる。p.z が 0 なら一番暗い段まで
float toonShadowedLevel(float level, float lit, float4 p)
{
    float down = (p.z > 0.5) ? max(level - p.z, 0.0) : 0.0;
    return lerp(down, level, lit);
}

// 段の位置の色。段の境では隣の 2 色を混ぜる
float3 toonBandTint(float level, float4 t0, float4 t1, float4 t2, float4 t3)
{
    return t0.rgb + saturate(level) * (t1.rgb - t0.rgb) + saturate(level - 1.0) * (t2.rgb - t1.rgb)
         + saturate(level - 2.0) * (t3.rgb - t2.rgb);
}
)hlsl";

/// @brief 線の 2 パスが共有する定数と画素の読み方
inline constexpr const char* DX12_TOON_LINE_COMMON_HLSL = R"hlsl(
Texture2DMS<float, 4>  DepthTexture  : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);
Texture2D<float2>      SeedTexture   : register(t2);

cbuffer CbToonLine : register(b0)
{
    float4 ScreenInfo;     // xy = 画面の大きさ z = 未使用 w = 線の画素を探す半径
    float4 DepthParams;    // x = near y = far z = 線にする深さの段差 (割合) w = 線にする面の向きの差 (cos)
    float4 SlotColor[8];   // rgb = 線の色 a = 線の色の割合
    float4 SlotWidth[8];   // x = 近くの太さ y = 遠くの太さ (今の画面の画素) z = 遠くの太さにする距離
    float4 SlotFade[8];    // x = 薄くし始める距離 y = 消える距離 z = その先で残す不透明度
};

struct LinePixel
{
    float  d;
    float3 n;
    uint   slot;
    bool   excluded;
};

float lineDepth(float z)
{
    return DepthParams.x * DepthParams.y / (DepthParams.y - z * (DepthParams.y - DepthParams.x));
}

LinePixel loadLinePixel(int2 p)
{
    p = clamp(p, int2(0, 0), int2(ScreenInfo.xy) - 1);
    float4 e = NormalTexture.Load(p, 0);
    LinePixel o;
    o.d = lineDepth(DepthTexture.Load(p, 0));
    o.n = e.rgb * 2.0 - 1.0;
    o.slot = slotOfNormalA(e.a);
    o.excluded = all(e.rgb > 0.99);
    return o;
}
)hlsl";

/// @brief 線の種: 境の手前側の画素に、その画素の距離と印を書く (境ごとに片側だけ)
inline constexpr const char* DX12_TOON_LINE_SEED_PS = R"hlsl(
// 中心 c が、隣 q との境の手前側か。opp は q と反対側の隣。
// 深さは 1/距離 が平面の上で画面に線形なので、opp と c から q の所を外挿し、q がそれより段差の割合以上奥なら境とする
// (傾いた床は外挿どおりなので線にならない)
bool ownsEdge(LinePixel c, LinePixel q, LinePixel opp)
{
    if (q.excluded || q.d < c.d) { return false; }
    if (q.slot != c.slot) { return true; }
    float iOpp = opp.excluded ? 1.0 / c.d : 1.0 / opp.d;
    float iPred = 2.0 / c.d - iOpp;
    if (1.0 / q.d < iPred * (1.0 - DepthParams.z)) { return true; }
    // 法線の無い画素 (clod・空) は折れ目を見ない
    if (dot(c.n, c.n) < 0.25 || dot(q.n, q.n) < 0.25) { return false; }
    return dot(normalize(c.n), normalize(q.n)) < DepthParams.w;
}

float2 PSMain(float4 pos : SV_POSITION) : SV_TARGET
{
    int2 p = int2(pos.xy);
    LinePixel c = loadLinePixel(p);
    if (c.excluded) { return 0.0; }
    LinePixel l = loadLinePixel(p + int2(-1, 0));
    LinePixel r = loadLinePixel(p + int2(1, 0));
    LinePixel u = loadLinePixel(p + int2(0, -1));
    LinePixel d = loadLinePixel(p + int2(0, 1));
    bool own = ownsEdge(c, l, r) || ownsEdge(c, r, l) || ownsEdge(c, u, d) || ownsEdge(c, d, u);
    return own ? float2(c.d, (float)c.slot) : float2(0.0, 0.0);
}
)hlsl";

/// @brief 線: 近くの種から、その種の印の太さで線を引く。画素は種からの距離で覆われる割合を決める
inline constexpr const char* DX12_TOON_LINE_DRAW_PS = R"hlsl(
float lineWidthAt(uint s, float d)
{
    float4 w = SlotWidth[s];
    return (w.z > 0.0 && d > w.z) ? w.y : w.x;
}

float lineAlphaAt(uint s, float d)
{
    float4 f = SlotFade[s];
    if (!(f.y > f.x)) { return 1.0; }
    return lerp(1.0, f.z, saturate((d - f.x) / (f.y - f.x)));
}

float4 PSMain(float4 pos : SV_POSITION) : SV_TARGET
{
    int2 p = int2(pos.xy);
    float here = lineDepth(DepthTexture.Load(p, 0));
    int radius = (int)ScreenInfo.w;
    float bestA = 0.0;
    float bestD = 1e30;
    float3 bestC = 0.0;
    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            float2 s = SeedTexture.Load(int3(clamp(p + int2(x, y), int2(0, 0), int2(ScreenInfo.xy) - 1), 0));
            if (s.x <= 0.0) { continue; }
            uint slot = min((uint)(s.y + 0.5), 7u);
            float w = lineWidthAt(slot, s.x);
            // 線の持ち主より手前にある物の上には引かない
            if (w <= 0.0 || here < s.x * 0.97 - 0.02) { continue; }
            // 種を中心に幅 w の帯が、この画素 (幅 1) と重なる割合。1 画素より細い線も濃さの合計が w になる
            float t = length(float2(x, y));
            float cover = saturate(min(t + 0.5, w * 0.5) - max(t - 0.5, -w * 0.5));
            float a = cover * lineAlphaAt(slot, s.x) * SlotColor[slot].a;
            if (a > bestA + 1e-3 || (a > bestA - 1e-3 && a > 0.0 && s.x < bestD))
            {
                bestA = a;
                bestD = s.x;
                bestC = SlotColor[slot].rgb;
            }
        }
    }
    if (bestA <= 0.004) { discard; }
    return float4(bestC, bestA);
}
)hlsl";

/// @brief 止め絵。出力の大きさの写しを読み、画素の印が色のまま残す物でなければ 2 色に塗るか反転する
inline constexpr const char* DX12_IMPACT_FRAME_PS = R"hlsl(
Texture2D<float4>      g_scene  : register(t0);
Texture2DMS<float4, 4> g_normal : register(t1);

cbuffer CbImpact : register(b0)
{
    float4 LightColor;   // rgb = 明るい側の色 a = 明るさのしきい値
    float4 DarkColor;    // rgb = 暗い側の色 a = 寄せる割合
    float4 Params;       // x = 1 なら 2 色、2 なら反転 yz = 法線 RT の大きさ
    float4 KeepLo;       // 印 0..3 を色のまま残すか
    float4 KeepHi;       // 印 4..7
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 c = g_scene.Load(int3(input.Position.xy, 0)).rgb;
    int2 np = min(int2(input.TexCoord * Params.yz), int2(Params.yz) - 1);
    uint slot = slotOfNormalA(g_normal.Load(np, 0).a);
    float keep = (slot < 4) ? KeepLo[slot] : KeepHi[slot - 4];
    if (keep > 0.5) { return float4(c, 1.0); }
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 o = (Params.x < 1.5) ? ((lum >= LightColor.a) ? LightColor.rgb : DarkColor.rgb) : 1.0 - c;
    return float4(lerp(c, o, DarkColor.a), 1.0);
}
)hlsl";

} // namespace mitiru::render
