#pragma once

/// @file DX12LitShaders.hpp
/// @brief DX12 メインパスの陰影 PS (Toon / Phong / PBR と半透明の OIT)。材質のマップと局所光を共有する
/// @details PS は「共通部 + デカール + 局所光 1 灯の陰影 + 局所光の走査 + 本体」を連結して作る (`dx12LitPixelShader`)。
///          入力は DX12_DEFAULT_VS_3D の出力で、レジスタの割り当てはメインのルートシグネチャ
///          (DX12PipelineStates_Setup.inl の createRootSignature) の説明にある。

#include <string>

#include <mitiru/render/dx12/DX12DecalShaders.hpp>

namespace mitiru::render
{

inline constexpr const char* DX12_LIT_COMMON_HLSL = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float  MaterialShininess;
    float3 ShadowTint;   // 影部でアルベドに掛ける係数 (トゥーン)
    float4 FogColor;
    float4 FogParams;    // x=開始距離 y=完全に染まる距離 z=有効
    float4 MaterialParams; // x=alphaCutoff y=最近傍(0/1) z=抜き有効(0/1) w=輪郭線から除外(0/1)
    float4 ToonParams;   // x=段数 y=境の幅 z=ハイライト強さ w=ハイライト指数
    float4 ToonMidTint;
    float4 AmbientSky;    // xyz=上からの環境光 (半球を使わない時は AmbientColor と同値)
    float4 AmbientGround; // xyz=下からの環境光
    float4 RimParams;     // xyz=縁光の色 × 強さ w=1-NdotV の指数
    float4 ToonMaterial;  // xyz=材質のハイライト色 w=ToonParams.w に掛ける指数の係数
};

cbuffer CbDrawEx : register(b2)
{
    float4 BaseColor;      // PBR の基本色 (tint を掛け込み済み)
    float4 EmissiveFactor; // rgb=自発光の係数
    float4 MapFlags;       // x=法線マップ y=法線の強さ z=金属・粗さマップ w=自発光マップ (有無は 0/1)
    float4 PbrFactors;     // x=metallic y=roughness z=金属・粗さマップの R を遮蔽に使う強さ
    float4 TintAdd;        // rgb=照明の後に足す色
};

cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float    ShadowSoftness;   // PCF の端のタップまでの距離 (texel)
    float    ShadowBiasNdc;    // 0 = 固定の余白。正なら影マップ深度の余白 (shadowBiasFor が傾きで伸ばす)
    float4x4 LightViewProjFar2;
};

cbuffer CbCluster : register(b4)
{
    uint4  ClusterGrid;    // xyz=froxel の数 w=このフレームの局所光の数
    float4 ClusterDepth;   // x=near y=far z=Z/log(far/near) w=-Z*log(near)/log(far/near)
    float4 ClusterScreen;  // x=タイル数 X/画面幅 y=タイル数 Y/画面高さ
    float4 CameraForward;  // xyz=視線
    float4 IblParams;      // x=環境マップ有無 y=環境光の強さ z=prefiltered の最大 mip w=1 なら副ビュー
    uint4  SpotShadowLight;    // 枠 k の影を使う局所光の番号 (0xFFFFFFFF = 空き)
    float4 SpotShadowParams;   // x=アトラスの texel の幅 (u) y=高さ (v)
    float4x4 SpotShadowViewProj[4];
};

struct LocalLightGpu
{
    float3 positionWS;  float range;
    float3 color;       float spotScale;
    float3 directionWS; float spotOffset;
    float3 boundCenterVS; float boundRadius;
};

Texture2D                       g_albedo      : register(t0);
Texture2D                       g_shadow      : register(t1);
Texture2D                       g_shadowFar   : register(t2);
Texture2D                       g_normalMap   : register(t3);
Texture2D                       g_mrMap       : register(t4);
Texture2D                       g_emissiveMap : register(t5);
StructuredBuffer<LocalLightGpu> g_localLights : register(t6);
StructuredBuffer<uint>          g_clusterMasks : register(t7);
TextureCube                     g_irradiance  : register(t8);
TextureCube                     g_prefiltered : register(t9);
Texture2D                       g_brdfLut     : register(t10);
Texture2D                       g_spotShadow  : register(t11);
SamplerState                    g_samp        : register(s0);
SamplerComparisonState          g_pcf         : register(s1);
SamplerState                    g_sampPoint   : register(s2);
SamplerState                    g_sampClamp   : register(s3);

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

static const float PI = 3.14159265359;

// glTF のマテリアル指定を反映してアルベドを拾う
float4 sampleAlbedo(float2 uv)
{
    float4 c = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, uv)
                                        : g_albedo.Sample(g_samp, uv);
    if (MaterialParams.z > 0.5) { clip(c.a - MaterialParams.x); }
    return c;
}

// 接線を頂点に持たないので、画面上の位置と UV の微分から接空間を組む (Schuler の cotangent frame)。
// 画面の y は下向きなので、この式の T と B は u・v が増える向きの逆を指す (invmax の符号で戻す)。
// glTF の法線マップの緑は v が減る向きなので、v が増える向きの B に対しては符号を返す
float3 perturbNormal(float3 N, float3 worldPos, float2 uv)
{
    if (MapFlags.x < 0.5) { return N; }
    float2 xy = (g_normalMap.Sample(g_samp, uv).rg * 2.0 - 1.0) * MapFlags.y;
    xy.y = -xy.y;
    float3 tn = float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = -rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return normalize(T * (invmax * tn.x) + B * (invmax * tn.y) + N * tn.z);
}

// x=metallic y=roughness z=遮蔽 (1 = 遮らない)
float3 sampleMetalRough(float2 uv)
{
    float3 mr = float3(PbrFactors.x, PbrFactors.y, 1.0);
    if (MapFlags.z > 0.5)
    {
        float3 t = g_mrMap.Sample(g_samp, uv).rgb;
        mr.x *= t.b;
        mr.y *= t.g;
        mr.z = lerp(1.0, t.r, PbrFactors.z);
    }
    return mr;
}

float3 sampleEmissive(float2 uv)
{
    float3 e = EmissiveFactor.rgb;
    if (MapFlags.w > 0.5) { e *= g_emissiveMap.Sample(g_samp, uv).rgb; }
    return e + TintAdd.rgb;
}

// 影の比較の余白。タップを広げた分と、受ける面が光に傾いた分 (タップ間の深度差 = 間隔 × tan) だけ伸ばす
float shadowBiasFor(float3 N, float3 L)
{
    if (ShadowBiasNdc <= 0.0) { return 0.001 * max(ShadowSoftness, 1.0); }
    float c = max(saturate(dot(N, L)), 0.1);
    float t = min(sqrt(1.0 - c * c) / c, 6.0);
    return ShadowBiasNdc * (1.0 + max(ShadowSoftness, 1.0) * t);
}

// softness 1 までは 3x3 を softness texel おき。広いときは 5x5 のテント重みで softness / 2 おきに埋める
// (3x3 のまま広げると双線形の比較の間が空き、縁が 3 段に分かれる)。u は [uMin, uMax] に収める
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

// g_shadowFar は 2 列のアトラス (左 = カスケード1、右 = カスケード2)。PCF のタップは列の内側に収める
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

// カメラ距離で使うカスケードを選ぶ。カスケード無効時は CPU が分割距離を非常に大きくするので常にカスケード0
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

float3 applyFog(float3 color, float3 worldPos)
{
    if (FogParams.z < 0.5) { return color; }
    float dist = length(CameraPos - worldPos);
    float f = saturate((dist - FogParams.x) / max(FogParams.y - FogParams.x, 0.001));
    return lerp(color, FogColor.rgb, f);
}

float3 hemisphereAmbient(float3 N)
{
    return lerp(AmbientGround.rgb, AmbientSky.rgb, N.y * 0.5 + 0.5);
}

PSOutput packOutput(float3 color, float alpha, float3 N, float3 V)
{
    PSOutput o;
    o.Color = float4(color, alpha);
    o.Normal = float4(N * 0.5 + 0.5, saturate(dot(N, V)));
    // outlineCaster=false の印。単位法線の *0.5+0.5 は 3 成分そろって 1 にならないので見分けられる
    if (MaterialParams.w > 0.5) { o.Normal = float4(1.0, 1.0, 1.0, 1.0); }
    return o;
}

// 局所光の陰影に渡す面の性質
struct Surface
{
    float3 albedo;
    float3 N;
    float3 V;
    float  metallic;
    float  roughness;
    float3 F0;
};

// 局所光 1 灯が worldPos に届く様子
struct LocalHit
{
    float3 L;        // 面から光への向き
    float3 color;    // 光の色 × 強さ
    float  shape;    // range の窓とスポットの円錐 (0..1)。光の届く範囲の形
    float  falloff;  // 1 / (距離^2 + 1)。近くで発散させない距離の減衰
};

bool localLightAt(uint index, float3 worldPos, out LocalHit h)
{
    LocalLightGpu l = g_localLights[index];
    float3 d = l.positionWS - worldPos;
    float dist2 = dot(d, d);
    float r2 = l.range * l.range;
    h.L = d * rsqrt(max(dist2, 1e-8));
    h.color = l.color;
    h.falloff = 1.0 / (dist2 + 1.0);
    h.shape = 0.0;
    if (dist2 >= r2) { return false; }
    float win = saturate(1.0 - (dist2 * dist2) / (r2 * r2));
    float spot = saturate(dot(-h.L, l.directionWS) * l.spotScale + l.spotOffset);
    h.shape = win * win * spot * spot;
    return h.shape > 0.0;
}

// 影を落とすスポットなら、アトラスの自分の枠を 3x3 の PCF で引いて光の通る割合を返す。影の無い光は 1
float localShadow(uint index, float3 worldPos)
{
    [unroll]
    for (uint k = 0; k < 4; ++k)
    {
        if (SpotShadowLight[k] != index) { continue; }
        float4 clip = mul(SpotShadowViewProj[k], float4(worldPos, 1.0));
        float3 ndc = clip.xyz / max(clip.w, 1e-5);
        float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
        if (clip.w <= 0.0 || any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) { return 1.0; }
        // 透視の深度は遠くほど詰まるので、余白は深度の残り (1 - z) に比例させる
        float depthRef = ndc.z - (1.0 - ndc.z) * 0.02;
        float2 texel = SpotShadowParams.xy;
        float uMin = k * 0.25 + texel.x * 0.5;
        float uMax = (k + 1) * 0.25 - texel.x * 0.5;
        float lit = 0.0;
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 tap = float2(clamp((uv.x + k) * 0.25 + x * texel.x, uMin, uMax), uv.y + y * texel.y);
                lit += g_spotShadow.SampleCmpLevelZero(g_pcf, tap, depthRef);
            }
        }
        return lit / 9.0;
    }
    return 1.0;
}

uint clusterOf(float4 svPos, float3 worldPos)
{
    float depth = max(dot(worldPos - CameraPos, CameraForward.xyz), ClusterDepth.x);
    int z = (int)floor(log(depth) * ClusterDepth.z + ClusterDepth.w);
    z = clamp(z, 0, (int)ClusterGrid.z - 1);
    uint x = min((uint)(svPos.x * ClusterScreen.x), ClusterGrid.x - 1);
    uint y = min((uint)(svPos.y * ClusterScreen.y), ClusterGrid.y - 1);
    return ((uint)z * ClusterGrid.y + y) * ClusterGrid.x + x;
}

// GGX / Smith / Schlick (PBR と、PBR の局所光で共有する)
float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float geometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 拡散と鏡面を合わせた BRDF × NdotL
float3 cookTorrance(Surface s, float3 L)
{
    float3 H = normalize(s.V + L);
    float NdotL = saturate(dot(s.N, L));
    float NdotV = max(dot(s.N, s.V), 1e-4);
    float D = distributionGGX(saturate(dot(s.N, H)), s.roughness);
    float G = geometrySchlickGGX(NdotV, s.roughness) * geometrySchlickGGX(NdotL, s.roughness);
    float3 F = fresnelSchlick(saturate(dot(H, s.V)), s.F0);
    float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kD = (1.0 - F) * (1.0 - s.metallic);
    return (kD * s.albedo / PI + spec) * NdotL;
}
)hlsl";

/// @brief トゥーンの段 (主光源と同じ ramp) と、局所光 1 灯の陰影
inline constexpr const char* DX12_LIT_TOON_SHADE_HLSL = R"hlsl(
// 段の境の半幅。境が 1 画素より細くなる所 (斜めに寝た面、折れた法線、影の縁) では点々に砕けるので、
// その画素での lambert の傾き (fwidth) まで広げる。上限 0.25 は、段そのものが溶けて平坦にならないため
float toonEdgeHalfWidth(float half_, float grad)
{
    return max(half_, min(grad, 0.25));
}

// bands=0 は段を作らず、softness を巻き込み量にした wrap lambert をそのまま返す
float toonWrap(float lambert)
{
    float wrap = max(ToonParams.y, 0.001);
    return saturate((lambert + wrap) / (1.0 + wrap));
}

// lambert を等分のしきい値 k/bands で刻んで 0..1 の段位置にする
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

// 2 トーンは影→白の直線。3 段以上は中間色を経由して影→中→白
float3 toonTone(float t)
{
    if (ToonParams.x < 2.5) { return lerp(ShadowTint, float3(1.0, 1.0, 1.0), t); }
    return (t < 0.5) ? lerp(ShadowTint, ToonMidTint.rgb, t * 2.0)
                     : lerp(ToonMidTint.rgb, float3(1.0, 1.0, 1.0), t * 2.0 - 1.0);
}

// 局所光は届く範囲の形 × lambert を主光源と同じ段で刻む。光の溜まりがセルの縁を持ち、中は光の色で平らになる。
// 距離の 2 乗の減衰は掛けない (段で刻むと縁が距離の減衰に引きずられて内側へ寄る)。段なし (bands=0) は wrap lambert
float3 shadeLocal(Surface s, LocalHit h)
{
    float ndl = saturate(dot(s.N, h.L));
    float level = (ToonParams.x < 0.5) ? toonWrap(ndl) * h.shape : toonRamp(ndl * h.shape);
    return s.albedo * h.color * level;
}
)hlsl";

/// @brief Phong の局所光 1 灯 (Lambert + Blinn-Phong)
inline constexpr const char* DX12_LIT_PHONG_SHADE_HLSL = R"hlsl(
float3 shadeLocal(Surface s, LocalHit h)
{
    float ndl = saturate(dot(s.N, h.L));
    float3 H = normalize(h.L + s.V);
    float spec = pow(saturate(dot(s.N, H)), max(MaterialShininess, 1.0)) * ndl;
    return (s.albedo * ndl + MaterialSpecular.rgb * spec) * h.color * (h.shape * h.falloff);
}
)hlsl";

/// @brief PBR の局所光 1 灯 (主光源と同じ Cook-Torrance)
/// @details 光の強さは Phong・トゥーンと同じ約束 (強さ 1 の白い光が正面から当たった白い面が白) に揃えるため、
///          拡散の 1/π を光の側の π で打ち消す (光の色 × π を放射照度とみなす)
inline constexpr const char* DX12_LIT_PBR_SHADE_HLSL = R"hlsl(
float3 shadeLocal(Surface s, LocalHit h)
{
    return cookTorrance(s, h.L) * (h.color * PI) * (h.shape * h.falloff);
}
)hlsl";

/// @brief 画素の froxel に入っている局所光を走って shadeLocal を足す
inline constexpr const char* DX12_LIT_LOCAL_LOOP_HLSL = R"hlsl(
float3 accumulateLocalLights(PSInput input, Surface s)
{
    float3 sum = 0.0;
    uint count = ClusterGrid.w;
    if (count == 0) { return sum; }
    uint base = clusterOf(input.Position, input.WorldPos) * 8;
    uint words = (count + 31) / 32;
    [loop]
    for (uint w = 0; w < words; ++w)
    {
        uint bits = g_clusterMasks[base + w];
        [loop]
        while (bits != 0)
        {
            uint b = firstbitlow(bits);
            bits &= bits - 1;
            LocalHit h;
            if (localLightAt(w * 32 + b, input.WorldPos, h))
            {
                h.shape *= localShadow(w * 32 + b, input.WorldPos);
                sum += shadeLocal(s, h);
            }
        }
    }
    return sum;
}
)hlsl";

inline constexpr const char* DX12_LIT_TOON_MAIN_HLSL = R"hlsl(
PSOutput PSMain(PSInput input)
{
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 mr = sampleMetalRough(input.TexCoord);
    applyDecals(input.Position, input.WorldPos, Ng, albedo, N, mr.y);

    float distToCamera = length(CameraPos - input.WorldPos);
    float castShadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera, shadowBiasFor(N, L));

    float lambert = saturate(dot(N, L)) * castShadow;
    float3 tone = toonTone(toonRamp(lambert));
    // 半球アンビエント。半球を切っていれば CPU が sky/ground に同じ色を入れてくるので平坦な 1 色と一致する
    float3 color = albedo * tone * LightColor + hemisphereAmbient(N) * albedo * 0.30 * mr.z;

    // 段付きハイライト。影 (lambert 0) では出さない。境の幅は段と共有
    if (ToonParams.z > 0.0)
    {
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), max(ToonParams.w * ToonMaterial.w, 1.0));
        float half_ = toonEdgeHalfWidth(ToonParams.y * 0.5, fwidth(spec));
        float lit = smoothstep(0.5 - half_, 0.5 + half_, spec) * step(0.001, lambert);
        color += LightColor * ToonMaterial.rgb * ToonParams.z * lit;
    }

    // 縁光。強さは RimParams.rgb に畳んであるので、無効時は黒が足されるだけ
    color += RimParams.rgb * pow(1.0 - saturate(dot(N, V)), RimParams.w);

    Surface s;
    s.albedo = albedo; s.N = N; s.V = V; s.metallic = mr.x; s.roughness = mr.y; s.F0 = 0.04;
    color += accumulateLocalLights(input, s);
    color += sampleEmissive(input.TexCoord);
    color = applyFog(color, input.WorldPos);

    return packOutput(color, MaterialDiffuse.a * input.Color.a * texSample.a, Ng, V);
}
)hlsl";

inline constexpr const char* DX12_LIT_PHONG_MAIN_HLSL = R"hlsl(
float3 shadePhong(PSInput input, float3 N, float3 V, float4 texSample)
{
    float3 L = normalize(-LightDir);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 mr = sampleMetalRough(input.TexCoord);
    applyDecals(input.Position, input.WorldPos, normalize(input.WorldNorm), albedo, N, mr.y);
    float distToCamera = length(CameraPos - input.WorldPos);
    float shadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera, shadowBiasFor(N, L));

    float NdotL = saturate(dot(N, L));
    float3 H = normalize(L + V);
    float specFactor = pow(saturate(dot(N, H)), max(MaterialShininess, 1.0)) * NdotL * shadow;
    float3 lit = AmbientColor * albedo * mr.z + LightColor * albedo * NdotL * shadow
               + LightColor * MaterialSpecular.rgb * specFactor;

    Surface s;
    s.albedo = albedo; s.N = N; s.V = V; s.metallic = mr.x; s.roughness = mr.y; s.F0 = 0.04;
    lit += accumulateLocalLights(input, s);
    lit += sampleEmissive(input.TexCoord);
    return applyFog(lit, input.WorldPos);
}
)hlsl";

inline constexpr const char* DX12_LIT_PHONG_OPAQUE_HLSL = R"hlsl(
PSOutput PSMain(PSInput input)
{
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 V = normalize(CameraPos - input.WorldPos);
    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 lit = shadePhong(input, N, V, texSample);
    return packOutput(lit, MaterialDiffuse.a * input.Color.a * texSample.a, Ng, V);
}
)hlsl";

/// @brief 半透明: Phong と同じ陰影を Weighted-Blended OIT へ出す (weight は WeightedBlendedOIT.hpp と一致)
inline constexpr const char* DX12_LIT_PHONG_OIT_HLSL = R"hlsl(
struct OitOutput { float4 accum : SV_TARGET0; float reveal : SV_TARGET1; };

OitOutput PSMain(PSInput input)
{
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 V = normalize(CameraPos - input.WorldPos);
    float4 texSample = (MaterialParams.y > 0.5) ? g_albedo.Sample(g_sampPoint, input.TexCoord)
                                                : g_albedo.Sample(g_samp, input.TexCoord);
    float3 shaded = shadePhong(input, N, V, texSample);
    float alpha = MaterialDiffuse.a * input.Color.a * texSample.a;

    float z = input.Position.z;
    float w = clamp(pow(min(1.0, alpha * 10.0) + 0.01, 3.0) * 1e3 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
    OitOutput o;
    o.accum  = float4(shaded * alpha, alpha) * w;
    o.reveal = alpha;
    return o;
}
)hlsl";

inline constexpr const char* DX12_LIT_PBR_MAIN_HLSL = R"hlsl(
float3 ambientPbr(Surface s, float ao)
{
    if (IblParams.x < 0.5)
    {
        return hemisphereAmbient(s.N) * s.albedo * (1.0 - s.metallic) * ao;
    }
    float NdotV = saturate(dot(s.N, s.V));
    // 粗い面ほどフレネルの立ち上がりが鈍る (Lagarde)。kD の配分にはこちらを使う
    float3 kS = s.F0 + (max((1.0 - s.roughness).xxx, s.F0) - s.F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (1.0 - kS) * (1.0 - s.metallic);
    float3 diffuse = kD * g_irradiance.Sample(g_sampClamp, s.N).rgb * s.albedo;
    // split-sum: 粗さに応じた mip の prefiltered × 環境 BRDF 表 (A, B)
    float3 R = reflect(-s.V, s.N);
    float3 prefiltered = g_prefiltered.SampleLevel(g_sampClamp, R, s.roughness * IblParams.z).rgb;
    float2 envBrdf = g_brdfLut.Sample(g_sampClamp, float2(NdotV, s.roughness)).rg;
    return (diffuse + prefiltered * (s.F0 * envBrdf.x + envBrdf.y)) * ao * IblParams.y;
}

PSOutput PSMain(PSInput input)
{
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 L = normalize(-LightDir);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 mr = sampleMetalRough(input.TexCoord);
    Surface s;
    s.albedo = BaseColor.rgb * input.Color.rgb * texSample.rgb;
    s.N = N;
    s.V = V;
    s.metallic = saturate(mr.x);
    s.roughness = clamp(mr.y, 0.04, 1.0);
    applyDecals(input.Position, input.WorldPos, Ng, s.albedo, s.N, s.roughness);
    s.F0 = lerp(float3(0.04, 0.04, 0.04), s.albedo, s.metallic);

    float distToCamera = length(CameraPos - input.WorldPos);
    float shadow = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, distToCamera, shadowBiasFor(N, L));

    // トーンマップ・ガンマは共有の resolve/tonemap パスに任せ、線形 HDR のまま書く
    // 光の色 × π を放射照度とみなす (DX12_LIT_PBR_SHADE_HLSL と同じ約束)
    float3 color = cookTorrance(s, L) * (LightColor * PI) * shadow + ambientPbr(s, mr.z);
    color += accumulateLocalLights(input, s);
    color += sampleEmissive(input.TexCoord);
    color = applyFog(color, input.WorldPos);

    return packOutput(color, BaseColor.a * input.Color.a * texSample.a, Ng, V);
}
)hlsl";

/// @brief メインパスの陰影の種類
enum class LitShade
{
	Toon,
	Phong,
	PhongOit,
	Pbr,
};

/// @brief 共通部 + 局所光 1 灯の陰影 + 走査 + 本体を連結した PS のソース
[[nodiscard]] inline std::string dx12LitPixelShader(LitShade shade)
{
	std::string src = DX12_LIT_COMMON_HLSL;
	src += DX12_DECAL_APPLY_HLSL;
	switch (shade)
	{
	case LitShade::Toon:
		src += DX12_LIT_TOON_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_TOON_MAIN_HLSL;
		break;
	case LitShade::Phong:
	case LitShade::PhongOit:
		src += DX12_LIT_PHONG_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_PHONG_MAIN_HLSL;
		src += (shade == LitShade::Phong) ? DX12_LIT_PHONG_OPAQUE_HLSL : DX12_LIT_PHONG_OIT_HLSL;
		break;
	case LitShade::Pbr:
		src += DX12_LIT_PBR_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_PBR_MAIN_HLSL;
		break;
	}
	return src;
}

} // namespace mitiru::render
