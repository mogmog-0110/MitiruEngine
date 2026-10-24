// 太陽の影の深度パス。頂点の位置だけを読み、光の view * proj * world で写す。色は出さない。
// 影を引く側は forward.slang (shadowNear / shadowFar)。
// 光の view * proj * world は CPU が 1 描画ごとに掛けて instance の頂点属性 (列優先の 4 列) で渡す。同じ物を連続して描く物は 1 回の instanced draw になる。

struct ShadowVSIn
{
    float3 pos : POSITION;
    float4 c0  : TEXCOORD1;
    float4 c1  : TEXCOORD2;
    float4 c2  : TEXCOORD3;
    float4 c3  : TEXCOORD4;
};

[shader("vertex")]
float4 vsShadow(ShadowVSIn i) : SV_Position
{
    float4x4 mvp = float4x4(float4(i.c0.x, i.c1.x, i.c2.x, i.c3.x), float4(i.c0.y, i.c1.y, i.c2.y, i.c3.y),
                            float4(i.c0.z, i.c1.z, i.c2.z, i.c3.z), float4(i.c0.w, i.c1.w, i.c2.w, i.c3.w));
    return mul(mvp, float4(i.pos, 1.0));
}
