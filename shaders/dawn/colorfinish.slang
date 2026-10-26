// 色の仕上げ (LUT・ビネット・フィルムグレイン)。DX12 の DX12_COLOR_FINISH_PS と ColorFinishMath.hpp と同じ式。
// 出力のコピーを読み、LUT は 8 隅を整数座標で読んで自前でトライリニアに補間する (ハードウェアの補間は GPU で精度が違い、2D に展開した画像では隣の段へにじむ)。

struct ColorFinishCB
{
    float LutSize;
    float LutStrength;
    float VignetteStrength;
    float VignetteRadius;
    float GrainStrength;
    uint  GrainCell;
    uint  Seed;
    float pad0;
    float2 OutSize;
    float2 pad1;
};

[[vk::binding(0, 0)]] ConstantBuffer<ColorFinishCB> cf;
[[vk::binding(1, 0)]] Texture2D<float4> sceneCopy;
[[vk::binding(2, 0)]] Texture2D<float4> lutTex;

struct FinishOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
FinishOut vsColorFinish(uint vid : SV_VertexID)
{
    FinishOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

uint grainHash(uint x, uint y, uint seed)
{
    uint h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (seed * 0xcb1ab31fu);
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return h;
}

float3 lutTexel(int n, int r, int g, int b)
{
    return lutTex.Load(int3(b * n + r, g, 0)).rgb;
}

float3 sampleLut(float3 c)
{
    int n = int(cf.LutSize);
    float3 s = saturate(c) * (cf.LutSize - 1.0);
    float3 fl = floor(s);
    int3 lo = int3(fl);
    int3 hi = min(lo + int3(1, 1, 1), int3(n - 1, n - 1, n - 1));
    float3 f = s - fl;
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int corner = 0; corner < 8; ++corner)
    {
        bool hr = (corner & 1) != 0;
        bool hg = (corner & 2) != 0;
        bool hb = (corner & 4) != 0;
        float w = (hr ? f.x : 1.0 - f.x) * (hg ? f.y : 1.0 - f.y) * (hb ? f.z : 1.0 - f.z);
        sum += lutTexel(n, hr ? hi.x : lo.x, hg ? hi.y : lo.y, hb ? hi.z : lo.z) * w;
    }
    return sum;
}

[shader("fragment")]
float4 fsColorFinish(FinishOut i) : SV_Target
{
    uint2 px = uint2(i.pos.xy);
    float4 src = sceneCopy.Load(int3(int2(px), 0));
    float3 c = src.rgb;
    if (cf.LutSize >= 2.0)
    {
        c += (sampleLut(c) - c) * cf.LutStrength;
    }
    if (cf.VignetteStrength > 0.0)
    {
        float2 halfSize = cf.OutSize * 0.5;
        float2 d = float2(px) + float2(0.5, 0.5) - halfSize;
        float r = sqrt(dot(d, d)) / sqrt(dot(halfSize, halfSize));
        c *= 1.0 - cf.VignetteStrength * smoothstep(cf.VignetteRadius, 1.0, r);
    }
    if (cf.GrainStrength > 0.0)
    {
        float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
        float midtone = 0.5 + 0.5 * (4.0 * luma * (1.0 - luma));
        uint cell = max(cf.GrainCell, 1u);
        float noise = float(grainHash(px.x / cell, px.y / cell, cf.Seed) >> 8u) * (2.0 / 16777216.0) - 1.0;
        c += noise * cf.GrainStrength * midtone;
    }
    return float4(saturate(c), src.a);
}
