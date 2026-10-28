// 物理ベースの空 (Hillaire 2020)。DX12 の DX12AtmosphereShaders.hpp の HLSL を、compute から fragment のパスへ直した物。
// 互換レベルでは書き込み用のテクスチャ (storage) に頼れないので、透過率・多重散乱・空の見え方の LUT は描画先へ描く。
// 長さは km、惑星の中心が原点、カメラは y 軸の上。LUT は太陽の放射照度 1 あたりの輝度で、描くときに sunDir.w を掛ける。
// 空気遠近と体積フォグはまだ無い。

struct SkyCB
{
    float4 cameraKm;           // xyz = 大気の座標でのカメラ (km) w = 世界の 1 単位の km
    float4 sunDir;             // xyz = 太陽への向き w = sunIlluminance
    float4 rayleigh;           // xyz w = Rayleigh のスケールハイト
    float4 mie;                // x = 散乱 y = 消散 z = スケールハイト w = g
    float4 ozone;              // xyz w = オゾン層の中心
    float4 ground;             // xyz = 地表の反射 w = オゾン層の半分の厚み
    float4 radii;              // x = 地表 y = 大気の上端 z = 太陽の円盤の半径 (rad) w = トゥーンの段数
    float4 camForward;         // w = HDR の絵が 1 で頭打ちの形式 (互換レベル) のとき色に掛ける倍率 (0 は掛けない)。画面の端の向き = forward + ndc.x * right + ndc.y * up (right と up は tan で伸ばしてある)
    float4 camRight;           // w = 空気遠近の距離の倍率
    float4 camUp;              // w = 輪郭線の下地の距離を詰めた範囲 (カメラの遠方)
    float4 cloud;              // x = 雲の量 0..1 (0 は雲なし) y = 動きの時刻 (秒、決定論のフレーム番号から)
};

[[vk::binding(0, 0)]] ConstantBuffer<SkyCB> sky;
[[vk::binding(1, 0)]] SamplerState linearClamp;
[[vk::binding(2, 0)]] Texture2D<float4> transmittanceTex;
[[vk::binding(3, 0)]] Texture2D<float4> multiScatterTex;
[[vk::binding(4, 0)]] Texture2D<float4> skyViewTex;
[[vk::binding(5, 0)]] Texture2DMS<float4> lineDepthMs;   // 空気遠近のパスだけが使う (forward.slang の packLinearDepth)

static const float PI = 3.14159265358979;
static const float2 kTransmittanceSize = float2(256.0, 64.0);
static const float2 kMultiScatterSize = float2(32.0, 32.0);
static const float2 kSkyViewSize = float2(192.0, 108.0);

float3 extinctionAt(float h)
{
    float r = exp(-h / sky.rayleigh.w);
    float m = exp(-h / sky.mie.z);
    float o = max(0.0, 1.0 - abs(h - sky.ozone.w) / sky.ground.w);
    return sky.rayleigh.xyz * r + float3(sky.mie.y * m, sky.mie.y * m, sky.mie.y * m) + sky.ozone.xyz * o;
}

float3 scatteringRayleigh(float h) { return sky.rayleigh.xyz * exp(-h / sky.rayleigh.w); }
float scatteringMie(float h) { return sky.mie.x * exp(-h / sky.mie.z); }
float rayleighPhase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }

float miePhase(float g, float c)
{
    float g2 = g * g;
    return 3.0 * (1.0 - g2) * (1.0 + c * c) / (8.0 * PI * (2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

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
    return mu < 0.0 && r * r * (mu * mu - 1.0) + sky.radii.x * sky.radii.x >= 0.0;
}

float2 transmittanceUv(float r, float mu)
{
    float H = sqrt(sky.radii.y * sky.radii.y - sky.radii.x * sky.radii.x);
    float rho = sqrt(max(r * r - sky.radii.x * sky.radii.x, 0.0));
    float disc = r * r * (mu * mu - 1.0) + sky.radii.y * sky.radii.y;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dMin = sky.radii.y - r;
    float dMax = rho + H;
    return float2((d - dMin) / max(dMax - dMin, 1e-6), rho / H);
}

void transmittanceRMu(float2 uv, out float r, out float mu)
{
    float H = sqrt(sky.radii.y * sky.radii.y - sky.radii.x * sky.radii.x);
    float rho = H * uv.y;
    r = sqrt(rho * rho + sky.radii.x * sky.radii.x);
    float dMin = sky.radii.y - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

float2 multiScatterUv(float r, float cosSun)
{
    return float2(saturate(cosSun * 0.5 + 0.5), saturate((r - sky.radii.x) / (sky.radii.y - sky.radii.x)));
}

float3 cameraUp() { return normalize(sky.cameraKm.xyz); }

float2 skyViewUv(float r, float3 dir)
{
    float vHorizon = sqrt(max(r * r - sky.radii.x * sky.radii.x, 0.0));
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
    float2 sh = sky.sunDir.xz;
    float az = 0.0;
    if (dot(dh, dh) > 1e-8 && dot(sh, sh) > 1e-8)
    {
        az = acos(clamp(dot(normalize(dh), normalize(sh)), -1.0, 1.0));
    }
    return float2(sqrt(az / PI), v);
}

float3 sampleTransmittance(float r, float mu)
{
    return transmittanceTex.SampleLevel(linearClamp, transmittanceUv(r, mu), 0.0).rgb;
}

struct ScatterResult
{
    float3 inscatter;
    float3 transmittance;
};

ScatterResult integrateScattering(float3 pos, float3 dir, float maxKm, int steps, bool multi, bool stopAtGround)
{
    ScatterResult res;
    res.inscatter = float3(0.0, 0.0, 0.0);
    res.transmittance = float3(1.0, 1.0, 1.0);
    float r = length(pos);
    float mu = dot(pos, dir) / r;
    float tAtmo = distanceToSphere(r, mu, sky.radii.y);
    float tGround = (stopAtGround && hitsGround(r, mu)) ? distanceToSphere(r, mu, sky.radii.x) : -1.0;
    float tMax = (tGround > 0.0) ? tGround : tAtmo;
    tMax = min(tMax, maxKm);
    if (!(tMax > 0.0)) { return res; }
    float c = dot(dir, sky.sunDir.xyz);
    float pr = rayleighPhase(c);
    float pm = miePhase(sky.mie.w, c);
    float dt = tMax / float(steps);
    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float t = (float(i) + 0.5) * dt;
        float3 p = pos + dir * t;
        float3 up = normalize(p);
        float pr2 = max(length(p), sky.radii.x + 1e-3);
        float h = pr2 - sky.radii.x;
        float sunMu = dot(up, sky.sunDir.xyz);
        float3 sunT = hitsGround(pr2, sunMu) ? float3(0.0, 0.0, 0.0) : sampleTransmittance(pr2, sunMu);
        float3 sR = scatteringRayleigh(h);
        float sM = scatteringMie(h);
        float3 S = sunT * (sR * pr + float3(sM * pm, sM * pm, sM * pm));
        if (multi)
        {
            float3 ms = multiScatterTex.SampleLevel(linearClamp, multiScatterUv(pr2, sunMu), 0.0).rgb;
            S += (sR + float3(sM, sM, sM)) * ms;
        }
        float3 ext = max(extinctionAt(h), float3(1e-7, 1e-7, 1e-7));
        float3 segT = exp(-ext * dt);
        res.inscatter += res.transmittance * (S - S * segT) / ext;
        res.transmittance *= segT;
    }
    return res;
}

struct FullOut
{
    float4 pos : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
FullOut vsFull(uint vid : SV_VertexID)
{
    FullOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.ndc = t * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fsTransmittance(FullOut i) : SV_Target
{
    float2 uv = (floor(i.pos.xy) + 0.5) / kTransmittanceSize;
    float r;
    float mu;
    transmittanceRMu(uv, r, mu);
    float t = distanceToSphere(r, mu, sky.radii.y);
    const int kSteps = 40;
    float dt = max(t, 0.0) / float(kSteps);
    float3 optical = float3(0.0, 0.0, 0.0);
    for (int k = 0; k < kSteps; ++k)
    {
        float s = (float(k) + 0.5) * dt;
        float rr = sqrt(r * r + s * s + 2.0 * r * mu * s);
        optical += extinctionAt(rr - sky.radii.x) * dt;
    }
    return float4(exp(-optical), 1.0);
}

[shader("fragment")]
float4 fsMultiScatter(FullOut i) : SV_Target
{
    float2 uv = (floor(i.pos.xy) + 0.5) / kMultiScatterSize;
    float cosSun = uv.x * 2.0 - 1.0;
    float r = sky.radii.x + uv.y * (sky.radii.y - sky.radii.x);
    float3 pos = float3(0.0, max(r, sky.radii.x + 0.01), 0.0);
    float3 sun = float3(sqrt(saturate(1.0 - cosSun * cosSun)), cosSun, 0.0);
    float3 L2 = float3(0.0, 0.0, 0.0);
    float3 fms = float3(0.0, 0.0, 0.0);
    const int kDirs = 8;
    [loop]
    for (int a = 0; a < kDirs; ++a)
    {
        [loop]
        for (int b = 0; b < kDirs; ++b)
        {
            float u = (float(a) + 0.5) / float(kDirs);
            float v = (float(b) + 0.5) / float(kDirs);
            float cosT = 1.0 - 2.0 * u;
            float sinT = sqrt(saturate(1.0 - cosT * cosT));
            float phi = 2.0 * PI * v;
            float3 dir = float3(sinT * cos(phi), cosT, sinT * sin(phi));
            float rr = length(pos);
            float mu = dot(pos, dir) / rr;
            float tGround = hitsGround(rr, mu) ? distanceToSphere(rr, mu, sky.radii.x) : -1.0;
            float tMax = (tGround > 0.0) ? tGround : distanceToSphere(rr, mu, sky.radii.y);
            const int kSteps = 20;
            float dt = max(tMax, 0.0) / float(kSteps);
            float3 T = float3(1.0, 1.0, 1.0);
            float3 L = float3(0.0, 0.0, 0.0);
            float3 F = float3(0.0, 0.0, 0.0);
            for (int k = 0; k < kSteps; ++k)
            {
                float3 p = pos + dir * ((float(k) + 0.5) * dt);
                float pr2 = length(p);
                float h = pr2 - sky.radii.x;
                float sunMu = dot(p / pr2, sun);
                float3 sunT = hitsGround(pr2, sunMu) ? float3(0.0, 0.0, 0.0) : sampleTransmittance(pr2, sunMu);
                float sM = scatteringMie(h);
                float3 scat = scatteringRayleigh(h) + float3(sM, sM, sM);
                float3 ext = max(extinctionAt(h), float3(1e-7, 1e-7, 1e-7));
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
                L += T * sampleTransmittance(gr, max(gmu, 0.0)) * saturate(gmu) * sky.ground.xyz / PI;
            }
            L2 += L;
            fms += F;
        }
    }
    float n = float(kDirs * kDirs);
    L2 /= n;
    fms /= n;
    return float4(L2 / max(float3(1.0, 1.0, 1.0) - fms, float3(1e-3, 1e-3, 1e-3)), 1.0);
}

[shader("fragment")]
float4 fsSkyView(FullOut i) : SV_Target
{
    float2 uv = (floor(i.pos.xy) + 0.5) / kSkyViewSize;
    float r = length(sky.cameraKm.xyz);
    float vHorizon = sqrt(max(r * r - sky.radii.x * sky.radii.x, 0.0));
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
    float2 sh = sky.sunDir.xz;
    sh = (dot(sh, sh) > 1e-8) ? normalize(sh) : float2(1.0, 0.0);
    float2 rot = float2(sh.x * cos(az) - sh.y * sin(az), sh.x * sin(az) + sh.y * cos(az));
    float sz = sin(viewZenith);
    float3 dir = float3(rot.x * sz, cos(viewZenith), rot.y * sz);
    ScatterResult s = integrateScattering(float3(0.0, r, 0.0), dir, 1.0e9, 30, true, true);
    return float4(s.inscatter, 1.0);
}

// 明るさを x / (x + ref) で 0..1 に寄せてから段に切り、色相はそのまま残す
float3 toonBand(float3 c, float bands, float refL)
{
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    if (bands < 1.5 || l <= 1e-6) { return c; }
    float x = l / (l + refL);
    float xq = min(round(x * bands) / bands, 0.999);
    return c * (refL * xq / (1.0 - xq) / l);
}

// post.slang のトーンマップの肩。HDR が 1 で頭打ちになる形式では、頭打ちの前に空の明るさをここで丸める
float3 neutralShoulder(float3 c)
{
    const float knee = 0.9;
    const float d = 1.0 - knee;
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= knee) { return c; }
    float newPeak = 1.0 - d * d / (peak + d - knee);
    return c * (newPeak / peak);
}

struct SkyOut
{
    float4 color  : SV_Target0;
    float4 normal : SV_Target1;
    float4 depth  : SV_Target2;
};

// 雲の層 (R5)。天球の上に平らな層を一枚置き、視線がその層を貫く点を fbm の値ノイズで塗る。テクスチャは持たない。
// 整数のハッシュなので、どの GPU でも同じ値になる。DX12 の DX12AtmosphereShaders.hpp に同じ式がある (どちらかを直したら両方を直す)
float cloudHash(int2 p)
{
    uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return float(h ^ (h >> 16u)) * (1.0 / 4294967296.0);
}

float cloudNoise(float2 p)
{
    float2 f = frac(p);
    int2 i = int2(floor(p));
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = cloudHash(i);
    float b = cloudHash(i + int2(1, 0));
    float c = cloudHash(i + int2(0, 1));
    float d = cloudHash(i + int2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float cloudFbm(float2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 4; ++k)
    {
        v += a * cloudNoise(p);
        p = p * 2.03 + 17.0;
        a *= 0.5;
    }
    return v / 0.9375;
}

// 空の色に雲の層を重ねる。cover が 0 のときは入口で戻る。縁は太陽の側が明るく、トゥーンの段があれば量を段に切る
float3 applyCloudLayer(float3 c, float3 dir, float r, float bands)
{
    float cover = sky.cloud.x;
    float fade = smoothstep(0.02, 0.22, dir.y);
    if (cover <= 0.0 || fade <= 0.0) { return c; }
    float2 uv = dir.xz / (dir.y + 0.15) * 1.6 + float2(sky.cloud.y * 0.012, sky.cloud.y * 0.005);
    float n = cloudFbm(uv);
    float thr = 1.0 - cover;
    float d = smoothstep(thr - 0.02, thr + 0.28, n);
    float2 toSun = normalize(sky.sunDir.xz + float2(1e-4, 0.0)) * 0.07;
    float lit = saturate(0.55 + (n - cloudFbm(uv + toSun)) * 4.0);
    if (bands > 1.5)
    {
        d = round(d * bands) / bands;
        lit = round(lit * bands) / bands;
    }
    float sunMu = dot(cameraUp(), sky.sunDir.xyz);
    float3 sunT = hitsGround(r, sunMu) ? float3(0.0, 0.0, 0.0) : sampleTransmittance(r, sunMu);
    float3 ambient = skyViewTex.SampleLevel(linearClamp, skyViewUv(r, float3(0.0, 1.0, 0.0)), 0.0).rgb * sky.sunDir.w;
    float rim = pow(saturate(dot(dir, sky.sunDir.xyz)), 8.0) * (1.0 - d);
    float thick = smoothstep(thr + 0.1, thr + 0.45, n);   // 厚い所ほど底が暗い (縁は明るいまま)
    float3 col = (sunT * sky.sunDir.w * (0.12 + 0.62 * lit + 0.5 * rim) + ambient * 0.7) * (1.0 - 0.4 * thick * (1.0 - rim));
    return lerp(c, col, d * fade * 0.96);
}

// 空を主パスの 3 枚へ描く。下地は法線なし (0.5, 0.5, 0.5, 印 0)、距離は遠方
[shader("fragment")]
SkyOut fsSkyDraw(FullOut i)
{
    float3 dir = normalize(sky.camForward.xyz + i.ndc.x * sky.camRight.xyz + i.ndc.y * sky.camUp.xyz);
    float r = length(sky.cameraKm.xyz);
    float3 c = skyViewTex.SampleLevel(linearClamp, skyViewUv(r, dir), 0.0).rgb * sky.sunDir.w;
    float bands = sky.radii.w;
    if (sky.radii.z > 0.0)
    {
        float cs = dot(dir, sky.sunDir.xyz);
        float edge = cos(sky.radii.z);
        float inner = cos(sky.radii.z * ((bands > 1.5) ? 1.0 : 0.7));
        float disk = (bands > 1.5) ? step(edge, cs) : smoothstep(edge, inner, cs);
        float mu = dot(dir, cameraUp());
        float3 t = hitsGround(r, mu) ? float3(0.0, 0.0, 0.0) : sampleTransmittance(r, mu);
        c += t * sky.sunDir.w * 4.0 * disk;
    }
    c = applyCloudLayer(c, dir, r, bands);
    c = toonBand(c, bands, sky.sunDir.w * 0.04);
    if (sky.camForward.w > 0.0) { c = neutralShoulder(c * sky.camForward.w); }
    SkyOut o;
    o.color = float4(c, 1.0);
    o.normal = float4(0.5, 0.5, 0.5, 0.0);
    o.depth = float4(1.0, 1.0, 1.0, 1.0);
    return o;
}

// 空気遠近。場面の色へ (距離ごとの散乱 S を足し、透過率 T を掛ける) ための S と T を書く。距離は輪郭線の下地の線形の距離を使う。
// DX12 は 32^3 の froxel に積んで引くが、ここでは画素ごとに光線を数歩だけ積む。互換レベルでも使えるよう、サンプル 0 だけを読む
[shader("fragment")]
float4 fsAerial(FullOut i) : SV_Target
{
    int2 pix = int2(i.pos.xy);
    float4 e = lineDepthMs.Load(pix, 0);
    uint v = (uint(e.r * 255.0 + 0.5) << 16u) | (uint(e.g * 255.0 + 0.5) << 8u) | uint(e.b * 255.0 + 0.5);
    if (v >= 16777215u) { return float4(0.0, 0.0, 0.0, 1.0); }   // 何も描かれていない所 (空) には掛けない
    float dist = float(v) / 16777215.0 * sky.camUp.w;
    float km = dist * sky.cameraKm.w * sky.camRight.w;
    float w = sqrt(saturate(km / 32.0));
    float f = saturate(w * 64.0);
    float3 dir = normalize(sky.camForward.xyz + i.ndc.x * sky.camRight.xyz + i.ndc.y * sky.camUp.xyz);
    ScatterResult r = integrateScattering(sky.cameraKm.xyz, dir, km, 10, true, false);
    float apT = lerp(1.0, dot(r.transmittance, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)), f);
    float3 apS = r.inscatter * sky.sunDir.w * f;
    float bands = sky.radii.w;
    if (bands > 1.5)
    {
        float q = ceil(apT * bands) / bands;
        apS *= (1.0 - q) / max(1.0 - apT, 1e-4);
        apT = q;
    }
    return float4(apS * ((sky.camForward.w > 0.0) ? sky.camForward.w : 1.0), apT);
}
