// トゥーンの段と焼いた間接光 (forward.slang から include される。束縛と FrameCB は forward.slang が先に宣言する)

// ── トゥーンの段 (DX12ToonShaders.hpp / DX12LitShaders.hpp) ──

float toonBandLevel(float lambert, float4 p, float4 th, float grad)
{
    float h = max(p.y, min(grad, 0.25));
    float level = 0.0;
    [unroll]
    for (int k = 0; k < 3; ++k)
    {
        level += (float(k + 1) < p.x) ? smoothstep(th[k] - h, th[k] + h, lambert) : 0.0;
    }
    return level;
}

float toonShadowedLevel(float level, float lit, float4 p)
{
    float down = (p.z > 0.5) ? max(level - p.z, 0.0) : 0.0;
    return lerp(down, level, lit);
}

float3 toonBandTint(float level, float4 t0, float4 t1, float4 t2, float4 t3)
{
    return t0.rgb + saturate(level) * (t1.rgb - t0.rgb) + saturate(level - 1.0) * (t2.rgb - t1.rgb)
         + saturate(level - 2.0) * (t3.rgb - t2.rgb);
}

float toonEdgeHalfWidth(float half_, float grad)
{
    return max(half_, min(grad, 0.25));
}

float toonWrap(float lambert)
{
    float wrap = max(frame.toonParams.y, 0.001);
    return saturate((lambert + wrap) / (1.0 + wrap));
}

float toonRamp(float lambert)
{
    if (frame.toonParams.x < 0.5) { return toonWrap(lambert); }
    float bands = clamp(frame.toonParams.x, 2.0, 4.0);
    float half_ = toonEdgeHalfWidth(frame.toonParams.y * 0.5, fwidth(lambert));
    float level = 0.0;
    [unroll]
    for (int k = 1; k < 4; ++k)
    {
        float th = float(k) / bands;
        level += (float(k) < bands) ? smoothstep(th - half_, th + half_, lambert) : 0.0;
    }
    return level / (bands - 1.0);
}

float3 toonTone(float t)
{
    float3 white = float3(1.0, 1.0, 1.0);
    if (frame.toonParams.x < 2.5) { return lerp(frame.shadowTint.rgb, white, t); }
    return (t < 0.5) ? lerp(frame.shadowTint.rgb, frame.toonMidTint.rgb, t * 2.0)
                     : lerp(frame.toonMidTint.rgb, white, t * 2.0 - 1.0);
}

// ── 焼いた間接光 (DX12 の DX12LightingProbeShaders.hpp の拡散の部分。画像は textureLoad で読む) ──

static const float kPi = 3.14159265358979;

float4 giProbeTexel(uint probe, uint k)
{
    return giProbes.Load(int3(int((probe & 255u) * 7u + k), int(probe >> 8u), 0));
}

float3 giProbeIrradiance(uint probe, float3 n)
{
    float4 a0 = giProbeTexel(probe, 0u);
    float4 a1 = giProbeTexel(probe, 1u);
    float4 a2 = giProbeTexel(probe, 2u);
    float4 a3 = giProbeTexel(probe, 3u);
    float4 a4 = giProbeTexel(probe, 4u);
    float4 a5 = giProbeTexel(probe, 5u);
    float4 a6 = giProbeTexel(probe, 6u);
    float3 e = a0.xyz * 0.282095
             + float3(a0.w, a1.xy) * (0.488603 * n.y) + float3(a1.zw, a2.x) * (0.488603 * n.z) + a2.yzw * (0.488603 * n.x)
             + a3.xyz * (1.092548 * n.x * n.y) + float3(a3.w, a4.xy) * (1.092548 * n.y * n.z)
             + float3(a4.zw, a5.x) * (0.315392 * (3.0 * n.z * n.z - 1.0)) + a5.yzw * (1.092548 * n.x * n.z)
             + a6.xyz * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, float3(0.0, 0.0, 0.0));
}

float2 giOctEncode(float3 n)
{
    float2 uv = float2(n.x, n.z) / (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.y < 0.0)
    {
        float2 sgn = float2(uv.x >= 0.0 ? 1.0 : -1.0, uv.y >= 0.0 ? 1.0 : -1.0);
        uv = (float2(1.0, 1.0) - abs(uv.yx)) * sgn;
    }
    return uv;
}

float2 giVisibilityAt(uint probe, float3 dir)
{
    uint tiles = uint(frame.giDims.w);
    float2 tile = float2(float(probe % tiles), float(probe / tiles)) * 18.0;
    float2 px = tile + 1.0 + (giOctEncode(dir) * 0.5 + 0.5) * 16.0;
    return giVisibility.SampleLevel(giSampler, px * frame.giParams.zw, 0.0);
}

// 周りの 8 個のプローブを三線形で混ぜ、壁の向こうのプローブはチェビシェフの不等式で落とす。混ぜられなければ ok = false
float3 giIrradiance(float3 P, float3 N, float3 V, out bool ok)
{
    float3 biased = P + (N * 0.2 + V * 0.8) * frame.giParams.x;
    int3 dims = int3(frame.giDims.xyz);
    float3 c = clamp((biased - frame.giOrigin.xyz) / frame.giSpacing.xyz, float3(0.0, 0.0, 0.0), float3(dims - int3(1, 1, 1)));
    float3 inside = frame.giOrigin.xyz + c * frame.giSpacing.xyz;
    int3 base = min(int3(floor(c)), max(dims - int3(2, 2, 2), int3(0, 0, 0)));
    float3 f = saturate(c - float3(base));
    float3 sum = float3(0.0, 0.0, 0.0);
    float wsum = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        int3 off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        int3 cc = min(base + off, dims - int3(1, 1, 1));
        uint idx = uint(cc.x + dims.x * (cc.y + dims.y * cc.z));
        if (giProbeTexel(idx, 6u).w < 0.5) { continue; }
        float3 probeP = frame.giOrigin.xyz + float3(cc) * frame.giSpacing.xyz;
        float3 tri = lerp(float3(1.0, 1.0, 1.0) - f, f, float3(off));
        float wrap = (dot(normalize(probeP - P + float3(1e-6, 1e-6, 1e-6)), N) + 1.0) * 0.5;
        float w = wrap * wrap + 0.2;
        float3 toPoint = inside - probeP;
        float r = length(toPoint);
        float2 m = giVisibilityAt(idx, r > 1e-4 ? toPoint / r : float3(0.0, 1.0, 0.0));
        if (r > m.x)
        {
            float var = abs(m.y - m.x * m.x);
            float d = r - m.x;
            float ch = var / (var + d * d);
            w *= ch * ch * ch;
        }
        if (w < 0.2) { w *= w * w * 25.0; }
        w *= tri.x * tri.y * tri.z;
        sum += giProbeIrradiance(idx, N) * w;
        wsum += w;
    }
    ok = wsum > 1e-6;
    return ok ? sum / wsum : float3(0.0, 0.0, 0.0);
}

// トゥーン: 間接光の明るさを段に刻む。刻まない時 (段が 1 以下か暗すぎる) は負を返す
float giToonCoord(float3 e)
{
    float bands = frame.giParams.y;
    float l = max(max(e.r, e.g), e.b);
    return (bands < 1.5 || l <= 1e-5) ? -1.0 : l / (l + 1.0) * bands;
}

float3 giToonBandAt(float3 e, float x, float w)
{
    if (x < 0.0) { return e; }
    float bands = frame.giParams.y;
    float l = max(max(e.r, e.g), e.b);
    w = max(w, 1e-4);
    float q = (floor(x) + smoothstep(1.0 - w, 1.0, frac(x)) + 0.5) / bands;
    float lq = q / max(1.0 - q, 1e-3);
    return e * (lq / l);
}

float3 hemisphereAmbient(float3 N)
{
    return lerp(frame.ambientGround.rgb, frame.ambientSky.rgb, N.y * 0.5 + 0.5);
}

float toonMetalSteps(float t, float bands, float aa)
{
    if (bands < 0.5) { return saturate(t); }
    bands = clamp(round(bands), 1.0, 8.0);
    float width = max(aa, 0.002);
    float level = 0.0;
    [unroll]
    for (int k = 1; k < 8; ++k)
    {
        float th = float(k) / bands;
        level += (float(k) < bands) ? smoothstep(th - width, th + width, t) : 0.0;
    }
    return level / max(bands - 1.0, 1.0);
}

float3 toonMetalEnv(float3 N, float3 V)
{
    float3 R = reflect(-V, N);
    float h = draw.metalB.x;
    float w = max(draw.metalB.y, fwidth(R.y));
    float above = smoothstep(h - w, h + w, R.y);
    float skyT = saturate((R.y - h) / max(1.0 - h, 1e-3));
    float groundT = saturate((h - R.y) / max(1.0 + h, 1e-3));
    float3 skySide = lerp(draw.metalHorizon.rgb, draw.metalSky.rgb, toonMetalSteps(skyT, draw.metalA.w, fwidth(skyT)));
    float3 groundSide = lerp(draw.metalGround.rgb, draw.metalHorizon.rgb, toonMetalSteps(groundT, draw.metalA.w, fwidth(groundT)));
    return lerp(groundSide, skySide, above);
}

float3 styledToonAlbedo(float3 albedo, float3 N, float3 V, float3 L, float lambert, float castShadow, out float3 unlitReflection)
{
    float level = toonBandLevel(lambert, draw.rampParams, draw.rampThresh, fwidth(lambert));
    level = toonShadowedLevel(level, castShadow, draw.rampParams);
    float3 bandTint = toonBandTint(level, draw.tint0, draw.tint1, draw.tint2, draw.tint3);
    float3 c = albedo * bandTint;
    unlitReflection = float3(0.0, 0.0, 0.0);
    if (draw.metalA.x > 0.0)
    {
        float3 refl = toonMetalEnv(N, V) * lerp(float3(1.0, 1.0, 1.0), albedo, draw.metalA.y)
                    * lerp(float3(1.0, 1.0, 1.0), bandTint, draw.metalA.z);
        unlitReflection = refl * draw.metalA.x;
        c = lerp(c, refl, draw.metalA.x);
    }
    if (draw.accRim.w > 0.0)
    {
        float ndv = saturate(dot(N, V));
        float hv = max(max(fwidth(ndv), 1e-4), draw.accRimLimit.y);
        float lit = lambert * castShadow;
        float hl = max(fwidth(lit), 1e-4);
        float side = (draw.accRimLimit.x >= 0.999) ? 1.0 : 1.0 - smoothstep(draw.accRimLimit.x - hl, draw.accRimLimit.x + hl, lit);
        float rim = (1.0 - smoothstep(draw.accRim.w - hv, draw.accRim.w + hv, ndv)) * side;
        c = lerp(c, draw.accRim.rgb, rim);
        unlitReflection *= 1.0 - rim;
    }
    if (draw.accGloss.w > 0.0)
    {
        float nh = saturate(dot(N, normalize(L + V)));
        float hw = max(max(fwidth(nh), 1e-4), draw.accRimLimit.z);
        float g = smoothstep(draw.accGloss.w - hw, draw.accGloss.w + hw, nh) * step(0.5, castShadow) * step(0.001, lambert);
        float gMix = g * min(draw.accRimLimit.w, 1.0);
        float gAdd = g * max(draw.accRimLimit.w - 1.0, 0.0);
        c = lerp(c, draw.accGloss.rgb, gMix) + draw.accGloss.rgb * gAdd;
        unlitReflection *= 1.0 - gMix;
    }
    return c;
}

float3 applyFog(float3 color, float3 worldPos)
{
    if (frame.fogParams.z < 0.5) { return color; }
    float dist = length(frame.cameraPos.xyz - worldPos);
    float f = saturate((dist - frame.fogParams.x) / max(frame.fogParams.y - frame.fogParams.x, 0.001));
    return lerp(color, frame.fogColor.rgb, f);
}
