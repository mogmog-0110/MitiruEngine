#pragma once

/// @file IRenderer3D.hpp
/// @brief 3Dレンダラー統一抽象インターフェース
/// @details DX11(Renderer3D)とDX12(Renderer3D_DX12)を統一的に扱うための
///          仮想インターフェース。Gameクラスは具体的なバックエンドを知らずに
///          3D描画を行える。

#include <span>

#include <sgc/math/Mat4.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Cubemap.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>

namespace mitiru::render
{

/// @brief シェーダーモード
enum class ShaderMode3D : uint8_t
{
	Phong = 0,     ///< 標準Phongシェーディング
	Toon,          ///< セルシェーディング
	Unlit,         ///< ライティングなし
	Flat,          ///< フラットシェーディング
	Posterize,     ///< ポスタリゼーション
	Halftone,      ///< ハーフトーン
	Hatching,      ///< ハッチング
	GradientMap,   ///< グラデーションマップ
	Silhouette,    ///< シルエット
	Watercolor,    ///< 水彩風
};

} // namespace mitiru::render

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

/// @brief ポストプロセスアウトラインのモード
/// @details IRenderer3D::setOutlineMode() で切り替える。
///          DX12では全モードが実装され、DX11ではno-opとなる。
enum class OutlineMode : int
{
	DepthSobel     = 0,  ///< 深度Sobel（線形化）— デフォルト
	DepthLaplacian = 1,  ///< 深度Laplacian 3x3
	DepthSobelNdotV = 2, ///< 深度Sobel + NdotVフィルタ（凹面除外）
	ColorEdge      = 3,  ///< 色エッジ（輝度Sobel）
	DepthColorCombo = 4, ///< 深度+色 複合（両方にエッジがある場合のみ）
	Fresnel        = 5,  ///< Fresnel（メインシェーダー内N.Vリム効果、ポストプロセスなし）
};

/// @brief アウトラインモードの総数
constexpr int OUTLINE_MODE_COUNT = 6;

/// @brief 3Dレンダラー統一インターフェース
/// @details DX11/DX12レンダラーの共通操作を定義する。
///          フレーム開始→カメラ/ライト設定→メッシュ描画→フレーム終了の
///          基本フローはどのバックエンドでも同一。
class IRenderer3D
{
public:
	virtual ~IRenderer3D() = default;

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] virtual bool isInitialized() const noexcept = 0;

	/// @brief フレーム描画を開始する
	/// @param clearColor バックバッファのクリア色
	virtual void beginFrame(const sgc::Colorf& clearColor = {0.2f, 0.2f, 0.3f, 1.0f}) = 0;

	/// @brief フレーム描画を終了する
	virtual void endFrame() = 0;

	/// @brief カメラを設定する
	/// @param camera 3Dカメラ
	virtual void setCamera(const Camera3D& camera) = 0;

	/// @brief ライトを設定する
	/// @param light ライト情報
	virtual void setLight(const Light& light) = 0;

	/// @brief 複数ライトを設定する
	/// @param lights ライト配列のビュー（最大 kMaxLights 個）
	/// @details マルチライト対応バックエンドは複数ライトをすべて使う。
	///          単一ライト前提の既存バックエンドはデフォルト実装で
	///          先頭ライトのみ `setLight()` 経由で使う後方互換動作。
	///
	///          空配列を渡すと「ライトなし（環境光のみ）」の意味になる。
	///          配列が `kMaxLights` を超えた場合の挙動はバックエンド依存
	///          （Renderer3D は先頭 `kMaxLights` 個のみ使用、残りは破棄）。
	virtual void setLights(std::span<const Light> lights)
	{
		if (!lights.empty())
		{
			setLight(lights.front());
		}
	}

	/// @brief マルチライトの最大サポート数
	/// @details setLights() に渡せる最大数。GPU 側 cbuffer のサイズで決まる。
	static constexpr int kMaxLights = 8;

	// ── マルチライト経路の有効化（DX11/DX12 共通 API）────────────
	// バックエンドが実体実装を持つ。デフォルトはストアのみで、
	// drawMesh の描画パスがバックエンド側で multi-light 経路に切り替わる
	// かは override の有無で決まる。

	/// @brief マルチライト経路を有効/無効にする
	/// @details true にすると `setLights()` で渡された全ライトを使う
	///          multi-light シェーダー経路に切り替わる（Phong 系のみ）。
	///          false (デフォルト) は単一光源の互換動作。
	///          DX11 / DX12 で実装される。それ以外のバックエンドでは no-op。
	virtual void setUseMultiLight(bool /*useMulti*/) {}

	/// @brief 現在マルチライト経路かを返す
	[[nodiscard]] virtual bool useMultiLight() const noexcept { return false; }

	// ── Skybox / 環境キューブマップ（DX11/DX12 共通 API）──────────

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

	/// @brief メッシュを描画する
	/// @param mesh 描画対象メッシュ
	/// @param worldTransform ワールド変換行列
	/// @param material マテリアル
	virtual void drawMesh(const Mesh& mesh,
	                      const sgc::Mat4f& worldTransform,
	                      const Material& material = {}) = 0;

	/// @brief フレームアクティブフラグをリセットする（Engine側で毎フレーム呼ぶ）
	virtual void resetFrameActive() noexcept = 0;

	/// @brief このフレームで3D描画が行われたかを返す
	[[nodiscard]] virtual bool isFrameActive() const noexcept = 0;

	/// @brief 直前フレームのドローコール数を返す
	[[nodiscard]] virtual int drawCallCount() const noexcept = 0;

	/// @brief シェーダーモードを設定する（トゥーン、フラット等）
	/// @param mode シェーダーモード
	virtual void setShaderMode([[maybe_unused]] ShaderMode3D mode) {}

	// ── アウトライン（DX12で実装、DX11ではno-op） ──

	/// @brief アウトライン描画の有効/無効を設定する
	virtual void setOutlineEnabled(bool /*enabled*/) {}

	/// @brief アウトライン描画が有効かどうかを返す
	[[nodiscard]] virtual bool isOutlineEnabled() const noexcept { return false; }

	/// @brief アウトラインモードを設定する
	virtual void setOutlineMode(OutlineMode /*mode*/) {}

	/// @brief 現在のアウトラインモードを返す
	[[nodiscard]] virtual OutlineMode outlineMode() const noexcept { return OutlineMode::DepthSobel; }

	// ── HDR / tonemap (ENG-106) ─────────────────────────────────

	/// @brief 露出 (exposure) を設定する
	/// @details ACES filmic 前の線形係数。1.0 が標準。明るくしたいなら >1、
	///          暗くしたいなら <1。屋外シーン:0.5–1.0 / 暗所:1.5–3.0 が目安。
	///          DX12 のみ実装。DX11 では no-op。
	virtual void setTonemapExposure(float /*exposure*/) {}

	/// @brief 現在の exposure 値を返す
	[[nodiscard]] virtual float tonemapExposure() const noexcept { return 1.0f; }

	/// @brief 出力ガンマを設定する (default 2.2 = sRGB approx)
	/// @details tonemap 後に `pow(c, 1.0/gamma)` を掛ける。
	virtual void setTonemapGamma(float /*gamma*/) {}

	/// @brief 現在の gamma 値を返す
	[[nodiscard]] virtual float tonemapGamma() const noexcept { return 2.2f; }

	// ── 2Dオーバーレイ（DX12で実装、DX11ではno-op） ──

	/// @brief 2Dオーバーレイ用のScreen参照を設定する
	/// @param screen Screenへのポインタ（nullptrで解除）
	virtual void setOverlayScreen(const Screen* /*screen*/) {}

	/// @brief endFrame()内で2Dオーバーレイを自動描画するかどうかを返す
	/// @details trueの場合、Engineは screen->present() をスキップする（レンダラーが処理する）
	///          falseの場合、Engineが screen->present() を呼んで2D描画をGPU送信する
	[[nodiscard]] virtual bool hasOverlaySupport() const noexcept { return false; }

	/// @brief 現在開いているコマンドリストを取得する（DX12用、ImGui描画挿入用）
	/// @return コマンドリストのvoidポインタ（DX11ではnullptr）
	[[nodiscard]] virtual void* nativeCommandList() const noexcept { return nullptr; }

	/// @brief ネイティブGPUデバイスを取得する（DX12用）
	/// @return DX12ではDx12Device*、DX11ではnullptr
	[[nodiscard]] virtual void* nativeDevice() const noexcept { return nullptr; }

	/// @brief ネイティブスワップチェーンを取得する（DX12用）
	/// @return DX12ではDx12SwapChain*、DX11ではnullptr
	[[nodiscard]] virtual void* nativeSwapChain() const noexcept { return nullptr; }

	/// @brief endFrame()後のコマンドリスト最終化（バリア+実行）
	/// @details DX12ではendFrame()がコマンドリストを開いたままにし、
	///          ImGui描画を追記した後にfinalizeFrame()で閉じて実行する。
	///          DX11ではno-op。
	virtual void finalizeFrame() {}
};

} // namespace mitiru::render
