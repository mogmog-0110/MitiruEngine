#pragma once

/// @file DX12DecalShaders.hpp
/// @brief 前方描画の PS に足す投影デカール (Decals.hpp と同じ式)
/// @details DX12_LIT_COMMON_HLSL の後に連結する。画素の froxel のビット集合を引き、照明の前に
///          基本色・法線・粗さを書き換える。局所光も書き換えた面で受けるので、跡は光で正しく照らされる。
///          ビットは描く順に並んでいるので、後のデカールが前のものの上に重なる。
///          レジスタは t35〜t38。lit の本体を使い回す屋外の地形と草の root sig が t12〜t34 を使うので、
///          その後ろに置く (どちらの root sig も場面の表の 7〜10 枚目に張る)。

namespace mitiru::render
{

inline constexpr const char* DX12_DECAL_APPLY_HLSL = R"hlsl(
struct DecalGpu
{
    float4 toDecal0;
    float4 toDecal1;
    float4 toDecal2;
    float4 color;    // rgb = 基本色に掛ける色、a = 不透明度
    float4 axisZ;    // xyz = 局所 +z、w = 縁のぼかし
    float4 axisX;    // xyz = 局所 +x、w = 法線の強さ
    float4 params;   // x = 基本色の層 y = 法線の層 z = 粗さ w = 粗さの書き換え量
    float4 fade;     // x = cos(開始角) y = cos(終了角)
};

StructuredBuffer<DecalGpu> g_decals     : register(t35);
StructuredBuffer<uint>     g_decalMasks : register(t36);   // [0] = 数、[4..] = froxel ごとに 8 語
Texture2DArray             g_vfxColor   : register(t37);   // sRGB として読む
Texture2DArray             g_vfxData    : register(t38);   // 同じ層をそのまま読む (法線)

float decalEdge(float x, float soft)
{
    return (soft > 0.0) ? saturate((0.5 - abs(x)) / (0.5 * soft)) : 1.0;
}

// 層が無い時の形。中心 0、縁 0.5 の円を soft の幅でぼかす
float decalDisc(float2 p, float soft)
{
    float r = length(p) * 2.0;
    return (soft > 0.0) ? saturate((1.0 - r) / soft) : step(r, 1.0);
}

float3 decalNormal(DecalGpu d, float3 uvw, float2 gx, float2 gy, float3 N)
{
    float2 xy = g_vfxData.SampleGrad(g_sampClamp, uvw, gx, gy).rg * 2.0 - 1.0;
    float3 T = d.axisX.xyz - N * dot(d.axisX.xyz, N);
    T = (dot(T, T) > 1e-8) ? normalize(T) : d.axisX.xyz;
    float3 B = cross(N, T);
    // 層の v は箱の -y へ進むので、緑は箱の +y (= B) を指す
    float3 n = T * xy.x + B * xy.y + N * sqrt(saturate(1.0 - dot(xy, xy)));
    return normalize(n);
}

void applyDecals(float4 svPos, float3 worldPos, float3 Ng, inout float3 albedo, inout float3 N, inout float roughness)
{
    uint count = g_decalMasks[0];
    // 副ビュー (DX12Views.hpp) の froxel は主ビューのカメラで作っていないので読まない
    if (count == 0 || IblParams.w > 0.5) { return; }
    uint base = 4 + clusterOf(svPos, worldPos) * 8;
    uint words = (count + 31) / 32;
    // 分岐の中では暗黙の微分が使えないので、面の位置の微分から uv の微分を作って mip を選ぶ
    float3 dpdx = ddx(worldPos);
    float3 dpdy = ddy(worldPos);
    [loop]
    for (uint w = 0; w < words; ++w)
    {
        uint bits = g_decalMasks[base + w];
        [loop]
        while (bits != 0)
        {
            uint b = firstbitlow(bits);
            bits &= bits - 1;
            DecalGpu d = g_decals[w * 32 + b];
            float4 wp = float4(worldPos, 1.0);
            float3 p = float3(dot(d.toDecal0, wp), dot(d.toDecal1, wp), dot(d.toDecal2, wp));
            if (any(abs(p) > 0.5)) { continue; }
            float facing = saturate((dot(Ng, d.axisZ.xyz) - d.fade.y) / max(d.fade.x - d.fade.y, 1e-4));
            float soft = d.axisZ.w;
            float a = d.color.a * facing * decalEdge(p.z, soft);
            float3 uvw = float3(p.x + 0.5, 0.5 - p.y, d.params.x);
            float2 gx = float2(dot(d.toDecal0.xyz, dpdx), -dot(d.toDecal1.xyz, dpdx));
            float2 gy = float2(dot(d.toDecal0.xyz, dpdy), -dot(d.toDecal1.xyz, dpdy));
            float4 tex = float4(1.0, 1.0, 1.0, decalDisc(p.xy, soft));
            if (d.params.x >= 0.0) { tex = g_vfxColor.SampleGrad(g_sampClamp, uvw, gx, gy); }
            a *= tex.a;
            if (a <= 0.0) { continue; }
            albedo = lerp(albedo, d.color.rgb * tex.rgb, a);
            roughness = lerp(roughness, d.params.z, a * d.params.w);
            if (d.params.y >= 0.0)
            {
                float3 nd = decalNormal(d, float3(uvw.xy, d.params.y), gx, gy, N);
                N = normalize(lerp(N, nd, a * d.axisX.w));
            }
        }
    }
}
)hlsl";

} // namespace mitiru::render
