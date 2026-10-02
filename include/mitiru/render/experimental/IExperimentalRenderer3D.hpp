#pragma once

/// @file IExperimentalRenderer3D.hpp
/// @brief 実験的機能 (splat / Live2D / ニューラル現像 / clod・glTF モデル) 用インターフェース
/// @details §6-2: 肥大化した IRenderer3D から、DX12 のみが実装する実験系メソッドを分離した
///          もの。`IRenderer3D::experimental()` が対応バックエンドでは自分自身を返す
///          （未対応なら nullptr）。既定実装は全て IRenderer3D 分割前と同じ no-op / 既定値
///          （後方互換）。docs/3D_RENDERING.md の「experimental は隔離」方針と対応する。

#include <cstdint>

#include <sgc/math/Vec3.hpp>

namespace mitiru::render
{

/// @brief 実験的 3D レンダリング機能インターフェース
class IExperimentalRenderer3D
{
public:
	virtual ~IExperimentalRenderer3D() = default;

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

	// ── 現像焼き込み (2D 絵画を 3D スプラットへ、DX12 で実装) ──────────────
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

	// ── clod 仮想ジオメトリ / Makina CSG / glTF モデル (DX12 のみ) ──────
	/// @brief Makina の CSG ソリッド（焼き済み）を置く
	/// @param bakeManifestPath .csgbake.json への**ファイルパス**（vfs ではない。
	///        bake は DXIL を隣から読むので、実在するディレクトリに展開されていること）
	/// @param timeSec モーションの時刻 (秒、Makina D-15)。トラックを持つ立体を live に
	///        焼いてあればその時刻の姿で描く。静止した立体や焼き込みの bake では無視される
	/// @details 既定は何もしない。DX12 かつ MITIRU_HAS_MAKINA のビルドだけが実装を持つ。
	///          他のバックエンドで知らないうちに消えるのは drawModel と同じ扱いで、
	///          「無い機能は絵から抜ける」がこのインターフェースの規約である。
	virtual void drawSolid(const char* bakeManifestPath, const sgc::Vec3f& position,
	                       float rotYDeg, float scale, float timeSec)
	{
		(void)bakeManifestPath; (void)position; (void)rotYDeg; (void)scale; (void)timeSec;
	}

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

	/// @brief スキンアニメ付き glTF/glb を forward パスで描く (DX12 は compute スキニング)。
	/// @details clipA/timeA = 再生クリップ名と絶対時間 (秒、ループ)。clipB 非 null で
	///          A→B の crossfade (blend01: 0=A, 1=B)。clip 名が空/不在はレストポーズ。
	///          時間はゲーム側 (GameMemory) が所有し、ポーズは (clip, time) の純関数。
	///          未対応バックエンドでは no-op。
	virtual void drawSkinnedModel(const char* path, const sgc::Vec3f& position,
	                              float rotYDeg, float scale,
	                              const char* clipA, float timeA,
	                              const char* clipB, float timeB, float blend01)
	{
		(void)path;
		(void)position;
		(void)rotYDeg;
		(void)scale;
		(void)clipA;
		(void)timeA;
		(void)clipB;
		(void)timeB;
		(void)blend01;
	}

	/// @brief glTF/glb を forward パスで 3 軸回転して描く (viewmodel / 傾く小物用、ABI v26)。
	/// @details rotDeg は drawMesh と同じ {pitch, yaw, roll} 度。骨があっても
	///          レストポーズの剛体として描く。未対応バックエンドでは no-op。
	virtual void drawModelRot(const char* path, const sgc::Vec3f& position,
	                          const sgc::Vec3f& rotDeg, float scale)
	{
		(void)path;
		(void)position;
		(void)rotDeg;
		(void)scale;
	}
};

} // namespace mitiru::render
