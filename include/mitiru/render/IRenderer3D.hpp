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

	/// @brief 内部 RT を新しい backbuffer サイズ (物理 px) へ追従させる
	/// @details 既定 no-op。swapchain resize 後・次の beginFrame 前に呼ぶこと。
	virtual void resize(int /*width*/, int /*height*/) {}

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

	// ── 影（DX12で実装、DX11ではno-op） ──
	/// @brief 影（シャドウマップ）の有効/無効を設定する
	virtual void setShadowEnabled(bool /*enabled*/) {}
	/// @brief 影を落とす平行光の向きを設定する（通常はライトの direction と揃える）
	virtual void setShadowDirection(const sgc::Vec3f& /*dir*/) {}

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

	// ── 3D Gaussian Splatting (M1、DX12 で実装、それ以外は no-op) ──────────
	/// @brief .splat シーンを読み込んで GPU にアップロードする (一度だけ)。失敗時 false。
	virtual bool loadSplatScene(const char* /*path*/) { return false; }
	/// @brief 読み込み済みスプラットシーンを現在のカメラで描画する (beginFrame 後)。
	virtual void drawSplats() {}
	/// @brief 読み込み済みシーンの境界球 (重心 + 半径)。カメラ自動フレーミング用。
	virtual void splatBounds(float& cx, float& cy, float& cz, float& r) const { cx = cy = cz = 0.0f; r = 1.0f; }

	// ── Live2D (Cubism Framework 駆動 + 自前 D3D12 レンダラ、DX12+Cubism で実装、それ以外は no-op) ──
	/// @brief Live2D モデル (model3.json) を描く要求。初回に Framework が moc/テクスチャ/モーション/
	///        物理/エフェクトを一括ロードし、以後毎フレーム更新 (公式サンプル相当) + 描画する。
	virtual void drawLive2D(const char* /*model3jsonPath*/) {}
	/// @brief Live2D の注視先 (nx,ny∈[-1,1])。頭/目/体がマウス等に追従する。
	virtual void live2dLookAt(float /*nx*/, float /*ny*/) {}
	/// @brief Live2D のタップ操作。TapBody (無ければ idle) グループのモーションを再生する。
	virtual void live2dTap() {}
	/// @brief 公式 LAppView 相当のステージ画像 (背景/歯車/閉じる)。drawLive2D より前に一度設定する。
	virtual void live2dStage(const char* /*bg*/, const char* /*gear*/, const char* /*close*/) {}

	// ── DirectML in-pipeline ニューラル後処理 (DX12+DirectML で実装) ──
	/// @brief backbuffer に DirectML 推論を CPU 往復なしで適用する on/off + 強度 (0..2)。
	virtual void enableNeuralFx(bool /*enabled*/, float /*strength*/ = 0.5f) {}

	/// @brief ニューラル・リライティング on/off + 光源方向 (lx,ly∈[-1,1]) + 陰影/リム強度。
	/// @details 平面 Live2D から法線を推定し可動光源で再ライティング (従来は照明固定で不可能)。
	virtual void enableRelight(bool /*enabled*/, float /*lightX*/ = 0.4f, float /*lightY*/ = 0.4f,
	                           float /*strength*/ = 0.6f, float /*rim*/ = 0.5f) {}
	/// @brief リライト用の単眼深度モデル (ONNX) パスを設定する。
	virtual void setRelightDepthModel(const char* /*path*/) {}

	// ── ニューラル現像 (M3、DX12+DirectML で実装、それ以外は no-op) ──────────
	/// @brief 次の安全境界で現在のフレームを style モデルで 2D 化するよう要求する。
	virtual void requestDevelop(const char* /*modelPath*/) {}
	/// @brief engine フレーム頭 (backbuffer=PRESENT) で呼ぶ: 要求があれば readback+推論。
	virtual void tickDevelop() {}
	/// @brief 現像済み状態を解除して 3D 表示へ戻す。
	virtual void clearDevelop() {}
	/// @brief 現像済み 2D 画像が利用可能か。
	[[nodiscard]] virtual bool styleReady() const { return false; }
	/// @brief 現像済み 2D 画像 (RGBA8、tight) の先頭。未準備なら nullptr。
	[[nodiscard]] virtual const std::uint8_t* styleImageData() const { return nullptr; }
	/// @brief 現像済み 2D 画像の幅・高さ。
	[[nodiscard]] virtual int styleImageW() const { return 0; }
	[[nodiscard]] virtual int styleImageH() const { return 0; }
	/// @brief 現像 2D の全画面合成強度 (0=3D / 1=完全 2D)。post-process で blit される。
	virtual void setStyleStrength(float /*strength*/) {}

	// ── 現像焼き込み (M4: 2D 絵画を 3D スプラットへ、DX12 で実装) ──────────
	/// @brief 直前の現像 2D を、その現像視点から見えるスプラットへ色として焼き込む。
	virtual void bakeStyleToSplats() {}
	/// @brief スプラット色を元の写実色へ戻す (焼き込み解除)。
	virtual void resetSplatColors() {}
	/// @brief 焼き込み済みスプラットの割合 (0..1、塗り達成率)。
	[[nodiscard]] virtual float bakedFraction() const { return 0.0f; }

	// ── 現像合わせ (お題再現パズル、DX12 で実装) ──────────────
	/// @brief 現在の現像 2D を「お題」として保存する。
	virtual void captureTargetFromStyle() {}
	/// @brief blit でお題(true)／自分の現像(false)を表示する。
	virtual void setShowTarget(bool /*b*/) {}
	/// @brief お題が保存済みか。
	[[nodiscard]] virtual bool hasTarget() const { return false; }
	/// @brief 現在の現像 2D とお題の一致度 (0..1)。
	[[nodiscard]] virtual float matchScore() const { return 0.0f; }

	/// @brief ワールド座標を現在のカメラで画面正規化座標 (u,v ∈ 0..1, 左上原点) へ射影する。
	/// @return 視錐台内 (手前かつ画面内) なら true。アナモルフォーズ等の射影パズル用。
	virtual bool worldToScreen(float /*wx*/, float /*wy*/, float /*wz*/, float& u, float& v) const { u = v = -1.0f; return false; }

	// ── clod 仮想ジオメトリ (ADR 0027、DX12 のみ。vtable 末尾固定) ──────
	/// @brief .clod モデルのインスタンスを描画する (大規模静的ジオメトリ)。
	/// @param path .clod への vfs パス。未対応バックエンドでは no-op。
	virtual void drawModel(const char* path, const sgc::Vec3f& position, float rotYDeg,
	                       float scale)
	{
		(void)path;
		(void)position;
		(void)rotYDeg;
		(void)scale;
	}
};

} // namespace mitiru::render
