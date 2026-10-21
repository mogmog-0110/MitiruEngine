// 副ビューの出力 (最終色の形式、sRGB の値) を、画面の矩形へそのまま貼る。矩形は呼び手の viewport で決める。

[[vk::binding(0, 0)]] Texture2D<float4> viewImage;
[[vk::binding(1, 0)]] SamplerState viewSampler;

struct ViewOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
ViewOut vsView(uint vid : SV_VertexID)
{
    ViewOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.uv = t;
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fsView(ViewOut i) : SV_Target
{
    return float4(viewImage.Sample(viewSampler, i.uv).rgb, 1.0);
}
