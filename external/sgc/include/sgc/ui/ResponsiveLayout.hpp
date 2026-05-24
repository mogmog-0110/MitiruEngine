#pragma once

/// @file ResponsiveLayout.hpp
/// @brief ブレークポイントベースのレスポンシブレイアウト
///
/// 画面サイズに応じてレイアウトパラメータを自動切替する。
/// モバイル→デスクトップのマルチ解像度対応を簡潔に記述できる。
///
/// @code
/// using namespace sgc::ui;
///
/// // ブレークポイント定義
/// ResponsiveBreakpoints bp;
/// bp.add(0,    LayoutParams{1, 4.0f, 8.0f});   // 小画面: 1列
/// bp.add(800,  LayoutParams{2, 8.0f, 12.0f});  // 中画面: 2列
/// bp.add(1280, LayoutParams{3, 12.0f, 16.0f}); // 大画面: 3列
///
/// auto params = bp.resolve(screenWidth);
/// // params.columns, params.gap, params.padding を使ってレイアウト
///
/// // セーフエリア（ノッチ/タスクバー考慮）
/// auto safeArea = safeRect(1280.0f, 720.0f, Margin{40, 20, 20, 20});
/// @endcode

#include "sgc/math/Rect.hpp"
#include "sgc/ui/Anchor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgc::ui
{

/// @brief レイアウトパラメータ（ブレークポイントごとに異なる）
struct LayoutParams
{
	int32_t columns{1};    ///< グリッド列数
	float gap{8.0f};       ///< アイテム間スペース
	float padding{16.0f};  ///< コンテナ内側の余白
	float fontSize{16.0f}; ///< 基準フォントサイズ
};

/// @brief ブレークポイントエントリ
struct BreakpointEntry
{
	float minWidth{0.0f};     ///< このブレークポイントが有効になる最小幅
	LayoutParams params{};    ///< レイアウトパラメータ
};

/// @brief ブレークポイント管理クラス
///
/// 画面幅に応じて最適なLayoutParamsを返す。
/// ブレークポイントはminWidthの昇順で評価される。
class ResponsiveBreakpoints
{
public:
	/// @brief ブレークポイントを追加する
	/// @param minWidth このブレークポイントが有効になる最小画面幅
	/// @param params 対応するレイアウトパラメータ
	void add(float minWidth, const LayoutParams& params)
	{
		m_entries.push_back({minWidth, params});
		std::sort(m_entries.begin(), m_entries.end(),
			[](const auto& a, const auto& b) { return a.minWidth < b.minWidth; });
	}

	/// @brief 画面幅に応じたLayoutParamsを返す
	/// @param screenWidth 現在の画面幅
	/// @return 該当するLayoutParams（ブレークポイント未定義ならデフォルト）
	[[nodiscard]] LayoutParams resolve(float screenWidth) const
	{
		LayoutParams result{};
		for (const auto& entry : m_entries)
		{
			if (screenWidth >= entry.minWidth)
			{
				result = entry.params;
			}
			else
			{
				break;
			}
		}
		return result;
	}

	/// @brief 登録済みブレークポイント数を返す
	[[nodiscard]] std::size_t count() const noexcept
	{
		return m_entries.size();
	}

	/// @brief ブレークポイントをクリアする
	void clear()
	{
		m_entries.clear();
	}

private:
	std::vector<BreakpointEntry> m_entries;
};

/// @brief セーフエリア矩形を計算する
///
/// 画面全体からマージン（ノッチ、タスクバー、ベゼル等）を差し引いた
/// 安全な描画領域を返す。
///
/// @param screenWidth 画面幅
/// @param screenHeight 画面高さ
/// @param margins 上下左右のマージン
/// @return セーフエリア矩形
[[nodiscard]] inline Rectf safeRect(
	float screenWidth, float screenHeight,
	const Margin& margins = {}) noexcept
{
	return Rectf{
		{margins.left, margins.top},
		{screenWidth - margins.left - margins.right,
		 screenHeight - margins.top - margins.bottom}
	};
}

/// @brief グリッドレイアウトを計算する
///
/// 指定列数のグリッドに均等サイズのセルを配置する。
/// gap、padding を考慮する。
///
/// @param container コンテナ矩形
/// @param itemCount アイテム数
/// @param columns 列数
/// @param gap セル間のスペース
/// @return 各セルの矩形リスト
[[nodiscard]] inline std::vector<Rectf> gridLayout(
	const Rectf& container,
	std::size_t itemCount,
	int32_t columns,
	float gap = 0.0f)
{
	if (columns <= 0 || itemCount == 0) return {};

	const auto cols = static_cast<std::size_t>(columns);
	const float totalGapX = gap * static_cast<float>(cols - 1);
	const float cellW = (container.width() - totalGapX) / static_cast<float>(cols);

	// 行数の計算
	const auto rows = (itemCount + cols - 1) / cols;
	const float totalGapY = gap * static_cast<float>(rows - 1);
	const float cellH = (container.height() - totalGapY) / static_cast<float>(rows);

	std::vector<Rectf> result;
	result.reserve(itemCount);

	for (std::size_t i = 0; i < itemCount; ++i)
	{
		const auto col = i % cols;
		const auto row = i / cols;
		const float x = container.x() + static_cast<float>(col) * (cellW + gap);
		const float y = container.y() + static_cast<float>(row) * (cellH + gap);
		result.push_back(Rectf{{x, y}, {cellW, cellH}});
	}

	return result;
}

/// @brief パーセントベースのサイズ計算
///
/// 親サイズに対するパーセンテージでサイズを計算する。
///
/// @param parentSize 親のサイズ
/// @param percent パーセンテージ（0〜100）
/// @return 計算されたサイズ
[[nodiscard]] inline constexpr float percentOf(float parentSize, float percent) noexcept
{
	return parentSize * percent * 0.01f;
}

/// @brief パーセント指定で矩形を計算する
///
/// 親矩形に対するパーセンテージで子矩形を配置する。
///
/// @param parent 親矩形
/// @param xPercent X位置（0〜100%）
/// @param yPercent Y位置（0〜100%）
/// @param wPercent 幅（0〜100%）
/// @param hPercent 高さ（0〜100%）
/// @return 計算された矩形
[[nodiscard]] inline Rectf percentRect(
	const Rectf& parent,
	float xPercent, float yPercent,
	float wPercent, float hPercent) noexcept
{
	return Rectf{
		{parent.x() + percentOf(parent.width(), xPercent),
		 parent.y() + percentOf(parent.height(), yPercent)},
		{percentOf(parent.width(), wPercent),
		 percentOf(parent.height(), hPercent)}
	};
}

} // namespace sgc::ui
