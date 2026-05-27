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

float4 PSMain(PSIn i) : SV_TARGET
{
	float3 c = src.Sample(samp, i.uv).rgb;          // POINT = ニアレスト拡大

	// ディザ位相は低解像テクセル基準（網点を内部解像度グリッドに揃える）
	int2 p = int2(floor(i.uv * float2(texW, texH)));
	float bn = 0.0;
	if (doDither > 0.5)
		bn = (BAYER[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0 - 0.5;

	if (doQuantize > 0.5)
	{
		c.r = qch(c.r, bitsR, bn, ditherStrength);
		c.g = qch(c.g, bitsG, bn, ditherStrength);
		c.b = qch(c.b, bitsB, bn, ditherStrength);
	}
	return float4(c, 1.0);
}
)hlsl";

} // namespace mitiru::render::lofi
