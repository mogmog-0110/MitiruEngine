// MSAA を解いた HDR (RGBA16F) を LDR (RGBA8) にする。DX12 の DX12Tonemap.hpp (中立の肩 + 彩度・コントラスト + sRGB) と同じ式。
// 内部解像度が出力より小さいとき (画質の段の renderScale) は、fsTonemapScaled が双線形で引き伸ばす。
// bloom は DX12 の g_bloom と同じく、露出の前に HDR へ Strength 倍して足す。AO は次の区切り。

struct PostCB
{
    float4 tone;   // x = 露出 y = ガンマ z = 彩度 w = コントラスト
    float4 size;   // xy = 出力の大きさ (画素) z = bloom の強さ (0 は足さない) w = 中間の色の倍率の逆数
};

[[vk::binding(0, 0)]] ConstantBuffer<PostCB> post;
[[vk::binding(1, 0)]] Texture2D<float4> hdr;
[[vk::binding(2, 0)]] SamplerState hdrSampler;
[[vk::binding(3, 0)]] Texture2D<float4> bloomTex;

struct PostOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
PostOut vsPost(uint vid : SV_VertexID)
{
    PostOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 neutralShoulder(float3 c)
{
    const float knee = 0.9;
    const float d = 1.0 - knee;
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= knee) { return c; }
    float newPeak = 1.0 - d * d / (peak + d - knee);
    return c * (newPeak / peak);
}

float3 linearToSrgb(float3 x)
{
    float3 lo = x * 12.92;
    float3 hi = 1.055 * pow(max(x, float3(0.0031308, 0.0031308, 0.0031308)), float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) - 0.055;
    return float3(x.r <= 0.0031308 ? lo.r : hi.r, x.g <= 0.0031308 ? lo.g : hi.g, x.b <= 0.0031308 ? lo.b : hi.b);
}

float4 tonemap(float4 c, float2 uv)
{
    if (post.size.z > 0.0) { c.rgb += bloomTex.SampleLevel(hdrSampler, uv, 0.0).rgb * post.size.z; }
    c.rgb *= post.size.w;
    float3 mapped = neutralShoulder(max(c.rgb * post.tone.x, float3(0.0, 0.0, 0.0)));
    float luma = dot(mapped, float3(0.2126, 0.7152, 0.0722));
    mapped = lerp(float3(luma, luma, luma), mapped, post.tone.z);
    mapped = saturate(0.18 + (mapped - 0.18) * post.tone.w);
    mapped = pow(mapped, float3(2.2 / max(post.tone.y, 1e-4), 2.2 / max(post.tone.y, 1e-4), 2.2 / max(post.tone.y, 1e-4)));
    return float4(linearToSrgb(mapped), c.a);
}

[shader("fragment")]
float4 fsTonemap(PostOut i) : SV_Target
{
    return tonemap(hdr.Load(int3(int2(i.pos.xy), 0)), i.pos.xy / post.size.xy);
}

[shader("fragment")]
float4 fsTonemapScaled(PostOut i) : SV_Target
{
    float2 uv = i.pos.xy / post.size.xy;
    return tonemap(hdr.SampleLevel(hdrSampler, uv, 0.0), uv);
}
