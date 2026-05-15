#pragma once

/// @file Camera2DHelper.hpp
/// @brief 2Dカメラヘルパー（中心座標/ズーム/ビューポート）
/// @details ワールド座標とスクリーン座標の相互変換を提供する。

#include <algorithm>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru::util
{

/// @brief 2Dカメラ（中心座標・ズーム・ビューポートサイズ）
class Camera2DHelper
{
public:
	/// @brief コンストラクタ
	/// @param viewportWidth ビューポート幅（ピクセル）
	/// @param viewportHeight ビューポート高さ（ピクセル）
	Camera2DHelper(float viewportWidth, float viewportHeight) noexcept
		: m_viewportW(viewportWidth)
		, m_viewportH(viewportHeight)
	{
	}

	/// @brief ワールド座標→スクリーン座標変換
	/// @param worldPos ワールド座標
	/// @return スクリーン座標
	[[nodiscard]] sgc::Vec2f worldToScreen(const sgc::Vec2f& worldPos) const noexcept
	{
		return {
			(worldPos.x - m_center.x) * m_zoom + m_viewportW * 0.5f,
			(worldPos.y - m_center.y) * m_zoom + m_viewportH * 0.5f
		};
	}

	/// @brief スクリーン座標→ワールド座標変換
	/// @param screenPos スクリーン座標
	/// @return ワールド座標
	[[nodiscard]] sgc::Vec2f screenToWorld(const sgc::Vec2f& screenPos) const noexcept
	{
		return {
			(screenPos.x - m_viewportW * 0.5f) / m_zoom + m_center.x,
			(screenPos.y - m_viewportH * 0.5f) / m_zoom + m_center.y
		};
	}

	/// @brief カメラの可視領域をワールド座標で取得する
	/// @return 可視領域の矩形
	[[nodiscard]] sgc::Rectf visibleBounds() const noexcept
	{
		const float halfW = m_viewportW * 0.5f / m_zoom;
		const float halfH = m_viewportH * 0.5f / m_zoom;
		return {m_center.x - halfW, m_center.y - halfH, halfW * 2.0f, halfH * 2.0f};
	}

	/// @brief カメラ中心座標を設定する
	/// @param center 新しい中心ワールド座標
	void setCenter(const sgc::Vec2f& center) noexcept { m_center = center; }

	/// @brief カメラ中心座標を取得する
	[[nodiscard]] sgc::Vec2f center() const noexcept { return m_center; }

	/// @brief ズーム倍率を設定する（0より大きい値）
	/// @param zoom ズーム倍率
	void setZoom(float zoom) noexcept { m_zoom = std::max(0.01f, zoom); }

	/// @brief ズーム倍率を取得する
	[[nodiscard]] float zoom() const noexcept { return m_zoom; }

	/// @brief ビューポートサイズを設定する
	void setViewport(float w, float h) noexcept { m_viewportW = w; m_viewportH = h; }

	/// @brief ビューポート幅を取得する
	[[nodiscard]] float viewportWidth() const noexcept { return m_viewportW; }

	/// @brief ビューポート高さを取得する
	[[nodiscard]] float viewportHeight() const noexcept { return m_viewportH; }

private:
	sgc::Vec2f m_center{0.0f, 0.0f};  ///< カメラ中心（ワールド座標）
	float m_zoom = 1.0f;               ///< ズーム倍率
	float m_viewportW;                  ///< ビューポート幅
	float m_viewportH;                  ///< ビューポート高さ
};

} // namespace mitiru::util
