// bloom の縮小 (しきい値つき) と戻し。DX12 の DX12BloomShaders.hpp と同じ式。
// 1/2 → 1/4 に縮めて 1/4 を tent で 1/2 に戻し、1/2 と足す。結果を post.slang が HDR に Strength 倍して足す。

struct BloomCB
{
    float4 params;   // xy = 読む側の 1 texel (uv) z = しきい値 w = 1 なら明部だけ残す (最初の縮小)
};

[[vk::binding(0, 0)]] ConstantBuffer<BloomCB> bloom;
[[vk::binding(1, 0)]] Texture2D<float4> srcTex;
[[vk::binding(2, 0)]] Texture2D<float4> addTex;
[[vk::binding(3, 0)]] SamplerState linearClamp;

struct BloomOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
BloomOut vsBloom(uint vid : SV_VertexID)
{
    BloomOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}

// 平均してから切ると明るい 1 画素が周りに薄められて拾えないので、しきい値は各タップに掛けてから平均する
float3 prefilter(float3 c)
{
    if (bloom.params.w < 0.5) { return c; }
    float br = max(c.r, max(c.g, c.b));
    float w = max(br - bloom.params.z, 0.0) / max(br, 1e-4);
    return c * w;
}

[shader("fragment")]
float4 fsBloomDown(BloomOut i) : SV_Target
{
    float2 o = bloom.params.xy;
    float3 c = prefilter(srcTex.SampleLevel(linearClamp, i.uv + float2(-o.x, -o.y), 0.0).rgb)
             + prefilter(srcTex.SampleLevel(linearClamp, i.uv + float2( o.x, -o.y), 0.0).rgb)
             + prefilter(srcTex.SampleLevel(linearClamp, i.uv + float2(-o.x,  o.y), 0.0).rgb)
             + prefilter(srcTex.SampleLevel(linearClamp, i.uv + float2( o.x,  o.y), 0.0).rgb);
    return float4(c * 0.25, 1.0);
}

[shader("fragment")]
float4 fsBloomUp(BloomOut i) : SV_Target
{
    float2 o = bloom.params.xy;
    float3 c = srcTex.SampleLevel(linearClamp, i.uv + float2(-o.x, -o.y), 0.0).rgb
             + srcTex.SampleLevel(linearClamp, i.uv + float2( 0.0, -o.y), 0.0).rgb * 2.0
             + srcTex.SampleLevel(linearClamp, i.uv + float2( o.x, -o.y), 0.0).rgb
             + srcTex.SampleLevel(linearClamp, i.uv + float2(-o.x,  0.0), 0.0).rgb * 2.0
             + srcTex.SampleLevel(linearClamp, i.uv, 0.0).rgb * 4.0
             + srcTex.SampleLevel(linearClamp, i.uv + float2( o.x,  0.0), 0.0).rgb * 2.0
             + srcTex.SampleLevel(linearClamp, i.uv + float2(-o.x,  o.y), 0.0).rgb
             + srcTex.SampleLevel(linearClamp, i.uv + float2( 0.0,  o.y), 0.0).rgb * 2.0
             + srcTex.SampleLevel(linearClamp, i.uv + float2( o.x,  o.y), 0.0).rgb;
    return float4(c / 16.0 + addTex.SampleLevel(linearClamp, i.uv, 0.0).rgb, 1.0);
}
