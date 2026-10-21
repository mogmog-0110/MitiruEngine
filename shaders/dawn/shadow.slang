// 太陽の影の深度パス。頂点の位置だけを読み、光の view * proj * world で写す。色は出さない。
// 影を引く側は forward.slang (shadowNear / shadowFar)。

struct ShadowDrawCB
{
    float4x4 mvp;   // 光の view * proj * world
};

[[vk::binding(0, 0)]] ConstantBuffer<ShadowDrawCB> shadowDraw;

struct ShadowVSIn
{
    float3 pos : POSITION;
};

[shader("vertex")]
float4 vsShadow(ShadowVSIn i) : SV_Position
{
    return mul(shadowDraw.mvp, float4(i.pos, 1.0));
}
