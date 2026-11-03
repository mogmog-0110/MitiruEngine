// MSAA を解いた HDR (RGBA16F) を LDR (RGBA8) にする。DX12 の DX12Tonemap.hpp (中立の肩 + 彩度・コントラスト + sRGB) と同じ式。
// 内部解像度が出力より小さいとき (画質の段の renderScale) は、fsTonemapScaled が引き伸ばす。方式は up.x (0 = 双線形、1 = Catmull-Rom)。
// bloom は DX12 の g_bloom と同じく、露出の前に HDR へ Strength 倍して足す。AO は次の区切り。

struct PostCB
{
    float4 tone;   // x = 露出 y = ガンマ z = 彩度 w = コントラスト
    float4 size;   // xy = 出力の大きさ (画素) z = bloom の強さ (0 は足さない) w = 中間の色の倍率の逆数
    float4 up;     // x = 拡大の方式
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

// 内部の画素の列を 5 回の双線形で Catmull-Rom に畳み込む (DX12 の TAAU の履歴の読みと同じ重み)。
// 負の重みの輪が出ないよう、結果は最も近い 2x2 の最小と最大へ収める。
float3 upscaleCatmullRom(float2 uv)
{
    uint2 dim;
    hdr.GetDimensions(dim.x, dim.y);
    float2 size = float2(dim);
    float2 texel = 1.0 / size;
    float2 samplePos = uv * size;
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - tc1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc0 = (tc1 - 1.0) * texel;
    float2 tc3 = (tc1 + 2.0) * texel;
    float2 tc12 = (tc1 + w2 / w12) * texel;
    float3 r = hdr.SampleLevel(hdrSampler, float2(tc12.x, tc0.y), 0.0).rgb * (w12.x * w0.y)
             + hdr.SampleLevel(hdrSampler, float2(tc0.x, tc12.y), 0.0).rgb * (w0.x * w12.y)
             + hdr.SampleLevel(hdrSampler, tc12, 0.0).rgb * (w12.x * w12.y)
             + hdr.SampleLevel(hdrSampler, float2(tc3.x, tc12.y), 0.0).rgb * (w3.x * w12.y)
             + hdr.SampleLevel(hdrSampler, float2(tc12.x, tc3.y), 0.0).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    r = max(r / wsum, float3(0.0, 0.0, 0.0));
    int2 hi = int2(dim) - 1;
    int2 p = clamp(int2(tc1 - 0.5), int2(0, 0), hi);
    int2 q = clamp(p + int2(1, 1), int2(0, 0), hi);
    float3 a = hdr.Load(int3(p, 0)).rgb;
    float3 b = hdr.Load(int3(int2(q.x, p.y), 0)).rgb;
    float3 c = hdr.Load(int3(int2(p.x, q.y), 0)).rgb;
    float3 d = hdr.Load(int3(q, 0)).rgb;
    return clamp(r, min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
}

float4 upscaledLdr(float2 uv)
{
    float4 c = float4(upscaleCatmullRom(uv), hdr.SampleLevel(hdrSampler, uv, 0.0).a);
    return tonemap(c, uv);
}

[shader("fragment")]
float4 fsTonemapScaled(PostOut i) : SV_Target
{
    float2 uv = i.pos.xy / post.size.xy;
    if (post.up.x < 0.5) { return tonemap(hdr.SampleLevel(hdrSampler, uv, 0.0), uv); }
    return upscaledLdr(uv);
}
