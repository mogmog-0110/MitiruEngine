#pragma once

/// @file GraphArea.hpp
/// @brief 論理座標・ワールド座標・スクリーン座標の変換を管理するグラフ領域
///
/// GraphAreaはグラフの論理座標（数学座標系）とワールド座標（画面座標系）の
/// 相互変換を提供する。Y軸の反転とスケーリングを一元管理する。
///
/// @code
/// mitiru::gw::GraphArea area{60.0f, {400.0f, 300.0f}};
/// auto worldPos = area.logicToWorld({1.0f, 2.0f});
/// auto logicPos = area.worldToLogic(worldPos);
/// @endcode

#include <vector>
#include <utility>
#include <cmath>

#include "sgc/math/Vec2.hpp"

namespace mitiru::gw
{

/// @brief グラフの座標変換を管理するクラス
///
/// 論理座標（数学的なxy平面）とワールド座標（画面ピクセル座標）の
/// 変換を提供する。論理座標系はY軸が上向き正、ワールド座標系はY軸が下向き正。
class GraphArea
{
public:
	/// @brief コンストラクタ
	/// @param pixelsPerUnit 論理座標1単位あたりのピクセル数
	/// @param origin ワールド座標系での原点位置
	constexpr GraphArea(float pixelsPerUnit = 60.0f, sgc::Vec2f origin = {0.0f, 0.0f}) noexcept
		: m_pixelsPerUnit(pixelsPerUnit)
		, m_origin(origin)
	{
	}

	/// @brief 論理座標をワールド座標に変換する
	/// @param logicPos 論理座標
	/// @return ワールド座標（Y軸反転・スケーリング済み）
	[[nodiscard]] constexpr sgc::Vec2f logicToWorld(sgc::Vec2f logicPos) const noexcept
	{
		return {
			m_origin.x + logicPos.x * m_pixelsPerUnit,
			m_origin.y - logicPos.y * m_pixelsPerUnit
		};
	}

	/// @brief ワールド座標を論理座標に変換する
	/// @param worldPos ワールド座標
	/// @return 論理座標
	[[nodiscard]] constexpr sgc::Vec2f worldToLogic(sgc::Vec2f worldPos) const noexcept
	{
		return {
			(worldPos.x - m_origin.x) / m_pixelsPerUnit,
			(m_origin.y - worldPos.y) / m_pixelsPerUnit
		};
	}

	/// @brief 論理X座標をワールドX座標に変換する
	/// @param x 論理X座標
	/// @return ワールドX座標
	[[nodiscard]] constexpr float logicToWorld(float x) const noexcept
	{
		return m_origin.x + x * m_pixelsPerUnit;
	}

	/// @brief 論理Y座標をワールドY座標に変換する（Y軸反転）
	/// @param y 論理Y座標
	/// @return ワールドY座標
	[[nodiscard]] constexpr float logicToWorldY(float y) const noexcept
	{
		return m_origin.y - y * m_pixelsPerUnit;
	}

	/// @brief 表示範囲内のグリッド線を生成する
	/// @param viewMin ワールド座標系での表示範囲左上
	/// @param viewMax ワールド座標系での表示範囲右下
	/// @param step 論理座標系でのグリッド間隔
	/// @return グリッド線の始点・終点ペアの配列（ワールド座標）
	[[nodiscard]] std::vector<std::pair<sgc::Vec2f, sgc::Vec2f>> getGridLines(
		sgc::Vec2f viewMin, sgc::Vec2f viewMax, float step) const
	{
		std::vector<std::pair<sgc::Vec2f, sgc::Vec2f>> lines;

		if (step <= 0.0f)
		{
			return lines;
		}

		// ビュー範囲を論理座標に変換
		// viewMinはワールド座標の左上、viewMaxは右下
		const auto logicMin = worldToLogic(viewMin);
		const auto logicMax = worldToLogic(viewMax);

		// 論理座標系でのX範囲（左→右）
		const float xStart = std::floor(logicMin.x / step) * step;
		const float xEnd = std::ceil(logicMax.x / step) * step;

		// 論理座標系でのY範囲（logicMaxのYが小さい側＝ビュー下部）
		const float yLow = (logicMin.y < logicMax.y) ? logicMin.y : logicMax.y;
		const float yHigh = (logicMin.y < logicMax.y) ? logicMax.y : logicMin.y;
		const float yStart = std::floor(yLow / step) * step;
		const float yEnd = std::ceil(yHigh / step) * step;

		// 縦線（X固定）
		for (float x = xStart; x <= xEnd + step * 0.5f; x += step)
		{
			const auto top = logicToWorld({x, yHigh});
			const auto bottom = logicToWorld({x, yLow});
			lines.emplace_back(top, bottom);
		}

		// 横線（Y固定）
		for (float y = yStart; y <= yEnd + step * 0.5f; y += step)
		{
			const auto left = logicToWorld({xStart, y});
			const auto right = logicToWorld({xEnd, y});
			lines.emplace_back(left, right);
		}

		return lines;
	}

	/// @brief ピクセル/ユニット比率を取得する
	/// @return 1論理単位あたりのピクセル数
	[[nodiscard]] constexpr float getPixelsPerUnit() const noexcept
	{
		return m_pixelsPerUnit;
	}

	/// @brief ピクセル/ユニット比率を設定する
	/// @param ppu 新しい比率（正の値）
	constexpr void setPixelsPerUnit(float ppu) noexcept
	{
		if (ppu > 0.0f)
		{
			m_pixelsPerUnit = ppu;
		}
	}

	/// @brief ワールド座標系での原点位置を取得する
	/// @return 原点のワールド座標
	[[nodiscard]] constexpr sgc::Vec2f getOrigin() const noexcept
	{
		return m_origin;
	}

	/// @brief ワールド座標系での原点位置を設定する
	/// @param origin 新しい原点位置
	constexpr void setOrigin(sgc::Vec2f origin) noexcept
	{
		m_origin = origin;
	}

private:
	float m_pixelsPerUnit = 60.0f;   ///< 1論理単位あたりのピクセル数
	sgc::Vec2f m_origin{0.0f, 0.0f}; ///< ワールド座標系での原点位置
};

} // namespace mitiru::gw
