// water — 3 つの波の傾きで法線を揺らし、波の高さで深い青と浅い青を混ぜる (面は上向きの前提)
void material(SurfaceIn i, inout SurfaceOut o)
{
    float2 p = i.worldPos.xz * param_scale;
    float t = i.time * param_speed;
    float a = p.x * 3.0 + t;
    float b = p.y * 4.3 - t * 1.3;
    float c = (p.x + p.y) * 2.1 + t * 0.7;
    float height = (sin(a) + sin(b) + sin(c)) * 0.33;
    float2 slope = float2(cos(a) * 3.0 + cos(c) * 2.1, cos(b) * 4.3 + cos(c) * 2.1) * 0.12 * param_wave;
    o.normal = normalize(float3(-slope.x, 1.0, -slope.y));
    o.baseColor = lerp(float3(0.03, 0.18, 0.45), float3(0.25, 0.75, 0.85), saturate(height * 0.5 + 0.5));
}
