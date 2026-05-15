#pragma once

/// @file PostProcessPass.hpp
/// @brief ポストプロセスパス抽象基底クラス

#ifdef _WIN32

#include <cstdint>
#include <string_view>

#include <d3d11.h>

#include <mitiru/render/postprocess/PostProcessUtils.hpp>

namespace mitiru::render
{

/// @brief ポストプロセスパスの抽象基底クラス
/// @details 各パスは入力SRVを受け取り、出力RTVに描画する。
class PostProcessPass
{
public:
	virtual ~PostProcessPass() = default;

	/// @brief パスを適用する
	/// @param context D3D11コンテキスト
	/// @param inputSRV 前パスの出力（シェーダーリソースビュー）
	/// @param outputRTV 出力先レンダーターゲット
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	virtual void apply(
		ID3D11DeviceContext* context,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		std::uint32_t screenW,
		std::uint32_t screenH) = 0;

	/// @brief パスが有効かどうか
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief パスの有効/無効を切り替える
	void setEnabled(bool enabled) noexcept { m_enabled = enabled; }

	/// @brief パス名を取得する
	[[nodiscard]] virtual std::string_view name() const noexcept = 0;

protected:
	/// @brief フルスクリーン三角形を描画する共通処理
	/// @param context D3D11コンテキスト
	/// @param vs 頂点シェーダー
	/// @param ps ピクセルシェーダー
	/// @param inputSRV 入力SRV（スロット0）
	/// @param outputRTV 出力RTV
	/// @param sampler サンプラー
	/// @param cb 定数バッファ（nullptrなら設定しない）
	/// @param screenW スクリーン幅
	/// @param screenH スクリーン高さ
	void drawFullscreenPass(
		ID3D11DeviceContext* context,
		ID3D11VertexShader* vs,
		ID3D11PixelShader* ps,
		ID3D11ShaderResourceView* inputSRV,
		ID3D11RenderTargetView* outputRTV,
		ID3D11SamplerState* sampler,
		ID3D11Buffer* cb,
		std::uint32_t screenW,
		std::uint32_t screenH) const
	{
		/// ビューポート設定
		D3D11_VIEWPORT vp = {};
		vp.Width = static_cast<float>(screenW);
		vp.Height = static_cast<float>(screenH);
		vp.MaxDepth = 1.0f;
		context->RSSetViewports(1, &vp);

		/// レンダーターゲット設定
		context->OMSetRenderTargets(1, &outputRTV, nullptr);

		/// シェーダー設定
		context->VSSetShader(vs, nullptr, 0);
		context->PSSetShader(ps, nullptr, 0);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		/// テクスチャ・サンプラー設定
		context->PSSetShaderResources(0, 1, &inputSRV);
		context->PSSetSamplers(0, 1, &sampler);

		/// 定数バッファ設定
		if (cb)
		{
			context->PSSetConstantBuffers(0, 1, &cb);
		}

		/// フルスクリーン三角形描画（3頂点）
		context->Draw(3, 0);

		/// SRVバインドを解除する（次パスへの干渉を防ぐ）
		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
	}

	bool m_enabled = true;    ///< 有効フラグ
};

} // namespace mitiru::render

#endif // _WIN32
