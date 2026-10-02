#pragma once

/// @file DX12ParticleShaders.hpp
/// @brief GPU パーティクルの刻み (compute) と描画 (板ポリ) の HLSL。放出の規則は GpuParticles.hpp と同じ式
/// @details 刻みは 1 スレッドが 1 席を受け持つ。席 s に入る粒は「時刻 t までに出た粒のうち、番号が
///          s を席の数で割った余りになる最も新しいもの」で、番号が変わったらその粒を出た時の姿から作る。
///          描画は深度を束縛せず、PS で深度を読んで隠れと柔らかい交差を自分で決める。

namespace mitiru::render
{

inline constexpr const char* DX12_PARTICLE_COMMON_HLSL = R"hlsl(
#define NO_INDEX 0xFFFFFFFFu
#define STEP_HZ 60.0

cbuffer CbEmitter : register(b0)
{
    float4 OriginRadius;   // xyz = 出る位置 w = 散らす球の半径
    float4 DirCosSpread;   // xyz = 向き (長さ 1) w = cos(円錐の半角)
    float4 SpeedLife;      // speedMin speedMax lifeMin lifeMax
    float4 GravityDrag;    // xyz = 重力 w = drag
    float4 Emission;       // x = rate y = duration z = bounce w = 衝突 (0 なし 1 跳ねる 2 消える)
    uint4  Counts;         // x = burst y = 席の数 z = 先頭の席 w = seed
    float4 SizeKeys;       // 寿命の 0・mid・1 での直径、w = mid
    float4 Color0;
    float4 Color1;
    float4 Color2;
    float4 Look;           // x = stretch y = softDistance z = lit w = テクスチャの層 (-1 = 手続き)
    float4 Shape;          // x = 形 (0 円 1 火花) y = 反応マスクの上限
};

cbuffer CbParticleFrame : register(b1)
{
    float4x4 ViewProj;          // 主パスと同じずらし込み
    float4x4 ViewProjNoJitter;  // 衝突の判定 (深度の画素を引く)
    float4 CameraPos;
    float4 CameraRight;
    float4 CameraUp;
    float4 CameraForward;
    float4 Screen;              // x = 幅 y = 高さ z = near w = far
    float4 SunDir;              // xyz = 光の進む向き
    float4 SunColor;
    float4 Ambient;
};

struct Particle
{
    float3 position;
    float  age;
    float3 velocity;
    uint   index;
    float  lifetime;
    float  random;
    float2 pad;
};

float linearDepth(float d) { return Screen.z * Screen.w / (Screen.w - d * (Screen.w - Screen.z)); }

uint particleHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float particleRandom(uint index, uint channel)
{
    return (float)(particleHash(Counts.w ^ particleHash(index * 16u + channel)) >> 8) * (1.0 / 16777216.0);
}
)hlsl";

/// @brief 1 刻み進める。root 定数 b2 = { 刻みの番号, 1 なら席を空きとみなす }
inline constexpr const char* DX12_PARTICLE_STEP_CS = R"hlsl(
cbuffer CbStep : register(b2)
{
    uint StepIndex;
    uint ResetSlots;
};

RWStructuredBuffer<Particle> g_pool   : register(u0);
Texture2DMS<float, 4>        g_depth  : register(t0);
Texture2DMS<float4, 4>       g_normal : register(t1);

uint spawnedBy(uint step)
{
    uint n = Counts.x;
    float rate = Emission.x;
    if (rate > 0.0)
    {
        uint cont = (uint)floor((float)step * rate / STEP_HZ + 1e-4) + 1u;
        if (Emission.y > 0.0) { cont = min(cont, (uint)ceil(Emission.y * rate)); }
        n += cont;
    }
    return n;
}

float spawnTime(uint index)
{
    return (index < Counts.x || Emission.x <= 0.0) ? 0.0 : (float)(index - Counts.x) / Emission.x;
}

float3 initialVelocity(uint index)
{
    float z = DirCosSpread.w + (1.0 - DirCosSpread.w) * particleRandom(index, 0);
    float phi = 6.28318530718 * particleRandom(index, 1);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float3 w = DirCosSpread.xyz;
    float3 a = (abs(w.y) < 0.999) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 u = normalize(cross(a, w));
    float3 v = cross(w, u);
    float speed = SpeedLife.x + (SpeedLife.y - SpeedLife.x) * particleRandom(index, 2);
    return (u * (r * cos(phi)) + v * (r * sin(phi)) + w * z) * speed;
}

float3 spawnOffset(uint index)
{
    if (OriginRadius.w <= 0.0) { return 0.0; }
    float z = particleRandom(index, 4) * 2.0 - 1.0;
    float phi = 6.28318530718 * particleRandom(index, 5);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float rad = OriginRadius.w * pow(particleRandom(index, 6), 1.0 / 3.0);
    return float3(r * cos(phi), r * sin(phi), z) * rad;
}

// 出た時刻から a 秒の姿 (a は 1 刻み以内。抵抗は 1 刻みに満たないので掛けない)
Particle spawnParticle(uint index, float t)
{
    Particle p;
    float a = t - spawnTime(index);
    p.index = index;
    p.random = particleRandom(index, 7);
    p.lifetime = SpeedLife.z + (SpeedLife.w - SpeedLife.z) * particleRandom(index, 3);
    p.pad = 0.0;
    float3 v0 = initialVelocity(index);
    p.position = OriginRadius.xyz + spawnOffset(index) + v0 * a + 0.5 * GravityDrag.xyz * a * a;
    p.velocity = v0 + GravityDrag.xyz * a;
    p.age = a;
    // 進め直しの最初の刻みより前に出た粒は、目標の時刻には寿命が尽きているので作らない
    if (a > 1.01 / STEP_HZ + 1e-5) { p.age = 0.0; p.lifetime = 0.0; }
    return p;
}

// 深度バッファの面の裏へ入ったら、跳ね返すか消す
void collide(inout Particle p, float3 previous)
{
    float4 clip = mul(ViewProjNoJitter, float4(p.position, 1.0));
    if (clip.w <= Screen.z) { return; }
    float2 ndc = clip.xy / clip.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv >= 1.0)) { return; }
    int2 px = int2(uv * Screen.xy);
    float scene = linearDepth(g_depth.Load(px, 0));
    float thickness = max(0.3, length(p.velocity) * 2.0 / STEP_HZ);
    if (clip.w < scene || clip.w > scene + thickness) { return; }
    if (Emission.w > 1.5) { p.age = p.lifetime; return; }
    float4 enc = g_normal.Load(px, 0);
    float3 n = enc.xyz * 2.0 - 1.0;
    // 輪郭線から外した物の印 (1,1,1) と、法線の無い所は視線の逆を面の向きとする
    if (all(enc.xyz > 0.999) || dot(n, n) < 0.25) { n = normalize(CameraPos.xyz - p.position); }
    n = normalize(n);
    if (dot(p.velocity, n) < 0.0) { p.velocity = reflect(p.velocity, n) * Emission.z; }
    p.position = previous;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint s = id.x;
    if (s >= Counts.y) { return; }
    uint slot = Counts.z + s;
    Particle p = g_pool[slot];
    if (ResetSlots != 0) { p.index = NO_INDEX; p.age = 0.0; p.lifetime = 0.0; }
    float dt = 1.0 / STEP_HZ;
    float t = (float)StepIndex / STEP_HZ;
    uint n = spawnedBy(StepIndex);
    if (n > s)
    {
        uint newest = s + Counts.y * ((n - 1u - s) / Counts.y);
        if (newest != p.index)
        {
            g_pool[slot] = spawnParticle(newest, t);
            return;
        }
    }
    if (p.index == NO_INDEX || p.age >= p.lifetime)
    {
        g_pool[slot] = p;
        return;
    }
    float3 previous = p.position;
    p.velocity += GravityDrag.xyz * dt;
    p.velocity *= 1.0 / (1.0 + GravityDrag.w * dt);
    p.position += p.velocity * dt;
    p.age += dt;
    if (Emission.w > 0.5) { collide(p, previous); }
    g_pool[slot] = p;
}
)hlsl";

/// @brief 板ポリの VS と、加算・WBOIT・反応マスクの PS
inline constexpr const char* DX12_PARTICLE_DRAW_HLSL = R"hlsl(
cbuffer CbCluster : register(b3)
{
    uint4  ClusterGrid;    // xyz = froxel の数 w = 局所光の数
    float4 ClusterDepth;
    float4 ClusterScreen;
};

struct LocalLightGpu
{
    float3 positionWS;  float range;
    float3 color;       float spotScale;
    float3 directionWS; float spotOffset;
    float3 boundCenterVS; float boundRadius;
};

StructuredBuffer<Particle>      g_pool         : register(t0);
Texture2DMS<float, 4>           g_depth        : register(t1);
Texture2DArray                  g_vfxColor     : register(t2);
StructuredBuffer<LocalLightGpu> g_localLights  : register(t3);
StructuredBuffer<uint>          g_clusterMasks : register(t4);
SamplerState                    s_linear       : register(s0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
    float2 Uv       : TEXCOORD0;
    float  Depth    : TEXCOORD1;   // 視点からの奥行き (柔らかい交差に使う)
};

float4 curve3(float4 a, float4 b, float4 c, float mid, float t)
{
    return (t < mid) ? lerp(a, b, t / max(mid, 1e-4)) : lerp(b, c, (t - mid) / max(1.0 - mid, 1e-4));
}

// 局所光は届く範囲の形だけ掛ける (トゥーンの局所光と同じく距離の減衰は掛けない)
float3 localLightAt(float3 pos, float4 clip)
{
    float3 sum = 0.0;
    uint count = ClusterGrid.w;
    if (count == 0 || clip.w <= 0.0) { return sum; }
    float2 ndc = clip.xy / clip.w;
    float2 pix = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * Screen.xy;
    float depth = max(dot(pos - CameraPos.xyz, CameraForward.xyz), ClusterDepth.x);
    int z = clamp((int)floor(log(depth) * ClusterDepth.z + ClusterDepth.w), 0, (int)ClusterGrid.z - 1);
    uint x = min((uint)max(pix.x * ClusterScreen.x, 0.0), ClusterGrid.x - 1);
    uint y = min((uint)max(pix.y * ClusterScreen.y, 0.0), ClusterGrid.y - 1);
    uint base = (((uint)z * ClusterGrid.y + y) * ClusterGrid.x + x) * 8;
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
            LocalLightGpu l = g_localLights[w * 32 + b];
            float3 d = l.positionWS - pos;
            float dist2 = dot(d, d);
            float r2 = l.range * l.range;
            if (dist2 >= r2) { continue; }
            float win = saturate(1.0 - (dist2 * dist2) / (r2 * r2));
            float spot = saturate(dot(-d * rsqrt(max(dist2, 1e-8)), l.directionWS) * l.spotScale + l.spotOffset);
            sum += l.color * win * win * spot * spot;
        }
    }
    return sum;
}

float3 sceneLight(float3 pos, float4 clip)
{
    float3 toEye = normalize(CameraPos.xyz - pos);
    float sun = 0.35 + 0.65 * saturate(dot(toEye, -SunDir.xyz));
    return Ambient.rgb + SunColor.rgb * sun + localLightAt(pos, clip);
}

VSOutput VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOutput o;
    o.Position = float4(2.0, 2.0, 2.0, 1.0);
    o.Color = 0.0;
    o.Uv = 0.0;
    o.Depth = 0.0;
    if (iid >= Counts.y) { return o; }
    Particle p = g_pool[Counts.z + iid];
    if (p.index == NO_INDEX || p.age >= p.lifetime) { return o; }
    float t = saturate(p.age / max(p.lifetime, 1e-4));
    float size = curve3(SizeKeys.xxxx, SizeKeys.yyyy, SizeKeys.zzzz, SizeKeys.w, t).x;
    float4 color = curve3(Color0, Color1, Color2, SizeKeys.w, t);
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float3 toEye = normalize(CameraPos.xyz - p.position);
    float3 v = p.velocity - toEye * dot(p.velocity, toEye);
    float speed = length(v);
    float3 world;
    if (Look.x > 0.0 && speed > 1e-4)
    {
        float3 axis = v / speed;
        float3 side = normalize(cross(axis, toEye));
        world = p.position + side * (corner.x * size * 0.5) + axis * (corner.y * (size * 0.5 + speed * Look.x * 0.5));
    }
    else
    {
        float ang = p.random * 6.28318530718;
        float2 c = float2(corner.x * cos(ang) - corner.y * sin(ang), corner.x * sin(ang) + corner.y * cos(ang));
        world = p.position + (CameraRight.xyz * c.x + CameraUp.xyz * c.y) * (size * 0.5);
    }
    o.Position = mul(ViewProj, float4(world, 1.0));
    o.Depth = dot(world - CameraPos.xyz, CameraForward.xyz);
    if (Look.z > 0.0) { color.rgb *= lerp(1.0, sceneLight(p.position, o.Position), Look.z); }
    o.Color = color;
    o.Uv = corner * float2(0.5, -0.5) + 0.5;
    return o;
}

float4 sprite(float2 uv)
{
    if (Look.w >= 0.0) { return g_vfxColor.Sample(s_linear, float3(uv, Look.w)); }
    float r = length(uv * 2.0 - 1.0);
    float a = (Shape.x > 0.5) ? saturate((1.0 - r) * 3.0) : saturate(1.0 - r) * saturate(1.0 - r);
    return float4(1.0, 1.0, 1.0, a);
}

// 面の手前なら 1、softDistance かけて面で 0。面の裏は 0
float softFade(VSOutput input)
{
    float scene = linearDepth(g_depth.Load(int2(input.Position.xy), 0));
    float gap = scene - input.Depth;
    return (Look.y > 0.0) ? saturate(gap / Look.y) : step(0.0, gap);
}

float4 PSAdditive(VSOutput input) : SV_TARGET
{
    float4 s = sprite(input.Uv);
    float a = input.Color.a * s.a * softFade(input);
    return float4(input.Color.rgb * s.rgb * a, a);
}

struct OitOutput { float4 accum : SV_TARGET0; float reveal : SV_TARGET1; };

// WeightedBlendedOIT.hpp と同じ重み
OitOutput PSAlpha(VSOutput input)
{
    float4 s = sprite(input.Uv);
    float a = saturate(input.Color.a * s.a * softFade(input));
    float3 c = input.Color.rgb * s.rgb;
    float z = input.Position.z;
    float w = clamp(pow(min(1.0, a * 10.0) + 0.01, 3.0) * 1e3 * pow(1.0 - z * 0.9, 3.0), 1e-2, 3e3);
    OitOutput o;
    o.accum = float4(c * a, a) * w;
    o.reveal = a;
    return o;
}

float4 PSReactive(VSOutput input) : SV_TARGET
{
    float4 s = sprite(input.Uv);
    return min(saturate(input.Color.a * s.a * softFade(input)), Shape.y).xxxx;
}
)hlsl";

} // namespace mitiru::render
