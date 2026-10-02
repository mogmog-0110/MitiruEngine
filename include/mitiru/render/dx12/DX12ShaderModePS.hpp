#pragma once

/// @file DX12ShaderModePS.hpp
/// @brief DX12 メインパス用の ShaderMode3D 別ピクセルシェーダー（MRT 出力）
/// @details DX12 のメインパスは MRT (RT0=color + RT1=worldNormal) なので、
///          DX11 用の `DefaultShaders3D.hpp` の単一 SV_TARGET PS をそのまま使えず、
///          バリアントごとに MRT 対応版を用意する。
///
///          全モード共通の頂点シェーダー: `TOON_VS_3D` (出力 VSOutput が同形)。
///          ライティングは `CbLighting (b1)` から material + cameraPos + 単一光源を読む。
///          マルチライト経路は `DX12MultiLightShaders.hpp` (b2=CbLightArray) に分離。

namespace mitiru::render
{

/// @brief MRT 用 Toon PS。量子化 Lambert + リム + albedo テクスチャ
///        DX11 の TOON_PS_3D は単一 SV_TARGET。DX12 メインパスは MRT (color+normal)
///        + t0 albedo を扱うため、ここで DX12 専用の変種を用意する。
inline constexpr const char* DX12_TOON_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 ShadowTint;   // 影部でアルベドに掛ける係数
    float4 FogColor;
    float4 FogParams;    // x=開始距離 y=完全に染まる距離 z=有効
    float4 MaterialParams;
    float4 ToonParams;   // x=段数 y=境の幅 z=ハイライト強さ w=ハイライト指数
    float4 ToonMidTint;
    float4 AmbientSky;    // xyz=上からの環境光 (半球を使わない時は AmbientColor と同値)
    float4 AmbientGround; // xyz=下からの環境光
    float4 RimParams;     // xyz=縁光の色 × 強さ w=1-NdotV の指数
    float4 ToonMaterial;  // xyz=材質のハイライト色 w=ToonParams.w に掛ける指数の係数
};

Texture2D                g_albedo : register(t0);
Texture2D                g_shadow : register(t1);
Texture2D                g_shadowFar : register(t2);
SamplerState             g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}

// 段の境の半幅。softness (lambert 単位) が画面上で 1 画素より細くなる所 (斜めに寝た面、
// 分割の粗いモデルの折れた法線、影の縁) では境が sub-pixel になって点々に砕けるので、
// その画素での lambert の傾き (fwidth) まで広げて 1 画素ぶんの階調を必ず作る。
// 上限 0.25 は、傾きが暴れる所で段そのものが溶けて平坦にならないための歯止め。
float toonEdgeHalfWidth(float half_, float grad)
{
    return max(half_, min(grad, 0.25));
}

// bands=0 は段を作らず、softness を巻き込み量にした wrap lambert をそのまま返す。
// 真横から下を 0 で切ると陰が急に潰れるので、-softness まで伸ばしてから 0..1 に写す
float toonWrap(float lambert)
{
    float wrap = max(ToonParams.y, 0.001);
    return saturate((lambert + wrap) / (1.0 + wrap));
}

// lambert を等分のしきい値 k/bands で刻んで 0..1 の段位置にする。bands=2 なら
// しきい値 0.5 の 1 段 = 従来の smoothstep(0.44, 0.56) と同じ (softness 0.12)。
float toonRamp(float lambert)
{
    if (ToonParams.x < 0.5) { return toonWrap(lambert); }
    float bands = clamp(ToonParams.x, 2.0, 4.0);
    float half_ = toonEdgeHalfWidth(ToonParams.y * 0.5, fwidth(lambert));
    float level = 0.0;
    [unroll]
    for (int k = 1; k < 4; ++k)
    {
        float th = k / bands;
        level += (k < bands) ? smoothstep(th - half_, th + half_, lambert) : 0.0;
    }
    return level / (bands - 1.0);
}

// 2 トーンは影→白の直線 (従来と同じ絵)。3 段以上は中間色を経由して影→中→白
float3 toonTone(float t)
{
    if (ToonParams.x < 2.5) { return lerp(ShadowTint, float3(1.0, 1.0, 1.0), t); }
    return (t < 0.5) ? lerp(ShadowTint, ToonMidTint.rgb, t * 2.0)
                     : lerp(ToonMidTint.rgb, float3(1.0, 1.0, 1.0), t * 2.0 - 1.0);
}

SamplerComparisonState   g_pcf    : register(s1);

// B13 カスケードシャドウ。LightViewProj (カスケード0) は VS が読んで LightSpacePos を
// 計算済みなのでここでは未使用、LightViewProjFar (カスケード1) は WorldPos から
// このシェーダー自身が light-space 座標を組む。
cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float    ShadowSoftness;   // PCF の端のタップまでの距離 (texel)。1 = 従来 (pcfFilter)
    float    ShadowBiasNdc;    // 0 = 従来の余白。正なら影の比較の余白 (影マップ深度、shadowBiasFor が傾きで伸ばす)
    float4x4 LightViewProjFar2;
};

struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

// 影の比較の余白。タップを広げた分と、受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan) だけ伸ばす。
// ShadowBiasNdc 0 は従来の式 (傾きを見ない固定の余白) のまま
float shadowBiasFor(float3 N, float3 L)
{
    if (ShadowBiasNdc <= 0.0) { return 0.001 * max(ShadowSoftness, 1.0); }
    float c = max(saturate(dot(N, L)), 0.1);
    float t = min(sqrt(1.0 - c * c) / c, 6.0);
    return ShadowBiasNdc * (1.0 + max(ShadowSoftness, 1.0) * t);
}

// softness 1 までは 3x3 を softness texel おき (従来の絵)。それより広げると、双線形の比較 (幅 1 texel) の
// 間に隙間が空いて影の縁が間隔ごとの 3 段に分かれる。広いときは同じ幅を 5x5 のテント重みで softness / 2 おきに埋める。
// u は [uMin, uMax] に収める (アトラスの列からはみ出さないため。単一のマップは広い範囲を渡して何もしない)
float pcfFilter(Texture2D shadowTex, float2 uv, float depthRef, float2 texel, float uMin, float uMax)
{
    float shadow = 0.0;
    if (ShadowSoftness <= 1.0)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 tap = float2(clamp(uv.x + x * texel.x, uMin, uMax), uv.y + y * texel.y);
                shadow += shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
            }
        }
        return shadow / 9.0;
    }
    const float2 stepUv = texel * 0.5;
    [unroll]
    for (int ty = -2; ty <= 2; ++ty)
    {
        [unroll]
        for (int tx = -2; tx <= 2; ++tx)
        {
            float2 tap = float2(clamp(uv.x + tx * stepUv.x, uMin, uMax), uv.y + ty * stepUv.y);
            shadow += (3.0 - abs(tx)) * (3.0 - abs(ty)) * shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return shadow / 81.0;
}

float samplePCFTex(Texture2D shadowTex, float3 ndc, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    // 光の錐台の外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelSize = ShadowSoftness / 1024.0;
    return pcfFilter(shadowTex, uv, depthRef, float2(texelSize, texelSize), -1.0e9, 1.0e9);
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。u を列の中へ写し、PCF の
// タップが隣の列へはみ出さないよう列の内側に収める。
float samplePCFAtlas(float3 ndc, float column, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelV = ShadowSoftness / 1024.0;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    return pcfFilter(g_shadowFar, float2((uv.x + column) * 0.5, uv.y), depthRef, float2(texelU, texelV), uMin, uMax);
}

// B13: カメラ距離が CascadeSplitDistance 未満ならカスケード0 (VS 計算済みの
// LightSpacePos)、それ以上ならカスケード1 (WorldPos から自前で計算) を使う。
// カスケード無効時は CPU 側が CascadeSplitDistance に非常に大きい値を入れるため
// 常にカスケード0 を通る (従来と同じ結果)。
float sampleCascadedShadow(float3 worldPos, float4 lightSpacePos0, float distanceFromCamera, float bias)
{
    if (distanceFromCamera < CascadeSplitDistance)
    {
        float3 ndc = lightSpacePos0.xyz / max(lightSpacePos0.w, 1e-4);
        return samplePCFTex(g_shadow, ndc, bias);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        float4 lsFar = mul(LightViewProjFar, float4(worldPos, 1.0));
        return samplePCFAtlas(lsFar.xyz / max(lsFar.w, 1e-4), 0.0, bias);
    }
    float4 lsFar2 = mul(LightViewProjFar2, float4(worldPos, 1.0));
    return samplePCFAtlas(lsFar2.xyz / max(lsFar2.w, 1e-4), 1.0, bias);
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    float distToCamera = length(CameraPos - input.WorldPos);
    float castShadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera, shadowBiasFor(N, L));

    float lambert = saturate(dot(N, L)) * castShadow;
    float3 tone = toonTone(toonRamp(lambert));
    // 半球アンビエント。上を向いた面に空、下を向いた面に地面の照り返し。半球を切っていれば
    // CPU が sky/ground に同じ色を入れてくるので、この lerp は平坦な 1 色と完全に一致する
    float3 ambient = lerp(AmbientGround.rgb, AmbientSky.rgb, N.y * 0.5 + 0.5);
    float3 color = albedo * tone * LightColor + ambient * albedo * 0.30;

    // 段付きハイライト。影 (lambert 0) では出さない。境の幅は段と共有
    if (ToonParams.z > 0.0)
    {
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), max(ToonParams.w * ToonMaterial.w, 1.0));
        float half_ = toonEdgeHalfWidth(ToonParams.y * 0.5, fwidth(spec));
        float lit = smoothstep(0.5 - half_, 0.5 + half_, spec) * step(0.001, lambert);
        color += LightColor * ToonMaterial.rgb * ToonParams.z * lit;
    }

    // 縁光。強さは RimParams.rgb に畳んであるので、無効時は黒が足されるだけで分岐が要らない
    color += RimParams.rgb * pow(1.0 - saturate(dot(N, V)), RimParams.w);

    if (FogParams.z > 0.5)
    {
        float dist = length(CameraPos - input.WorldPos);
        float f = saturate((dist - FogParams.x) / max(FogParams.y - FogParams.x, 0.001));
        color = lerp(color, FogColor.rgb, f);
    }

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(color, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    // outlineCaster=false の印。単位法線の *0.5+0.5 は 3 成分そろって 1 にならないので、
    // outline パスはこの値で除外画素を見分ける (ToonShaders3D.hpp の outlineExcluded)
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Fresnel Toon PS。OutlineMode::Fresnel でメイン PS を差し替える
///        DX12_TOON_PS_3D にシルエット付近 (NdotV 小) の暗化を加えた変種。
///        DX11 世代 TOON_PS_3D_FRESNEL は LightSpacePos 無しで VS-PS linkage が
///        不成立だったため、DX12 VS の出力 signature に合わせてここへ移植。
inline constexpr const char* DX12_TOON_PS_3D_FRESNEL = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


// LightSpacePos は VS が出力するため PSInput でも宣言してレジスタ整合を取る。
struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // アンビエント
    float3 ambient = AmbientColor * albedo;

    // ディフューズ。NdotL を 3 段階に量子化（toon 帯）
    float rawNdotL = saturate(dot(N, L));
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * albedo * toon;

    // ハイライト
    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specFactor = pow(NdotH, max(MaterialShininess, 1.0)) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    // Fresnel リム。シルエット付近 (NdotV 小) を暗化してアウトラインに
    float NdotV = saturate(dot(N, V));
    float fresnelEdge = 1.0 - smoothstep(0.0, 0.4, NdotV);
    float3 outlineColor = float3(0.08, 0.06, 0.04);

    float3 finalColor = ambient + diffuse + specular;
    finalColor = lerp(finalColor, outlineColor, fresnelEdge * 0.95);
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color = float4(finalColor, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Phong PS。単一光源 Lambert + Phong + albedo テクスチャ + shadow PCF
inline constexpr const char* DX12_PHONG_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

Texture2D                g_albedo  : register(t0);
Texture2D                g_shadow  : register(t1);
Texture2D                g_shadowFar : register(t2);
SamplerState             g_samp    : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}

SamplerComparisonState   g_pcf     : register(s1);

// B13 カスケードシャドウ。DX12_TOON_PS_3D と同じ規約 (LightViewProj はここでは未使用、
// LightViewProjFar + CascadeSplitDistance だけカスケード1 の判定/サンプルに使う)。
cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float    ShadowSoftness;   // PCF の端のタップまでの距離 (texel)。1 = 従来 (pcfFilter)
    float    ShadowBiasNdc;    // 0 = 従来の余白。正なら影の比較の余白 (影マップ深度、shadowBiasFor が傾きで伸ばす)
    float4x4 LightViewProjFar2;
};

struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

// 影の比較の余白。タップを広げた分と、受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan) だけ伸ばす。
// ShadowBiasNdc 0 は従来の式 (傾きを見ない固定の余白) のまま
float shadowBiasFor(float3 N, float3 L)
{
    if (ShadowBiasNdc <= 0.0) { return 0.001 * max(ShadowSoftness, 1.0); }
    float c = max(saturate(dot(N, L)), 0.1);
    float t = min(sqrt(1.0 - c * c) / c, 6.0);
    return ShadowBiasNdc * (1.0 + max(ShadowSoftness, 1.0) * t);
}

// DX12_TOON_PS_3D の pcfFilter と同じ
float pcfFilter(Texture2D shadowTex, float2 uv, float depthRef, float2 texel, float uMin, float uMax)
{
    float shadow = 0.0;
    if (ShadowSoftness <= 1.0)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 tap = float2(clamp(uv.x + x * texel.x, uMin, uMax), uv.y + y * texel.y);
                shadow += shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
            }
        }
        return shadow / 9.0;
    }
    const float2 stepUv = texel * 0.5;
    [unroll]
    for (int ty = -2; ty <= 2; ++ty)
    {
        [unroll]
        for (int tx = -2; tx <= 2; ++tx)
        {
            float2 tap = float2(clamp(uv.x + tx * stepUv.x, uMin, uMax), uv.y + ty * stepUv.y);
            shadow += (3.0 - abs(tx)) * (3.0 - abs(ty)) * shadowTex.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return shadow / 81.0;
}

float samplePCFTex(Texture2D shadowTex, float3 ndc, float bias)
{
    // ndc: [-1,1] xy → UV [0,1], z → DX [0,1] そのまま
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    // light frustum 外は影なし。奥行きも見る (遠方クリップ面の外は影マップに何も無い)
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelSize = ShadowSoftness / 1024.0;  // map size に整合させること
    return pcfFilter(shadowTex, uv, depthRef, float2(texelSize, texelSize), -1.0e9, 1.0e9);
}

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。u を列の中へ写し、PCF の
// タップが隣の列へはみ出さないよう列の内側に収める。
float samplePCFAtlas(float3 ndc, float column, float bias)
{
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float depthRef = ndc.z - bias;
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    const float texelV = ShadowSoftness / 1024.0;
    const float texelU = texelV * 0.5;
    const float uMin = column * 0.5 + texelU * 0.5;
    const float uMax = (column + 1.0) * 0.5 - texelU * 0.5;
    return pcfFilter(g_shadowFar, float2((uv.x + column) * 0.5, uv.y), depthRef, float2(texelU, texelV), uMin, uMax);
}

// B13: DX12_TOON_PS_3D の sampleCascadedShadow と同じ判定 (距離 < CascadeSplitDistance
// でカスケード0、それ以外はカスケード1)。カスケード無効時は CPU が分割距離を
// 非常に大きくするため常にカスケード0 (従来と同じ結果)。
float sampleCascadedShadow(float3 worldPos, float4 lightSpacePos0, float distanceFromCamera, float bias)
{
    if (distanceFromCamera < CascadeSplitDistance)
    {
        float3 ndc = lightSpacePos0.xyz / max(lightSpacePos0.w, 1e-4);
        return samplePCFTex(g_shadow, ndc, bias);
    }
    if (distanceFromCamera < CascadeSplitDistance2)
    {
        float4 lsFar = mul(LightViewProjFar, float4(worldPos, 1.0));
        return samplePCFAtlas(lsFar.xyz / max(lsFar.w, 1e-4), 0.0, bias);
    }
    float4 lsFar2 = mul(LightViewProjFar2, float4(worldPos, 1.0));
    return samplePCFAtlas(lsFar2.xyz / max(lsFar2.w, 1e-4), 1.0, bias);
}

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    // shadow factor (1.0 = 影なし)
    float distToCamera = length(CameraPos - input.WorldPos);
    float shadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera, shadowBiasFor(N, L));

    float3 ambient = AmbientColor * albedo;

    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL * shadow;

    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float specPow = max(MaterialShininess, 1.0);
    float specFactor = pow(NdotH, specPow) * NdotL * shadow;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    float3 lit = ambient + diffuse + specular;
    if (FogParams.z > 0.5)
    {
        float dist = length(CameraPos - input.WorldPos);
        float f = saturate((dist - FogParams.x) / max(FogParams.y - FogParams.x, 0.001));
        lit = lerp(lit, FogColor.rgb, f);
    }

    PSOutput o;
    o.Color  = float4(lit, alpha);
    float NdotV = saturate(dot(N, V));
    o.Normal = float4(N * 0.5 + 0.5, NdotV);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Unlit PS。頂点色 × material diffuse × albedo テクスチャ
inline constexpr const char* DX12_UNLIT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 _unusedLightDir;    float _pad0;
    float3 _unusedLightColor;  float _pad1;
    float3 _unusedAmbient;     float _pad2;
    float3 _unusedCameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 _unusedSpecular;
    float  _unusedShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


struct PSInput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(albedo, alpha);
    o.Normal = float4(N * 0.5 + 0.5, 1.0);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

/// @brief MRT 用 Flat PS。面ごと一様陰影 + albedo テクスチャ
inline constexpr const char* DX12_FLAT_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 _pad4;
    float4 FogColor;
    float4 FogParams;
    float4 MaterialParams;
};

Texture2D    g_albedo : register(t0);
SamplerState g_samp   : register(s0);
SamplerState             g_sampPoint : register(s2);

// glTF のマテリアル指定を反映してアルベドを拾う。
// MaterialParams: x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}


struct PSInput
{
    float4 Position                  : SV_POSITION;
    float3 WorldPos                  : TEXCOORD0;
    nointerpolation float3 WorldNorm : TEXCOORD1;
    float2 TexCoord                  : TEXCOORD2;
    float4 LightSpacePos             : TEXCOORD3;
    float4 Color                     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;

    float3 ambient = AmbientColor * albedo;
    float NdotL = saturate(dot(N, L));
    float3 diffuse = LightColor * albedo * NdotL;
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    PSOutput o;
    o.Color  = float4(ambient + diffuse, alpha);
    o.Normal = float4(N * 0.5 + 0.5, NdotL);
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}
)hlsl";

} // namespace mitiru::render
