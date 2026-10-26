// モーフを足してからスキンする compute。skin.slang と同じ式で、モーフのある体で重みが 0 でないときだけ使う
// (モーフなしの描画は skin.slang のままなので、絵も性能も変わらない)。DX12 の kSkinningMorphCsHlsl と render/MorphTargets.hpp の morphSkinVertices と同じ式。
// 互換レベルは 1 段に付く storage buffer が 4 本までなので、バインドは skin.slang と同じ本数に収めている。
//   源の buffer   : 頂点 (1 つ 20 word) の並びの後ろに、モーフの行 (頂点数 + 1 word) と要素 (1 つ 8 word) を詰めた uint32 の配列
//   パレットの buffer : 行列の後ろに重みを置く。重みは 16 float を 1 単位として積む (行列と同じ単位で先頭を指す)
//   個体の表   : 個体ごとに 2 要素。{パレットの先頭, 出力の先頭}、{重みの先頭, 重みの数}。重みはこの prim のモーフ 0 番から並べて渡す

struct SkinMorphParams
{
    uint vertexCount;     // この dispatch の源の頂点数
    uint jointCount;      // パレットの 1 体ぶんの行列の数 (スキンの無いモーフの体は 0)
    uint firstInstance;   // 個体の表の、この dispatch の先頭の要素 (個体ごとに 2 要素ずつ進む)
    uint rowsOffset;      // 源の buffer の中の、モーフの行の先頭の word
    uint entriesOffset;   // 同じく、モーフの要素の先頭の word
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::binding(0, 0)]] ConstantBuffer<SkinMorphParams> params;
[[vk::binding(1, 0)]] StructuredBuffer<uint> source;
[[vk::binding(2, 0)]] StructuredBuffer<float4> palette;
[[vk::binding(3, 0)]] StructuredBuffer<uint2> instances;
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> outVertices;

float4 sourceF4(uint word)
{
    return float4(asfloat(source[word]), asfloat(source[word + 1]), asfloat(source[word + 2]), asfloat(source[word + 3]));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void csSkinMorph(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= params.vertexCount) { return; }
    const uint at = params.firstInstance + id.y * 2;
    const uint2 inst = instances[at];
    const uint2 weightInst = instances[at + 1];
    const uint b = i * 20;
    float4 v0 = sourceF4(b);
    float4 v1 = sourceF4(b + 4);
    const float4 v2 = sourceF4(b + 8);
    const uint4 joints = uint4(source[b + 12], source[b + 13], source[b + 14], source[b + 15]);
    const float4 weights = sourceF4(b + 16);

    float3 mpos = v0.xyz;
    float3 mnrm = float3(v0.w, v1.x, v1.y);
    bool touched = false;
    const uint rowBegin = source[params.rowsOffset + i];
    const uint rowEnd = source[params.rowsOffset + i + 1];
    for (uint e = rowBegin; e < rowEnd; ++e)
    {
        const uint eb = params.entriesOffset + e * 8;
        const uint slot = source[eb];
        if (slot >= weightInst.y) { continue; }
        const uint wi = weightInst.x * 16 + slot;
        const float mw = palette[wi / 4][wi % 4];
        if (mw == 0.0) { continue; }
        mpos += float3(asfloat(source[eb + 1]), asfloat(source[eb + 2]), asfloat(source[eb + 3])) * mw;
        mnrm += float3(asfloat(source[eb + 4]), asfloat(source[eb + 5]), asfloat(source[eb + 6])) * mw;
        touched = true;
    }
    float4 o0 = float4(mpos, mnrm.x);
    float4 o1 = float4(mnrm.y, mnrm.z, v1.z, v1.w);

    const float wsum = weights.x + weights.y + weights.z + weights.w;
    if (wsum > 1e-8)
    {
        float4 r0 = float4(0.0);
        float4 r1 = float4(0.0);
        float4 r2 = float4(0.0);
        float4 r3 = float4(0.0);
        [unroll] for (uint k = 0; k < 4; ++k)
        {
            const float w = weights[k];
            const uint j = joints[k];
            if (w == 0.0 || j >= params.jointCount) { continue; }
            const float f = w / wsum;
            const uint pb = (inst.x + j) * 4;
            r0 += palette[pb] * f;
            r1 += palette[pb + 1] * f;
            r2 += palette[pb + 2] * f;
            r3 += palette[pb + 3] * f;
        }
        const float4 p = float4(mpos, 1.0);
        const float pw = dot(r3, p);
        const float invW = (pw == 0.0) ? 0.0 : 1.0 / pw;
        const float3 pos = float3(dot(r0, p), dot(r1, p), dot(r2, p)) * invW;
        float3 n = float3(dot(r0.xyz, mnrm), dot(r1.xyz, mnrm), dot(r2.xyz, mnrm));
        const float len = length(n);
        if (len > 1e-7) { n = n / len; }
        o0 = float4(pos, n.x);
        o1 = float4(n.y, n.z, v1.z, v1.w);
    }
    else if (touched)
    {
        const float len = length(mnrm);
        if (len > 1e-7)
        {
            const float3 n = mnrm / len;
            o0 = float4(mpos, n.x);
            o1 = float4(n.y, n.z, v1.z, v1.w);
        }
    }
    const uint o = (inst.y + i) * 3;
    outVertices[o] = o0;
    outVertices[o + 1] = o1;
    outVertices[o + 2] = v2;
}
