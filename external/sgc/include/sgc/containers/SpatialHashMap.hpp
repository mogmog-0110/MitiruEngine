#pragma once

/// @file SpatialHashMap.hpp
/// @brief 空間ハッシュマップ（ブロードフェーズ衝突検出用）
///
/// セルベースの空間ハッシュにより、AABB同士の近傍検索をO(1)〜O(k)で実現。
/// ブロードフェーズ衝突検出やゲーム内オブジェクトの近傍クエリに最適。
///
/// @code
/// using SHM = sgc::containers::SpatialHashMap<int, float>;
/// SHM map(32.0f);  // セルサイズ32
///
/// SHM::AABB box{10.0f, 20.0f, 50.0f, 60.0f};
/// map.insert(box, 42);
///
/// SHM::AABB query{0.0f, 0.0f, 100.0f, 100.0f};
/// auto results = map.query(query);
/// // results contains 42
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sgc::containers
{

/// @brief 空間ハッシュマップ
///
/// 2D空間をグリッドセルに分割し、各セルにオブジェクトを格納。
/// AABBベースの挿入とクエリを提供する。
///
/// @tparam T 格納する値の型
/// @tparam FloatType 座標の浮動小数点型（デフォルト: float）
template <typename T, typename FloatType = float>
class SpatialHashMap
{
public:
	/// @brief 軸平行バウンディングボックス
	struct AABB
	{
		FloatType minX{};   ///< 左端X
		FloatType minY{};   ///< 上端Y
		FloatType maxX{};   ///< 右端X
		FloatType maxY{};   ///< 下端Y
	};

	/// @brief コンストラクタ
	/// @param cellSize セルの辺の長さ（正の値）
	explicit SpatialHashMap(FloatType cellSize)
		: m_cellSize(cellSize)
		, m_invCellSize(static_cast<FloatType>(1) / cellSize)
	{
	}

	/// @brief オブジェクトを挿入する
	///
	/// AABBがカバーする全セルにオブジェクトを登録する。
	///
	/// @param aabb バウンディングボックス
	/// @param value 格納する値
	void insert(const AABB& aabb, const T& value)
	{
		const auto [cellMinX, cellMinY, cellMaxX, cellMaxY] = toCellRange(aabb);
		for (std::int64_t cy = cellMinY; cy <= cellMaxY; ++cy)
		{
			for (std::int64_t cx = cellMinX; cx <= cellMaxX; ++cx)
			{
				const auto key = cellKey(cx, cy);
				m_cells[key].push_back(value);
			}
		}
		++m_count;
	}

	/// @brief 指定AABBと重なるセルの全オブジェクトを取得する
	///
	/// 重複を排除した結果を返す（==演算子が必要）。
	///
	/// @param aabb クエリ用バウンディングボックス
	/// @return 重なるオブジェクトのリスト
	[[nodiscard]] std::vector<T> query(const AABB& aabb) const
	{
		std::vector<T> results;
		const auto [cellMinX, cellMinY, cellMaxX, cellMaxY] = toCellRange(aabb);

		for (std::int64_t cy = cellMinY; cy <= cellMaxY; ++cy)
		{
			for (std::int64_t cx = cellMinX; cx <= cellMaxX; ++cx)
			{
				const auto key = cellKey(cx, cy);
				const auto it = m_cells.find(key);
				if (it != m_cells.end())
				{
					for (const auto& val : it->second)
					{
						// 重複チェック
						if (std::find(results.begin(), results.end(), val) == results.end())
						{
							results.push_back(val);
						}
					}
				}
			}
		}
		return results;
	}

	/// @brief 全セルをクリアする
	void clear()
	{
		m_cells.clear();
		m_count = 0;
	}

	/// @brief 挿入されたオブジェクト数を返す
	/// @note 同一オブジェクトが複数セルに登録されていてもカウントは1
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_count;
	}

	/// @brief マップが空か判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return m_count == 0;
	}

	/// @brief セルサイズを返す
	[[nodiscard]] FloatType cellSize() const noexcept
	{
		return m_cellSize;
	}

	/// @brief 使用中のセル数を返す
	[[nodiscard]] std::size_t cellCount() const noexcept
	{
		return m_cells.size();
	}

private:
	/// @brief セル座標の範囲
	struct CellRange
	{
		std::int64_t minX;
		std::int64_t minY;
		std::int64_t maxX;
		std::int64_t maxY;
	};

	/// @brief 座標をセルインデックスに変換する
	/// @param coord ワールド座標
	/// @return セルインデックス
	[[nodiscard]] std::int64_t toCell(FloatType coord) const noexcept
	{
		return static_cast<std::int64_t>(std::floor(coord * m_invCellSize));
	}

	/// @brief AABBをセル座標範囲に変換する
	/// @param aabb バウンディングボックス
	/// @return セル座標の範囲
	[[nodiscard]] CellRange toCellRange(const AABB& aabb) const noexcept
	{
		return {
			toCell(aabb.minX),
			toCell(aabb.minY),
			toCell(aabb.maxX),
			toCell(aabb.maxY)
		};
	}

	/// @brief セル座標からハッシュキーを生成する
	/// @param cx セルX座標
	/// @param cy セルY座標
	/// @return ハッシュキー
	[[nodiscard]] static std::uint64_t cellKey(std::int64_t cx, std::int64_t cy) noexcept
	{
		// Cantor pairing function の変形（負の座標対応）
		const auto ux = static_cast<std::uint64_t>(cx);
		const auto uy = static_cast<std::uint64_t>(cy);
		return (ux * 73856093ULL) ^ (uy * 19349663ULL);
	}

	std::unordered_map<std::uint64_t, std::vector<T>> m_cells;  ///< セルマップ
	FloatType m_cellSize;                                        ///< セルサイズ
	FloatType m_invCellSize;                                     ///< セルサイズの逆数
	std::size_t m_count = 0;                                     ///< 挿入数
};

} // namespace sgc::containers
