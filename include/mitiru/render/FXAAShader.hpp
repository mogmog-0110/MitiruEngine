#pragma once

/// @file FXAAShader.hpp
/// @brief FXAA 3.11ポストプロセスパス
/// @details NVIDIA FXAA 3.11アルゴリズムに基づく高速近似アンチエイリアシング。
///          PostProcessPassとして実装し、ポストプロセスチェーンの最終段に挿入する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string_view>

#include <d3d11.h>
#include <wrl/client.h>

#include <mitiru/render/PostProcess.hpp>

namespace mitiru::render
{

// ============================================================================
// HLSL定数 — FXAAピクセルシェーダー
// ============================================================================

/// @brief FXAA 3.11ピクセルシェーダー
/// @details エッジ検出 → エッジ方向判定 → エッジ端点探索 → サブピクセルブレンド
constexpr std::string_view PP_FXAA_PS = R"hlsl(
Texture2D sceneTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FXAAParams : register(b0)
{
	float2 rcpFrame;          // 1.0 / screenSize
	float subpixQuality;      // サブピクセル品質 (0.0-1.0, default 0.75)
	float edgeThreshold;      // エッジ検出閾値 (default 0.166)
	float edgeThresholdMin;   // 最小エッジ閾値 (default 0.0833)
	float3 fxaaPadding;       // アライメントパディング
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// ── 知覚輝度計算（緑チャネル重み付き） ──────────────────
float FxaaLuma(float3 rgb)
{
	return dot(rgb, float3(0.299, 0.587, 0.114));
}

// ── オフセットテクスチャサンプル ──────────────────────
float3 FxaaTexOff(float2 uv, float2 offset)
{
	return sceneTexture.Sample(linearSampler, uv + offset * rcpFrame).rgb;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.texCoord;

	// ════════════════════════════════════════════════════
	// ステップ1: 中心と4近傍の輝度を取得する
	// ════════════════════════════════════════════════════
	float3 rgbM = sceneTexture.Sample(linearSampler, uv).rgb;
	float lumaM = FxaaLuma(rgbM);
	float lumaN = FxaaLuma(FxaaTexOff(uv, float2( 0, -1)));
	float lumaS = FxaaLuma(FxaaTexOff(uv, float2( 0,  1)));
	float lumaE = FxaaLuma(FxaaTexOff(uv, float2( 1,  0)));
	float lumaW = FxaaLuma(FxaaTexOff(uv, float2(-1,  0)));

	// ════════════════════════════════════════════════════
	// ステップ2: ローカルコントラストを計算してエッジ判定する
	// ════════════════════════════════════════════════════
	float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
	float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
	float lumaRange = lumaMax - lumaMin;

	// コントラストが低すぎる場合はFXAAをスキップする
	if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold))
	{
		return float4(rgbM, 1.0);
	}

	// ════════════════════════════════════════════════════
	// ステップ3: 対角近傍も取得してエッジ方向を判定する
	// ════════════════════════════════════════════════════
	float lumaNW = FxaaLuma(FxaaTexOff(uv, float2(-1, -1)));
	float lumaNE = FxaaLuma(FxaaTexOff(uv, float2( 1, -1)));
	float lumaSW = FxaaLuma(FxaaTexOff(uv, float2(-1,  1)));
	float lumaSE = FxaaLuma(FxaaTexOff(uv, float2( 1,  1)));

	float lumaNS = lumaN + lumaS;
	float lumaEW = lumaE + lumaW;

	float lumaNWSW = lumaNW + lumaSW;
	float lumaNENE2 = lumaNE + lumaSE;
	float lumaNWNE = lumaNW + lumaNE;
	float lumaSWSE = lumaSW + lumaSE;

	// 水平勾配 vs 垂直勾配
	float edgeHorz = abs(lumaNWSW - 2.0 * lumaW) +
	                 abs(lumaNS   - 2.0 * lumaM) * 2.0 +
	                 abs(lumaNENE2 - 2.0 * lumaE);
	float edgeVert = abs(lumaNWNE - 2.0 * lumaN) +
	                 abs(lumaEW   - 2.0 * lumaM) * 2.0 +
	                 abs(lumaSWSE - 2.0 * lumaS);

	bool isHorizontal = (edgeHorz >= edgeVert);

	// ════════════════════════════════════════════════════
	// ステップ4: エッジに垂直な方向のステップサイズを決定する
	// ════════════════════════════════════════════════════
	float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;

	float luma1 = isHorizontal ? lumaN : lumaW;
	float luma2 = isHorizontal ? lumaS : lumaE;

	float gradient1 = luma1 - lumaM;
	float gradient2 = luma2 - lumaM;

	bool is1Steepest = abs(gradient1) >= abs(gradient2);
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	// エッジの法線方向にオフセットする
	if (!is1Steepest)
	{
		stepLength = -stepLength;
	}

	float lumaLocalAvg = 0.0;
	if (is1Steepest)
	{
		lumaLocalAvg = 0.5 * (luma1 + lumaM);
	}
	else
	{
		lumaLocalAvg = 0.5 * (luma2 + lumaM);
	}

	// エッジ中心にUVをオフセットする
	float2 currentUV = uv;
	if (isHorizontal)
	{
		currentUV.y += stepLength * 0.5;
	}
	else
	{
		currentUV.x += stepLength * 0.5;
	}

	// ════════════════════════════════════════════════════
	// ステップ5: エッジに沿って端点を探索する（最大12ステップ）
	// ════════════════════════════════════════════════════
	float2 offset2 = isHorizontal
		? float2(rcpFrame.x, 0.0)
		: float2(0.0, rcpFrame.y);

	float2 uv1 = currentUV - offset2;
	float2 uv2 = currentUV + offset2;

	float lumaEnd1 = FxaaLuma(sceneTexture.Sample(linearSampler, uv1).rgb);
	float lumaEnd2 = FxaaLuma(sceneTexture.Sample(linearSampler, uv2).rgb);
	lumaEnd1 -= lumaLocalAvg;
	lumaEnd2 -= lumaLocalAvg;

	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;
	bool reachedBoth = reached1 && reached2;

	if (!reached1) uv1 -= offset2;
	if (!reached2) uv2 += offset2;

	// 端点探索ループ（最大12ステップ）
	// 品質係数でステップサイズを段階的に拡大する
	static const float QUALITY[12] = {
		1.0, 1.0, 1.0, 1.0, 1.0,
		1.5, 2.0, 2.0, 2.0, 2.0,
		4.0, 8.0
	};

	[unroll]
	for (int i = 0; i < 12 && !reachedBoth; i++)
	{
		if (!reached1)
		{
			lumaEnd1 = FxaaLuma(
				sceneTexture.Sample(linearSampler, uv1).rgb);
			lumaEnd1 -= lumaLocalAvg;
		}
		if (!reached2)
		{
			lumaEnd2 = FxaaLuma(
				sceneTexture.Sample(linearSampler, uv2).rgb);
			lumaEnd2 -= lumaLocalAvg;
		}

		reached1 = abs(lumaEnd1) >= gradientScaled;
		reached2 = abs(lumaEnd2) >= gradientScaled;
		reachedBoth = reached1 && reached2;

		if (!reached1) uv1 -= offset2 * QUALITY[i];
		if (!reached2) uv2 += offset2 * QUALITY[i];
	}

	// ════════════════════════════════════════════════════
	// ステップ6: 端点までの距離からブレンド係数を計算する
	// ════════════════════════════════════════════════════
	float dist1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
	float dist2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

	bool isDir1 = dist1 < dist2;
	float distFinal = min(dist1, dist2);
	float edgeLength = dist1 + dist2;

	float pixelOffset = -distFinal / edgeLength + 0.5;

	// エッジの反対側にあるかチェックする
	bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
	bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0)
		!= isLumaCenterSmaller;
	float finalOffset = correctVariation ? pixelOffset : 0.0;

	// ════════════════════════════════════════════════════
	// ステップ7: サブピクセルアンチエイリアシング
	// ════════════════════════════════════════════════════
	float lumaAvg = (1.0 / 12.0) * (2.0 * lumaNS + 2.0 * lumaEW +
		lumaNWSW + lumaNENE2);
	float subpixOffset1 = saturate(abs(lumaAvg - lumaM) / lumaRange);
	float subpixOffset2 = (-2.0 * subpixOffset1 + 3.0) *
		subpixOffset1 * subpixOffset1;
	float subpixOffsetFinal = subpixOffset2 * subpixOffset2 *
		subpixQuality;

	// エッジオフセットとサブピクセルオフセットの大きい方を使う
	finalOffset = max(finalOffset, subpixOffsetFinal);

	// ════════════════════════════════════════════════════
	// ステップ8: 最終サンプリング
	// ════════════════════════════════════════════════════
	float2 finalUV = uv;
	if (isHorizontal)
	{
		finalUV.y += finalOffset * stepLength;
	}
	else
	{
		finalUV.x += finalOffset * stepLength;
	}

	float3 finalColor = sceneTexture.Sample(linearSampler, finalUV).rgb;
	return float4(finalColor, 1.0);
}
)hlsl";

// ============================================================================
// FXAAQuality — 品質プリセット
// ============================================================================

/// @brief FXAA品質プリセット
enum class FXAAQuality
{
	Low,       ///< 高速・低品質（モバイル向け）
	Medium,    ///< バランス（デフォルト）
	High       ///< 高品質・低速
};

// ============================================================================
// FXAAConfig — FXAA設定
// ============================================================================

/// @brief FXAA設定
struct FXAAConfig
{
	float subpixQuality = 0.75f;       ///< サブピクセル品質 (0.0-1.0)
	float edgeThreshold = 0.166f;      ///< エッジ検出閾値
	float edgeThresholdMin = 0.0833f;  ///< 最小エッジ閾値

	/// @brief 品質プリセットからFXAAConfigを生成する
	/// @param quality 品質プリセット
	/// @return 対応するFXAAConfig
	[[nodiscard]] static FXAAConfig fromQuality(
		FXAAQuality quality) noexcept
	{
		FXAAConfig cfg;
		switch (quality)
		{
		case FXAAQuality::Low:
			cfg.subpixQuality = 0.50f;
			cfg.edgeThreshold = 0.250f;
			cfg.edgeThresholdMin = 0.0833f;
			break;
		case FXAAQuality::Medium:
			cfg.subpixQuality = 0.75f;
			cfg.edgeThreshold = 0.166f;
			cfg.edgeThresholdMin = 0.0833f;
			break;
		case FXAAQuality::High:
			cfg.subpixQuality = 1.00f;
			cfg.edgeThreshold = 0.063f;
			cfg.edgeThresholdMin = 0.0312f;
			break;
		}
		return cfg;
	}
};

// ============================================================================
// FXAAPass — FXAAポストプロセスパス
// ============================================================================

/// @brief FXAAポストプロセスパス
/// @details FXAA 3.11に基づく高速近似アンチエイリアシング。
///          ポストプロセスチェーンの最終段（ブルーム・カラーグレーディング後）に
///          挿入してスクリーンスペースAAを適用する。
///
/// @code
/// // プリセットから生成する
/// auto fxaa = FXAAPass::medium(device, fullscreenVS, sampler);
///
/// // カスタム設定で生成する
/// FXAAPass fxaa(device, fullscreenVS, sampler);
/// fxaa.setQuality(0.9f, 0.1f, 0.05f);
/// @endcode
class FXAAPass final : public PostProcessPass
{
public:
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief コンストラクタ（デフォルトはMedium品質）
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	FXAAPass(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
		: m_fullscreenVS(fullscreenVS)
		, m_sampler(sampler)
	{
		m_ps = compilePostProcessPS(device, PP_FXAA_PS);
		m_cb = createConstantBuffer(device, sizeof(FXAACB));
	}

	/// @brief Low品質プリセットで生成する
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	/// @return Low品質のFXAAPass
	[[nodiscard]] static FXAAPass low(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
	{
		FXAAPass pass(device, fullscreenVS, sampler);
		pass.setConfig(FXAAConfig::fromQuality(FXAAQuality::Low));
		return pass;
	}

	/// @brief Medium品質プリセットで生成する
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	/// @return Medium品質のFXAAPass
	[[nodiscard]] static FXAAPass medium(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
	{
		FXAAPass pass(device, fullscreenVS, sampler);
		pass.setConfig(FXAAConfig::fromQuality(FXAAQuality::Medium));
		return pass;
	}

	/// @brief High品質プリセットで生成する
	/// @param device D3D11デバイス
	/// @param fullscreenVS フルスクリーン頂点シェーダー（共有）
	/// @param sampler リニアサンプラー（共有）
	/// @return High品質のFXAAPass
	[[nodiscard]] static FXAAPass high(
		ID3D11Device* device,
		const ComPtr<ID3D11VertexShader>& fullscreenVS,
		const ComPtr<ID3D11SamplerState>& sampler)
	{
		FXAAPass pass(device, fullscreenVS, sampler);
		pass.setConfig(FXAAConfig::fromQuality(FXAAQuality::High));
		return pass;
	}

	/// @brief FXAA設定を変更する
	/// @param cfg FXAA設定
	void setConfig(const FXAAConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief 品質パラメータを直接設定する
	/// @param subpixel サブピクセル品質 (0.0-1.0)
	/// @param edgeThreshold エッジ検出閾値
	/// @param edgeThresholdMin 最小エッジ閾値
	void setQuality(
		float subpixel,
		float edgeThreshold,
		float edgeThresholdMin) noexcept
	{
		m_config.subpixQuality = subpixel;
		m_config.edgeThreshold = edgeThreshold;
		m_config.edgeThresholdMin = edgeThresholdMin;
	}

	void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) override
	{
		if (screenW == 0 || screenH == 0)
		{
			return;
		}

		/// 定数バッファを更新する
		FXAACB cbData = {};
		cbData.rcpFrame[0] = 1.0f / static_cast<float>(screenW);
		cbData.rcpFrame[1] = 1.0f / static_cast<float>(screenH);
		cbData.subpixQuality = m_config.subpixQuality;
		cbData.edgeThreshold = m_config.edgeThreshold;
		cbData.edgeThresholdMin = m_config.edgeThresholdMin;
		cbData.padding[0] = 0.0f;
		cbData.padding[1] = 0.0f;
		cbData.padding[2] = 0.0f;
		updateConstantBuffer(context, m_cb.Get(),
			&cbData, sizeof(cbData));

		/// フルスクリーン三角形でFXAAを適用する
		drawFullscreenPass(context,
			m_fullscreenVS.Get(), m_ps.Get(),
			inputSRV, outputRTV,
			m_sampler.Get(), m_cb.Get(),
			screenW, screenH);
	}

	[[nodiscard]] std::string_view name() const noexcept override
	{
		return "FXAA";
	}

	/// @brief 現在の設定を取得する
	[[nodiscard]] const FXAAConfig& config() const noexcept
	{
		return m_config;
	}

private:
	/// @brief FXAA定数バッファレイアウト（32バイト、16バイトアライン）
	struct FXAACB
	{
		float rcpFrame[2];         ///< 1.0 / screenSize
		float subpixQuality;       ///< サブピクセル品質
		float edgeThreshold;       ///< エッジ検出閾値
		float edgeThresholdMin;    ///< 最小エッジ閾値
		float padding[3];          ///< アライメントパディング
	};

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11PixelShader> m_ps;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11Buffer> m_cb;
	FXAAConfig m_config;
};

} // namespace mitiru::render

#endif // _WIN32
