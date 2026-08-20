#pragma once
/// @file LoFiShader.hpp
/// @brief ローファイ・ポストFX 用フルスクリーン HLSL（量子化 + Bayer ディザ + ニアレスト拡大）。
/// @details 低解像オフスクリーン RT を point サンプルし、LoFiQuantize.hpp と**同一式**で
///          パレット量子化 + 4×4 Bayer ディザを掛けてバックバッファへ書く。
///          ディザ位相は低解像テクセル基準（floor(uv*texSize)）にして、網点を内部解像度の
///          ピクセルグリッドに揃える（拡大後もチャンキーな網点に見える）。

#include <string_view>

namespace mitiru::render::lofi
{

/// @brief ポスト処理パラメータ定数バッファ（HLSL b0 と一致させる）。
struct LoFiParamsCB
{
	float texW = 320.0f, texH = 240.0f; ///< 内部解像度（ディザ位相算出に使う）
	float bitsR = 5.0f, bitsG = 6.0f;   ///< 量子化ビット数（RGB565 既定）
	float bitsB = 5.0f;
	float ditherStrength = 1.0f;        ///< ディザ強度
	float doQuantize = 1.0f;            ///< 量子化 ON/OFF（>0.5 で ON）
	float doDither = 1.0f;              ///< ディザ ON/OFF
	float soft = 1.0f;                  ///< 拡大: >0.5 = 柔らか (テクセル量子化のままバイリニア)、
	                                    ///<        0 = ニアレストの硬いピクセル
	float viFilter = 0.0f;              ///< 映像出力段の de-dither + divot（>0.5 で ON、拡大はニアレスト固定）
	float gamma = 1.0f;                 ///< 出力ガンマ（1 で素通し）
	float _pad2 = 0.0f;
};

/// @brief フルスクリーン三角形 VS（SV_VertexID で頂点バッファ不要）。
constexpr std::string_view LOFI_VS = R"hlsl(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
	VSOut o;
	float2 uv = float2((id << 1) & 2, id & 2); // (0,0),(2,0),(0,2)
	o.uv  = uv;
	o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return o;
}
)hlsl";

/// @brief 量子化 + Bayer ディザ PS（LoFiQuantize.hpp と同一式）。
constexpr std::string_view LOFI_PS = R"hlsl(
Texture2D    src     : register(t0);
SamplerState samp    : register(s0); // POINT（ニアレスト）

cbuffer Params : register(b0)
{
	float texW; float texH; float bitsR; float bitsG;
	float bitsB; float ditherStrength; float doQuantize; float doDither;
	float soft; float viFilter; float gamma; float _pad2;
};

struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

static const float BAYER[16] = {
	 0.0,  8.0,  2.0, 10.0,
	12.0,  4.0, 14.0,  6.0,
	 3.0, 11.0,  1.0,  9.0,
	15.0,  7.0, 13.0,  5.0
};

float qch(float v, float bits, float bn, float strength)
{
	if (bits <= 0.0 || bits >= 8.0) return v;       // 0/8bit は素通し
	float levels = exp2(bits) - 1.0;                // 例 5bit→31
	float step   = 1.0 / levels;
	float d      = saturate(v + bn * step * strength);
	return floor(d * levels + 0.5) / levels;        // 最近傍量子化
}

float bayerAt(int2 p)
{
	return (BAYER[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0 - 0.5;
}

float3 quantTexel(int2 p, float2 invTex)
{
	// テクセル中心を POINT サンプルし、そのテクセルのディザ位相で量子化する
	float2 uv = (float2(p) + 0.5) * invTex;
	float3 c = src.Sample(samp, uv).rgb;
	float bn = (doDither > 0.5) ? bayerAt(p) : 0.0;
	if (doQuantize > 0.5)
	{
		c.r = qch(c.r, bitsR, bn, ditherStrength);
		c.g = qch(c.g, bitsG, bn, ditherStrength);
		c.b = qch(c.b, bitsB, bn, ditherStrength);
	}
	return c;
}

// 上位 5bit だけ見て 8 近傍との差を ±1 に丸めて足し戻す (LoFiQuantize.hpp と同一式)。
// w は 0..255 の 5x3 窓、cx は列 (1..3)、中央行は 1。
float3 deDither(float3 w[5][3], int cx)
{
	float3 c5 = floor(w[cx][1] / 8.0);
	float3 accum = float3(0.0, 0.0, 0.0);
	[unroll] for (int oy = 0; oy < 3; ++oy)
	{
		[unroll] for (int ox = -1; ox <= 1; ++ox)
		{
			accum += clamp(floor(w[cx + ox][oy] / 8.0) - c5, -1.0, 1.0);
		}
	}
	return clamp(c5 * 8.0 + accum, 0.0, 255.0);
}

// de-dither → divot (横 3 タップの中央値)
float3 viFilterAt(int2 p, float2 invTex)
{
	float3 w[5][3];
	[unroll] for (int ix = 0; ix < 5; ++ix)
	{
		[unroll] for (int iy = 0; iy < 3; ++iy)
		{
			w[ix][iy] = round(quantTexel(int2(p.x + ix - 2, p.y + iy - 1), invTex) * 255.0);
		}
	}
	float3 l = deDither(w, 1);
	float3 m = deDither(w, 2);
	float3 r = deDither(w, 3);
	float3 lo = min(l, r);
	float3 hi = max(l, r);
	return clamp(m, lo, hi) / 255.0;
}

float4 PSMain(PSIn i) : SV_TARGET
{
	float2 tex = float2(texW, texH);
	float2 invTex = 1.0 / tex;
	float3 c;
	if (viFilter > 0.5)
	{
		// 映像出力段が網点を解いて縁を潰す。柔らかさはここで付くので拡大はニアレスト。
		c = viFilterAt(int2(floor(i.uv * tex)), invTex);
	}
	else if (soft > 0.5)
	{
		// 柔らか拡大: 4 近傍テクセルを個別に量子化してからバイリニア合成
		// (パレットはテクセル単位のまま、エッジだけ実機の映像出力ぽく滲む)
		float2 st = i.uv * tex - 0.5;
		float2 f  = frac(st);
		int2  p0  = int2(floor(st));
		float3 c00 = quantTexel(p0,               invTex);
		float3 c10 = quantTexel(p0 + int2(1, 0),  invTex);
		float3 c01 = quantTexel(p0 + int2(0, 1),  invTex);
		float3 c11 = quantTexel(p0 + int2(1, 1),  invTex);
		c = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
	}
	else
	{
		// 硬いピクセル: ニアレスト
		c = quantTexel(int2(floor(i.uv * tex)), invTex);
	}
	if (gamma != 1.0) { c = pow(saturate(c), gamma); }
	return float4(c, 1.0);
}
)hlsl";

} // namespace mitiru::render::lofi
