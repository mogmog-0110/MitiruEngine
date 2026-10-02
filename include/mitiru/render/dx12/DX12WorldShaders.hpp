#pragma once

/// @file DX12WorldShaders.hpp
/// @brief 地形、草、水面の HLSL。陰影にはメインパスの Toon / Phong / PBR の PS を使う
/// @details 地形の PS は DX12LitShaders.hpp の共通部に続けて sampleAlbedo、perturbNormal、
///          sampleMetalRough をマクロで地形用に差し替え、各陰影の本体を連結する。
///          影、局所光、フォグ、半球の環境光、輪郭線の法線出力には本体の処理を使う。草にはメインの PS を使い、
///          材質は白、色は頂点の色とする。
///          レジスタは Renderer3D_DX12 のメインのルートシグネチャ 0〜8 に、DX12World.hpp で 9 = 表 t12〜t33 (全段)、
///          10 = t34 の区画の並び (VS)、11 = b5 の定数 4 個、12 = b6 CbWorld を加える

#include <bit>
#include <string>

#include <mitiru/render/dx12/DX12LitShaders.hpp>

namespace mitiru::render
{

/// @brief 地形、草、水面が共有する宣言。VS と PS の先頭に置く
inline constexpr const char* DX12_WORLD_COMMON_HLSL = R"hlsl(
cbuffer CbWorld : register(b6)
{
    float4 TerrainOrigin;  // xyz=最小の角 w=標本 1.0 の高さ (m)
    float4 TerrainSize;    // x,y=x と z の全長 z,w=その逆数
    float4 TerrainGrid;    // x,y=標本の数 z,w=マスの幅
    float4 TerrainPatch;   // x=区画の格子のマス数 y=層の数 z=splat の枚数 w=時刻 (秒)
    float4 LayerTile[2];   // 層 k の 1/tile は LayerTile[k/4][k%4]
    float4 LayerColor[8];  // rgb=色 w=法線の強さ
    float4 LayerPbr[8];    // x=粗さ y=金属 z=色の画像あり w=法線の画像あり
    float4 GrassShape;     // x=高さ y=幅 z=縮め始める距離 w=消える距離
    float4 GrassBase;      // rgb=根元の色 w=cos(生やす最大の傾き)
    float4 GrassTip;       // rgb=先端の色 w=区画の 1 辺 (m)
    float4 GrassWind;      // xy=風の向き z=先端のずれ (m) w=速さ
    float4 WaterRect;      // 水面の minX, minZ, maxX, maxZ
    float4 WaterShallow;   // rgb=浅い所の色 w=水面の y
    float4 WaterDeep;      // rgb=深い所の色 w=泡の出る水深
    float4 WaterAbsorb;    // rgb=1 m あたりの減衰 w=屈折のずれ
    float4 WaterFoam;      // rgb=泡の色 w=トゥーンの段 (0 = 滑らか)
    float4 WaterWave;      // x=波の強さ y=細かさ z=速さ w=空の映り込み
    float4 ScreenSize;     // x,y=画素の数 z,w=その逆数
    float4x4 InvViewProj;  // 深度バッファの点を世界へ戻す (ずらし込みの射影)
    float4 GrassBenders[8];  // 草を押し倒す球 xyz=中心 w=半径 (OutdoorDrawPod)
    float4 GrassBenderCount; // x=球の数
};

cbuffer CbWorldDraw : register(b5)
{
    uint4 WorldDraw;       // 草: x=最初の区画 y=1 区画の本数
};

Texture2D<float> g_terrainHeight : register(t12);

static const uint kGolden = 0x9e3779b9u;

// Scatter.hpp の hash32 (lowbias32) と同じ式
uint hash32(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

float hash01(uint x) { return (hash32(x) >> 8) * (1.0 / 16777216.0); }

float terrainSampleHeight(int2 p)
{
    p = clamp(p, int2(0, 0), int2(TerrainGrid.xy) - 1);
    return TerrainOrigin.y + g_terrainHeight.Load(int3(p, 0)) * TerrainOrigin.w;
}

// Heightfield::heightAt と同じ割り方 (対角 b-c の 2 枚の平面)
float terrainHeight(float2 xz)
{
    float2 f = clamp((xz - TerrainOrigin.xz) / TerrainGrid.zw, 0.0, TerrainGrid.xy - 1.0);
    int2 c = min(int2(f), int2(TerrainGrid.xy) - 2);
    float2 uv = f - c;
    float ha = terrainSampleHeight(c);
    float hb = terrainSampleHeight(c + int2(1, 0));
    float hc = terrainSampleHeight(c + int2(0, 1));
    if (uv.x + uv.y <= 1.0) { return ha + (hb - ha) * uv.x + (hc - ha) * uv.y; }
    float hd = terrainSampleHeight(c + int2(1, 1));
    return hd + (hc - hd) * (1.0 - uv.x) + (hb - hd) * (1.0 - uv.y);
}

// 中心差分の法線 (Heightfield::smoothNormalAt と同じ作り方)
float3 terrainNormal(float2 xz)
{
    float2 d = TerrainGrid.zw;
    float dx = (terrainHeight(xz + float2(d.x, 0)) - terrainHeight(xz - float2(d.x, 0))) / (2.0 * d.x);
    float dz = (terrainHeight(xz + float2(0, d.y)) - terrainHeight(xz - float2(0, d.y))) / (2.0 * d.y);
    return normalize(float3(-dx, 1.0, -dz));
}
)hlsl";

/// @brief DX12_DEFAULT_VS_3D と同じ形の VS 出力と、VS が読む定数
inline constexpr const char* DX12_WORLD_VS_HEAD_HLSL = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

cbuffer CbLightingHead : register(b1)
{
    float3 LightDirVs;    float _padVs0;
    float3 LightColorVs;  float _padVs1;
    float3 AmbientVs;     float _padVs2;
    float3 CameraPosVs;   float _padVs3;
};

cbuffer CbShadow : register(b3)
{
    float4x4 LightViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position      : SV_POSITION;
    float3 WorldPos      : TEXCOORD0;
    float3 WorldNorm     : TEXCOORD1;
    float2 TexCoord      : TEXCOORD2;
    float4 LightSpacePos : TEXCOORD3;
    float4 Color         : COLOR0;
};

VSOutput finishVertex(float3 worldPos, float3 normal, float2 uv, float4 color)
{
    VSOutput o;
    o.WorldPos = worldPos;
    o.WorldNorm = normal;
    o.Position = mul(Projection, mul(View, float4(worldPos, 1.0)));
    o.LightSpacePos = mul(LightViewProj, float4(worldPos, 1.0));
    o.TexCoord = uv;
    o.Color = color;
    return o;
}
)hlsl";

/// @brief 地形の VS。区画 1 つを 1 インスタンスとし、Position.xz は格子の番号 0..patchCells を表す
inline constexpr const char* DX12_TERRAIN_VS_HLSL = R"hlsl(
struct TerrainPatchGpu { float4 rect; float4 morph; };   // rect=x0,z0,sizeX,sizeZ morph=開始,終了,段,preSnap
StructuredBuffer<TerrainPatchGpu> g_terrainPatches : register(t34);

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID)
{
    TerrainPatchGpu p = g_terrainPatches[iid];
    float cells = TerrainPatch.x;
    float step = (p.morph.w > 0.5) ? 2.0 : 1.0;
    float2 g = input.Position.xz;
    g -= fmod(g, step);
    float2 spacing = p.rect.zw / cells;
    float2 xz0 = p.rect.xy + g * spacing;
    float dist = distance(float3(xz0.x, terrainHeight(xz0), xz0.y), CameraPosVs);
    float k = saturate((dist - p.morph.x) / max(p.morph.y - p.morph.x, 1e-3));
    g -= fmod(g, 2.0 * step) * k;
    float2 xz = clamp(p.rect.xy + g * spacing, TerrainOrigin.xz, TerrainOrigin.xz + TerrainSize.xy);
    float3 wp = float3(xz.x, terrainHeight(xz), xz.y);
    float2 uv = (xz - TerrainOrigin.xz) * TerrainSize.zw;
    return finishVertex(wp, terrainNormal(xz), uv, float4(1.0, 1.0, 1.0, 1.0));
}
)hlsl";

/// @brief 草の VS。草 1 本を 1 インスタンスとする。Position.x は横方向の -1..1、Position.y は根元からの割合 0..1 を表す
inline constexpr const char* DX12_GRASS_VS_HLSL = R"hlsl(
StructuredBuffer<float4> g_grassPatches : register(t34);   // x=x0 y=z0 z=区画の通し番号 (asfloat)
Texture2D<float> g_grassDensity : register(t31);

// DensityMap::sample と同じ: 角に画素の中心を合わせた双線形
float grassDensityAt(float2 uv)
{
    uint w, h;
    g_grassDensity.GetDimensions(w, h);
    float2 f = saturate(uv) * float2(w - 1, h - 1);
    int2 c = int2(f);
    int2 c1 = min(c + 1, int2(w, h) - 1);
    float2 t = f - c;
    float a = g_grassDensity.Load(int3(c, 0));
    float b = g_grassDensity.Load(int3(int2(c1.x, c.y), 0));
    float d = g_grassDensity.Load(int3(int2(c.x, c1.y), 0));
    float e = g_grassDensity.Load(int3(c1, 0));
    return lerp(lerp(a, b, t.x), lerp(d, e, t.x), t.y);
}

VSOutput hiddenVertex()
{
    VSOutput o = (VSOutput)0;
    o.Position = float4(2.0, 2.0, 2.0, 1.0);   // 視錐台の外 (3 頂点とも外なので三角形ごと捨てられる)
    return o;
}

// 球の中に立つ草を、球の中心から外へ倒す向き (長さは 0..1 の倒れ具合)
float3 benderPush(float3 root, float height)
{
    float3 push = float3(0.0, 0.0, 0.0);
    uint n = min((uint)GrassBenderCount.x, 8u);
    for (uint i = 0; i < n; ++i)
    {
        float4 b = GrassBenders[i];
        float r = max(b.w, 1e-3);
        float2 d = root.xz - b.xz;
        float len = length(d);
        float k = saturate(1.0 - len / r) * saturate(1.0 - abs(root.y - b.y) / (r + height));
        push += float3(d.x, 0.0, d.y) / max(len, 1e-3) * k;
    }
    float m = length(push);
    return m > 1.0 ? push / m : push;
}

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID)
{
    uint perPatch = max(WorldDraw.y, 1u);
    float4 patch = g_grassPatches[WorldDraw.x + iid / perPatch];
    uint blade = iid % perPatch;
    uint key = hash32(asuint(patch.z) * kGolden ^ hash32(blade + 0x632be5abu));
    float2 xz = patch.xy + float2(hash01(key), hash01(key ^ kGolden)) * GrassTip.w;
    float2 uv = (xz - TerrainOrigin.xz) * TerrainSize.zw;
    if (any(uv < 0.0) || any(uv > 1.0) || hash01(key ^ 0x68e31da4u) >= grassDensityAt(uv)) { return hiddenVertex(); }
    float3 n = terrainNormal(xz);
    if (n.y < GrassBase.w) { return hiddenVertex(); }
    float3 root = float3(xz.x, terrainHeight(xz), xz.y);
    float dist = distance(root, CameraPosVs);
    float fade = 1.0 - saturate((dist - GrassShape.z) / max(GrassShape.w - GrassShape.z, 1e-3));
    if (fade <= 0.0) { return hiddenVertex(); }

    float t = input.Position.y;
    float height = GrassShape.x * (0.6 + 0.8 * hash01(key ^ 0x2545f491u)) * fade;
    // 遠くは本数が減るので、1 本を太くして地面の見え方を保つ
    float width = GrassShape.y * (1.0 + 1.5 * saturate(dist / GrassShape.w));
    float yaw = hash01(key ^ 0x51ed270bu) * 6.2831853;
    float3 side = float3(cos(yaw), 0.0, sin(yaw));
    float sway = sin(TerrainPatch.w * GrassWind.w + dot(xz, GrassWind.xy) * 0.35 + hash01(key ^ 0x1b873593u) * 6.2831853);
    float3 bend = float3(GrassWind.x, 0.0, GrassWind.y) * (GrassWind.z * (0.6 + 0.4 * sway)) * (t * t);
    float3 push = benderPush(root, height);
    float lean = length(push);
    float3 wp = root + side * (input.Position.x * width * (1.0 - t)) + float3(0.0, height * t * (1.0 - 0.75 * lean), 0.0) +
                bend * fade + push * (height * t * t);
    float shade = 0.85 + 0.3 * hash01(key ^ 0x85ebca6bu);
    float3 color = lerp(GrassBase.rgb, GrassTip.rgb, t) * shade;
    return finishVertex(wp, n, float2(0.0, 0.0), float4(color, 1.0));
}
)hlsl";

/// @brief 地形の PS に置く層の合成。続く陰影本体の sampleAlbedo などをマクロで差し替える
inline constexpr const char* DX12_TERRAIN_SURFACE_HLSL = R"hlsl(
Texture2D g_splat0 : register(t13);
Texture2D g_splat1 : register(t14);
Texture2D g_layerAlbedo[8] : register(t15);
Texture2D g_layerNormal[8] : register(t23);

struct TerrainLayers { float w[8]; };

TerrainLayers terrainWeights(float2 uv)
{
    uint sw, sh;
    g_splat0.GetDimensions(sw, sh);
    float2 size = float2(sw, sh);
    float2 suv = (uv * (size - 1.0) + 0.5) / size;   // 地形の角に画素の中心を合わせる (OutdoorWorld::dominantLayerAt と同じ)
    float4 a = g_splat0.Sample(g_sampClamp, suv);
    float4 b = (TerrainPatch.z > 1.5) ? g_splat1.Sample(g_sampClamp, suv) : float4(0, 0, 0, 0);
    TerrainLayers l;
    l.w[0] = a.x; l.w[1] = a.y; l.w[2] = a.z; l.w[3] = a.w;
    l.w[4] = b.x; l.w[5] = b.y; l.w[6] = b.z; l.w[7] = b.w;
    float sum = 0.0;
    [unroll] for (int k = 0; k < 8; ++k) { l.w[k] = (k < (int)TerrainPatch.y) ? l.w[k] : 0.0; sum += l.w[k]; }
    if (sum < 1e-4) { l.w[0] = 1.0; sum = 1.0; }
    [unroll] for (int j = 0; j < 8; ++j) { l.w[j] /= sum; }
    return l;
}

float layerTile(int k) { return LayerTile[k / 4][k % 4]; }

float4 terrainAlbedo(PSInput input)
{
    TerrainLayers l = terrainWeights(input.TexCoord);
    float3 c = 0.0;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float3 tex = (LayerPbr[k].z > 0.5) ? g_layerAlbedo[k].Sample(g_samp, input.WorldPos.xz * layerTile(k)).rgb : 1.0;
        c += l.w[k] * tex * LayerColor[k].rgb;
    }
    return float4(c, 1.0);
}

// 高さマップの中心差分の法線に、層の法線マップを重みで混ぜる。接空間は x が +x、緑が -z (glTF と同じ向き)
float3 terrainShadingNormal(PSInput input)
{
    float3 N = terrainNormal(input.WorldPos.xz);
    TerrainLayers l = terrainWeights(input.TexCoord);
    float2 xy = 0.0;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        if (LayerPbr[k].w > 0.5)
        {
            float2 t = g_layerNormal[k].Sample(g_samp, input.WorldPos.xz * layerTile(k)).rg * 2.0 - 1.0;
            xy += l.w[k] * t * LayerColor[k].w;
        }
    }
    float3 T = normalize(float3(N.y, -N.x, 0.0));
    float3 B = cross(N, T);
    return normalize(T * xy.x + B * xy.y + N * sqrt(saturate(1.0 - dot(xy, xy))));
}

float3 terrainMetalRough(PSInput input)
{
    TerrainLayers l = terrainWeights(input.TexCoord);
    float2 mr = 0.0;
    [unroll] for (int k = 0; k < 8; ++k) { mr += l.w[k] * float2(LayerPbr[k].y, LayerPbr[k].x); }
    return float3(mr, 1.0);
}

#define sampleAlbedo(uv) terrainAlbedo(input)
#define perturbNormal(N, P, uv) terrainShadingNormal(input)
#define sampleMetalRough(uv) terrainMetalRough(input)
)hlsl";

/// @brief 水面の VS と PS。VS は SV_VertexID から頂点バッファなしで長方形を作る
inline constexpr const char* DX12_WATER_VS_HLSL = R"hlsl(
VSOutput VSMain(uint vid : SV_VertexID)
{
    static const float2 corner[6] = { float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 0), float2(0, 1), float2(1, 1) };
    float2 c = corner[vid];
    float2 xz = lerp(WaterRect.xy, WaterRect.zw, c);
    return finishVertex(float3(xz.x, WaterShallow.w, xz.y), float3(0, 1, 0), c, float4(1, 1, 1, 1));
}
)hlsl";

inline constexpr const char* DX12_WATER_PS_HLSL = R"hlsl(
Texture2D           g_sceneColor : register(t32);
Texture2DMS<float>  g_sceneDepth : register(t33);

float3 worldFromDepth(float2 pix, float depth)
{
    float2 ndc = float2(pix.x * ScreenSize.z * 2.0 - 1.0, 1.0 - pix.y * ScreenSize.w * 2.0);
    float4 p = mul(InvViewProj, float4(ndc, depth, 1.0));
    return p.xyz / p.w;
}

// 3 方向の正弦波の和の傾き。高さは変えない (法線だけ揺らす)
float3 waveNormal(float2 xz)
{
    float t = TerrainPatch.w * WaterWave.z;
    float s = WaterWave.y * 6.2831853;
    float2 d0 = float2(1.0, 0.2), d1 = float2(-0.4, 1.0), d2 = float2(0.7, -0.8);
    float2 g = d0 * cos(dot(d0, xz) * s + t * 1.3)
             + d1 * cos(dot(d1, xz) * s * 1.7 + t * 1.9) * 0.6
             + d2 * cos(dot(d2, xz) * s * 2.9 + t * 2.7) * 0.35;
    return normalize(float3(-g.x * WaterWave.x, 1.0, -g.y * WaterWave.x));
}

float band(float v, float bands) { return (bands >= 2.0) ? floor(v * bands + 0.5) / bands : v; }

float3 skyColor(float3 R)
{
    if (IblParams.x > 0.5) { return g_prefiltered.SampleLevel(g_sampClamp, R, 0).rgb * IblParams.y; }
    float up = saturate(R.y);
    float3 sky = (FogParams.z > 0.5) ? FogColor.rgb : AmbientSky.rgb * 2.0;
    return lerp(sky, AmbientSky.rgb * 1.6 + 0.05, up * up);
}

PSOutput PSMain(PSInput input)
{
    float2 pix = input.Position.xy;
    float3 V = normalize(CameraPos - input.WorldPos);
    float3 N = waveNormal(input.WorldPos.xz);
    float bands = WaterFoam.w;

    float sceneD = g_sceneDepth.Load(int2(pix), 0);
    float3 sceneP = worldFromDepth(pix, sceneD);
    bool open = sceneD >= 1.0;   // 水の下に何も無い
    float vdepth = open ? 1.0e3 : max(WaterShallow.w - sceneP.y, 0.0);
    float path = open ? 1.0e3 : distance(sceneP, input.WorldPos);

    // 屈折: 法線で画面をずらす。ずらした先が水面より手前の物なら、ずらさない
    float2 uv = pix * ScreenSize.zw;
    float2 ruv = uv + N.xz * WaterAbsorb.w * saturate(vdepth);
    float rd = g_sceneDepth.Load(int2(saturate(ruv) * ScreenSize.xy), 0);
    if (rd < input.Position.z) { ruv = uv; }
    float3 refr = g_sceneColor.SampleLevel(g_sampClamp, saturate(ruv), 0).rgb;

    float3 trans = exp(-WaterAbsorb.rgb * band(saturate(path / 30.0), bands) * 30.0);
    float3 body = lerp(WaterDeep.rgb, WaterShallow.rgb, band(exp(-vdepth * 0.35), bands));
    float3 L = normalize(-LightDir);
    float dist = length(CameraPos - input.WorldPos);
    float lit = sampleCascadedShadow(input.WorldPos, input.LightSpacePos, dist, 0.002);
    float3 light = LightColor * (0.35 + 0.65 * saturate(L.y) * lit) + AmbientSky.rgb;
    float3 under = refr * trans + body * light * (1.0 - trans);

    float fres = band(0.02 + 0.98 * pow(1.0 - saturate(dot(N, V)), 5.0), bands) * WaterWave.w;
    float3 R = reflect(-V, N);
    float3 color = lerp(under, skyColor(R), fres);
    float spec = pow(saturate(dot(R, L)), 400.0) * lit;
    color += LightColor * ((bands >= 2.0) ? step(0.5, spec) : spec) * 4.0;

    // 泡: 水の向こうの物が水面のすぐ下にある所 (岸と、水面を貫く物の周り)
    float edge = 1.0 - saturate(vdepth / max(WaterDeep.w, 1e-3));
    float ripple = 0.5 + 0.5 * sin(vdepth * 25.0 - TerrainPatch.w * 3.0 + hash01(asuint(floor(input.WorldPos.x * 3.0)) ^ asuint(floor(input.WorldPos.z * 3.0))) * 2.0);
    float foam = (bands >= 2.0) ? step(0.55, edge * (0.6 + 0.4 * ripple)) : saturate(edge * edge * (0.7 + 0.6 * ripple));
    color = lerp(color, WaterFoam.rgb * light, foam);

    color = applyFog(color, input.WorldPos);
    return packOutput(color, 1.0, float3(0.0, 1.0, 0.0), V);
}
)hlsl";

/// @brief 水面の深度だけを書く PS。色の後で深度を書き、後段のフォグ、ぼけ、動きベクトルに水面を反映する
inline constexpr const char* DX12_WATER_DEPTH_PS_HLSL = R"hlsl(
void PSMain() {}
)hlsl";

/// @brief 地形の PS の陰影
enum class WorldShade
{
	Toon,
	Phong,
	Pbr,
};

/// @brief 地形の PS。共通部、デカール、世界の宣言、層の合成マクロ、局所光 1 灯、走査、本体を連結する
[[nodiscard]] inline std::string dx12TerrainPixelShader(WorldShade shade)
{
	std::string src = DX12_LIT_COMMON_HLSL;
	src += DX12_DECAL_APPLY_HLSL;
	src += DX12_WORLD_COMMON_HLSL;
	src += DX12_TERRAIN_SURFACE_HLSL;
	switch (shade)
	{
	case WorldShade::Toon:
		src += DX12_LIT_TOON_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_TOON_MAIN_HLSL;
		break;
	case WorldShade::Phong:
		src += DX12_LIT_PHONG_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_PHONG_MAIN_HLSL;
		src += DX12_LIT_PHONG_OPAQUE_HLSL;
		break;
	case WorldShade::Pbr:
		src += DX12_LIT_PBR_SHADE_HLSL;
		src += DX12_LIT_LOCAL_LOOP_HLSL;
		src += DX12_LIT_PBR_MAIN_HLSL;
		break;
	}
	return src;
}

[[nodiscard]] inline std::string dx12TerrainVertexShader()
{
	return std::string(DX12_WORLD_VS_HEAD_HLSL) + DX12_WORLD_COMMON_HLSL + DX12_TERRAIN_VS_HLSL;
}

[[nodiscard]] inline std::string dx12GrassVertexShader()
{
	return std::string(DX12_WORLD_VS_HEAD_HLSL) + DX12_WORLD_COMMON_HLSL + DX12_GRASS_VS_HLSL;
}

[[nodiscard]] inline std::string dx12WaterVertexShader()
{
	return std::string(DX12_WORLD_VS_HEAD_HLSL) + DX12_WORLD_COMMON_HLSL + DX12_WATER_VS_HLSL;
}

[[nodiscard]] inline std::string dx12WaterPixelShader()
{
	return std::string(DX12_LIT_COMMON_HLSL) + DX12_WORLD_COMMON_HLSL + DX12_WATER_PS_HLSL;
}

} // namespace mitiru::render
