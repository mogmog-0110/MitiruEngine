#pragma once

/// @file DX12AtmosphereShaders.hpp
/// @brief 物理ベースの空 (Hillaire 2020) の HLSL。透過率・多重散乱・空の見え方の LUT と、空気遠近の froxel
/// @details 式は Atmosphere.hpp の CPU の基準と同じ。長さは km、惑星の中心が原点、カメラは y 軸の上に置く。
///          LUT は太陽の放射照度 1 あたりの輝度で持ち、描くときに SunDir.w (sunIlluminance) を掛ける。
///          LUT の座標の取り方は Bruneton 2008 と Hillaire 2020 の論文の非線形の写像に従う。

namespace mitiru::render
{

inline constexpr const char* DX12_ATMOSPHERE_COMMON_HLSL = R"hlsl(
cbuffer CbAtmosphere : register(b0)
{
    float4x4 InvViewProj;        // ずらしを抜いた clip → world
    float4   CameraKm;           // xyz = 大気の座標でのカメラ (km) w = 世界の 1 単位の km
    float4   SunDir;             // xyz = 太陽への向き w = sunIlluminance
    float4   RayleighScattering; // xyz w = Rayleigh のスケールハイト
    float4   MieParams;          // x = 散乱 y = 消散 z = スケールハイト w = g
    float4   OzoneAbsorption;    // xyz w = オゾン層の中心
    float4   GroundAlbedo;       // xyz w = オゾン層の半分の厚み
    float4   Radii;              // x = 地表 y = 大気の上端 z = 太陽の円盤の半径 (rad) w = トゥーンの段数
    float4   ApParams;           // x = 空気遠近の奥の端 (km) y = 距離の倍率 z = 未使用 w = 空気遠近を掛けるか
    float4   CameraWorld;        // xyz = 世界のカメラ位置
    float4   Screen;             // xy = 内部解像度 zw = その逆数
};

SamplerState s_linear : register(s0);

static const float PI = 3.14159265358979;
static const uint2 kTransmittanceSize = uint2(256, 64);
static const uint2 kMultiScatterSize = uint2(32, 32);
static const uint2 kSkyViewSize = uint2(192, 108);
static const uint  kApSlices = 32;

float3 extinctionAt(float h)
{
    float r = exp(-h / RayleighScattering.w);
    float m = exp(-h / MieParams.z);
    float o = max(0.0, 1.0 - abs(h - OzoneAbsorption.w) / GroundAlbedo.w);
    return RayleighScattering.xyz * r + MieParams.y * m + OzoneAbsorption.xyz * o;
}

float3 scatteringRayleigh(float h) { return RayleighScattering.xyz * exp(-h / RayleighScattering.w); }
float  scatteringMie(float h) { return MieParams.x * exp(-h / MieParams.z); }

float rayleighPhase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

float miePhase(float g, float c)
{
    float g2 = g * g;
    return 3.0 * (1.0 - g2) * (1.0 + c * c) / (8.0 * PI * (2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// 半径 r・天頂の余弦 mu から半径 R の球までの距離。当たらなければ -1
float distanceToSphere(float r, float mu, float R)
{
    float disc = r * r * (mu * mu - 1.0) + R * R;
    if (disc < 0.0) { return -1.0; }
    float s = sqrt(disc);
    float nearT = -r * mu - s;
    return (nearT > 0.0) ? nearT : (-r * mu + s);
}

bool hitsGround(float r, float mu)
{
    return mu < 0.0 && r * r * (mu * mu - 1.0) + Radii.x * Radii.x >= 0.0;
}

// 透過率 LUT の座標 (Bruneton 2008 の写像)
float2 transmittanceUv(float r, float mu)
{
    float H = sqrt(Radii.y * Radii.y - Radii.x * Radii.x);
    float rho = sqrt(max(r * r - Radii.x * Radii.x, 0.0));
    float disc = r * r * (mu * mu - 1.0) + Radii.y * Radii.y;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dMin = Radii.y - r;
    float dMax = rho + H;
    return float2((d - dMin) / max(dMax - dMin, 1e-6), rho / H);
}

void transmittanceRMu(float2 uv, out float r, out float mu)
{
    float H = sqrt(Radii.y * Radii.y - Radii.x * Radii.x);
    float rho = H * uv.y;
    r = sqrt(rho * rho + Radii.x * Radii.x);
    float dMin = Radii.y - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

float2 multiScatterUv(float r, float cosSun)
{
    return float2(saturate(cosSun * 0.5 + 0.5), saturate((r - Radii.x) / (Radii.y - Radii.x)));
}

float3 cameraUp() { return normalize(CameraKm.xyz); }

// 画面の uv (0..1、y は下) から世界の視線
float3 viewRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 a = mul(InvViewProj, float4(ndc, 0.0, 1.0));
    float4 b = mul(InvViewProj, float4(ndc, 1.0, 1.0));
    return normalize(b.xyz / b.w - a.xyz / a.w);
}

// 空の見え方の LUT の座標。地平線の近くを細かくとる (Hillaire 2020 の 5.3)
float2 skyViewUv(float r, float3 dir)
{
    float vHorizon = sqrt(max(r * r - Radii.x * Radii.x, 0.0));
    float beta = acos(clamp(vHorizon / r, -1.0, 1.0));
    float zenithHorizon = PI - beta;
    float viewZenith = acos(clamp(dot(dir, cameraUp()), -1.0, 1.0));
    float v;
    if (viewZenith < zenithHorizon)
    {
        float c = 1.0 - sqrt(1.0 - viewZenith / zenithHorizon);
        v = c * 0.5;
    }
    else
    {
        float c = sqrt(saturate((viewZenith - zenithHorizon) / beta));
        v = c * 0.5 + 0.5;
    }
    float2 dh = dir.xz;
    float2 sh = SunDir.xz;
    float az = 0.0;
    if (dot(dh, dh) > 1e-8 && dot(sh, sh) > 1e-8)
    {
        az = acos(clamp(dot(normalize(dh), normalize(sh)), -1.0, 1.0));
    }
    return float2(sqrt(az / PI), v);
}

// 位置 pos (km、惑星の中心が原点) から dir に distKm 進む間の散乱と透過率。Ms は多重散乱の LUT
struct ScatterResult
{
    float3 inscatter;
    float3 transmittance;
};
)hlsl";

/// @brief 光線に沿って散乱を積む。透過率と多重散乱の LUT を読む (Hillaire 2020 の式 1〜6)
inline constexpr const char* DX12_ATMOSPHERE_MARCH_HLSL = R"hlsl(
Texture2D<float4> g_transmittance : register(t0);
Texture2D<float4> g_multiScatter  : register(t1);

float3 sampleTransmittance(float r, float mu)
{
    return g_transmittance.SampleLevel(s_linear, transmittanceUv(r, mu), 0).rgb;
}

// stopAtGround = false は空気遠近の froxel 用。場面の物の手前まで積むので、惑星の地面で止めない (地面より下は地表の値で読む)
ScatterResult integrateScattering(float3 pos, float3 dir, float maxKm, int steps, bool multi, bool stopAtGround)
{
    ScatterResult res;
    res.inscatter = 0.0;
    res.transmittance = 1.0;
    float r = length(pos);
    float mu = dot(pos, dir) / r;
    float tAtmo = distanceToSphere(r, mu, Radii.y);
    float tGround = (stopAtGround && hitsGround(r, mu)) ? distanceToSphere(r, mu, Radii.x) : -1.0;
    float tMax = (tGround > 0.0) ? tGround : tAtmo;
    tMax = min(tMax, maxKm);
    if (!(tMax > 0.0)) { return res; }
    float c = dot(dir, SunDir.xyz);
    float pr = rayleighPhase(c);
    float pm = miePhase(MieParams.w, c);
    float dt = tMax / steps;
    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float t = (i + 0.5) * dt;
        float3 p = pos + dir * t;
        float3 up = normalize(p);
        float pr2 = max(length(p), Radii.x + 1e-3);
        float h = pr2 - Radii.x;
        float sunMu = dot(up, SunDir.xyz);
        float3 sunT = hitsGround(pr2, sunMu) ? 0.0 : sampleTransmittance(pr2, sunMu);
        float3 sR = scatteringRayleigh(h);
        float  sM = scatteringMie(h);
        float3 S = sunT * (sR * pr + sM * pm);
        if (multi)
        {
            float3 ms = g_multiScatter.SampleLevel(s_linear, multiScatterUv(pr2, sunMu), 0).rgb;
            S += (sR + sM) * ms;
        }
        float3 ext = max(extinctionAt(h), 1e-7);
        float3 segT = exp(-ext * dt);
        // 区間の中で積分した散乱 (Hillaire 2015 のエネルギーを保つ式)
        res.inscatter += res.transmittance * (S - S * segT) / ext;
        res.transmittance *= segT;
    }
    return res;
}
)hlsl";

/// @brief 透過率 LUT (256x64)。大気の上端までの透過率
inline constexpr const char* DX12_ATMOSPHERE_TRANSMITTANCE_CS = R"hlsl(
RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kTransmittanceSize)) { return; }
    float2 uv = (id.xy + 0.5) / float2(kTransmittanceSize);
    float r, mu;
    transmittanceRMu(uv, r, mu);
    float t = distanceToSphere(r, mu, Radii.y);
    const int kSteps = 40;
    float dt = max(t, 0.0) / kSteps;
    float3 optical = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        float s = (i + 0.5) * dt;
        float rr = sqrt(r * r + s * s + 2.0 * r * mu * s);
        optical += extinctionAt(rr - Radii.x) * dt;
    }
    g_out[id.xy] = float4(exp(-optical), 1.0);
}
)hlsl";

/// @brief 多重散乱 LUT (32x32)。等方の 2 次の散乱を 64 方向で平均し、級数 1 / (1 - f) で無限次へ伸ばす
inline constexpr const char* DX12_ATMOSPHERE_MULTISCATTER_CS = R"hlsl(
RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kMultiScatterSize)) { return; }
    float2 uv = (id.xy + 0.5) / float2(kMultiScatterSize);
    float cosSun = uv.x * 2.0 - 1.0;
    float r = Radii.x + uv.y * (Radii.y - Radii.x);
    float3 pos = float3(0.0, max(r, Radii.x + 0.01), 0.0);
    float3 sun = float3(sqrt(saturate(1.0 - cosSun * cosSun)), cosSun, 0.0);
    float3 L2 = 0.0;
    float3 fms = 0.0;
    const int kDirs = 8;
    [loop]
    for (int a = 0; a < kDirs; ++a)
    {
        [loop]
        for (int b = 0; b < kDirs; ++b)
        {
            float u = (a + 0.5) / kDirs;
            float v = (b + 0.5) / kDirs;
            float cosT = 1.0 - 2.0 * u;
            float sinT = sqrt(saturate(1.0 - cosT * cosT));
            float phi = 2.0 * PI * v;
            float3 dir = float3(sinT * cos(phi), cosT, sinT * sin(phi));
            float rr = length(pos);
            float mu = dot(pos, dir) / rr;
            float tGround = hitsGround(rr, mu) ? distanceToSphere(rr, mu, Radii.x) : -1.0;
            float tMax = (tGround > 0.0) ? tGround : distanceToSphere(rr, mu, Radii.y);
            const int kSteps = 20;
            float dt = max(tMax, 0.0) / kSteps;
            float3 T = 1.0;
            float3 L = 0.0;
            float3 F = 0.0;
            for (int i = 0; i < kSteps; ++i)
            {
                float3 p = pos + dir * ((i + 0.5) * dt);
                float pr2 = length(p);
                float h = pr2 - Radii.x;
                float sunMu = dot(p / pr2, sun);
                float3 sunT = hitsGround(pr2, sunMu) ? 0.0 : sampleTransmittance(pr2, sunMu);
                float3 scat = scatteringRayleigh(h) + scatteringMie(h);
                float3 ext = max(extinctionAt(h), 1e-7);
                float3 segT = exp(-ext * dt);
                float3 S = scat * sunT / (4.0 * PI);
                L += T * (S - S * segT) / ext;
                F += T * (scat - scat * segT) / ext;
                T *= segT;
            }
            if (tGround > 0.0)
            {
                float3 gp = pos + dir * tGround;
                float gr = length(gp);
                float gmu = dot(gp / gr, sun);
                L += T * sampleTransmittance(gr, max(gmu, 0.0)) * saturate(gmu) * GroundAlbedo.xyz / PI;
            }
            L2 += L;
            fms += F;
        }
    }
    // 球面の積分に等方の位相 1/(4π) を掛けると、64 方向の平均になる
    float n = kDirs * kDirs;
    L2 /= n;
    fms /= n;
    g_out[id.xy] = float4(L2 / max(1.0 - fms, 1e-3), 1.0);
}
)hlsl";

/// @brief 空の見え方の LUT (192x108)。毎フレーム、カメラの高さと太陽の向きで作り直す
inline constexpr const char* DX12_ATMOSPHERE_SKYVIEW_CS = R"hlsl(
RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kSkyViewSize)) { return; }
    float2 uv = (id.xy + 0.5) / float2(kSkyViewSize);
    float r = length(CameraKm.xyz);
    float vHorizon = sqrt(max(r * r - Radii.x * Radii.x, 0.0));
    float beta = acos(clamp(vHorizon / r, -1.0, 1.0));
    float zenithHorizon = PI - beta;
    float viewZenith;
    if (uv.y < 0.5)
    {
        float c = 1.0 - uv.y * 2.0;
        viewZenith = zenithHorizon * (1.0 - c * c);
    }
    else
    {
        float c = uv.y * 2.0 - 1.0;
        viewZenith = zenithHorizon + beta * c * c;
    }
    float az = uv.x * uv.x * PI;
    // 太陽の水平の向きを基準に、方位 az だけ回した視線
    float2 sh = SunDir.xz;
    sh = (dot(sh, sh) > 1e-8) ? normalize(sh) : float2(1.0, 0.0);
    float2 rot = float2(sh.x * cos(az) - sh.y * sin(az), sh.x * sin(az) + sh.y * cos(az));
    float sz = sin(viewZenith);
    float3 dir = float3(rot.x * sz, cos(viewZenith), rot.y * sz);
    ScatterResult s = integrateScattering(float3(0.0, r, 0.0), dir, 1.0e9, 30, true, true);
    g_out[id.xy] = float4(s.inscatter, 1.0);
}
)hlsl";

/// @brief 空気遠近の froxel (32x32x32)。rgb = カメラから froxel までの散乱、a = 平均の透過率
inline constexpr const char* DX12_ATMOSPHERE_AERIAL_CS = R"hlsl(
RWTexture3D<float4> g_out : register(u0);

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= kApSlices)) { return; }
    float2 uv = (id.xy + 0.5) / kApSlices;
    float w = (id.z + 0.5) / kApSlices;
    float distKm = ApParams.x * w * w;
    float3 dir = viewRay(uv);
    float3 pos = CameraKm.xyz;
    ScatterResult s = integrateScattering(pos, dir, distKm, max((int)id.z + 2, 4), true, false);
    g_out[id] = float4(s.inscatter, dot(s.transmittance, 1.0 / 3.0));
}
)hlsl";

/// @brief トゥーンの段。明るさを x / (x + ref) で 0..1 に寄せてから段に切り、色相はそのまま残す
inline constexpr const char* DX12_ATMOSPHERE_TOON_HLSL = R"hlsl(
float3 toonBand(float3 c, float bands, float ref)
{
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    if (bands < 1.5 || l <= 1e-6) { return c; }
    float x = l / (l + ref);
    float xq = min(round(x * bands) / bands, 0.999);
    return c * (ref * xq / (1.0 - xq) / l);
}
)hlsl";

/// @brief 空を主パスの深度 1 の所へ描く。VS は z = 1 のフルスクリーン三角形
inline constexpr const char* DX12_SKY_DRAW_HLSL = R"hlsl(
Texture2D<float4> g_skyView : register(t2);

struct SkyVSOut
{
    float4 Position : SV_POSITION;
};

SkyVSOut VSMain(uint vid : SV_VertexID)
{
    SkyVSOut o;
    float2 t = float2((vid << 1) & 2, vid & 2);
    o.Position = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
    return o;
}

float4 PSMain(SkyVSOut i) : SV_TARGET
{
    float2 uv = i.Position.xy * Screen.zw;
    float3 dir = viewRay(uv);
    float r = length(CameraKm.xyz);
    float3 sky = g_skyView.SampleLevel(s_linear, skyViewUv(r, dir), 0).rgb * SunDir.w;
    float bands = Radii.w;
    if (Radii.z > 0.0)
    {
        float c = dot(dir, SunDir.xyz);
        float edge = cos(Radii.z);
        float inner = cos(Radii.z * ((bands > 1.5) ? 1.0 : 0.7));
        float disk = (bands > 1.5) ? step(edge, c) : smoothstep(edge, inner, c);
        float mu = dot(dir, cameraUp());
        float3 t = hitsGround(r, mu) ? 0.0 : sampleTransmittance(r, mu);
        sky += t * SunDir.w * 4.0 * disk;
    }
    sky = toonBand(sky, bands, SunDir.w * 0.04);
    return float4(sky, 1.0);
}
)hlsl";

} // namespace mitiru::render
