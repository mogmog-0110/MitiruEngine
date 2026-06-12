#pragma once

/// @file GpuParticleDx12_shaders_tables.hpp
/// @brief GpuParticleDx12 用 HLSL シェーダーソース表（GpuParticleDx12.hpp から機械的分割）

#ifdef _WIN32

#include <string_view>

namespace mitiru::effects
{

// ── HLSL シェーダーソース（DX12版） ────────────────────────

/// @brief パーティクルシミュレーション用コンピュートシェーダー (CS 5.0)
static constexpr std::string_view DX12_PARTICLE_COMPUTE_HLSL = R"(
struct Particle
{
    float3 position;
    float3 velocity;
    float3 acceleration;
    float4 color;
    float size;
    float lifetime;
    float age;
};

struct DrawArgs
{
    uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

cbuffer SimConstants : register(b0)
{
    float deltaTime;
    float3 gravity;
    float drag;
    uint maxParticles;
    uint activeParticles;
    float padding;
};

StructuredBuffer<Particle> particlesIn : register(t0);
RWStructuredBuffer<Particle> particlesOut : register(u0);
RWStructuredBuffer<DrawArgs> drawArgs : register(u1);

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // 最初のスレッドがドローアーギュメントを初期化する
    if (dtid.x == 0)
    {
        DrawArgs args;
        args.vertexCountPerInstance = 6;
        args.instanceCount = 0;
        args.startVertexLocation = 0;
        args.startInstanceLocation = 0;
        drawArgs[0] = args;
    }

    GroupMemoryBarrierWithGroupSync();

    uint idx = dtid.x;
    if (idx >= activeParticles)
    {
        return;
    }

    Particle p = particlesIn[idx];

    // 寿命チェック
    p.age += deltaTime;
    if (p.age >= p.lifetime)
    {
        p.size = 0;
        p.color.a = 0;
        particlesOut[idx] = p;
        return;
    }

    // 物理シミュレーション
    float3 accel = gravity + p.acceleration;
    p.velocity += accel * deltaTime;
    p.velocity *= (1.0 - drag * deltaTime);
    p.position += p.velocity * deltaTime;

    // ライフタイム比率に基づくフェード
    float normalizedAge = p.age / p.lifetime;
    p.color.a *= (1.0 - normalizedAge * normalizedAge);

    particlesOut[idx] = p;

    // 生存パーティクルのインスタンスカウントをインクリメントする
    if (p.size > 0 && p.color.a > 0.001)
    {
        uint dummy;
        InterlockedAdd(drawArgs[0].instanceCount, 1, dummy);
    }
}
)";

/// @brief パーティクル描画用頂点シェーダー（DX12版）
static constexpr std::string_view DX12_PARTICLE_VS_HLSL = R"(
struct Particle
{
    float3 position;
    float3 velocity;
    float3 acceleration;
    float4 color;
    float size;
    float lifetime;
    float age;
};

cbuffer RenderConstants : register(b0)
{
    float4x4 viewProjection;
    float3 cameraRight;
    float pad0;
    float3 cameraUp;
    float pad1;
};

StructuredBuffer<Particle> particles : register(t0);

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VSOutput output;

    Particle p = particles[instanceId];

    static const float2 quadVerts[6] = {
        float2(-1, -1), float2(-1, 1), float2(1, -1),
        float2(1, -1), float2(-1, 1), float2(1, 1)
    };

    float2 corner = quadVerts[vertexId];
    float halfSize = p.size * 0.5;

    float3 worldPos = p.position
        + cameraRight * corner.x * halfSize
        + cameraUp * corner.y * halfSize;

    output.position = mul(viewProjection, float4(worldPos, 1.0));
    output.color = p.color;
    output.texCoord = corner * 0.5 + 0.5;

    return output;
}
)";

/// @brief パーティクル描画用ピクセルシェーダー（DX12版）
static constexpr std::string_view DX12_PARTICLE_PS_HLSL = R"(
struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

float4 PSMain(PSInput input) : SV_Target
{
    float2 center = input.texCoord - 0.5;
    float dist = length(center) * 2.0;
    float alpha = saturate(1.0 - dist * dist);

    float4 color = input.color;
    color.a *= alpha;

    if (color.a < 0.001)
    {
        discard;
    }

    return color;
}
)";

} // namespace mitiru::effects

#endif // _WIN32
