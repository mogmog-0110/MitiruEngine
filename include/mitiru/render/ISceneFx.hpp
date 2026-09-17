#pragma once

/// @file ISceneFx.hpp
/// @brief 「絵の設定」(skybox/ambient/shadow/outline/tonemap/shaderMode) 用インターフェース
/// @details §6-2: 肥大化した IRenderer3D から fx 系 setter/getter を分離したもの。
///          `IRenderer3D::sceneFx()` が対応バックエンドでは自分自身を返す（未対応なら nullptr）。
///          既定実装は全て IRenderer3D 分割前と同じ no-op / 既定値（後方互換）。

#include <cstdint>

#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/RendererEnums3D.hpp>

namespace mitiru::render
{

/// @brief 3D シーンの見た目設定インターフェース
class ISceneFx
{
public:
	virtual ~ISceneFx() = default;

	// ── Skybox / 環境キューブマップ ──────────────────────────────

	/// @brief キューブマップ skybox をセットする
	/// @details バックエンドは内部で GPU リソースを構築し、
	///          以降の `beginFrame()` の直後（メッシュ描画より前）に
	///          深度=1.0 の最遠面として skybox を描画する。
	///          DX11 / DX12 で実装される。空 cubemap を渡すと未設定状態に戻す。
	virtual void setSkybox(const Cubemap& /*cubemap*/) {}

	/// @brief skybox 描画の有効/無効
	/// @details `setSkybox()` で cubemap がセット済みでも、これを false に
	///          すれば一時的に skybox 描画をスキップできる。
	virtual void setSkyboxEnabled(bool /*enabled*/) {}

	/// @brief skybox 描画が有効か
	[[nodiscard]] virtual bool isSkyboxEnabled() const noexcept { return false; }

	/// @brief IBL (Image-Based Lighting) 用の環境キューブマップをセットする
	/// @details `setSkybox()` とは別系統。バックエンドは内部で `Cubemap::irradiance()` /
	///          `Cubemap::prefilterSpecular()` を使い diffuse/specular 用の畳み込み済み
	///          キューブマップを生成して GPU へ upload する。`ShaderMode3D::PBR` の
	///          描画でのみ参照される。DX12 のみ実装。DX11 では no-op（IBL なしの
	///          定数アンビエントにフォールバック）。空 cubemap を渡すと未設定に戻る。
	virtual void setEnvironment(const Cubemap& /*cubemap*/) {}

	// ── アンビエント ──────────────────────────────────────────

	/// @brief シーンのアンビエント色を設定する
	/// @param color RGB アンビエント色（A は通常無視される）
	/// @details 既存の Renderer3DConfig::defaultAmbient と同じ意味のシーン全体ベース光。
	///          初期値はバックエンド初期化時の defaultAmbient と一致するため、
	///          このメソッドを呼ばなければ既存挙動と同一。
	///          DX11 / DX12 で実装され、それ以外のバックエンドでは no-op。
	virtual void setAmbientColor(const sgc::Colorf& /*color*/) {}

	/// @brief 現在のシーンアンビエント色を返す
	/// @return 直前に setAmbientColor() で設定された色、未設定なら defaultAmbient
	[[nodiscard]] virtual sgc::Colorf ambientColor() const noexcept
	{
		return sgc::Colorf{0.15f, 0.15f, 0.15f, 1.0f};
	}

	// ── シェーダーモード ──────────────────────────────────────

	/// @brief シェーダーモードを設定する（トゥーン、フラット等）
	/// @param mode シェーダーモード
	virtual void setShaderMode([[maybe_unused]] ShaderMode3D mode) {}

	// ── 影（DX12実装、DX11はno-op） ──

	/// @brief 影（シャドウマップ）の有効/無効を設定する
	virtual void setShadowEnabled(bool /*enabled*/) {}
	/// @brief 影を落とす平行光の向きを設定する（通常はライトの direction と揃える）
	virtual void setShadowDirection(const sgc::Vec3f& /*dir*/) {}
	/// @brief 以後の描画が影を落とすかを切り替える (ABI v29)
	/// @details 一人称の武器のように、画面には出るが世界には影を落とさないものに使う。
	///          フレーム頭で true に戻る。未対応では no-op。
	virtual void setShadowCaster(bool enabled) { (void)enabled; }

	/// @brief カスケードシャドウ (B13) の有効/無効を設定する
	/// @details 2 カスケード (近距離/遠距離) を距離で切り替え、近距離側はテクセル密度を
	///          上げた狭い ortho box を使う。既定は無効 (従来の単一シャドウマップ)。
	///          DX12 実装、DX11 は no-op。
	virtual void setCascadedShadowEnabled(bool /*enabled*/) {}

	/// @brief カスケードシャドウが有効か
	[[nodiscard]] virtual bool isCascadedShadowEnabled() const noexcept { return false; }

	// ── アウトライン（DX12実装、DX11はno-op） ──

	/// @brief アウトライン描画の有効/無効を設定する
	virtual void setOutlineEnabled(bool /*enabled*/) {}

	/// @brief アウトライン描画が有効かどうかを返す
	[[nodiscard]] virtual bool isOutlineEnabled() const noexcept { return false; }

	/// @brief アウトラインモードを設定する
	virtual void setOutlineMode(OutlineMode /*mode*/) {}

	/// @brief 現在のアウトラインモードを返す
	[[nodiscard]] virtual OutlineMode outlineMode() const noexcept { return OutlineMode::DepthSobel; }

	/// @brief アウトラインの線幅 (px) と検出しきい値を設定する (ABI v27)
	/// @details しきい値が小さいほど線が増える。未対応バックエンドでは no-op。
	virtual void setOutlineParams(float widthPx, float threshold)
	{
		(void)widthPx;
		(void)threshold;
	}

	// ── 絵づくり (ABI v27, v28) ───────────────────────────────

	/// @brief トゥーンの影部でアルベドに掛ける係数を設定する
	/// @details 未対応バックエンドでは no-op。
	virtual void setToonShadowTint(const sgc::Colorf& tint) { (void)tint; }

	/// @brief 距離フォグを設定する (ABI v28)
	/// @details nearDist から farDist にかけて color へ染める。未対応では no-op。
	virtual void setFog(bool enabled, const sgc::Colorf& color, float nearDist,
	                    float farDist)
	{
		(void)enabled;
		(void)color;
		(void)nearDist;
		(void)farDist;
	}

	// ── HDRトーンマップ (ENG-106) ─────────────────────────────

	/// @brief 露出 (exposure) を設定する
	/// @details ACES filmic 前の線形係数。1.0 が標準。明るくしたいなら 1 超、
	///          暗くしたいなら 1 未満。屋外シーンは 0.5〜1.0、暗所は 1.5〜3.0 が目安。
	///          DX12 のみ実装。DX11 では no-op。
	virtual void setTonemapExposure(float /*exposure*/) {}

	/// @brief 現在の exposure 値を返す
	[[nodiscard]] virtual float tonemapExposure() const noexcept { return 1.0f; }

	/// @brief 出力ガンマを設定する (default 2.2、sRGB近似)
	/// @details tonemap 後に `pow(c, 1.0/gamma)` を掛ける。
	virtual void setTonemapGamma(float /*gamma*/) {}

	/// @brief 現在の gamma 値を返す
	[[nodiscard]] virtual float tonemapGamma() const noexcept { return 2.2f; }

	/// @brief カスケードシャドウの分割距離と ortho の大きさをカメラ視錐台から毎フレーム決める (v38)。
	///        maxDistance は影を付ける最遠距離。DX12 のみ実装 (`DirectionalShadowConfig::autoFitCascades`)
	virtual void setCascadedShadowAutoFit(bool /*enabled*/, float /*maxDistance*/) {}

	/// @brief カスケード数を 1〜3 で指定する (v38)。1 = 単一、2 = setCascadedShadowEnabled(true) と同じ、
	///        3 = 近/中/遠。DX12 のみ実装
	virtual void setShadowCascadeCount(int /*count*/) {}
};

} // namespace mitiru::render
