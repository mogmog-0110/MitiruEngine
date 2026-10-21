// 描画ごとの輪郭線 (DX12 の DX12ToonShaders.hpp の線の 2 パスと同じ式)。
//   fsSeed : 境の手前側の画素に、その画素の距離と印を書く (境ごとに片側だけ)
//   fsLine : 近くの種から、その種の印の太さで線を引く。画素は種からの距離で覆われる割合を決める
// 法線の MSAA (a = 印 × 32 + N·V) と、線形の距離を 24 bit に詰めた MSAA の色 (forward.slang の packLinearDepth) の
// サンプル 0 を読む。深度テクスチャは互換レベルで textureLoad できないので、通常のレベルも同じ作りにしてある。

struct LineCB
{
    float4 screen;       // xy = 場面 (MSAA の絵) の大きさ zw = 出力の大きさ
    float4 depthParams;  // x = 距離を詰めた範囲 (カメラの遠方) z = 線にする深さの段差 (割合) w = 折れ目とみなす面の向きの差 (cos)
    float4 extra;        // x = 線の画素を探す半径
    float4 slotColor[8]; // rgb = 線の色 a = 線の色の割合
    float4 slotWidth[8]; // x = 近くの太さ y = 遠くの太さ (画素) z = 遠くの太さにする距離
    float4 slotFade[8];  // x = 薄くし始める距離 y = 消える距離 z = その先で残す不透明度
};

[[vk::binding(0, 0)]] ConstantBuffer<LineCB> lineCb;
[[vk::binding(1, 0)]] Texture2DMS<float4> depthMs;
[[vk::binding(2, 0)]] Texture2DMS<float4> normalMs;
[[vk::binding(3, 0)]] Texture2D<float2> seedTex;

struct LineOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
LineOut vsLine(uint vid : SV_VertexID)
{
    LineOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

struct LinePixel
{
    float  d;
    float3 n;
    uint   slot;
    bool   excluded;
};

float decodeDepth(float4 c)
{
    uint v = (uint(c.r * 255.0 + 0.5) << 16u) | (uint(c.g * 255.0 + 0.5) << 8u) | uint(c.b * 255.0 + 0.5);
    return float(v) / 16777215.0 * lineCb.depthParams.x;
}

uint slotOfNormalA(float a)
{
    return uint(round(a * 255.0)) >> 5u;
}

LinePixel loadLinePixel(int2 p)
{
    p = clamp(p, int2(0, 0), int2(lineCb.screen.xy) - int2(1, 1));
    float4 e = normalMs.Load(p, 0);
    LinePixel o;
    o.d = decodeDepth(depthMs.Load(p, 0));
    o.n = e.rgb * 2.0 - 1.0;
    o.slot = slotOfNormalA(e.a);
    o.excluded = e.r > 0.99 && e.g > 0.99 && e.b > 0.99;
    return o;
}

// 出力の画素の位置を、場面の画素の位置へ (内部解像度が出力より小さいとき)
int2 scenePixel(float2 pos)
{
    return int2(pos * lineCb.screen.xy / lineCb.screen.zw);
}

// 中心 c が、隣 q との境の手前側か。opp は q と反対側の隣。
// 深さは 1/距離 が平面の上で画面に線形なので、opp と c から q の所を外挿し、q がそれより段差の割合以上奥なら境とする
// (傾いた床は外挿どおりなので線にならない)
bool ownsEdge(LinePixel c, LinePixel q, LinePixel opp)
{
    if (q.excluded || q.d < c.d) { return false; }
    if (q.slot != c.slot) { return true; }
    float iOpp = opp.excluded ? 1.0 / c.d : 1.0 / opp.d;
    float iPred = 2.0 / c.d - iOpp;
    if (1.0 / q.d < iPred * (1.0 - lineCb.depthParams.z)) { return true; }
    // 法線の無い画素 (空) は折れ目を見ない
    if (dot(c.n, c.n) < 0.25 || dot(q.n, q.n) < 0.25) { return false; }
    return dot(normalize(c.n), normalize(q.n)) < lineCb.depthParams.w;
}

[shader("fragment")]
float2 fsSeed(LineOut i) : SV_Target
{
    // 種の絵は場面と同じ大きさなので、fragment の位置がそのまま場面の画素
    int2 p = int2(i.pos.xy);
    LinePixel c = loadLinePixel(p);
    if (c.excluded) { return float2(0.0, 0.0); }
    LinePixel l = loadLinePixel(p + int2(-1, 0));
    LinePixel r = loadLinePixel(p + int2(1, 0));
    LinePixel u = loadLinePixel(p + int2(0, -1));
    LinePixel d = loadLinePixel(p + int2(0, 1));
    bool own = ownsEdge(c, l, r) || ownsEdge(c, r, l) || ownsEdge(c, u, d) || ownsEdge(c, d, u);
    return own ? float2(c.d, float(c.slot)) : float2(0.0, 0.0);
}

float lineWidthAt(uint s, float d)
{
    float4 w = lineCb.slotWidth[s];
    return (w.z > 0.0 && d > w.z) ? w.y : w.x;
}

float lineAlphaAt(uint s, float d)
{
    float4 f = lineCb.slotFade[s];
    if (!(f.y > f.x)) { return 1.0; }
    return lerp(1.0, f.z, saturate((d - f.x) / (f.y - f.x)));
}

[shader("fragment")]
float4 fsLine(LineOut i) : SV_Target
{
    int2 p = scenePixel(i.pos.xy);
    float here = decodeDepth(depthMs.Load(p, 0));
    int radius = int(lineCb.extra.x);
    float bestA = 0.0;
    float bestD = 1e30;
    float3 bestC = float3(0.0, 0.0, 0.0);
    int2 maxP = int2(lineCb.screen.xy) - int2(1, 1);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            float2 s = seedTex.Load(int3(clamp(p + int2(x, y), int2(0, 0), maxP), 0)).xy;
            if (s.x <= 0.0) { continue; }
            uint slot = min(uint(s.y + 0.5), 7u);
            float w = lineWidthAt(slot, s.x);
            // 線の持ち主より手前にある物の上には引かない
            if (w <= 0.0 || here < s.x * 0.97 - 0.02) { continue; }
            // 種を中心に幅 w の帯が、この画素 (幅 1) と重なる割合。1 画素より細い線も濃さの合計が w になる
            float t = length(float2(float(x), float(y)));
            float cover = saturate(min(t + 0.5, w * 0.5) - max(t - 0.5, -w * 0.5));
            float a = cover * lineAlphaAt(slot, s.x) * lineCb.slotColor[slot].a;
            if (a > bestA + 1e-3 || (a > bestA - 1e-3 && a > 0.0 && s.x < bestD))
            {
                bestA = a;
                bestD = s.x;
                bestC = lineCb.slotColor[slot].rgb;
            }
        }
    }
    if (bestA <= 0.004) { discard; }
    return float4(bestC, bestA);
}
