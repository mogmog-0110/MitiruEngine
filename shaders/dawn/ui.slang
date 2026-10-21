// RmlUi の描画。DX12 の RmlDx12Shaders.hpp と同じ式。幾何 (色・画像・グラデーション)、層の合成、filter (不透明度・色行列・
// マスク・影・ぼかし)。定数は全パイプラインで 1 つの Draw を共有し、使わない欄は無視する。色は全て乗算済みアルファ。

struct Draw
{
    float4x4 transform;       // 論理座標 -> クリップ空間 (RmlUi の射影 * 要素の変換)
    float4x4 colorMatrix;
    float4   stopColors[16];
    float4   stopPositions[4];
    float4   color;
    float4   weights;
    float4   a;               // xy = 要素の平行移動 zw = グラデーションの p
    float4   b;               // xy = グラデーションの v zw = ぼかしの 1 画素 (uv)
    float4   c;               // xy = 読む範囲の左上 (uv) zw = 右下
    float4   d;               // xy = uv の足し zw = uv の倍率
    float4   f;               // x = 不透明度
    int4     ints;            // x = グラデーションの種類 y = 色止めの数
};

[[vk::binding(0, 0)]] ConstantBuffer<Draw> draw;
[[vk::binding(1, 0)]] Texture2D<float4> uTex;
[[vk::binding(2, 0)]] Texture2D<float4> uMask;
[[vk::binding(3, 0)]] SamplerState sWrap;
[[vk::binding(4, 0)]] SamplerState sClamp;

struct GeoIn
{
    float2 pos   : POSITION;
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
};

struct GeoOut
{
    float4 pos   : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv    : TEXCOORD1;
};

struct FullOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
GeoOut vsGeometry(GeoIn i)
{
    GeoOut o;
    o.pos = mul(draw.transform, float4(i.pos + draw.a.xy, 0.0, 1.0));
    o.color = i.color;
    o.uv = i.uv;
    return o;
}

[shader("fragment")]
float4 fsColor(GeoOut i) : SV_Target
{
    return i.color;
}

[shader("fragment")]
float4 fsTexture(GeoOut i) : SV_Target
{
    return i.color * uTex.Sample(sWrap, i.uv);
}

float stopPosition(int k)
{
    return draw.stopPositions[k / 4][k % 4];
}

float4 mixStops(float t)
{
    float4 c = draw.stopColors[0];
    for (int k = 1; k < draw.ints.y; ++k)
    {
        c = lerp(c, draw.stopColors[k], smoothstep(stopPosition(k - 1), stopPosition(k), t));
    }
    return c;
}

float glslMod(float x, float y)
{
    return x - y * floor(x / y);
}

[shader("fragment")]
float4 fsGradient(GeoOut i) : SV_Target
{
    float t = 0.0;
    int f = draw.ints.x % 3;
    float2 d = i.uv - draw.a.zw;
    float2 v = draw.b.xy;
    if (f == 0)
    {
        t = dot(v, d) / dot(v, v);
    }
    else if (f == 1)
    {
        t = length(v * d);
    }
    else
    {
        float2 r = float2(v.x * d.x + v.y * d.y, -v.y * d.x + v.x * d.y);
        t = 0.5 + atan2(-r.x, r.y) / (2.0 * 3.14159265);
    }
    if (draw.ints.x >= 3)
    {
        float t0 = stopPosition(0);
        float t1 = stopPosition(draw.ints.y - 1);
        t = t0 + glslMod(t - t0, t1 - t0);
    }
    return i.color * mixStops(t);
}

// 画面全体を覆う三角形 1 枚。頂点バッファを使わない
[shader("vertex")]
FullOut vsFullscreen(uint id : SV_VertexID)
{
    FullOut o;
    float2 p = float2((id == 2u) ? 3.0 : -1.0, (id == 1u) ? 3.0 : -1.0);
    o.pos = float4(p, 0.0, 1.0);
    o.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5) * draw.d.zw + draw.d.xy;
    return o;
}

// 同じ大きさの描画先へ写すときは標本化せず画素をそのまま読む (補間で値が 1 でも動かないように)
[shader("fragment")]
float4 fsCopy(FullOut i) : SV_Target
{
    return uTex.Load(int3(int2(i.pos.xy), 0)) * draw.f.x;
}

[shader("fragment")]
float4 fsSample(FullOut i) : SV_Target
{
    return uTex.Sample(sClamp, i.uv);
}

[shader("fragment")]
float4 fsColorMatrix(FullOut i) : SV_Target
{
    float4 c = uTex.Load(int3(int2(i.pos.xy), 0));
    return float4(mul(draw.colorMatrix, c).rgb, c.a);
}

[shader("fragment")]
float4 fsBlendMask(FullOut i) : SV_Target
{
    int3 p = int3(int2(i.pos.xy), 0);
    return uTex.Load(p) * uMask.Load(p).a;
}

float inRegion(float2 uv)
{
    float2 s = step(draw.c.xy, uv) * step(uv, draw.c.zw);
    return s.x * s.y;
}

[shader("fragment")]
float4 fsDropShadow(FullOut i) : SV_Target
{
    return uTex.Sample(sClamp, i.uv).a * inRegion(i.uv) * draw.color;
}

// 7 点のぼかし。向きは b.zw (1 画素の uv)
[shader("fragment")]
float4 fsBlur(FullOut i) : SV_Target
{
    float4 c = float4(0.0, 0.0, 0.0, 0.0);
    for (int k = 0; k < 7; ++k)
    {
        float2 uv = i.uv - float(k - 3) * draw.b.zw;
        c += uTex.Sample(sClamp, uv) * inRegion(uv) * draw.weights[abs(k - 3)];
    }
    return c;
}

// 範囲を透明にする (層の初期化)
[shader("fragment")]
float4 fsClear(FullOut i) : SV_Target
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
