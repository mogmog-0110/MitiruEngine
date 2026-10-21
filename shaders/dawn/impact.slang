// 止め絵。DX12 の DX12_IMPACT_FRAME_PS と同じ式。出力の写しを読み、画素の印 (法線 RT の a) が色のまま残す物でなければ
// 2 色に塗るか反転する。印は場面の MSAA の法線のサンプル 0 から引く (場面が出力より小さいときは画素の位置を割合で合わせる)。

struct ImpactCB
{
    float4 lightColor;   // rgb = 明るい側の色 a = 明るさのしきい値
    float4 darkColor;    // rgb = 暗い側の色 a = 寄せる割合
    float4 params;       // x = 1 なら 2 色、2 なら反転 yz = 法線 RT の大きさ
    float4 keep[2];      // 印 0..3 / 4..7 を色のまま残すか
};

[[vk::binding(0, 0)]] ConstantBuffer<ImpactCB> impact;
[[vk::binding(1, 0)]] Texture2D<float4> sceneCopy;
[[vk::binding(2, 0)]] Texture2DMS<float4> normalMs;

struct ImpactOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
ImpactOut vsImpact(uint vid : SV_VertexID)
{
    ImpactOut o;
    float2 t = float2(float((vid << 1u) & 2u), float(vid & 2u));
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = t;
    return o;
}

[shader("fragment")]
float4 fsImpact(ImpactOut i) : SV_Target
{
    float3 c = sceneCopy.Load(int3(int2(i.pos.xy), 0)).rgb;
    int2 np = min(int2(i.uv * impact.params.yz), int2(impact.params.yz) - int2(1, 1));
    uint slot = uint(round(normalMs.Load(np, 0).a * 255.0)) >> 5u;
    float keep = (slot < 4u) ? impact.keep[0][slot] : impact.keep[1][slot - 4u];
    if (keep > 0.5) { return float4(c, 1.0); }
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float3 o = (impact.params.x < 1.5) ? ((lum >= impact.lightColor.a) ? impact.lightColor.rgb : impact.darkColor.rgb) : float3(1.0, 1.0, 1.0) - c;
    return float4(lerp(c, o, impact.darkColor.a), 1.0);
}
