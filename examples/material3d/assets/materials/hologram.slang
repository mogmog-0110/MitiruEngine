// hologram — 走査線の隙間を抜き、縁ほど明るい水色で光らせる (照明を掛けない)
void material(SurfaceIn i, inout SurfaceOut o)
{
    float scan = frac(i.pixel.y * 0.25 - i.time * param_speed);
    if (scan < 0.4) { o.discardPixel = true; }
    float rim = pow(1.0 - saturate(dot(i.normal, i.view)), 2.0);
    o.baseColor = float3(0.1, 0.8, 1.0) * (0.3 + rim);
    o.emissive = float3(0.0, 0.25, 0.4) * param_glow;
}
