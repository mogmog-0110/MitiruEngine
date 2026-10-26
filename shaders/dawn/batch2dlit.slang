// 2D ライト (R6) を当てる 2D のバッチ。DX12 の LIT_PS_2D (include/mitiru/render/DefaultShaders2DLit.hpp) と同じ式で、
// 式の正本は include/mitiru/render/Light2DMath.hpp。光を使わない描画は batch2d.slang のまま変えず、光が有効なフレームだけこちらを使う。
// 頂点色・テクスチャ x 頂点色・距離場の文字の 3 通りの色の出し方を残し、最後に環境光と灯を掛ける。

struct LitCB
{
    float4x4 ortho;     // 論理画面 -> クリップ空間 (列優先)
    float4 mode;        // x = 色の出し方 (0 頂点色、1 テクスチャ x 頂点色、2 距離場)、y = 距離場の幅、z = 法線マップあり、w = 1 なら point で読む
    float4 map;         // 論理座標 = (SV_Position.xy - zw) * xy
    float4 ambient;     // rgb = 環境光 (色 x 強さ)、w = 灯の数
    float4 lights[48];  // 1 灯 3 つ: (x, y, 高さ, 半径) (色 x 強さ, 減衰の指数) (向き x, y, cos 外側, cos 内側)
};

[[vk::binding(0, 0)]] ConstantBuffer<LitCB> lit;
[[vk::binding(1, 0)]] Texture2D<float4> colorTex;
[[vk::binding(2, 0)]] Texture2D<float4> normalTex;
[[vk::binding(3, 0)]] SamplerState linearSampler;
[[vk::binding(4, 0)]] SamplerState pointSampler;

struct LitIn
{
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct LitOut
{
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : TEXCOORD1;
};

[shader("vertex")]
LitOut vsLit(LitIn i)
{
    LitOut o;
    o.pos = mul(lit.ortho, float4(i.pos, 0.0, 1.0));
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

[shader("fragment")]
float4 fsLit(LitOut i) : SV_Target
{
    // 微分は分岐の前に取る
    float2 duvdx = ddx(i.uv);
    float2 duvdy = ddy(i.uv);
    float2 uvWidth = fwidth(i.uv);
    bool usePoint = lit.mode.w > 0.5;

    float4 baseColor = i.color;
    if (lit.mode.x > 1.5)
    {
        float4 s = colorTex.SampleLevel(linearSampler, i.uv, 0.0);
        float distance = max(min(s.r, s.g), min(max(s.r, s.g), s.b));
        uint texW;
        uint texH;
        colorTex.GetDimensions(texW, texH);
        float2 unitRange = lit.mode.y / float2(float(texW), float(texH));
        float2 screenTexSize = 1.0 / uvWidth;
        float screenRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float opacity = saturate(screenRange * (distance - 0.5) + 0.5);
        baseColor = float4(i.color.rgb, i.color.a * opacity);
    }
    else if (lit.mode.x > 0.5)
    {
        float4 texColor = usePoint ? colorTex.SampleLevel(pointSampler, i.uv, 0.0) : colorTex.SampleLevel(linearSampler, i.uv, 0.0);
        baseColor = texColor * i.color;
    }

    bool hasNormal = lit.mode.z > 0.5;
    float3 normal = float3(0.0, 0.0, 1.0);
    if (hasNormal)
    {
        float4 raw = usePoint ? normalTex.SampleLevel(pointSampler, i.uv, 0.0) : normalTex.SampleLevel(linearSampler, i.uv, 0.0);
        float3 t = raw.xyz * 2.0 - 1.0;
        float tl = length(t);
        t = tl < 1e-6 ? float3(0.0, 0.0, 1.0) : t / tl;
        float det = duvdx.x * duvdy.y - duvdy.x * duvdx.y;
        float sgn = det < 0.0 ? -1.0 : 1.0;
        float2 tDir = float2(duvdy.y, -duvdx.y);
        float2 bDir = float2(-duvdy.x, duvdx.x);
        float tLen = length(tDir);
        float bLen = length(bDir);
        if (tLen > 1e-12 && bLen > 1e-12)
        {
            tDir = tDir / tLen * sgn;
            bDir = bDir / bLen * sgn;
            float2 xy = t.x * tDir + t.y * -bDir;
            normal = float3(xy, t.z);
        }
    }

    float2 p = (i.pos.xy - lit.map.zw) * lit.map.xy;
    float3 total = lit.ambient.rgb;
    int count = int(lit.ambient.w);
    for (int n = 0; n < count; ++n)
    {
        float4 a = lit.lights[n * 3 + 0];
        float4 b = lit.lights[n * 3 + 1];
        float4 c = lit.lights[n * 3 + 2];
        float2 d = a.xy - p;
        float dist = length(d);
        if (dist >= a.w) { continue; }
        float weight = pow(1.0 - dist / a.w, b.w);
        if (c.z > -1.5)
        {
            float2 toPixel = c.xy;
            if (dist > 1e-5) { toPixel = -d / dist; }
            weight *= smoothstep(c.z, c.w, dot(c.xy, toPixel));
        }
        if (hasNormal)
        {
            float3 l = float3(d, a.z);
            l = l / max(length(l), 1e-6);
            weight *= max(dot(normal, l), 0.0);
        }
        total += b.xyz * weight;
    }
    return float4(baseColor.rgb * total, baseColor.a);
}
