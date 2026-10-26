// melt — 値のノイズがしきい値より小さい画素を抜き、抜けた縁を橙に光らせる (溶けて消える物)
float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(hash(i), hash(i + float3(1, 0, 0)), f.x);
    float b = lerp(hash(i + float3(0, 1, 0)), hash(i + float3(1, 1, 0)), f.x);
    float c = lerp(hash(i + float3(0, 0, 1)), hash(i + float3(1, 0, 1)), f.x);
    float d = lerp(hash(i + float3(0, 1, 1)), hash(i + float3(1, 1, 1)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}

void material(SurfaceIn i, inout SurfaceOut o)
{
    float edge = valueNoise(i.worldPos * 3.0) - param_amount;
    if (edge < 0.0) { o.discardPixel = true; }
    float rim = 1.0 - smoothstep(0.0, param_width, edge);
    o.emissive = float3(1.0, 0.45, 0.1) * rim * param_glow;
}
