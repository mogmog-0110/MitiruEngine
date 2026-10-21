// オフスクリーンの最終色 (RGBA8) を、スワップチェーンの絵 (BGRA8 など) へそのまま写す。窓つきの実行の最後の 1 パス。

[[vk::binding(0, 0)]] Texture2D<float4> blitSource;

struct BlitOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
BlitOut vsBlit(uint vid : SV_VertexID)
{
    BlitOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fsBlit(BlitOut i) : SV_Target
{
    return blitSource.Load(int3(int2(i.pos.xy), 0));
}
