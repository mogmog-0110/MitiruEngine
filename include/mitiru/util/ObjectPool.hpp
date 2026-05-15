#pragma once

/// @file ObjectPool.hpp
/// @brief 軽量オブジェクトプール
/// @details acquire/releaseでオブジェクトを管理し、forEachActiveでアクティブ要素を走査する。

#include <functional>
#include <stdexcept>
#include <vector>

namespace mitiru::util
{

/// @brief 軽量オブジェクトプール
/// @tparam T プールに格納するオブジェクトの型（デフォルト構築可能であること）
template <typename T>
class ObjectPool
{
public:
	/// @brief コンストラクタ
	/// @param capacity 初期容量
	explicit ObjectPool(int capacity = 64)
	{
		if (capacity <= 0)
		{
			throw std::invalid_argument("ObjectPool: capacity must be positive");
		}
		m_objects.resize(static_cast<std::size_t>(capacity));
		m_active.resize(static_cast<std::size_t>(capacity), false);
	}

	/// @brief オブジェクトを取得する
	/// @return 取得したオブジェクトへの参照とインデックスのペア
	/// @throws std::runtime_error プールが満杯の場合
	std::pair<T&, int> acquire()
	{
		for (std::size_t i = 0; i < m_active.size(); ++i)
		{
			if (!m_active[i])
			{
				m_active[i] = true;
				m_objects[i] = T{};
				++m_activeCount;
				return {m_objects[i], static_cast<int>(i)};
			}
		}
		throw std::runtime_error("ObjectPool: pool is full");
	}

	/// @brief オブジェクトを返却する
	/// @param index 返却するオブジェクトのインデックス
	void release(int index)
	{
		const auto idx = static_cast<std::size_t>(index);
		if (idx < m_active.size() && m_active[idx])
		{
			m_active[idx] = false;
			--m_activeCount;
		}
	}

	/// @brief アクティブな全オブジェクトに関数を適用する
	/// @tparam Func func(T& obj, int index) を受け取る呼び出し可能オブジェクト
	/// @param func 各アクティブ要素に適用する関数
	template <typename Func>
	void forEachActive(Func&& func)
	{
		for (std::size_t i = 0; i < m_active.size(); ++i)
		{
			if (m_active[i])
			{
				func(m_objects[i], static_cast<int>(i));
			}
		}
	}

	/// @brief アクティブな全オブジェクトに関数を適用する（const版）
	template <typename Func>
	void forEachActive(Func&& func) const
	{
		for (std::size_t i = 0; i < m_active.size(); ++i)
		{
			if (m_active[i])
			{
				func(m_objects[i], static_cast<int>(i));
			}
		}
	}

	/// @brief アクティブなオブジェクト数を返す
	[[nodiscard]] int activeCount() const noexcept { return m_activeCount; }

	/// @brief プール容量を返す
	[[nodiscard]] int capacity() const noexcept { return static_cast<int>(m_objects.size()); }

	/// @brief 指定インデックスのオブジェクトがアクティブかを返す
	[[nodiscard]] bool isActive(int index) const noexcept
	{
		const auto idx = static_cast<std::size_t>(index);
		return idx < m_active.size() && m_active[idx];
	}

	/// @brief 指定インデックスのオブジェクトへの参照を返す
	/// @throws std::out_of_range 範囲外の場合
	T& at(int index)
	{
		const auto idx = static_cast<std::size_t>(index);
		if (idx >= m_objects.size())
		{
			throw std::out_of_range("ObjectPool::at: index out of range");
		}
		return m_objects[idx];
	}

	/// @brief 指定インデックスのオブジェクトへのconst参照を返す
	const T& at(int index) const
	{
		const auto idx = static_cast<std::size_t>(index);
		if (idx >= m_objects.size())
		{
			throw std::out_of_range("ObjectPool::at: index out of range");
		}
		return m_objects[idx];
	}

private:
	std::vector<T> m_objects;         ///< オブジェクト配列
	std::vector<bool> m_active;       ///< アクティブフラグ配列
	int m_activeCount = 0;            ///< アクティブ数
};

} // namespace mitiru::util
