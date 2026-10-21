// 半透明の WBOIT の合成 (通常のレベル)。蓄積 (forward.slang の fsMainOit) を resolve した 2 枚を読み、場面の色へ over で重ねる。
// DX12 の WeightedBlendedOIT の composite と同じ式: 色 = accum.rgb / accum.a、被覆率 = 1 - reveal。

[[vk::binding(0, 0)]] Texture2D<float4> oitAccum;
[[vk::binding(1, 0)]] Texture2D<float4> oitReveal;

struct OitCompositeOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
OitCompositeOut vsOitComposite(uint vid : SV_VertexID)
{
    OitCompositeOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fsOitComposite(OitCompositeOut i) : SV_Target
{
    int3 p = int3(int2(i.pos.xy), 0);
    float4 ac = oitAccum.Load(p);
    float rv = oitReveal.Load(p).r;
    float3 col = ac.rgb / max(ac.a, 1e-5);
    return float4(col, 1.0 - rv);
}
