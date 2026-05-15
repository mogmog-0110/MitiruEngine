#pragma once

#include <vector>
#include <stdexcept>
#include <utility>
#include <string>

/// @file Grid2D.hpp
/// @brief 汎用2Dグリッドコンテナ

namespace mitiru::util
{

/// @brief 汎用2Dグリッド
/// @tparam T グリッドに格納する要素の型
/// @note 内部的にはフラットな std::vector で保持し、index = y * width + x でアクセスする
template <typename T>
class Grid2D
{
public:
	/// @brief コンストラクタ
	/// @param width グリッドの幅（列数）。正の値であること
	/// @param height グリッドの高さ（行数）。正の値であること
	/// @param defaultValue 全セルの初期値
	/// @throws std::invalid_argument width または height が 0以下の場合
	Grid2D(int width, int height, const T& defaultValue = T{})
		: m_width(width)
		, m_height(height)
		, m_data(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), defaultValue)
	{
		if (width <= 0 || height <= 0)
		{
			throw std::invalid_argument(
				"Grid2D: width and height must be positive (got "
				+ std::to_string(width) + "x" + std::to_string(height) + ")");
		}
	}

	/// @brief 指定座標の要素への参照を返す
	/// @param x 列インデックス（0始まり）
	/// @param y 行インデックス（0始まり）
	/// @return 要素への参照
	/// @throws std::out_of_range 座標が範囲外の場合
	T& at(int x, int y)
	{
		throwIfOutOfBounds(x, y);
		return m_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)];
	}

	/// @brief 指定座標の要素へのconst参照を返す
	/// @param x 列インデックス（0始まり）
	/// @param y 行インデックス（0始まり）
	/// @return 要素へのconst参照
	/// @throws std::out_of_range 座標が範囲外の場合
	const T& at(int x, int y) const
	{
		throwIfOutOfBounds(x, y);
		return m_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)];
	}

	/// @brief 座標が範囲内かを判定する
	/// @param x 列インデックス
	/// @param y 行インデックス
	/// @return 範囲内ならtrue
	bool inBounds(int x, int y) const noexcept
	{
		return x >= 0 && x < m_width && y >= 0 && y < m_height;
	}

	/// @brief 全セルを指定値で埋める
	/// @param value 埋める値
	void fill(const T& value)
	{
		std::fill(m_data.begin(), m_data.end(), value);
	}

	/// @brief グリッドの幅（列数）を返す
	/// @return 幅
	int width() const noexcept
	{
		return m_width;
	}

	/// @brief グリッドの高さ（行数）を返す
	/// @return 高さ
	int height() const noexcept
	{
		return m_height;
	}

	/// @brief 全セルに対して関数を適用する
	/// @tparam Func func(int x, int y, T& value) を受け取る呼び出し可能オブジェクト
	/// @param func 各セルに適用する関数
	template <typename Func>
	void forEach(Func&& func)
	{
		for (int y = 0; y < m_height; ++y)
		{
			for (int x = 0; x < m_width; ++x)
			{
				func(x, y, m_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)]);
			}
		}
	}

	/// @brief 条件を満たすセルの座標一覧を返す
	/// @tparam Pred pred(int x, int y, const T& value) -> bool を受け取る述語
	/// @param pred 判定関数
	/// @return 条件を満たすセルの (x, y) ペアのベクタ
	template <typename Pred>
	std::vector<std::pair<int, int>> findAll(Pred&& pred) const
	{
		std::vector<std::pair<int, int>> result;
		for (int y = 0; y < m_height; ++y)
		{
			for (int x = 0; x < m_width; ++x)
			{
				const auto& value = m_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)];
				if (pred(x, y, value))
				{
					result.emplace_back(x, y);
				}
			}
		}
		return result;
	}

private:
	/// @brief 範囲外チェック用ヘルパー
	/// @param x 列インデックス
	/// @param y 行インデックス
	/// @throws std::out_of_range 範囲外の場合
	void throwIfOutOfBounds(int x, int y) const
	{
		if (!inBounds(x, y))
		{
			throw std::out_of_range(
				"Grid2D::at: (" + std::to_string(x) + ", " + std::to_string(y)
				+ ") is out of bounds (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");
		}
	}

	/// @brief グリッドの幅
	int m_width;

	/// @brief グリッドの高さ
	int m_height;

	/// @brief フラットな1次元データ配列（index = y * m_width + x）
	std::vector<T> m_data;
};

} // namespace mitiru::util
