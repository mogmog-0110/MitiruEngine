// 太陽のカスケードの影 (forward.slang から include される)

// ── 太陽のカスケードの影 (DX12SunShadowShaders.hpp) ──

// 受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan)。真横に近い面で余白が際限なく伸びないよう 6 で止める
float shadowSlope(float3 N, float3 L)
{
    float c = max(saturate(dot(N, L)), 0.1);
    return min(sqrt(1.0 - c * c) / c, 6.0);
}

// カスケード (光の view * proj が vp) で比べる位置 ndc と余白 bias。既定 (shadowSplit.w が 0) は余白を影マップの
// 1 texel の世界の幅に比例させ、点を法線の向きへ 1 texel 浮かせる。cinfo は世界の texel の幅と光の深さの係数
void shadowLookup(float4x4 vp, float4 cinfo, float3 P, float3 N, float slope, out float3 ndc, out float bias)
{
    float soft = max(frame.shadowSplit.z, 1.0);
    float3 q = P;
    if (frame.shadowSplit.w > 0.0) { bias = frame.shadowSplit.w * (1.0 + soft * slope); }
    else
    {
        q = P + N * cinfo.x;
        bias = cinfo.y * cinfo.x * (0.5 + soft * slope);
    }
    float4 c = mul(vp, float4(q, 1.0));
    ndc = c.xyz / max(c.w, 1e-4);
}

// softness 1 までは 3x3 を softness texel おき。広いときは 5x5 のテント重みで softness / 2 おきに埋める
// (3x3 のまま広げると双線形の比較の間が空き、縁が 3 段に分かれる)
float pcfNear(float2 uv, float depthRef, float2 texel)
{
    float shadow = 0.0;
    if (frame.shadowSplit.z <= 1.0)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                shadow += shadowNear.SampleCmpLevelZero(shadowPcf, uv + float2(float(x), float(y)) * texel, depthRef);
            }
        }
        return shadow / 9.0;
    }
    float2 stepUv = texel * 0.5;
    [unroll]
    for (int ty = -2; ty <= 2; ++ty)
    {
        [unroll]
        for (int tx = -2; tx <= 2; ++tx)
        {
            shadow += (3.0 - abs(float(tx))) * (3.0 - abs(float(ty)))
                    * shadowNear.SampleCmpLevelZero(shadowPcf, uv + float2(float(tx), float(ty)) * stepUv, depthRef);
        }
    }
    return shadow / 81.0;
}

// アトラスの PCF。u は [uMin, uMax] に収めて、隣の列を引かない
float pcfAtlas(float2 uv, float depthRef, float2 texel, float uMin, float uMax)
{
    float shadow = 0.0;
    if (frame.shadowSplit.z <= 1.0)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 tap = float2(clamp(uv.x + float(x) * texel.x, uMin, uMax), uv.y + float(y) * texel.y);
                shadow += shadowFar.SampleCmpLevelZero(shadowPcf, tap, depthRef);
            }
        }
        return shadow / 9.0;
    }
    float2 stepUv = texel * 0.5;
    [unroll]
    for (int ty = -2; ty <= 2; ++ty)
    {
        [unroll]
        for (int tx = -2; tx <= 2; ++tx)
        {
            float2 tap = float2(clamp(uv.x + float(tx) * stepUv.x, uMin, uMax), uv.y + float(ty) * stepUv.y);
            shadow += (3.0 - abs(float(tx))) * (3.0 - abs(float(ty))) * shadowFar.SampleCmpLevelZero(shadowPcf, tap, depthRef);
        }
    }
    return shadow / 81.0;
}

float samplePcfNear(float3 ndc, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    // 光の錐台の外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) { return 1.0; }
    float t = frame.shadowSplit.z * frame.shadowTexel.x;
    return pcfNear(uv, depthRef, float2(t, t));
}

// アトラスは 2 列 (左 = カスケード 1、右 = カスケード 2)
float samplePcfAtlas(float3 ndc, float column, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) { return 1.0; }
    float texelV = frame.shadowSplit.z * frame.shadowTexel.y;
    float texelU = texelV * 0.5;
    float uMin = column * 0.5 + texelU * 0.5;
    float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    return pcfAtlas(float2((uv.x + column) * 0.5, uv.y), depthRef, float2(texelU, texelV), uMin, uMax);
}

// カメラ距離で使うカスケードを選ぶ。N は受ける面の向き、L は光へ向かう向き
float sampleCascadedShadow(float3 worldPos, float3 N, float3 L, float distanceFromCamera)
{
    if (frame.shadowTexel.z < 0.5) { return 1.0; }
    float slope = shadowSlope(N, L);
    float3 ndc;
    float bias;
    if (frame.shadowTexel.z < 1.5 || distanceFromCamera < frame.shadowSplit.x)
    {
        shadowLookup(frame.shadowVP0, frame.shadowC0, worldPos, N, slope, ndc, bias);
        return samplePcfNear(ndc, bias);
    }
    if (frame.shadowTexel.z < 2.5 || distanceFromCamera < frame.shadowSplit.y)
    {
        shadowLookup(frame.shadowVP1, frame.shadowC1, worldPos, N, slope, ndc, bias);
        return samplePcfAtlas(ndc, 0.0, bias);
    }
    shadowLookup(frame.shadowVP2, frame.shadowC2, worldPos, N, slope, ndc, bias);
    return samplePcfAtlas(ndc, 1.0, bias);
}
