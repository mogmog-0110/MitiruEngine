#pragma once

/// @file DX12LitShaders.hpp
/// @brief DX12 メインパスの陰影 PS (Toon / Phong / PBR と半透明の OIT)。材質のマップと局所光を共有する
/// @details PS は「共通部 + デカール + 間接光 + 局所光 1 灯の陰影 + 局所光の走査 + 本体」を連結して作る (`dx12LitPixelShader`)。
///          入力は DX12_DEFAULT_VS_3D の出力で、レジスタの割り当てはメインのルートシグネチャ
///          (DX12PipelineStates_Setup.inl の createRootSignature) の説明にある。

#include <string>

#include <mitiru/render/dx12/DX12DecalShaders.hpp>
#include <mitiru/render/dx12/DX12LightingProbeShaders.hpp>
#include <mitiru/render/dx12/DX12OccluderFadeShaders.hpp>
#include <mitiru/render/dx12/DX12SunShadowShaders.hpp>
#include <mitiru/render/dx12/DX12ToonShaders.hpp>

namespace mitiru::render
{

inline constexpr const char* DX12_LIT_COMMON_HLSL = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuseCb;
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
    float4 BaseColorCb;    // PBR の基本色 (tint を掛け込み済み)
    float4 EmissiveFactor; // rgb=自発光の係数
    float4 MapFlags;       // x=法線マップ y=法線の強さ z=金属・粗さマップ w=自発光マップ (有無は 0/1)
    float4 PbrFactors;     // x=metallic y=roughness z=金属・粗さマップの R を遮蔽に使う強さ
    float4 TintAdd;        // rgb=照明の後に足す色
    float4 DrawFlags;      // x=透かしの的として描いた物 (0/1、抜かない)
    // 描画ごとのトゥーン (ToonStyleGpu.hpp の ToonRampGpu と ToonAccentGpu)
    float4 ToonRampParams; // x=段の数 (0 = 場面の段のまま) y=境の半幅 z=落ちた影で下げる段 w=描画の印
    float4 ToonRampThresh; // xyz=段の境
    float4 ToonTint0;
    float4 ToonTint1;
    float4 ToonTint2;
    float4 ToonTint3;
    float4 ToonRim;        // rgb=縁の帯の色 w=N·V の上限 (0 = 帯なし)
    float4 ToonGloss;      // rgb=つやの色 w=N·H の下限 (0 = つやなし)
    float4 ToonRimLimit;   // x=帯にする N·L の上限 yz=縁・つやの半幅 w=つやの強さ
    float4 ToonMetalA;     // xyz=amount, tint, shadowFollow、w=bands
    float4 ToonMetalB;     // xy=horizonHeight, horizonSoft
    float4 ToonMetalSky;
    float4 ToonMetalHorizon;
    float4 ToonMetalGround;
    float4 CoatParams;     // x=クリアコートの強さ y=その粗さ z=異方性の強さ w=異方性の向き (ラジアン)。PBR だけが読む
    float4 SheenParams;    // rgb=シーンの色 w=シーンの粗さ
    float4 LayerSpare;
};

cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
    float4x4 LightViewProjFar;
    float    CascadeSplitDistance;
    float    CascadeSplitDistance2;
    float    ShadowSoftness;   // PCF の端のタップまでの距離 (texel)
    float    ShadowBiasNdc;    // 0 = 影マップの texel に比例する余白。正なら影マップ深度の余白 (shadowLookup が傾きで伸ばす)
    float4x4 LightViewProjFar2;
    float4   ShadowTexel;      // x = 1 / カスケード 0 の一辺 y = 1 / カスケード 1・2 の一辺 (アトラスの 1 列)
};

cbuffer CbCluster : register(b4)
{
    uint4  ClusterGrid;    // xyz=froxel の数 w=このフレームの局所光の数
    float4 ClusterDepth;   // x=near y=far z=Z/log(far/near) w=-Z*log(near)/log(far/near)
    float4 ClusterScreen;  // x=タイル数 X/画面幅 y=タイル数 Y/画面高さ
    float4 CameraForward;  // xyz=視線
    float4 IblParams;      // x=環境マップ有無 y=環境光の強さ z=prefiltered の最大 mip
    uint4  SpotShadowLight;    // 枠 k の影を使う局所光の番号 (0xFFFFFFFF = 空き)
    float4 SpotShadowParams;   // x=アトラスの texel の幅 (u) y=高さ (v)
    float4x4 SpotShadowViewProj[4];
    float4 GiOrigin;       // xyz=放射照度のプローブの格子の原点 w=有効
    float4 GiSpacing;      // xyz=格子の間隔 w=間接光の強さ
    uint4  GiDims;         // xyz=プローブの数 w=距離の地図の 1 行のプローブ数
    float4 GiParams;       // x=面から浮かせる距離 y=トゥーンの段 zw=1/距離の地図の幅と高さ
    float4 ReflParams;     // x=反射のプローブの数 y=最大の mip z=強さ
    float4 ReflBoxMin[8];  // w=箱の内側へ重みが 1 になる距離
    float4 ReflBoxMax[8];
    float4 ReflPos[8];     // xyz=撮った位置 w=配列の番号
    float4 SsrParams;      // x=有効 y=最大の粗さ z=厚み (m) w=強さ
    float4 SsrScreen;      // xy=HZB の段 0 の大きさ z=HZB の段数 w=色の段数
    float4 SsrRay;         // x=長さ (m) y=辿る回数 z=フレームの番号 w=粗さで向きを散らすか (TAA の時)
    float4 SsrDepth;       // xy=前フレームの深度 d を距離 y / (d + x) へ戻す係数 z=近クリップ
    float4x4 SsrPrevViewProj;
    OCCLUDER_FADE_FIELDS
    uint4  PointShadowLight;   // x=影を使う局所光の番号 (0xFFFFFFFF = なし)
    float4 PointShadowParams;  // x=アトラスの texel の幅 (u) y=高さ (v) z=1 面の幅 (u) w=深度の余白 (残りの深度 1 - z に掛ける)
    float4x4 PointShadowViewProj[6];
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
Texture2D                       g_pointShadow : register(t44);
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
    // まとめ描き (DX12DrawRun.hpp) のインスタンスの線形の拡散色。w が負なら CbLighting / CbDrawEx の値を使う
    nointerpolation float4 InstDiffuse : TEXCOORD4;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

static const float PI = 3.14159265359;

// 材質の拡散色と PBR の基本色。まとめ描きの 1 回の draw に混ざる、色だけが違うインスタンスのために、
// 本体の最初に chooseDrawColors が決める (定数バッファの値は同じ draw の全インスタンスで 1 つしか持てない)
static float4 MaterialDiffuse;
static float4 BaseColor;

void chooseDrawColors(PSInput input)
{
    const bool inst = input.InstDiffuse.w >= 0.0;
    MaterialDiffuse = inst ? input.InstDiffuse : MaterialDiffuseCb;
    BaseColor = inst ? input.InstDiffuse : BaseColorCb;
}

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

// 法線 RT の a には描画の印と N·V を詰める (DX12ToonShaders.hpp)
PSOutput packOutputSlot(float3 color, float alpha, float3 N, float3 V, float slot)
{
    PSOutput o;
    o.Color = float4(color, alpha);
    o.Normal = float4(N * 0.5 + 0.5, packSlotNdv(slot, dot(N, V)));
    // outlineCaster=false の印。単位法線の *0.5+0.5 は 3 成分そろって 1 にならないので見分けられる
    if (MaterialParams.w > 0.5) { o.Normal.rgb = 1.0; }
    return o;
}

PSOutput packOutput(float3 color, float alpha, float3 N, float3 V)
{
    return packOutputSlot(color, alpha, N, V, ToonRampParams.w);
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
    // PBR の層 (クリアコート、異方性、シーン)。layered が 0 なら読まない。トゥーンと Phong は設定しない
    float  layered;
    float3 Ng;               // クリアコートが使う面の法線 (法線マップの前)
    float  coat;
    float  coatRoughness;
    float  anisotropy;
    float  anisotropyRotation;
    float3 T;
    float3 B;
    float3 sheenColor;
    float  sheenRoughness;
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

// 影を落とす点光源なら、画素が入る面を光からの向きの最大軸で選び (PointShadowFaces.hpp と同じ規則)、
// アトラスの その面の列を 3x3 の PCF で引く。面は 90 度より広く撮ってあるので、境の読み出しは面の内側に収まる
float pointShadowAt(float3 lightPos, float3 worldPos)
{
    float3 d = worldPos - lightPos;
    float3 a = abs(d);
    uint f = (a.x >= a.y && a.x >= a.z) ? (d.x >= 0.0 ? 0u : 1u)
           : (a.y >= a.z)               ? (d.y >= 0.0 ? 2u : 3u)
                                        : (d.z >= 0.0 ? 4u : 5u);
    float4 clip = mul(PointShadowViewProj[f], float4(worldPos, 1.0));
    float3 ndc = clip.xyz / max(clip.w, 1e-5);
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (clip.w <= 0.0 || any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) { return 1.0; }
    float depthRef = ndc.z - (1.0 - ndc.z) * PointShadowParams.w;
    float2 texel = PointShadowParams.xy;
    float uMin = f * PointShadowParams.z + texel.x * 0.5;
    float uMax = (f + 1) * PointShadowParams.z - texel.x * 0.5;
    float lit = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 tap = float2(clamp((uv.x + f) * PointShadowParams.z + x * texel.x, uMin, uMax), uv.y + y * texel.y);
            lit += g_pointShadow.SampleCmpLevelZero(g_pcf, tap, depthRef);
        }
    }
    return lit / 9.0;
}

// 影を落とすスポットなら、アトラスの自分の枠を 3x3 の PCF で引いて光の通る割合を返す。影の無い光は 1
float localShadow(uint index, float3 worldPos)
{
    if (PointShadowLight.x == index) { return pointShadowAt(g_localLights[index].positionWS, worldPos); }
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

)hlsl";

/// @brief 層のある BRDF (異方性、シーン、クリアコート) と、拡散と鏡面を合わせた BRDF。DX12_LIT_COMMON_HLSL の続き。
///        MSVC の文字列定数は 16 KB までなので、共通部を 2 つに分けてある
inline constexpr const char* DX12_LIT_BRDF_HLSL = R"hlsl(
// 異方性 GGX (Burley)。at / ab は接線と従接線の向きの粗さ
float distributionGGXAniso(float at, float ab, float ToH, float BoH, float NoH)
{
    float a2 = at * ab;
    float3 d = float3(ab * ToH, at * BoH, a2 * NoH);
    float d2 = dot(d, d);
    float b2 = a2 / max(d2, 1e-8);
    return a2 * b2 * b2 * (1.0 / PI);
}

float visibilitySmithGGXAniso(float at, float ab, float ToV, float BoV, float ToL, float BoL, float NoV, float NoL)
{
    float lambdaV = NoL * length(float3(at * ToV, ab * BoV, NoV));
    float lambdaL = NoV * length(float3(at * ToL, ab * BoL, NoL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

// シーン (布の縁の光)。Charlie の分布と Neubelt の可視性
float distributionCharlie(float alpha, float NoH)
{
    float invAlpha = 1.0 / alpha;
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float visibilityNeubelt(float NoV, float NoL)
{
    return saturate(1.0 / (4.0 * (NoL + NoV - NoL * NoV)));
}

// クリアコートの鏡面反射の強さ (誘電体 F0 = 0.04、法線 N での角度 cosTheta)
float coatFresnel(float coat, float cosTheta)
{
    return coat * (0.04 + 0.96 * pow(saturate(1.0 - cosTheta), 5.0));
}

// 層のある面の BRDF × NdotL。基底は異方性か等方の GGX、その上にシーンを足し、クリアコートを重ねる。
// シーンの減衰は固定の 0.2 で近似する (方向ごとのアルベドの表は持たない)
float3 layeredBrdf(Surface s, float3 L)
{
    float3 H = normalize(s.V + L);
    float NdotL = saturate(dot(s.N, L));
    float NdotV = max(dot(s.N, s.V), 1e-4);
    float NdotH = saturate(dot(s.N, H));
    float VdotH = saturate(dot(s.V, H));
    float3 F = fresnelSchlick(VdotH, s.F0);
    float3 spec;
    if (s.anisotropy > 0.0)
    {
        float a = s.roughness * s.roughness;
        float at = max(a * (1.0 + s.anisotropy), 0.001);
        float ab = max(a * (1.0 - s.anisotropy), 0.001);
        float D = distributionGGXAniso(at, ab, dot(s.T, H), dot(s.B, H), NdotH);
        float Vis = visibilitySmithGGXAniso(at, ab, dot(s.T, s.V), dot(s.B, s.V), dot(s.T, L), dot(s.B, L), NdotV, NdotL);
        spec = D * Vis * F;
    }
    else
    {
        float D = distributionGGX(NdotH, s.roughness);
        float G = geometrySchlickGGX(NdotV, s.roughness) * geometrySchlickGGX(NdotL, s.roughness);
        spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    }
    float3 kD = (1.0 - F) * (1.0 - s.metallic);
    float3 base = kD * s.albedo / PI + spec;
    float sheenMax = max(max(s.sheenColor.r, s.sheenColor.g), s.sheenColor.b);
    if (sheenMax > 0.0)
    {
        float alpha = max(s.sheenRoughness * s.sheenRoughness, 0.07);
        base = base * (1.0 - sheenMax * 0.2) + s.sheenColor * (distributionCharlie(alpha, NdotH) * visibilityNeubelt(NdotV, NdotL));
    }
    float3 result = base * NdotL;
    if (s.coat > 0.0)
    {
        float NgL = saturate(dot(s.Ng, L));
        float NgH = saturate(dot(s.Ng, H));
        float Fc = coatFresnel(s.coat, VdotH);
        float a = max(s.coatRoughness, 0.045);
        float Dc = distributionGGX(NgH, a);
        float Vc = 0.25 / max(saturate(dot(L, H)) * saturate(dot(L, H)), 1e-4);   // Kelemen
        result = result * (1.0 - Fc) + (Dc * Vc * Fc * NgL).xxx;
    }
    return result;
}

// 拡散と鏡面を合わせた BRDF × NdotL
float3 cookTorrance(Surface s, float3 L)
{
    if (s.layered > 0.5) { return layeredBrdf(s, L); }
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

// 描画ごとの定数 (glTF の拡張) から層の値を読む。層の値は finishPbrLayers が使う形にそろえる
void loadPbrLayers(inout Surface s, float3 Ng)
{
    s.Ng = Ng;
    s.coat = saturate(CoatParams.x);
    s.coatRoughness = saturate(CoatParams.y);
    s.anisotropy = saturate(CoatParams.z);
    s.anisotropyRotation = CoatParams.w;
    s.sheenColor = saturate(SheenParams.rgb);
    s.sheenRoughness = saturate(SheenParams.w);
    s.T = float3(1.0, 0.0, 0.0);
    s.B = float3(0.0, 0.0, 1.0);
    s.layered = 0.0;
}

// 層のどれかが有効なら layered を立てる。異方性は UV の微分から接線を組み (perturbNormal と同じ Schuler の方法)、
// 向きの角度だけ面の法線まわりに回す。layered が 0 の面は従来の式のまま
void finishPbrLayers(inout Surface s, float3 worldPos, float2 uv)
{
    const float sheenMax = max(max(s.sheenColor.r, s.sheenColor.g), s.sheenColor.b);
    s.layered = (s.coat > 0.0 || s.anisotropy > 0.0 || sheenMax > 0.0) ? 1.0 : 0.0;
    if (s.anisotropy <= 0.0) { return; }
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    float3 t = (dp1 * duv2.y - dp2 * duv1.y) * ((det < 0.0) ? -1.0 : 1.0);
    t = t - s.N * dot(s.N, t);
    t = (dot(t, t) > 1e-12) ? normalize(t) : normalize(cross(s.N, abs(s.N.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
    float3 b = cross(s.N, t);
    float c = cos(s.anisotropyRotation);
    float sn = sin(s.anisotropyRotation);
    s.T = c * t + sn * b;
    s.B = cross(s.N, s.T);
}

void setupPbrLayers(inout Surface s, float3 Ng, float3 worldPos, float2 uv)
{
    loadPbrLayers(s, Ng);
    finishPbrLayers(s, worldPos, uv);
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
// 半球アンビエント。半球を切っていれば CPU が sky/ground に同じ色を入れてくるので平坦な 1 色と一致する。
// 焼いた光があれば、その放射照度を段に刻んで置き換える
float3 toonAmbient(PSInput input, float3 N, float3 V, float3 albedo, float ao)
{
    float3 ambient = hemisphereAmbient(N) * albedo * 0.30 * ao;
    if (giEnabled())
    {
        bool giOk;
        float3 e = giIrradiance(input.WorldPos, N, V, giOk);
        if (giOk) { ambient = giToonBand(e) * (GiSpacing.w / PI) * albedo * ao; }
    }
    return ambient;
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
        float th = k / bands;
        level += (k < bands) ? smoothstep(th - width, th + width, t) : 0.0;
    }
    return level / max(bands - 1.0, 1.0);
}

float3 toonMetalEnv(float3 N, float3 V)
{
    float3 R = reflect(-V, N);
    float h = ToonMetalB.x;
    float w = max(ToonMetalB.y, fwidth(R.y));
    float above = smoothstep(h - w, h + w, R.y);
    float skyT = saturate((R.y - h) / max(1.0 - h, 1e-3));
    float groundT = saturate((h - R.y) / max(1.0 + h, 1e-3));
    float3 skySide = lerp(ToonMetalHorizon.rgb, ToonMetalSky.rgb,
                          toonMetalSteps(skyT, ToonMetalA.w, fwidth(skyT)));
    float3 groundSide = lerp(ToonMetalGround.rgb, ToonMetalHorizon.rgb,
                             toonMetalSteps(groundT, ToonMetalA.w, fwidth(groundT)));
    return lerp(groundSide, skySide, above);
}

// 描画ごとの段 (ToonStyle)。段の色の後に縁の帯とつやを塗り重ねる。場面の段付きハイライトと縁光は使わない
float3 styledToonAlbedo(float3 albedo, float3 N, float3 V, float3 L, float lambert, float castShadow,
                        out float3 unlitReflection)
{
    float level = toonBandLevel(lambert, ToonRampParams, ToonRampThresh, fwidth(lambert));
    level = toonShadowedLevel(level, castShadow, ToonRampParams);
    float3 bandTint = toonBandTint(level, ToonTint0, ToonTint1, ToonTint2, ToonTint3);
    float3 c = albedo * bandTint;
    unlitReflection = 0.0;
    if (ToonMetalA.x > 0.0)
    {
        float3 refl = toonMetalEnv(N, V) * lerp(1.0, albedo, ToonMetalA.y) * lerp(1.0, bandTint, ToonMetalA.z);
        unlitReflection = refl * ToonMetalA.x;
        c = lerp(c, refl, ToonMetalA.x);
    }
    if (ToonRim.w > 0.0)
    {
        float ndv = saturate(dot(N, V));
        float hv = max(max(fwidth(ndv), 1e-4), ToonRimLimit.y);
        float lit = lambert * castShadow;
        float hl = max(fwidth(lit), 1e-4);
        float side = (ToonRimLimit.x >= 0.999) ? 1.0 : 1.0 - smoothstep(ToonRimLimit.x - hl, ToonRimLimit.x + hl, lit);
        float rim = (1.0 - smoothstep(ToonRim.w - hv, ToonRim.w + hv, ndv)) * side;
        c = lerp(c, ToonRim.rgb, rim);
        unlitReflection *= 1.0 - rim;
    }
    if (ToonGloss.w > 0.0)
    {
        float nh = saturate(dot(N, normalize(L + V)));
        float hw = max(max(fwidth(nh), 1e-4), ToonRimLimit.z);
        float g = smoothstep(ToonGloss.w - hw, ToonGloss.w + hw, nh) * step(0.5, castShadow) * step(0.001, lambert);
        float gMix = g * min(ToonRimLimit.w, 1.0);
        float gAdd = g * max(ToonRimLimit.w - 1.0, 0.0);
        c = lerp(c, ToonGloss.rgb, gMix) + ToonGloss.rgb * gAdd;
        unlitReflection *= 1.0 - gMix;
    }
    return c;
}

PSOutput styledToonMain(PSInput input, float3 N, float3 Ng, float3 L, float3 V, float3 albedo, float3 mr, float castShadow,
                        float texAlpha)
{
    float3 reflection;
    float3 styled = styledToonAlbedo(albedo, N, V, L, saturate(dot(N, L)), castShadow, reflection);
    float diffusePart = 1.0 - ToonMetalA.x;
    // 反射だけを光源色から外し、同じ画素に重ねた縁とつやは従来どおり光源色を受ける。
    float3 color = styled * LightColor + reflection * (1.0 - LightColor)
                 + toonAmbient(input, N, V, albedo, mr.z) * diffusePart;
    Surface s;
    s.albedo = albedo; s.N = N; s.V = V; s.metallic = mr.x; s.roughness = mr.y; s.F0 = 0.04;
    color += accumulateLocalLights(input, s) * diffusePart;
    color += sampleEmissive(input.TexCoord);
    color = applyFog(color, input.WorldPos);
    return packOutput(color, MaterialDiffuse.a * input.Color.a * texAlpha, Ng, V);
}

PSOutput PSMain(PSInput input)
{
    chooseDrawColors(input);
    clipOccluded(input.Position, input.WorldPos, input.WorldNorm);
    float3 Ng = normalize(input.WorldNorm);
    float3 N = perturbNormal(Ng, input.WorldPos, input.TexCoord);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    float4 texSample = sampleAlbedo(input.TexCoord);
    float3 albedo = MaterialDiffuse.rgb * input.Color.rgb * texSample.rgb;
    float3 mr = sampleMetalRough(input.TexCoord);
    applyDecals(input.Position, input.WorldPos, Ng, albedo, N, mr.y);

    float distToCamera = length(CameraPos - input.WorldPos);
    float castShadow = sampleCascadedShadow(input.WorldPos, N, L, distToCamera);

    if (ToonRampParams.x > 0.5) { return styledToonMain(input, N, Ng, L, V, albedo, mr, castShadow, texSample.a); }

    float lambert = saturate(dot(N, L)) * castShadow;
    float3 tone = toonTone(toonRamp(lambert));
    float3 color = albedo * tone * LightColor + toonAmbient(input, N, V, albedo, mr.z);

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
    float shadow = sampleCascadedShadow(input.WorldPos, N, L, distToCamera);

    float NdotL = saturate(dot(N, L));
    float3 H = normalize(L + V);
    float specFactor = pow(saturate(dot(N, H)), max(MaterialShininess, 1.0)) * NdotL * shadow;
    float3 lit = giDiffuse(input.WorldPos, N, V, AmbientColor) * albedo * mr.z + LightColor * albedo * NdotL * shadow
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
    chooseDrawColors(input);
    clipOccluded(input.Position, input.WorldPos, input.WorldNorm);
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
    chooseDrawColors(input);
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
// 異方性の面の鏡面反射は、接線の向きに沿って法線を曲げた向きの環境を引く (Filament の bent normal)
float3 layeredReflection(Surface s)
{
    float3 N = s.N;
    if (s.anisotropy > 0.0)
    {
        float3 grain = cross(s.B, s.V);
        float3 bent = normalize(cross(grain, s.B));
        N = normalize(lerp(s.N, bent, s.anisotropy * saturate(5.0 * s.roughness)));
    }
    return reflect(-s.V, N);
}

// クリアコートの層。下の層の環境光を (1 - 反射の強さ) に弱め、面の法線で引いた環境をクリアコートの粗さで足す
float3 coatEnvironment(Surface s, float3 below)
{
    if (s.coat <= 0.0) { return below; }
    float Fc = coatFresnel(s.coat, saturate(dot(s.Ng, s.V)));
    float3 env;
    if (IblParams.x > 0.5)
    {
        env = g_prefiltered.SampleLevel(g_sampClamp, reflect(-s.V, s.Ng), max(s.coatRoughness, 0.045) * IblParams.z).rgb * IblParams.y;
    }
    else
    {
        env = hemisphereAmbient(reflect(-s.V, s.Ng));
    }
    return below * (1.0 - Fc) + env * Fc;
}

float3 ambientPbr(Surface s, float ao)
{
    if (IblParams.x < 0.5)
    {
        float3 plain = hemisphereAmbient(s.N) * s.albedo * (1.0 - s.metallic) * ao;
        return (s.layered > 0.5) ? coatEnvironment(s, plain) : plain;
    }
    float NdotV = saturate(dot(s.N, s.V));
    // 粗い面ほどフレネルの立ち上がりが鈍る (Lagarde)。kD の配分にはこちらを使う
    float3 kS = s.F0 + (max((1.0 - s.roughness).xxx, s.F0) - s.F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (1.0 - kS) * (1.0 - s.metallic);
    float3 diffuse = kD * g_irradiance.Sample(g_sampClamp, s.N).rgb * s.albedo;
    // split-sum: 粗さに応じた mip の prefiltered × 環境 BRDF 表 (A, B)
    float3 R = (s.layered > 0.5) ? layeredReflection(s) : reflect(-s.V, s.N);
    float3 prefiltered = g_prefiltered.SampleLevel(g_sampClamp, R, s.roughness * IblParams.z).rgb;
    float2 envBrdf = g_brdfLut.Sample(g_sampClamp, float2(NdotV, s.roughness)).rg;
    float3 color = (diffuse + prefiltered * (s.F0 * envBrdf.x + envBrdf.y)) * ao * IblParams.y;
    if (s.layered > 0.5) { color = coatEnvironment(s, color); }
    return color;
}

bool indirectActive() { return giEnabled() || ReflParams.x > 0.5 || SsrParams.x > 0.5; }

// 焼いた拡散・反射のプローブ・画面の反射のどれかが有効な時の環境光。鏡面は画面の反射 > 反射のプローブ > IBL の順に重ねる
float3 ambientPbrIndirect(Surface s, float ao, float3 P, float2 svPos)
{
    bool ibl = IblParams.x > 0.5;
    float NdotV = saturate(dot(s.N, s.V));
    float3 kS = s.F0 + (max((1.0 - s.roughness).xxx, s.F0) - s.F0) * pow(1.0 - NdotV, 5.0);
    float3 kD = (1.0 - kS) * (1.0 - s.metallic);
    float3 envDiffuse = ibl ? g_irradiance.Sample(g_sampClamp, s.N).rgb * IblParams.y : hemisphereAmbient(s.N);
    float3 diffuse = kD * s.albedo * giDiffuse(P, s.N, s.V, envDiffuse);
    float3 R = (s.layered > 0.5) ? layeredReflection(s) : reflect(-s.V, s.N);
    float3 env = ibl ? g_prefiltered.SampleLevel(g_sampClamp, R, s.roughness * IblParams.z).rgb * IblParams.y : 0.0;
    float4 probe = reflectionProbes(P, R, s.roughness);
    env = lerp(env, probe.rgb, probe.a);
    float4 ssr = traceSsr(P, s.N, s.V, s.roughness, svPos);
    env = lerp(env, ssr.rgb, ssr.a);
    float2 envBrdf = ibl ? g_brdfLut.Sample(g_sampClamp, float2(NdotV, s.roughness)).rg : envBrdfApprox(NdotV, s.roughness);
    float3 color = (diffuse + env * (s.F0 * envBrdf.x + envBrdf.y)) * ao;
    if (s.layered > 0.5) { color = coatEnvironment(s, color); }
    return color;
}

PSOutput PSMain(PSInput input)
{
    chooseDrawColors(input);
    clipOccluded(input.Position, input.WorldPos, input.WorldNorm);
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
    setupPbrLayers(s, Ng, input.WorldPos, input.TexCoord);

    float distToCamera = length(CameraPos - input.WorldPos);
    float shadow = sampleCascadedShadow(input.WorldPos, N, L, distToCamera);

    // トーンマップ・ガンマは共有の resolve/tonemap パスに任せ、線形 HDR のまま書く
    // 光の色 × π を放射照度とみなす (DX12_LIT_PBR_SHADE_HLSL と同じ約束)
    float3 ambient;
    if (indirectActive()) { ambient = ambientPbrIndirect(s, mr.z, input.WorldPos, input.Position.xy); }
    else { ambient = ambientPbr(s, mr.z); }
    float3 color = cookTorrance(s, L) * (LightColor * PI) * shadow + ambient;
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

/// @brief 共通部と、的を隠す面を抜く判定。DX12_LIT_COMMON_HLSL を使うソースはここから始める
[[nodiscard]] inline std::string dx12LitCommonHlsl()
{
	std::string src = DX12_OCCLUDER_FADE_FIELDS_HLSL;
	src += DX12_TOON_SLOT_HLSL;
	src += DX12_LIT_COMMON_HLSL;
	src += DX12_LIT_BRDF_HLSL;
	src += DX12_OCCLUDER_FADE_HLSL;
	src += DX12_OCCLUDER_FADE_FORWARD_HLSL;
	return src;
}

/// @brief 共通部 + 局所光 1 灯の陰影 + 走査 + 本体を連結した PS のソース
[[nodiscard]] inline std::string dx12LitPixelShader(LitShade shade)
{
	std::string src = dx12LitCommonHlsl();
	src += DX12_SUN_SHADOW_HLSL;
	src += DX12_DECAL_APPLY_HLSL;
	src += DX12_LIGHTING_PROBES_HLSL;
	switch (shade)
	{
	case LitShade::Toon:
		src += DX12_TOON_RAMP_HLSL;
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
