// 前方の 3D の描画。DX12 の DX12LitShaders.hpp (トゥーンの本体) と DX12ShaderModeVS.hpp の VS を、
// 局所光・間接光・マップを外して移したもの。影は DX12SunShadowShaders.hpp、輪郭線の下地 (法線と印) は
// DX12LitShaders.hpp の packOutput に合わせてある。Slang で書き、tools/gen_dawn_shaders.py が WGSL にする。
// 行列は列優先 (slangc -matrix-layout-column-major)。クリップ空間の深さは 0..1、前面は反時計回り。

struct FrameCB
{
    float4x4 viewProj;
    float4   cameraPos;      // xyz
    float4   lightDir;       // xyz = 光が進む向き
    float4   lightColor;     // rgb (強度込み、線形)
    float4   ambientSky;     // rgb (線形)
    float4   ambientGround;  // rgb (線形)
    float4   shadowTint;     // rgb (線形)
    float4   toonMidTint;    // rgb (線形)
    float4   toonParams;     // x = 段数 (0 は段なし) y = 境の幅 z = ハイライト強さ w = ハイライト指数
    float4   rimParams;      // rgb = 色 * 強さ w = 指数
    float4   fogColor;       // rgb (線形)
    float4   fogParams;      // x = 近 y = 遠 z = 有効
    float4   skyZenith;      // rgb (線形) w = 空を描くか
    float4   skyNadir;       // rgb (線形)
    float4   camRight;       // xyz * 画面の水平の半幅 (tan)
    float4   camUp;          // xyz * 画面の垂直の半幅 (tan)
    float4   camForward;     // xyz
    float4x4 shadowVP0;      // 太陽のカスケード 0 (近い)。光の view * proj
    float4x4 shadowVP1;      // カスケード 1 (アトラスの左列)
    float4x4 shadowVP2;      // カスケード 2 (アトラスの右列)
    float4   shadowC0;       // x = 影マップ 1 texel の世界の幅 y = 光の深さの 1 世界単位あたりの変化 (カスケード 0)
    float4   shadowC1;
    float4   shadowC2;
    float4   shadowSplit;    // x = 1 → 2 の距離 y = 2 → 3 の距離 z = PCF の広がり (texel) w = 余白 (影マップ深度の単位、0 は texel に比例)
    float4   shadowTexel;    // x = 1 / カスケード 0 の一辺 y = 1 / アトラスの 1 列の一辺 z = カスケードの数 (0 は影なし)
    float4   viewInfo;       // x = 線形の深度を詰める範囲 (カメラの遠方)
    float4   giOrigin;       // xyz = 格子の原点 w = 焼いた光を使うか
    float4   giSpacing;      // xyz = プローブの間隔 w = 強さ
    float4   giDims;         // xyz = 各軸のプローブの数 w = 距離の地図の 1 行のタイルの数
    float4   giParams;       // x = 面から浮かせる距離 y = トゥーンの段数 zw = 距離の地図の 1 画素 (uv)
    float4   fogA;           // 体積フォグ: x = 基準の密度 y = 高さの減り方 z = 基準の高さ w = 一様に足す密度
    float4   fogB;           // rgb = 散乱の割合 w = HG の g
    float4   fogC;           // rgb = 主光源の色 * 強さ * 倍率 w = 影を見るか
    float4   fogD;           // rgb = 環境光の平均 * 倍率 w = 奥の端 (世界単位)
    float4   ambientFlat;    // rgb = 半球を使わない環境光 (線形)
    float4   decalInfo;      // x = デカールの数
    float4   lightInfo;      // x = 局所光の数 y = 中間の色に掛ける倍率
    float4   fadeCamera;     // 透かし (OccluderFadeGpu と同じ並び): xyz = 目 w = 的の数 (0 は透かさない)
    float4   fadeParams;     // x = 中心で抜く割合 y = 縁の幅 z = 閾値のずらし
    float4   fadeTargets[4]; // xyz = 的 w = 半径
    float4   fadeCones[4];   // xyz = 目から的への単位ベクトル w = 円錐の半角の tan
    float4   fadeSilColor;   // 影絵の色 (線形) と濃さ
    float4   fadeSilParams;  // x = 影絵のために的を目の側へ寄せる距離
};

struct DrawCB
{
    float4x4 world;
    float4   diffuse;        // 線形、tint.mul 込み。a は不透明度
    float4   tintAdd;        // rgb = 照明の後に足す色
    float4   toonMaterial;   // rgb = 段付きハイライトの色 w = 指数の係数
    float4   rampParams;     // x = 段の数 (0 は場面の値) y = 境の半幅 z = 落ちた影で下げる段 w = 印
    float4   rampThresh;     // xyz = 段の境
    float4   tint0;
    float4   tint1;
    float4   tint2;
    float4   tint3;
    float4   accRim;         // rgb = 帯の色 w = N.V の上限
    float4   accGloss;       // rgb = つやの色 w = N.H の下限
    float4   accRimLimit;    // x = 帯にする N.L の上限 yz = 帯・つやの半幅 w = つやの強さ
    float4   metalA;         // xyz = amount, tint, shadowFollow w = bands
    float4   metalB;         // xy = horizonHeight, horizonSoft
    float4   metalSky;
    float4   metalHorizon;
    float4   metalGround;
    float4   misc;           // x = 1 なら輪郭線の検出から外す (outlineCaster = false) y = 1 なら透かしの的 (抜かない)
    float4   phong;          // rgb = 鏡面の色 (線形) w = 光沢度
    float4   mapFlags;       // x = 基本色の画像 y = 法線の画像 z = 自発光の画像 w = 抜きのしきい値 (0 は抜かない)
    float4   mapParams;      // x = 法線の強さ y = 1 なら最近傍で読む z = 1 なら基本色の画像は sRGB の値のまま (副ビューの出力)
    float4   emissiveFactor; // rgb = 自発光の係数
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameCB> frame;
[[vk::binding(1, 0)]] ConstantBuffer<DrawCB>  draw;
[[vk::binding(2, 0)]] DepthTexture2D          shadowNear;
[[vk::binding(3, 0)]] DepthTexture2D          shadowFar;
[[vk::binding(4, 0)]] SamplerComparisonState  shadowPcf;
[[vk::binding(5, 0)]] Texture2D<float4>       giProbes;        // 1 プローブは横に float4 7 個 (係数 27 個と有効かどうか)。256 個ずつで折り返す
[[vk::binding(6, 0)]] Texture2D<float2>       giVisibility;    // プローブから見た面までの距離の八面体の地図 (平均, 2 乗の平均)
[[vk::binding(7, 0)]] SamplerState            giSampler;
[[vk::binding(8, 0)]] Texture2D<float4>       decalTex;        // デカール 1 枚は横に float4 × 8 (DecalGpu の 128 byte)。縦に並べる
[[vk::binding(9, 0)]] Texture2DArray<float4>  vfxColor;        // 色の層 (sRGB)
[[vk::binding(10, 0)]] Texture2DArray<float4> vfxData;         // 法線の層 (そのまま読む)
[[vk::binding(11, 0)]] SamplerState           vfxSampler;
[[vk::binding(12, 0)]] Texture2D<float4>      localTex;        // 局所光 1 灯は横に float4 × 4 (LocalLightGpu の 64 byte)。縦に並べる
[[vk::binding(0, 1)]] Texture2DMS<float4>     fogLineDepth;    // 体積フォグのパスだけが使う (輪郭線の下地の距離)
// 材質の画像 (DawnMaterialMaps)。画像が無い材質には 1 画素の既定の画像が結んである。場面の描画のパスだけが使う
[[vk::binding(1, 1)]] Texture2D<float4>       matBase;
[[vk::binding(2, 1)]] Texture2D<float4>       matNormal;
[[vk::binding(3, 1)]] Texture2D<float4>       matEmissive;
[[vk::binding(4, 1)]] SamplerState            matSampler;
[[vk::binding(5, 1)]] SamplerState            matSamplerNearest;

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float4 color  : COLOR0;
};

struct VSOut
{
    float4 pos       : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float4 color     : TEXCOORD2;
    float  viewDepth : TEXCOORD3;
    float2 uv        : TEXCOORD4;
};

// 頂点色は書いた sRGB。1 を超える色は 1 に収めて線形にし、倍率を戻す (ColorSpace.hpp の linearRgb と同じ)
float3 srgbToLinear(float3 c)
{
    float m = max(max(c.r, c.g), max(c.b, 1.0));
    c /= m;
    float3 lo = c / 12.92;
    float3 hi = pow((max(c, float3(0.04045, 0.04045, 0.04045)) + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return float3(c.r <= 0.04045 ? lo.r : hi.r, c.g <= 0.04045 ? lo.g : hi.g, c.b <= 0.04045 ? lo.b : hi.b) * m;
}

// 非一様スケールでも面に垂直なまま運ぶため、3x3 の余因子行列を掛ける。鏡映は行列式の符号で戻す
float3 transformNormal(float3x3 m, float3 n)
{
    float3 c0 = cross(m[1], m[2]);
    float3 c1 = cross(m[2], m[0]);
    float3 c2 = cross(m[0], m[1]);
    float det = dot(m[0], c0);
    float3 r = float3(dot(c0, n), dot(c1, n), dot(c2, n));
    return normalize(r * (det < 0.0 ? -1.0 : 1.0));
}

[shader("vertex")]
VSOut vsMain(VSIn i)
{
    VSOut o;
    float4 wp = mul(draw.world, float4(i.pos, 1.0));
    o.worldPos = wp.xyz;
    float3x3 w3 = float3x3(draw.world[0].xyz, draw.world[1].xyz, draw.world[2].xyz);
    o.worldNorm = transformNormal(w3, i.normal);
    o.pos = mul(frame.viewProj, wp);
    o.viewDepth = o.pos.w;
    o.uv = i.uv;
    o.color = float4(srgbToLinear(i.color.rgb), i.color.a);
    return o;
}

#include "forward_toon.slang"

#include "forward_shadow.slang"

// 色が 1 で頭打ちの形式 (互換レベル) へ書く色。倍率で縮め、1 に近い所から色相を保ったまま丸める (成分ごとに頭打ちにすると白く抜ける)
float3 neutralShoulder(float3 c)
{
    const float knee = 0.9;
    const float d = 1.0 - knee;
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= knee) { return c; }
    float newPeak = 1.0 - d * d / (peak + d - knee);
    return c * (newPeak / peak);
}

// 10 bit の刻みで暗い所に縞が出るので、三角分布の細かい雑音 (±1 段) を足して崩す。MSAA の resolve で平均されて目立たない
float3 storeHdr(float3 c, float scale, float2 pixel)
{
    if (scale >= 1.0) { return c; }
    float n0 = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float n1 = frac(52.9829189 * frac(dot(pixel + float2(17.0, 31.0), float2(0.06711056, 0.00583715))));
    return neutralShoulder(c * scale) + float3(n0 + n1 - 1.0, n1 + n0 - 1.0, n0 - n1) * (1.0 / 1023.0);
}

// ── 輪郭線の下地 (DX12LitShaders.hpp の packOutput)。法線 RT の a は「描画の印 × 32 + N·V を 31 段」を 255 で割った値 ──

float packSlotNdv(float slot, float ndv)
{
    return (slot * 32.0 + round(saturate(ndv) * 31.0)) / 255.0;
}

// 互換レベルは深度を textureLoad できないので、線形の深度 (視点からの距離) を 24 bit に詰めて色で持つ。
// 8 bit ずつ別の色の成分に書くので、取り出しに丸めの誤差が出ない
float4 packLinearDepth(float viewDepth)
{
    float d = saturate(viewDepth / max(frame.viewInfo.x, 1e-3));
    uint v = uint(d * 16777215.0 + 0.5);
    return float4(float((v >> 16u) & 255u), float((v >> 8u) & 255u), float(v & 255u), 255.0) / 255.0;
}

struct FsOut
{
    float4 color  : SV_Target0;
    float4 normal : SV_Target1;
    float4 depth  : SV_Target2;
};

FsOut packOutput(float3 color, float alpha, float3 Ng, float3 V, float viewDepth, float2 pixel)
{
    FsOut o;
    o.color = float4(storeHdr(color, frame.lightInfo.y, pixel), alpha);
    o.normal = float4(Ng * 0.5 + 0.5, packSlotNdv(draw.rampParams.w, dot(Ng, V)));
    // outlineCaster = false の印。単位法線の * 0.5 + 0.5 は 3 成分そろって 1 にならないので見分けられる
    if (draw.misc.x > 0.5) { o.normal.rgb = float3(1.0, 1.0, 1.0); }
    o.depth = packLinearDepth(viewDepth);
    return o;
}

#include "forward_surface.slang"

#include "forward_fade.slang"

[shader("fragment")]
FsOut fsMain(VSOut i)
{
    float3 N;
    float3 V;
    float4 c = shadeScene(i, N, V);
    // 的を隠す面は網点で抜く。微分を使う処理の後で捨てる
    if (draw.misc.y < 0.5 && occluderFaded(i.pos.xy, i.worldPos, i.worldNorm)) { discard; }
    // 抜き (alphaMode Mask): 画像の a がしきい値に満たない所は描かない。微分を使う処理の後で捨てる
    if (draw.mapFlags.w > 0.0 && baseTexel(i.uv).a < draw.mapFlags.w) { discard; }
    return packOutput(c.rgb, c.a, N, V, i.viewDepth, i.pos.xy);
}

// 半透明の陰影。DX12 の半透明 (OIT) は、場面がトゥーンでも Phong で照らす (DX12_LIT_PHONG_OIT_HLSL の shadePhong)
float4 shadePhong(VSOut i)
{
    float3 Ng = normalize(i.worldNorm);
    float4 texel = baseTexel(i.uv);
    float3 N = perturbNormal(Ng, i.worldPos, i.uv);
    float3 L = normalize(-frame.lightDir.xyz);
    float3 V = normalize(frame.cameraPos.xyz - i.worldPos);
    float3 albedo = draw.diffuse.rgb * i.color.rgb * texel.rgb;
    applyDecals(i.worldPos, Ng, albedo, N);
    float alpha = draw.diffuse.a * i.color.a * texel.a;
    float castShadow = sampleCascadedShadow(i.worldPos, N, L, length(frame.cameraPos.xyz - i.worldPos));
    float3 ambient = frame.ambientFlat.rgb;
    if (frame.giOrigin.w > 0.5)
    {
        bool giOk;
        float3 e = giIrradiance(i.worldPos, N, V, giOk);
        if (giOk) { ambient = e * (frame.giSpacing.w / kPi); }
    }
    float NdotL = saturate(dot(N, L));
    float3 H = normalize(L + V);
    float specFactor = pow(saturate(dot(N, H)), max(draw.phong.w, 1.0)) * NdotL * castShadow;
    float3 color = ambient * albedo + frame.lightColor.rgb * albedo * NdotL * castShadow + frame.lightColor.rgb * draw.phong.rgb * specFactor;
    color += accumulateLocalLights(i.worldPos, albedo, N, V, true);
    color += sampleEmissive(i.uv) + draw.tintAdd.rgb;
    color = applyFog(color, i.worldPos);
    return float4(storeHdr(color, frame.lightInfo.y, i.pos.xy), alpha);
}

// 半透明。互換レベルは出力ごとに合成や書き込みを変えられないので、下地を書かない別のパスで色だけを書く
[shader("fragment")]
float4 fsMainColor(VSOut i) : SV_Target
{
    return shadePhong(i);
}

// 半透明の WBOIT の蓄積 (通常のレベル)。陰影と重みは DX12 の DX12_LIT_PHONG_OIT_HLSL と WeightedBlendedOIT の式と同じ。
// accum は (色 × alpha, alpha) × 重みを足し、reveal は alpha を (1 - alpha) の積にする (合成は oit.slang)
struct OitOut
{
    float4 accum  : SV_Target0;
    float  reveal : SV_Target1;
};

[shader("fragment")]
OitOut fsMainOit(VSOut i)
{
    float4 c = shadePhong(i);
    float w = clamp(pow(min(1.0, c.a * 10.0) + 0.01, 3.0) * 1e3 * pow(1.0 - i.pos.z * 0.9, 3.0), 1e-2, 3e3);
    OitOut o;
    o.accum = float4(c.rgb * c.a, c.a) * w;
    o.reveal = c.a;
    return o;
}

// ── 空 (縦のグラデーション)。cubemap の縦グラデーションと同じ: 真上と真下は単色、横は面の縦に線形 ──

struct SkyOut
{
    float4 pos : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
SkyOut vsSky(uint vid : SV_VertexID)
{
    SkyOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.ndc = t * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}

float srgbToLinearChannel(float x)
{
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

[shader("fragment")]
FsOut fsSky(SkyOut i)
{
    float3 dir = normalize(frame.camForward.xyz + i.ndc.x * frame.camRight.xyz + i.ndc.y * frame.camUp.xyz);
    float3 a = abs(dir);
    float side = max(a.x, a.z);
    float3 c;
    if (a.y >= side)
    {
        c = dir.y > 0.0 ? frame.skyZenith.rgb : frame.skyNadir.rgb;
    }
    else
    {
        float t = clamp(dir.y / side, -1.0, 1.0) * 0.5 + 0.5;
        c = lerp(frame.skyNadir.rgb, frame.skyZenith.rgb, t);
    }
    // c は書いた sRGB のまま混ぜた値。色を線形へ戻す
    c = float3(srgbToLinearChannel(c.r), srgbToLinearChannel(c.g), srgbToLinearChannel(c.b));
    // 下地は法線なし (0.5, 0.5, 0.5, 印 0)、距離は遠方 (24 bit がすべて 1)
    FsOut o;
    o.color = float4(storeHdr(c, frame.lightInfo.y, i.pos.xy), 1.0);
    o.normal = float4(0.5, 0.5, 0.5, 0.0);
    o.depth = float4(1.0, 1.0, 1.0, 1.0);
    return o;
}


// ── 体積フォグ (DX12 の DX12FogShaders.hpp の注入と積分を、画素ごとの数歩の積分に直した物) ──
// 奥行きは DX12 の froxel と同じ t^2 の切り方。出力は (散乱, 透過率) で、混ぜは dst * a + src

float fogShadowTap(bool nearMap, float4 clip, float column, float columns)
{
    float3 ndc = clip.xyz / max(clip.w, 1e-4);
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) { return 1.0; }
    uv.x = (uv.x + column) / columns;
    float depthRef = ndc.z - 0.002;
    return nearMap ? shadowNear.SampleCmpLevelZero(shadowPcf, uv, depthRef) : shadowFar.SampleCmpLevelZero(shadowPcf, uv, depthRef);
}

// 前方の影と同じ規則でカスケードを選ぶ。霧はぼけるので PCF はせず 1 タップ
float fogSunShadow(float3 wp, float dist)
{
    if (frame.fogC.w < 0.5 || frame.shadowTexel.z < 0.5) { return 1.0; }
    if (frame.shadowTexel.z < 1.5 || dist < frame.shadowSplit.x)
    {
        return fogShadowTap(true, mul(frame.shadowVP0, float4(wp, 1.0)), 0.0, 1.0);
    }
    if (frame.shadowTexel.z < 2.5 || dist < frame.shadowSplit.y)
    {
        return fogShadowTap(false, mul(frame.shadowVP1, float4(wp, 1.0)), 0.0, 2.0);
    }
    return fogShadowTap(false, mul(frame.shadowVP2, float4(wp, 1.0)), 1.0, 2.0);
}

float fogHg(float g, float c)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

[shader("fragment")]
float4 fsFog(SkyOut i) : SV_Target
{
    int2 pix = int2(i.pos.xy);
    float4 e = fogLineDepth.Load(pix, 0);
    uint v = (uint(e.r * 255.0 + 0.5) << 16u) | (uint(e.g * 255.0 + 0.5) << 8u) | uint(e.b * 255.0 + 0.5);
    float range = frame.fogD.w;
    // 何も描かれていない所 (空) は奥の端まで積む
    float dist = (v >= 16777215u) ? range : min(float(v) / 16777215.0 * frame.viewInfo.x, range);
    float tMax = sqrt(saturate(dist / max(range, 1e-4)));
    float3 dir = normalize(frame.camForward.xyz + i.ndc.x * frame.camRight.xyz + i.ndc.y * frame.camUp.xyz);
    float3 toSun = -normalize(frame.lightDir.xyz);
    float phase = fogHg(frame.fogB.w, dot(dir, toSun));
    float3 S = float3(0.0, 0.0, 0.0);
    float T = 1.0;
    const int kSteps = 24;
    for (int k = 0; k < kSteps; ++k)
    {
        float t0 = tMax * float(k) / float(kSteps);
        float t1 = tMax * float(k + 1) / float(kSteps);
        float d0 = range * t0 * t0;
        float d1 = range * t1 * t1;
        float dm = range * (0.5 * (t0 + t1)) * (0.5 * (t0 + t1));
        float3 wp = frame.cameraPos.xyz + dir * dm;
        float ext = max(frame.fogA.x * exp(-frame.fogA.y * (wp.y - frame.fogA.z)) + frame.fogA.w, 0.0);
        float3 light = frame.fogC.rgb * fogSunShadow(wp, dm) * phase + frame.fogD.rgb;
        float3 inject = frame.fogB.rgb * ext * light;
        float e2 = max(ext, 1e-6);
        float segT = exp(-e2 * (d1 - d0));
        S += T * (inject - inject * segT) / e2;
        T *= segT;
    }
    return float4(S * frame.lightInfo.y, T);
}
