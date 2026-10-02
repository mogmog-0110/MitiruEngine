#pragma once

/// @file ToonShaders3D.hpp
/// @brief 3D トゥーン描画用シェーダー（HLSL SM 5.0）の宣言。

namespace mitiru::render
{

/// @brief DEFAULT_VS_3D と同じ CbTransform レイアウトと入出力構造体を使う。
constexpr const char* TOON_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(World, float4(input.Position, 1.0));
    output.WorldPos = worldPos.xyz;
    output.WorldNorm = normalize(mul((float3x3)World, input.Normal));

    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);

    output.TexCoord = input.TexCoord;
    output.Color = input.Color;

    return output;
}
)hlsl";

/// @brief NdotL を 3 段階に量子化し、リムライトと彩度補正を加える。
constexpr const char* TOON_PS_3D = R"hlsl(
cbuffer CbLighting : register(b1)
{
    float3 LightDir;    float _pad0;
    float3 LightColor;  float _pad1;
    float3 AmbientColor; float _pad2;
    float3 CameraPos;   float _pad3;
    float4 MaterialDiffuse;
    float4 MaterialSpecular;
    float MaterialShininess;
    float3 _pad4;
};

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 WorldPos  : TEXCOORD0;
    float3 WorldNorm : TEXCOORD1;
    float2 TexCoord  : TEXCOORD2;
    float4 Color     : COLOR0;
};

struct PSOutput
{
    float4 Color  : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;

    float3 N = normalize(input.WorldNorm);
    float3 L = normalize(-LightDir);
    float3 V = normalize(CameraPos - input.WorldPos);

    // アンビエント
    float3 ambient = AmbientColor * MaterialDiffuse.rgb;

    // ディフューズ。NdotLを量子化
    float rawNdotL = max(dot(N, L), 0.0);
    float toon = (rawNdotL > 0.5) ? 1.0 : (rawNdotL > 0.15) ? 0.6 : 0.3;
    float3 diffuse = LightColor * MaterialDiffuse.rgb * toon;

    // スペキュラー
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specFactor = pow(NdotH, MaterialShininess) * 0.3;
    float3 specular = LightColor * MaterialSpecular.rgb * specFactor;

    float3 finalColor = ambient + diffuse + specular;
    float alpha = MaterialDiffuse.a * input.Color.a;

    output.Color = float4(finalColor * input.Color.rgb, alpha);
    // 法線をRT1に出力（[0,1]にパック + NdotVをアルファに）
    float NdotV = max(dot(N, V), 0.0);
    output.Normal = float4(N * 0.5 + 0.5, NdotV);

    return output;
}
)hlsl";

/// @brief 背面を法線方向に広げてアウトラインを描く。
constexpr const char* OUTLINE_VS_3D = R"hlsl(
cbuffer CbTransform : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float3 expandedPos = input.Position + input.Normal * 0.012;
    float4 worldPos = mul(World, float4(expandedPos, 1.0));
    float4 viewPos = mul(View, worldPos);
    output.Position = mul(Projection, viewPos);
    // アウトラインをメインパスの奥に押し出す（深度バッファで隠れる）
    output.Position.z += 0.002 * output.Position.w;
    output.Color = float4(0.1, 0.08, 0.06, 1.0);
    return output;
}
)hlsl";

constexpr const char* OUTLINE_PS_3D = R"hlsl(
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.Color;
}
)hlsl";

/// @brief フルスクリーン三角形を使うポストプロセスアウトライン用頂点シェーダー。
constexpr const char* OUTLINE_POST_VS = R"hlsl(
struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    // フルスクリーン三角形（3頂点、頂点バッファ不要）
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}
)hlsl";

/// @brief Sobel 法でエッジを検出するポストプロセスアウトライン用ピクセルシェーダー。
constexpr const char* OUTLINE_POST_PS = R"hlsl(
// MSAA 4x (ENG-105 v2)。4 サンプル全部で判定し、被覆率をアルファにする
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;   // 1.0 / viewport size
    float OutlineWidth; // アウトライン太さ（ピクセル）
    float Threshold;    // エッジ検出閾値
    float NearZ;        // カメラの near/far。決め打ちだと setCamera の値と食い違い、
    float FarZ;         //   線形化が歪んで閾値の効きが距離で変わる
    float FadeNear;     // ここまでは線を全部出す (ビュー距離)
    float FadeFar;      // ここで FadeMin まで薄くなる。FadeFar <= FadeNear なら減衰しない
    float FadeMin;      // 遠方で残す不透明度 0..1
    float Darken;       // 線を下の色へ寄せる度合い 0..1。1 = 従来のインク色そのまま
    float _pad1, _pad2;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

// outlineCaster=false の描画は法線 RT に (1,1,1) を書く (単位法線の *0.5+0.5 では出ない値)。
// その画素は縁を出さず、近傍としては中心の値に置き換えて段差ごと消す。中心側だけ抜くと
// 隣の物の画素に板の外周の線が 1px 残る
bool outlineExcluded(int2 pos, int s)
{
    return all(NormalTexture.Load(pos, s).rgb > 0.99);
}

// 近傍は端の画素で止める。画面外の Load は 0 (= near) なので、縁が段差として拾われるため
int2 inView(int2 p)
{
    uint w, h, n;
    DepthTexture.GetDimensions(w, h, n);
    return clamp(p, int2(0, 0), int2(w, h) - 1);
}

float tapDepth(int2 pos, int s, float dC)
{
    pos = inView(pos);
    return outlineExcluded(pos, s) ? dC : linearizeDepth(DepthTexture.Load(pos, s), NearZ, FarZ);
}

// 1 サンプルぶんのエッジ量。一次差分 (dC-dL) は傾いた面の勾配そのものを拾い、
// 視線すれすれの床が丸ごとエッジになる。二次差分 (dL+dR-2dC) は平面上で
// 打ち消し合って 0 になるので、本当の段差だけが残る。
float edgeAt(int2 pos, int w, int s)
{
    if (outlineExcluded(pos, s)) { return 0.0; }
    float dC = linearizeDepth(DepthTexture.Load(pos, s), NearZ, FarZ);
    float dL = tapDepth(pos + int2(-w, 0), s, dC);
    float dR = tapDepth(pos + int2( w, 0), s, dC);
    float dU = tapDepth(pos + int2( 0,-w), s, dC);
    float dD = tapDepth(pos + int2( 0, w), s, dC);
    return max(abs(dL + dR - 2.0 * dC), abs(dU + dD - 2.0 * dC));
}

// 線が属する物までの距離。中心だけを見ると、物の外側 (背景) に乗る半分の線が空の
// 深度 (far) で測られ、手前の物の輪郭まで薄くなる。縁に触れている一番近い面を取る
float nearestDepth(int2 pos, int w)
{
    float dC = linearizeDepth(DepthTexture.Load(pos, 0), NearZ, FarZ);
    float d  = dC;
    d = min(d, tapDepth(pos + int2(-w, 0), 0, dC));
    d = min(d, tapDepth(pos + int2( w, 0), 0, dC));
    d = min(d, tapDepth(pos + int2( 0,-w), 0, dC));
    d = min(d, tapDepth(pos + int2( 0, w), 0, dC));
    return d;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);

    // 二値 (出す/出さない) だと線が 1px 階段のまま乗り、シーン本体は MSAA で
    // 滑らかなのに輪郭だけがたつく。サンプルごとの smoothstep を平均して
    // 被覆率アルファにする (この PSO はアルファブレンド有効)。
    float a = 0.0;
    [unroll] for (int s = 0; s < 4; ++s)
    {
        a += smoothstep(Threshold, Threshold * 1.8, edgeAt(pos, w, s));
    }
    a *= 0.25;

    // 凹面・すれすれ面は硬い門 (NdotV > 0.15) だと境界で明滅する。滑らかに落とす。
    float NdotV = NormalTexture.Load(pos, 0).a;
    a *= smoothstep(0.10, 0.25, NdotV);

    // 遠くの小さい物 (棚の瓶) にも同じ濃さの線が乗ると画面がごちゃつく。太さではなく
    // 不透明度を落とす: 幅を細らせると 1 px を割った所で線が途切れてちらつく
    if (FadeFar > FadeNear)
    {
        float t = saturate((nearestDepth(pos, w) - FadeNear) / (FadeFar - FadeNear));
        a *= lerp(1.0, FadeMin, t);
    }

    // この PSO はアルファブレンドなので、出力は lerp(下の色, インク色, a)。被覆率に Darken を
    // 掛けるだけで「線の色 = 下の色 × (1 - Darken) + インク × Darken」になり、色を読まずに済む
    a *= Darken;

    if (a <= 0.004) { discard; }
    return float4(0.1, 0.08, 0.06, a);
}
)hlsl";

/// @brief アウトラインモード 2。隣接する法線の内積から不連続を検出し、深度は使わない。
constexpr const char* OUTLINE_POST_PS_LAPLACIAN = R"hlsl(
// MSAA 4x (ENG-105 v2)。sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
    // ここから先は DepthSobel 用。offset を合わせるためだけに並べる
    float _unusedNearZ, _unusedFarZ, _unusedFadeNear, _unusedFadeFar, _unusedFadeMin;
    float Darken;       // 線を下の色へ寄せる度合い 0..1。1 = 従来のインク色そのまま
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float3 unpackNormal(float4 data)
{
    return data.rgb * 2.0 - 1.0;
}

// outlineCaster=false の描画は法線 RT に (1,1,1) を書く (単位法線の *0.5+0.5 では出ない値)。
// その画素は縁を出さず、近傍としては中心の値に置き換えて段差ごと消す
bool outlineExcluded(int2 pos)
{
    return all(NormalTexture.Load(pos, 0).rgb > 0.99);
}

// 近傍は端の画素で止める。画面外の Load は 0 なので、縁が法線の段差として拾われるため
int2 inView(int2 p)
{
    uint w, h, n;
    NormalTexture.GetDimensions(w, h, n);
    return clamp(p, int2(0, 0), int2(w, h) - 1);
}

float3 tapNormal(int2 pos, float3 nC)
{
    pos = inView(pos);
    return outlineExcluded(pos) ? nC : unpackNormal(NormalTexture.Load(pos, 0));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    if (outlineExcluded(pos)) { discard; }
    int w = max(int(OutlineWidth), 1);

    // 中心と4近傍の法線を取得
    float3 nC = unpackNormal(NormalTexture.Load(pos, 0));
    float3 nL = tapNormal(pos + int2(-w, 0), nC);
    float3 nR = tapNormal(pos + int2( w, 0), nC);
    float3 nU = tapNormal(pos + int2( 0,-w), nC);
    float3 nD = tapNormal(pos + int2( 0, w), nC);

    // 法線の差異: 1 - dot(n1, n2) で角度差を測定
    float edge = max(max(1.0 - dot(nC, nL), 1.0 - dot(nC, nR)),
                     max(1.0 - dot(nC, nU), 1.0 - dot(nC, nD)));

    float NdotV = NormalTexture.Load(pos, 0).a;

    if (edge > Threshold && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, Darken);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief アウトラインモード 3。深度 Sobel と NdotV で凹面内部のエッジを抑える。
constexpr const char* OUTLINE_POST_PS_DEPTH_NDOTV = R"hlsl(
// MSAA 4x (ENG-105 v2)。sample 0 のみ参照
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
    float NearZ, FarZ;
    // ここから先は DepthSobel 用。offset を合わせるためだけに並べる
    float _unusedFadeNear, _unusedFadeFar, _unusedFadeMin;
    float Darken;       // 線を下の色へ寄せる度合い 0..1。1 = 従来のインク色そのまま
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float sampleDepth(int2 pos)
{
    return DepthTexture.Load(pos, 0);
}

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

// outlineCaster=false の描画は法線 RT に (1,1,1) を書く (単位法線の *0.5+0.5 では出ない値)。
// その画素は縁を出さず、近傍としては中心の値に置き換えて段差ごと消す
bool outlineExcluded(int2 pos)
{
    return all(NormalTexture.Load(pos, 0).rgb > 0.99);
}

// 近傍は端の画素で止める。画面外の Load は 0 (= near) なので、縁が段差として拾われるため
int2 inView(int2 p)
{
    uint w, h, n;
    DepthTexture.GetDimensions(w, h, n);
    return clamp(p, int2(0, 0), int2(w, h) - 1);
}

float tapDepth(int2 pos, float dC)
{
    pos = inView(pos);
    return outlineExcluded(pos) ? dC : linearizeDepth(sampleDepth(pos), NearZ, FarZ);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    if (outlineExcluded(pos)) { discard; }
    int w = max(int(OutlineWidth), 1);

    // 二次差分: 傾いた平面上では打ち消し合って 0 になり、すれすれの床を丸ごと線にしない
    float dC = linearizeDepth(sampleDepth(pos), NearZ, FarZ);
    float dL = tapDepth(pos + int2(-w, 0), dC);
    float dR = tapDepth(pos + int2( w, 0), dC);
    float dU = tapDepth(pos + int2( 0,-w), dC);
    float dD = tapDepth(pos + int2( 0, w), dC);
    float edge = max(abs(dL + dR - 2.0 * dC), abs(dU + dD - 2.0 * dC));

    // NdotVフィルタ: 凹面内部(NdotV低)を抑制
    float4 normalData = NormalTexture.Load(pos, 0);
    float NdotV = normalData.a;
    float ndotVMask = smoothstep(0.1, 0.35, NdotV);

    if (edge * ndotVMask > Threshold)
    {
        return float4(0.1, 0.08, 0.06, Darken);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief OutlineMode::ColorEdge。輝度差から色エッジを検出する。
/// @details m_colorEdgeSRVHeap の並びに合わせ、t0 に色コピー、t1 に除外印と NdotV 用の法線を置く。
constexpr const char* OUTLINE_POST_PS_COLOR_EDGE = R"hlsl(
Texture2D<float4> ColorTexture : register(t0);
// MSAA 4x (ENG-105 v2)。sample 0 のみ参照
Texture2DMS<float4, 4> NormalTexture : register(t1);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
    // ここから先は DepthSobel 用。offset を合わせるためだけに並べる
    float _unusedNearZ, _unusedFarZ, _unusedFadeNear, _unusedFadeFar, _unusedFadeMin;
    float Darken;       // 線を下の色へ寄せる度合い 0..1。1 = 従来のインク色そのまま
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float luminance(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

// outlineCaster=false の描画は法線 RT に (1,1,1) を書く (単位法線の *0.5+0.5 では出ない値)。
// その画素は縁を出さず、近傍としては中心の値に置き換えて段差ごと消す
bool outlineExcluded(int2 pos)
{
    return all(NormalTexture.Load(pos, 0).rgb > 0.99);
}

// 近傍は端の画素で止める。画面外の Load は 0 (= 黒) なので、縁が輝度の段差として拾われるため
int2 inView(int2 p)
{
    uint w, h;
    ColorTexture.GetDimensions(w, h);
    return clamp(p, int2(0, 0), int2(w, h) - 1);
}

float tapLum(int2 pos, float lC)
{
    pos = inView(pos);
    return outlineExcluded(pos) ? lC : luminance(ColorTexture.Load(int3(pos, 0)).rgb);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    if (outlineExcluded(pos)) { discard; }
    int w = max(int(OutlineWidth), 1);

    float lC = luminance(ColorTexture.Load(int3(pos, 0)).rgb);
    float edge = max(max(abs(lC - tapLum(pos + int2(-w, 0), lC)), abs(lC - tapLum(pos + int2( w, 0), lC))),
                     max(abs(lC - tapLum(pos + int2( 0,-w), lC)), abs(lC - tapLum(pos + int2( 0, w), lC))));

    float NdotV = NormalTexture.Load(pos, 0).a;

    if (edge > Threshold && NdotV > 0.15)
    {
        return float4(0.1, 0.08, 0.06, Darken);
    }

    discard;
    return float4(0, 0, 0, 0);
}
)hlsl";

/// @brief OutlineMode::DepthColorCombo。outline3D の既定として深度と色のエッジを組み合わせる。
/// @details t0 に深度、t1 に法線、t2 に色バッファコピーを置く。
constexpr const char* OUTLINE_POST_PS_DEPTH_COLOR = R"hlsl(
// 深度の判定は OUTLINE_POST_PS と同じ (二次差分・4 サンプルの被覆率・距離で減衰)。
// 色の段差は、弱い深度の段差を伴う所でだけ線にする (床の模様やテクスチャの柄は拾わない)
Texture2DMS<float, 4> DepthTexture : register(t0);
Texture2DMS<float4, 4> NormalTexture : register(t1);
Texture2D<float4> ColorTexture : register(t2);

cbuffer CbOutline : register(b0)
{
    float2 TexelSize;
    float OutlineWidth;
    float Threshold;
    float NearZ, FarZ;
    float FadeNear, FadeFar, FadeMin;
    float Darken;       // 線を下の色へ寄せる度合い 0..1。1 = 従来のインク色そのまま
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float linearizeDepth(float d, float nearZ, float farZ)
{
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float luminance(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

// outlineCaster=false の描画は法線 RT に (1,1,1) を書く (単位法線の *0.5+0.5 では出ない値)。
// その画素は縁を出さず、近傍としては中心の値に置き換えて段差ごと消す
bool outlineExcluded(int2 pos, int s)
{
    return all(NormalTexture.Load(pos, s).rgb > 0.99);
}

// 近傍は端の画素で止める。画面外の Load は 0 (= near) なので、縁が段差として拾われるため
int2 inView(int2 p)
{
    uint w, h, n;
    DepthTexture.GetDimensions(w, h, n);
    return clamp(p, int2(0, 0), int2(w, h) - 1);
}

float tapDepth(int2 pos, int s, float dC)
{
    pos = inView(pos);
    return outlineExcluded(pos, s) ? dC : linearizeDepth(DepthTexture.Load(pos, s), NearZ, FarZ);
}

float tapLum(int2 pos, float lC)
{
    pos = inView(pos);
    return outlineExcluded(pos, 0) ? lC : luminance(ColorTexture.Load(int3(pos, 0)).rgb);
}

// 1 方向の段差 (1/距離 の二次差分)。両隣の傾き a・b の食い違いを、大きい方の傾きで割った比で門をかける。
// 隠れの段差は片側だけが跳ぶので比が 1 前後、すれすれに見た面の折れ目は同じ向きの急な傾きが
// 少し変わるだけなので比が小さい (これを通すと稜線の端や起伏の遠景が筋になる)
float stepAcross(float iA, float iC, float iB)
{
    float a = iA - iC;
    float b = iC - iB;
    float bend = abs(a - b);
    float ratio = bend / max(max(abs(a), abs(b)), 1e-12);
    // 段差は、続いている側の面が 1 画素で変える深度の何倍か。稜線が奥の斜面へ溶ける所は、すれすれの
    // 面に途切れ途切れの小さな隙間ができ、面自身の傾きと同程度の段差が筋の束になる。正面向きの面の
    // 上の段差 (体の前の手) は面の傾きがほぼ 0 なので、小さくても残る
    float standOut = bend / max(min(abs(a), abs(b)), 1e-12);
    return bend * smoothstep(0.35, 0.7, ratio) * smoothstep(2.0, 6.0, standOut);
}

float depthEdgeAt(int2 pos, int w, int s)
{
    if (outlineExcluded(pos, s)) { return 0.0; }
    float dC = linearizeDepth(DepthTexture.Load(pos, s), NearZ, FarZ);
    float dL = tapDepth(pos + int2(-w, 0), s, dC);
    float dR = tapDepth(pos + int2( w, 0), s, dC);
    float dU = tapDepth(pos + int2( 0,-w), s, dC);
    float dD = tapDepth(pos + int2( 0, w), s, dC);
    // 平面上で画面に対して線形なのは 1/距離。距離そのものの二次差分は遠くの床ほど 0 から外れ、
    // 地平線が線になる。1/距離 で二次差分を取り、中心の距離の 2 乗を掛けてメートルへ戻す
    float iC = 1.0 / dC;
    float h = stepAcross(1.0 / dL, iC, 1.0 / dR);
    float v = stepAcross(1.0 / dU, iC, 1.0 / dD);
    // 10 m より先は段差を距離に対する割合で測る。メートルのままだと遠景ほど小さな起伏まで線になる
    return max(h, v) * dC * dC / max(1.0, dC / 10.0);
}

// 線が属する物までの距離。縁に触れている一番近い面を取る (OUTLINE_POST_PS と同じ理由)
float nearestDepth(int2 pos, int w)
{
    float dC = linearizeDepth(DepthTexture.Load(pos, 0), NearZ, FarZ);
    float d  = dC;
    d = min(d, tapDepth(pos + int2(-w, 0), 0, dC));
    d = min(d, tapDepth(pos + int2( w, 0), 0, dC));
    d = min(d, tapDepth(pos + int2( 0,-w), 0, dC));
    d = min(d, tapDepth(pos + int2( 0, w), 0, dC));
    return d;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2 pos = int2(input.Position.xy);
    int w = max(int(OutlineWidth), 1);

    // 色の段差は解決済みの色バッファでしか測れないので画素単位
    float lC = luminance(ColorTexture.Load(int3(pos, 0)).rgb);
    float colorEdge = max(max(abs(lC - tapLum(pos + int2(-w, 0), lC)), abs(lC - tapLum(pos + int2( w, 0), lC))),
                          max(abs(lC - tapLum(pos + int2( 0,-w), lC)), abs(lC - tapLum(pos + int2( 0, w), lC))));
    float colorA = smoothstep(Threshold * 0.3, Threshold * 0.54, colorEdge);

    float a = 0.0;
    [unroll] for (int s = 0; s < 4; ++s)
    {
        float e = depthEdgeAt(pos, w, s);
        float depthA = smoothstep(Threshold, Threshold * 1.8, e);
        float weakA  = smoothstep(Threshold * 0.3, Threshold * 0.54, e);
        a += max(depthA, colorA * weakA);
    }
    a *= 0.25;

    // NdotV では弱めない。外形の線は縁 (NdotV≒0) か空・すれすれの床 (NdotV 小) の画素に乗るので
    // 外形ほど消える。平面の誤検出は二次差分が先に消している

    if (FadeFar > FadeNear)
    {
        float t = saturate((nearestDepth(pos, w) - FadeNear) / (FadeFar - FadeNear));
        a *= lerp(1.0, FadeMin, t);
    }

    a *= Darken;

    if (a <= 0.004) { discard; }
    return float4(0.1, 0.08, 0.06, a);
}
)hlsl";

// アウトラインモード 6 (Fresnel) の PS は DX12 VS の出力 signature に合わせた
// MRT 変種 DX12_TOON_PS_3D_FRESNEL (dx12/DX12ShaderModePS.hpp) へ移行済み。

} // namespace mitiru::render
