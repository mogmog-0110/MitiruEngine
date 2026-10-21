// スキンの頂点を GPU で変形する compute。式は DX12 の Dx12SkinningCompute と Skinning.hpp の skinVertices と同じ。
// 出力は Vertex3D と同じ 48 byte (float4 を 3 つ) で、そのまま頂点バッファとして描く。
// 互換レベルは 1 段に付く storage buffer が 4 本までなので、束縛は源・パレット・個体の表・出力の 4 本に収めている。

struct SkinParams
{
    uint vertexCount;     // この dispatch の源の頂点数
    uint jointCount;      // パレットの 1 体ぶんの行列の数
    uint firstInstance;   // 個体の表の、この dispatch の先頭
    uint pad;
};

// 頂点 1 つぶん。v0 = (位置, 法線 x)、v1 = (法線 y z, uv)、v2 = 色
struct SkinSource
{
    float4 v0;
    float4 v1;
    float4 v2;
    uint4 joints;
    float4 weights;
};

[[vk::binding(0, 0)]] ConstantBuffer<SkinParams> params;
[[vk::binding(1, 0)]] StructuredBuffer<SkinSource> source;
[[vk::binding(2, 0)]] StructuredBuffer<float4> palette;       // 行優先の 4x4 を float4 の 4 行で並べる
[[vk::binding(3, 0)]] StructuredBuffer<uint2> instances;      // x = パレットの先頭の行列 y = 出力の先頭の頂点
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> outVertices;

[shader("compute")]
[numthreads(64, 1, 1)]
void csSkin(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= params.vertexCount) { return; }
    const uint2 inst = instances[params.firstInstance + id.y];
    const SkinSource s = source[i];
    float4 o0 = s.v0;
    float4 o1 = s.v1;
    const float wsum = s.weights.x + s.weights.y + s.weights.z + s.weights.w;
    if (wsum > 1e-8)
    {
        float4 r0 = float4(0.0);
        float4 r1 = float4(0.0);
        float4 r2 = float4(0.0);
        float4 r3 = float4(0.0);
        [unroll] for (uint k = 0; k < 4; ++k)
        {
            const float w = s.weights[k];
            const uint j = s.joints[k];
            if (w == 0.0 || j >= params.jointCount) { continue; }
            const float f = w / wsum;
            const uint b = (inst.x + j) * 4;
            r0 += palette[b] * f;
            r1 += palette[b + 1] * f;
            r2 += palette[b + 2] * f;
            r3 += palette[b + 3] * f;
        }
        const float4 p = float4(s.v0.xyz, 1.0);
        const float pw = dot(r3, p);
        const float invW = (pw == 0.0) ? 0.0 : 1.0 / pw;
        const float3 pos = float3(dot(r0, p), dot(r1, p), dot(r2, p)) * invW;
        const float3 nIn = float3(s.v0.w, s.v1.x, s.v1.y);
        float3 n = float3(dot(r0.xyz, nIn), dot(r1.xyz, nIn), dot(r2.xyz, nIn));
        const float len = length(n);
        if (len > 1e-7) { n = n / len; }
        o0 = float4(pos, n.x);
        o1 = float4(n.y, n.z, s.v1.z, s.v1.w);
    }
    const uint o = (inst.y + i) * 3;
    outVertices[o] = o0;
    outVertices[o + 1] = o1;
    outVertices[o + 2] = s.v2;
}
