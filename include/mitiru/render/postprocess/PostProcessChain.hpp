#pragma once

/// @file PostProcessChain.hpp
/// @brief ポストプロセスチェーン実行器と統合設定

#ifdef _WIN32

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>
#include <mitiru/render/postprocess/PostProcessPass.hpp>
#include <mitiru/render/postprocess/GaussianBlurPass.hpp>
#include <mitiru/render/postprocess/BloomPass.hpp>
#include <mitiru/render/postprocess/ColorGradingPass.hpp>
#include <mitiru/render/postprocess/AtmosphericEffects.hpp>
#include <mitiru/render/postprocess/TransitionEffects.hpp>

namespace mitiru::render
{

// ============================================================================
// PostProcessConfig — 統合設定
// ============================================================================

/// @brief ポストプロセス統合設定
/// @details 全パスのデフォルト値を一括で保持する。
struct PostProcessConfig
{
	/// @brief ブルーム設定
	struct
	{
		bool enabled = false;
		float threshold = 0.8f;
		float intensity = 1.0f;
		int blurRadius = 8;
	} bloom;

	/// @brief カラーグレーディング設定
	struct
	{
		bool enabled = false;
		float brightness = 1.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float gamma = 1.0f;
		float tintR = 1.0f;
		float tintG = 1.0f;
		float tintB = 1.0f;
	} colorGrading;

	/// @brief ビネット設定
	struct
	{
		bool enabled = false;
		float intensity = 0.5f;
		float radius = 0.8f;
		float softness = 0.5f;
	} vignette;

	/// @brief 色収差設定
	struct
	{
		bool enabled = false;
		float intensity = 1.0f;
	} chromaticAberration;

	/// @brief フィルムグレイン設定
	struct
	{
		bool enabled = false;
		float intensity = 0.05f;
		float speed = 1.0f;
	} filmGrain;

	/// @brief フェード設定
	struct
	{
		bool enabled = false;
		float colorR = 0.0f;
		float colorG = 0.0f;
		float colorB = 0.0f;
		float progress = 0.0f;
	} fade;

	/// @brief フロストグラス設定
	struct
	{
		bool enabled = false;
		float blurAmount = 2.0f;
		float tintR = 0.9f;
		float tintG = 0.95f;
		float tintB = 1.0f;
	} frostGlass;

	/// @brief FXAA設定
	struct
	{
		bool enabled = false;
		float subpixQuality = 0.75f;       ///< サブピクセル品質 (0.0-1.0)
		float edgeThreshold = 0.166f;      ///< エッジ検出閾値
		float edgeThresholdMin = 0.0833f;  ///< 最小エッジ閾値
	} fxaa;
};

// ============================================================================
// PostProcessChain — パスチェーン実行器
// ============================================================================

/// @brief ポストプロセスチェーン
/// @details 複数のPostProcessPassを順次実行し、
///          ピンポンレンダーターゲットで中間結果を受け渡す。
///
/// @code
/// auto chain = PostProcessChain(device, 1280, 720);
/// chain.addPass(std::make_unique<BloomPass>(...));
/// chain.addPass(std::make_unique<VignettePass>(...));
///
/// // フレームごと:
/// chain.execute(context, sceneTextureSRV, backbufferRTV);
/// @endcode
class PostProcessChain
{
public:
	/// @brief コンストラクタ
	/// @param device D3D11デバイス
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	PostProcessChain(
		ID3D11Device* device,
		std::uint32_t screenW,
		std::uint32_t screenH)
		: m_device(device)
		, m_screenW(screenW)
		, m_screenH(screenH)
	{
		/// ピンポンバッファを2つ生成する
		m_pingPong[0] = createRenderTarget(device, screenW, screenH);
		m_pingPong[1] = createRenderTarget(device, screenW, screenH);
	}

	/// @brief パスを末尾に追加する
	/// @param pass 追加するパス
	void addPass(std::unique_ptr<PostProcessPass> pass)
	{
		m_passes.push_back(std::move(pass));
	}

	/// @brief 指定インデックスのパスを削除する
	/// @param index 削除対象インデックス
	void removePass(std::size_t index)
	{
		if (index < m_passes.size())
		{
			m_passes.erase(m_passes.begin() +
				static_cast<std::ptrdiff_t>(index));
		}
	}

	/// @brief パスを取得する
	/// @param index パスインデックス
	/// @return パスへのポインタ（範囲外ならnullptr）
	[[nodiscard]] PostProcessPass* getPass(
		std::size_t index) const noexcept
	{
		if (index < m_passes.size())
		{
			return m_passes[index].get();
		}
		return nullptr;
	}

	/// @brief パス数を取得する
	[[nodiscard]] std::size_t passCount() const noexcept
	{
		return m_passes.size();
	}

	/// @brief 有効なパス数を取得する
	[[nodiscard]] std::size_t enabledPassCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& pass : m_passes)
		{
			if (pass->isEnabled())
			{
				++count;
			}
		}
		return count;
	}

	/// @brief 全パスをチェーン実行する
	/// @param context D3D11コンテキスト
	/// @param sceneTextureSRV シーン描画結果のSRV
	/// @param finalRTV 最終出力先（通常はバックバッファRTV）
	void execute(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* sceneTextureSRV,
		ID3D11RenderTargetView* finalRTV)
	{
		/// 有効なパスを収集する
		std::vector<PostProcessPass*> activePasses;
		activePasses.reserve(m_passes.size());
		for (const auto& pass : m_passes)
		{
			if (pass->isEnabled())
			{
				activePasses.push_back(pass.get());
			}
		}

		/// 有効パスがなければ何もしない
		if (activePasses.empty())
		{
			return;
		}

		/// ピンポンバッファでチェーン実行する
		ID3D11ShaderResourceView* currentInput = sceneTextureSRV;
		std::size_t pingPongIdx = 0;

		for (std::size_t i = 0; i < activePasses.size(); ++i)
		{
			const bool isLast =
				(i == activePasses.size() - 1);

			/// 最終パスはfinalRTVに直接出力する
			ID3D11RenderTargetView* outputRTV = isLast
				? finalRTV
				: m_pingPong[pingPongIdx].rtv.Get();

			activePasses[i]->apply(
				context, currentInput, outputRTV,
				m_screenW, m_screenH);

			if (!isLast)
			{
				/// 次パスの入力は今回の出力
				currentInput =
					m_pingPong[pingPongIdx].srv.Get();
				/// ピンポンインデックスを切り替える
				pingPongIdx = 1 - pingPongIdx;
			}
		}
	}

	/// @brief スクリーンサイズ変更時にバッファを再生成する
	/// @param screenW 新しいスクリーン幅
	/// @param screenH 新しいスクリーン高さ
	void resize(std::uint32_t screenW, std::uint32_t screenH)
	{
		m_screenW = screenW;
		m_screenH = screenH;
		m_pingPong[0] = createRenderTarget(
			m_device.Get(), screenW, screenH);
		m_pingPong[1] = createRenderTarget(
			m_device.Get(), screenW, screenH);
	}

	/// @brief PostProcessConfigから一括でチェーンを構築する
	/// @param device D3D11デバイス
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	/// @param config 統合設定
	/// @return 構築されたチェーン
	[[nodiscard]] static PostProcessChain createFromConfig(
		ID3D11Device* device,
		std::uint32_t screenW,
		std::uint32_t screenH,
		const PostProcessConfig& config)
	{
		PostProcessChain chain(device, screenW, screenH);

		auto vs = compileFullscreenVS(device);
		auto sampler = createLinearClampSampler(device);

		/// ブルーム
		if (config.bloom.enabled)
		{
			auto pass = std::make_unique<BloomPass>(
				device, vs, sampler, screenW, screenH);
			BloomConfig bc;
			bc.threshold = config.bloom.threshold;
			bc.intensity = config.bloom.intensity;
			bc.blurRadius = config.bloom.blurRadius;
			pass->setConfig(bc);
			chain.addPass(std::move(pass));
		}

		/// カラーグレーディング
		if (config.colorGrading.enabled)
		{
			auto pass = std::make_unique<ColorGradingPass>(
				device, vs, sampler);
			ColorGradingConfig cgc;
			cgc.brightness = config.colorGrading.brightness;
			cgc.contrast = config.colorGrading.contrast;
			cgc.saturation = config.colorGrading.saturation;
			cgc.gamma = config.colorGrading.gamma;
			cgc.tintR = config.colorGrading.tintR;
			cgc.tintG = config.colorGrading.tintG;
			cgc.tintB = config.colorGrading.tintB;
			pass->setConfig(cgc);
			chain.addPass(std::move(pass));
		}

		/// 色収差
		if (config.chromaticAberration.enabled)
		{
			auto pass = std::make_unique<ChromaticAberrationPass>(
				device, vs, sampler);
			ChromaticAberrationConfig cac;
			cac.intensity = config.chromaticAberration.intensity;
			pass->setConfig(cac);
			chain.addPass(std::move(pass));
		}

		/// フィルムグレイン
		if (config.filmGrain.enabled)
		{
			auto pass = std::make_unique<FilmGrainPass>(
				device, vs, sampler);
			FilmGrainConfig fgc;
			fgc.intensity = config.filmGrain.intensity;
			fgc.speed = config.filmGrain.speed;
			pass->setConfig(fgc);
			chain.addPass(std::move(pass));
		}

		/// ビネット
		if (config.vignette.enabled)
		{
			auto pass = std::make_unique<VignettePass>(
				device, vs, sampler);
			VignetteConfig vc;
			vc.intensity = config.vignette.intensity;
			vc.radius = config.vignette.radius;
			vc.softness = config.vignette.softness;
			pass->setConfig(vc);
			chain.addPass(std::move(pass));
		}

		/// フェード（最後に適用すべき）
		if (config.fade.enabled)
		{
			auto pass = std::make_unique<FadePass>(
				device, vs, sampler);
			FadeConfig fc;
			fc.colorR = config.fade.colorR;
			fc.colorG = config.fade.colorG;
			fc.colorB = config.fade.colorB;
			fc.progress = config.fade.progress;
			pass->setConfig(fc);
			chain.addPass(std::move(pass));
		}

		/// フロストグラス
		if (config.frostGlass.enabled)
		{
			auto pass = std::make_unique<FrostGlassPass>(
				device, vs, sampler);
			FrostGlassConfig fgc;
			fgc.blurAmount = config.frostGlass.blurAmount;
			fgc.tintR = config.frostGlass.tintR;
			fgc.tintG = config.frostGlass.tintG;
			fgc.tintB = config.frostGlass.tintB;
			pass->setConfig(fgc);
			chain.addPass(std::move(pass));
		}

		return chain;
	}

private:
	ComPtr<ID3D11Device> m_device;                                ///< D3D11デバイス
	std::uint32_t m_screenW = 0;                                  ///< スクリーン幅
	std::uint32_t m_screenH = 0;                                  ///< スクリーン高さ
	std::array<PostProcessRT, 2> m_pingPong;                      ///< ピンポンバッファ
	std::vector<std::unique_ptr<PostProcessPass>> m_passes;       ///< パスチェーン
};

} // namespace mitiru::render

#endif // _WIN32
