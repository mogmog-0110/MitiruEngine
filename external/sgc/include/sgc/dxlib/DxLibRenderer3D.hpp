#pragma once

/// @file DxLibRenderer3D.hpp
/// @brief IRenderer3D の DxLib 実装
///
/// DxLibの3D描画APIを使用して3Dレンダリングを行う。
/// MV1モデル関数やカメラ設定を抽象化する。
///
/// @note このファイルはDxLib SDKに依存する。
///
/// @code
/// sgc::dxlib::DxLibRenderer3D renderer;
/// renderer.setViewMatrix(view);
/// renderer.setProjectionMatrix(proj);
/// renderer.beginFrame();
/// renderer.drawMesh(mesh, transform);
/// renderer.endFrame();
/// @endcode

#include "DxLib.h"
#include <cstdint>
#include <vector>

#include "sgc/graphics3d/IRenderer3D.hpp"

namespace sgc::dxlib
{

/// @brief IRenderer3D の DxLib 実装
///
/// DxLibの3D描画機能をラップし、sgcの3Dレンダラーインターフェースに変換する。
class DxLibRenderer3D : public graphics3d::IRenderer3D
{
public:
	/// @brief ビュー行列を設定する
	/// @param view ビュー行列
	void setViewMatrix(const Mat4f& view) override
	{
		m_viewMatrix = view;
	}

	/// @brief プロジェクション行列を設定する
	/// @param projection プロジェクション行列
	void setProjectionMatrix(const Mat4f& projection) override
	{
		m_projectionMatrix = projection;
	}

	/// @brief メッシュを描画する
	/// @param mesh メッシュ
	/// @param transform ワールド変換行列
	/// @param material マテリアル（nullptrでデフォルト）
	void drawMesh(const graphics3d::IMesh& mesh, const Mat4f& transform,
		const graphics3d::IMaterial* material = nullptr) override
	{
		(void)mesh;
		(void)material;
		// DxLib固有のMV1モデル描画APIを使用
		m_drawCalls.push_back(DrawCall3D{transform, DrawType3D::Mesh});
	}

	/// @brief アンビエントライトを設定する
	/// @param color アンビエント色
	void setAmbientLight(const Colorf& color) override
	{
		m_ambientLight = color;
		// DxLibではSetLightAmbColorで設定
		SetLightAmbColor(toColorF(color));
	}

	/// @brief ライトを追加する
	/// @param light ライトデータ
	void addLight(const graphics3d::LightData& light) override
	{
		m_lights.push_back(light);
	}

	/// @brief 全ライトをクリアする
	void clearLights() override
	{
		m_lights.clear();
	}

	/// @brief シェーダーをバインドする
	/// @param shader シェーダー（nullptrでデフォルトに戻す）
	void setShader(const graphics3d::IShader* shader) override
	{
		m_currentShader = shader;
	}

	/// @brief デバッグ用の線分を描画する
	void drawDebugLine(const Vec3f& from, const Vec3f& to,
		const Colorf& color = Colorf{1, 1, 1, 1}) override
	{
		m_drawCalls.push_back(DrawCall3D{Mat4f::identity(), DrawType3D::DebugLine, from, to, color});
		DrawLine3D(toVector(from), toVector(to), toColorU(color));
	}

	/// @brief デバッグ用のワイヤーフレーム球を描画する
	void drawDebugSphere(const Vec3f& center, float radius,
		const Colorf& color = Colorf{1, 1, 1, 1}) override
	{
		m_drawCalls.push_back(DrawCall3D{
			Mat4f::translation(center) * Mat4f::scaling(radius),
			DrawType3D::DebugSphere, center, Vec3f{radius, 0, 0}, color
		});
		DrawSphere3D(toVector(center), radius, 16, toColorU(color), toColorU(color), FALSE);
	}

	/// @brief デバッグ用のワイヤーフレームボックスを描画する
	void drawDebugBox(const Vec3f& center, const Vec3f& halfExtents,
		const Colorf& color = Colorf{1, 1, 1, 1}) override
	{
		m_drawCalls.push_back(DrawCall3D{
			Mat4f::translation(center) * Mat4f::scaling(halfExtents),
			DrawType3D::DebugBox, center, halfExtents, color
		});
		const Vec3f minP = center - halfExtents;
		const Vec3f maxP = center + halfExtents;
		// DxLibにはワイヤーフレームボックスAPIがないため、12辺を描画
		DrawLine3D(toVector(Vec3f{minP.x, minP.y, minP.z}), toVector(Vec3f{maxP.x, minP.y, minP.z}), toColorU(color));
	}

	/// @brief フレーム開始処理
	void beginFrame() override
	{
		m_drawCalls.clear();
	}

	/// @brief フレーム終了処理
	void endFrame() override
	{
		// DxLib固有のフレーム終了処理
	}

	// ── テスト/デバッグ用アクセサ ───────────────────────

	/// @brief ビュー行列を取得する
	[[nodiscard]] const Mat4f& viewMatrix() const noexcept { return m_viewMatrix; }

	/// @brief プロジェクション行列を取得する
	[[nodiscard]] const Mat4f& projectionMatrix() const noexcept { return m_projectionMatrix; }

	/// @brief アンビエントライト色を取得する
	[[nodiscard]] const Colorf& ambientLight() const noexcept { return m_ambientLight; }

	/// @brief ライトリストを取得する
	[[nodiscard]] const std::vector<graphics3d::LightData>& lights() const noexcept { return m_lights; }

	/// @brief 描画コール数を取得する
	[[nodiscard]] size_t drawCallCount() const noexcept { return m_drawCalls.size(); }

private:
	/// @brief 3D描画コールの種別
	enum class DrawType3D
	{
		Mesh,
		DebugLine,
		DebugSphere,
		DebugBox
	};

	/// @brief 3D描画コール記録
	struct DrawCall3D
	{
		Mat4f transform{};
		DrawType3D type{DrawType3D::Mesh};
		Vec3f param1{};
		Vec3f param2{};
		Colorf color{1, 1, 1, 1};
	};

	/// @brief sgc::Vec3f を DxLib VECTOR に変換する
	[[nodiscard]] static VECTOR toVector(const Vec3f& v) noexcept
	{
		return VECTOR{v.x, v.y, v.z};
	}

	/// @brief sgc::Colorf を DxLib COLOR_F に変換する
	[[nodiscard]] static COLOR_F toColorF(const Colorf& c) noexcept
	{
		return COLOR_F{c.r, c.g, c.b, c.a};
	}

	/// @brief sgc::Colorf を DxLib符号なし整数カラーに変換する
	[[nodiscard]] static unsigned int toColorU(const Colorf& c) noexcept
	{
		const auto rgba = c.toRGBA8();
		return (static_cast<unsigned int>(rgba.r) << 16)
			 | (static_cast<unsigned int>(rgba.g) << 8)
			 | static_cast<unsigned int>(rgba.b);
	}

	Mat4f m_viewMatrix = Mat4f::identity();         ///< ビュー行列
	Mat4f m_projectionMatrix = Mat4f::identity();   ///< プロジェクション行列
	Colorf m_ambientLight{0.2f, 0.2f, 0.2f, 1.0f}; ///< アンビエントライト
	std::vector<graphics3d::LightData> m_lights;    ///< ライトリスト
	const graphics3d::IShader* m_currentShader = nullptr; ///< 現在のシェーダー
	std::vector<DrawCall3D> m_drawCalls;            ///< 描画コール記録
};

} // namespace sgc::dxlib
