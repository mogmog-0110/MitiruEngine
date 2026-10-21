// FXAA 3.11。DX12 の DX12FXAAShaders.hpp と同じ式。トーンマップと輪郭線の後の最終色 (LDR) を、写した絵から読んで書き直す。

struct FxaaCB
{
    float4 params;   // xy = 1 / 画面の大きさ z = subpixQuality w = edgeThreshold
    float4 params2;  // x = edgeThresholdMin
};

[[vk::binding(0, 0)]] ConstantBuffer<FxaaCB> fxaa;
[[vk::binding(1, 0)]] Texture2D<float4> sceneTexture;
[[vk::binding(2, 0)]] SamplerState linearSampler;

struct FxaaOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
FxaaOut vsFxaa(uint vid : SV_VertexID)
{
    FxaaOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}

float fxaaLuma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

float3 fxaaTexOff(float2 uv, float2 offset)
{
    return sceneTexture.SampleLevel(linearSampler, uv + offset * fxaa.params.xy, 0.0).rgb;
}

float3 fxaaTex(float2 uv)
{
    return sceneTexture.SampleLevel(linearSampler, uv, 0.0).rgb;
}

static const float kQuality[12] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0 };

[shader("fragment")]
float4 fsFxaa(FxaaOut input) : SV_Target
{
    float2 rcpFrame = fxaa.params.xy;
    float2 uv = input.uv;

    float3 rgbM = fxaaTex(uv);
    float lumaM = fxaaLuma(rgbM);
    float lumaN = fxaaLuma(fxaaTexOff(uv, float2(0.0, -1.0)));
    float lumaS = fxaaLuma(fxaaTexOff(uv, float2(0.0, 1.0)));
    float lumaE = fxaaLuma(fxaaTexOff(uv, float2(1.0, 0.0)));
    float lumaW = fxaaLuma(fxaaTexOff(uv, float2(-1.0, 0.0)));

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(fxaa.params2.x, lumaMax * fxaa.params.w))
    {
        return float4(rgbM, 1.0);
    }

    float lumaNW = fxaaLuma(fxaaTexOff(uv, float2(-1.0, -1.0)));
    float lumaNE = fxaaLuma(fxaaTexOff(uv, float2(1.0, -1.0)));
    float lumaSW = fxaaLuma(fxaaTexOff(uv, float2(-1.0, 1.0)));
    float lumaSE = fxaaLuma(fxaaTexOff(uv, float2(1.0, 1.0)));

    float lumaNS = lumaN + lumaS;
    float lumaEW = lumaE + lumaW;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;

    float edgeHorz = abs(lumaNWSW - 2.0 * lumaW) + abs(lumaNS - 2.0 * lumaM) * 2.0 + abs(lumaNESE - 2.0 * lumaE);
    float edgeVert = abs(lumaNWNE - 2.0 * lumaN) + abs(lumaEW - 2.0 * lumaM) * 2.0 + abs(lumaSWSE - 2.0 * lumaS);
    bool isHorizontal = (edgeHorz >= edgeVert);

    float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
    float luma1 = isHorizontal ? lumaN : lumaW;
    float luma2 = isHorizontal ? lumaS : lumaE;
    float gradient1 = luma1 - lumaM;
    float gradient2 = luma2 - lumaM;
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    if (!is1Steepest) { stepLength = -stepLength; }

    float lumaLocalAvg = is1Steepest ? 0.5 * (luma1 + lumaM) : 0.5 * (luma2 + lumaM);

    float2 currentUV = uv;
    if (isHorizontal) { currentUV.y += stepLength * 0.5; }
    else { currentUV.x += stepLength * 0.5; }

    float2 offset2 = isHorizontal ? float2(rcpFrame.x, 0.0) : float2(0.0, rcpFrame.y);
    float2 uv1 = currentUV - offset2;
    float2 uv2 = currentUV + offset2;

    float lumaEnd1 = fxaaLuma(fxaaTex(uv1)) - lumaLocalAvg;
    float lumaEnd2 = fxaaLuma(fxaaTex(uv2)) - lumaLocalAvg;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) { uv1 -= offset2; }
    if (!reached2) { uv2 += offset2; }

    for (int i = 0; i < 12; i++)
    {
        if (reachedBoth) { break; }
        if (!reached1) { lumaEnd1 = fxaaLuma(fxaaTex(uv1)) - lumaLocalAvg; }
        if (!reached2) { lumaEnd2 = fxaaLuma(fxaaTex(uv2)) - lumaLocalAvg; }
        reached1 = abs(lumaEnd1) >= gradientScaled;
        reached2 = abs(lumaEnd2) >= gradientScaled;
        reachedBoth = reached1 && reached2;
        if (!reached1) { uv1 -= offset2 * kQuality[i]; }
        if (!reached2) { uv2 += offset2 * kQuality[i]; }
    }

    float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool isDir1 = dist1 < dist2;
    float distFinal = min(dist1, dist2);
    float edgeLength = dist1 + dist2;
    float pixelOffset = -distFinal / max(edgeLength, 1e-5) + 0.5;

    bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
    bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    float lumaAvg = (1.0 / 12.0) * (2.0 * lumaNS + 2.0 * lumaEW + lumaNWSW + lumaNESE);
    float subpixOffset1 = saturate(abs(lumaAvg - lumaM) / max(lumaRange, 1e-5));
    float subpixOffset2 = (-2.0 * subpixOffset1 + 3.0) * subpixOffset1 * subpixOffset1;
    float subpixOffsetFinal = subpixOffset2 * subpixOffset2 * fxaa.params.z;

    finalOffset = max(finalOffset, subpixOffsetFinal);

    float2 finalUV = uv;
    if (isHorizontal) { finalUV.y += finalOffset * stepLength; }
    else { finalUV.x += finalOffset * stepLength; }
    return float4(fxaaTex(finalUV), 1.0);
}
