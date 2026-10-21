// 2D の頂点色バッチ。DX12 の 2D の経路 (RenderPipeline2D_Dx12) と同じく、書いた色のまま RGBA8 へ通常のアルファで重ねる。

struct BatchCB
{
    float4x4 ortho;   // 論理画面 -> クリップ空間 (列優先)
};

[[vk::binding(0, 0)]] ConstantBuffer<BatchCB> batch;

struct BatchIn
{
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

struct BatchOut
{
    float4 pos   : SV_Position;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
BatchOut vsBatch(BatchIn i)
{
    BatchOut o;
    o.pos = mul(batch.ortho, float4(i.pos, 0.0, 1.0));
    o.color = i.color;
    return o;
}

[shader("fragment")]
float4 fsBatch(BatchOut i) : SV_Target
{
    return i.color;
}
