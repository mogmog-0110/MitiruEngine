#pragma once

/// @file IRenderer3D.hpp
/// @brief 3D描画の抽象インターフェース
///
/// フレームワーク非依存の3D描画APIを定義する。
/// メッシュ描画、ライティング、デバッグ描画を抽象化する。
///
/// @code
/// class MyRenderer3D : public sgc::graphics3d::IRenderer3D {
///     void drawMesh(const IMesh& mesh, const Mat4f& transform,
///         const IMaterial* material) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <cstdint>
#include <span>

#include "sgc/math/Mat4.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/types/Color.hpp"

namespace sgc::graphics3d
{

// 前方宣言
class IMesh;
class IMaterial;
class IShader;

/// @brief ライトの種別
enum class LightType : int32_t
{
	Directional,  ///< 平行光源（太陽光）
	Point,        ///< 点光源
	Spot          ///< スポットライト
};

/// @brief ライトデータ
struct LightData
{
	LightType type{LightType::Directional};  ///< ライト種別
	Vec3f position{0, 10, 0};                ///< 位置（Point/Spot）
	Vec3f direction{0, -1, 0};               ///< 方向（Directional/Spot）
	Colorf color{1.0f, 1.0f, 1.0f, 1.0f};   ///< ライト色
	float intensity = 1.0f;                   ///< 強度
	float range = 100.0f;                     ///< 到達距離（Point/Spot）
	float spotAngle = 30.0f;                  ///< スポット角度（度）
};

/// @brief 3D描画プリミティブの種別
enum class DebugPrimitive : int32_t
{
	Line,     ///< 線分
	Sphere,   ///< 球（ワイヤーフレーム）
	Box,      ///< ボックス（ワイヤーフレーム）
	Arrow     ///< 矢印
};

/// @brief 3D描画の抽象インターフェース
///
/// メッシュ描画、ライティング設定、ビュー・プロジェクション設定、
/// デバッグ描画を提供する。
class IRenderer3D
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IRenderer3D() = default;

	// ── ビュー・プロジェクション ─────────────────────────

	/// @brief ビュー行列を設定する
	/// @param view ビュー行列
	virtual void setViewMatrix(const Mat4f& view) = 0;

	/// @brief プロジェクション行列を設定する
	/// @param projection プロジェクション行列
	virtual void setProjectionMatrix(const Mat4f& projection) = 0;

	// ── メッシュ描画 ────────────────────────────────────

	/// @brief メッシュを描画する
	/// @param mesh メッシュ
	/// @param transform ワールド変換行列
	/// @param material マテリアル（nullptrでデフォルト）
	virtual void drawMesh(const IMesh& mesh, const Mat4f& transform,
		const IMaterial* material = nullptr) = 0;

	// ── ライティング ────────────────────────────────────

	/// @brief アンビエントライトを設定する
	/// @param color アンビエント色
	virtual void setAmbientLight(const Colorf& color) = 0;

	/// @brief ライトを追加する
	/// @param light ライトデータ
	virtual void addLight(const LightData& light) = 0;

	/// @brief 全ライトをクリアする
	virtual void clearLights() = 0;

	// ── シェーダー ──────────────────────────────────────

	/// @brief シェーダーをバインドする
	/// @param shader シェーダー（nullptrでデフォルトに戻す）
	virtual void setShader(const IShader* shader) = 0;

	// ── デバッグ描画 ────────────────────────────────────

	/// @brief デバッグ用の線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 線の色
	virtual void drawDebugLine(const Vec3f& from, const Vec3f& to,
		const Colorf& color = Colorf{1, 1, 1, 1}) = 0;

	/// @brief デバッグ用のワイヤーフレーム球を描画する
	/// @param center 中心位置
	/// @param radius 半径
	/// @param color 球の色
	virtual void drawDebugSphere(const Vec3f& center, float radius,
		const Colorf& color = Colorf{1, 1, 1, 1}) = 0;

	/// @brief デバッグ用のワイヤーフレームボックスを描画する
	/// @param center 中心位置
	/// @param halfExtents 半サイズ
	/// @param color ボックスの色
	virtual void drawDebugBox(const Vec3f& center, const Vec3f& halfExtents,
		const Colorf& color = Colorf{1, 1, 1, 1}) = 0;

	// ── フレーム管理 ────────────────────────────────────

	/// @brief フレーム開始処理
	virtual void beginFrame() = 0;

	/// @brief フレーム終了処理
	virtual void endFrame() = 0;
};

} // namespace sgc::graphics3d
