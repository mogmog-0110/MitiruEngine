// 粒の板ポリ。DX12 の DX12ParticleShaders.hpp の PS (sprite / PSAdditive / PSAlpha / PSOpaque / 縁の硬い円と影の三日月) と同じ式。
// 頂点は CPU が板の四隅まで計算してある (DawnParticles.cpp)。加算と半透明は深度を読むだけで、輪郭線の下地の距離 (24 bit) から
// 面との柔らかい交差を作る。不透明の粒は場面の描画の中で深度と下地を書き、縁は alpha to coverage にする。

struct ParticleCB
{
    float4x4 viewProj;
    float4   params;   // x = 距離を詰めた範囲 (カメラの遠方) y = 中間の色に掛ける倍率
};

[[vk::binding(0, 0)]] ConstantBuffer<ParticleCB> pcb;
[[vk::binding(1, 0)]] Texture2DArray<float4>      vfxColor;
[[vk::binding(2, 0)]] SamplerState                vfxSampler;
[[vk::binding(3, 0)]] Texture2DMS<float4>         sceneDepth;   // 加算と半透明のパスだけが読む

struct VsIn
{
    // 頂点の入力の場所は Slang が意味名で並べ直すので、番号つきの TEXCOORD で C++ の頂点の並び (位置、uv、色、info、影、extra) に合わせる
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD1;
    float4 color  : TEXCOORD2;
    float4 info   : TEXCOORD3;   // x = 形 (0 円 1 火花 2 縁の硬い円) y = 影の色を持つか zw = 明るい円をずらす量
    float3 shadow : TEXCOORD4;
    float4 extra  : TEXCOORD5;   // x = VFX テクスチャの層 (-1 は形) y = 面と交わる所を消していく距離
};

struct VsOut
{
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float4 color  : TEXCOORD1;
    float4 info   : TEXCOORD2;
    float3 shadow : TEXCOORD3;
    float4 extra  : TEXCOORD4;
    float  depth  : TEXCOORD5;   // 視点からの奥行き (クリップの w)
};

[shader("vertex")]
VsOut vsParticle(VsIn i)
{
    VsOut o;
    o.pos = mul(pcb.viewProj, float4(i.pos, 1.0));
    o.uv = i.uv;
    o.color = i.color;
    o.info = i.info;
    o.shadow = i.shadow;
    o.extra = i.extra;
    o.depth = o.pos.w;
    return o;
}

// 色が 1 で頭打ちの形式 (互換レベル) へ書く色。倍率で縮め、1 に近い所から色相を保ったまま丸める (成分ごとに頭打ちにすると白く抜ける)
float3 neutralShoulder(float3 c)
{
    const float knee = 0.9;
    const float d = 1.0 - knee;
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= knee) { return c; }
    float newPeak = 1.0 - d * d / (peak + d - knee);
    return c * (newPeak / peak);
}

// 10 bit の刻みで暗い所に縞が出るので、三角分布の細かい雑音 (±1 段) を足して崩す。MSAA の resolve で平均されて目立たない
float3 storeHdr(float3 c, float scale, float2 pixel)
{
    if (scale >= 1.0) { return c; }
    float n0 = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float n1 = frac(52.9829189 * frac(dot(pixel + float2(17.0, 31.0), float2(0.06711056, 0.00583715))));
    return neutralShoulder(c * scale) + float3(n0 + n1 - 1.0, n1 + n0 - 1.0, n0 - n1) * (1.0 / 1023.0);
}

// 中心からの距離 r (半径 1) の円の縁。縁は画素 1 個ぶんだけ滑らかにする
float hardEdge(float r)
{
    return saturate((1.0 - r) / max(fwidth(r), 1e-4) + 0.5);
}

// 色 (rgb) と形 (a)。層があればテクスチャ、なければ手続きの形。微分は分岐の外で取る
float4 sprite(VsOut i)
{
    float2 gx = ddx(i.uv);
    float2 gy = ddy(i.uv);
    float r = length(i.uv * 2.0 - float2(1.0, 1.0));
    float hard = hardEdge(r);
    if (i.extra.x >= 0.0) { return vfxColor.SampleGrad(vfxSampler, float3(i.uv, i.extra.x), gx, gy); }
    if (i.info.x > 1.5) { return float4(1.0, 1.0, 1.0, hard); }
    if (i.info.x > 0.5) { return float4(1.0, 1.0, 1.0, saturate((1.0 - r) * 3.0)); }
    float a = saturate(1.0 - r);
    return float4(1.0, 1.0, 1.0, a * a);
}

// 縁の硬い円に影の色を持つ粒は、影の色の円の上に、ずらした明るい円を重ねる (はみ出した所は描かない)
float3 baseColor(VsOut i)
{
    float2 q = i.uv * 2.0 - float2(1.0, 1.0);
    float lit = hardEdge(length(q - float2(i.info.z, -i.info.w)));
    if (i.info.y < 0.5) { return i.color.rgb; }
    return lerp(i.shadow, i.color.rgb, lit);
}

float decodeSceneDepth(int2 p)
{
    float4 c = sceneDepth.Load(p, 0);
    uint v = (uint(c.r * 255.0 + 0.5) << 16u) | (uint(c.g * 255.0 + 0.5) << 8u) | uint(c.b * 255.0 + 0.5);
    return float(v) / 16777215.0 * pcb.params.x;
}

// 面の手前なら 1、softDistance かけて面で 0。distance が 0 なら深度テストに任せる
float softFade(VsOut i)
{
    if (!(i.extra.y > 0.0)) { return 1.0; }
    float gap = decodeSceneDepth(int2(i.pos.xy)) - i.depth;
    return saturate(gap / i.extra.y);
}

[shader("fragment")]
float4 fsAdditive(VsOut i) : SV_Target
{
    float4 s = sprite(i);
    float a = i.color.a * s.a * softFade(i);
    return float4(baseColor(i) * s.rgb * a * pcb.params.y, a);
}

[shader("fragment")]
float4 fsAlpha(VsOut i) : SV_Target
{
    float4 s = sprite(i);
    float a = saturate(i.color.a * s.a * softFade(i));
    return float4(storeHdr(baseColor(i) * s.rgb, pcb.params.y, i.pos.xy), a);
}

// 24 bit の線形の距離を RGBA8 に詰める (forward.slang の packLinearDepth と同じ)
float4 packLinearDepth(float viewDepth)
{
    float d = saturate(viewDepth / max(pcb.params.x, 1e-3));
    uint v = uint(d * 16777215.0 + 0.5);
    return float4(float((v >> 16u) & 255u), float((v >> 8u) & 255u), float(v & 255u), 255.0) / 255.0;
}

struct OpaqueOut
{
    float4 color  : SV_Target0;
    float4 normal : SV_Target1;
    float4 depth  : SV_Target2;
};

// 不透明の平らな粒。色の a は形で、縁は alpha to coverage に任せる。輪郭線の検出からは外す印 (1, 1, 1) を書く
[shader("fragment")]
OpaqueOut fsOpaque(VsOut i)
{
    float4 s = sprite(i);
    OpaqueOut o;
    o.color = float4(storeHdr(baseColor(i) * s.rgb, pcb.params.y, i.pos.xy), s.a);
    o.normal = float4(1.0, 1.0, 1.0, 31.0 / 255.0);
    o.depth = packLinearDepth(i.depth);
    return o;
}

// alpha to coverage が使えない作りのとき: 形の半分より濃い所だけを描く
[shader("fragment")]
OpaqueOut fsOpaqueCut(VsOut i)
{
    float4 s = sprite(i);
    if (s.a < 0.5) { discard; }
    OpaqueOut o;
    o.color = float4(storeHdr(baseColor(i) * s.rgb, pcb.params.y, i.pos.xy), 1.0);
    o.normal = float4(1.0, 1.0, 1.0, 31.0 / 255.0);
    o.depth = packLinearDepth(i.depth);
    return o;
}
