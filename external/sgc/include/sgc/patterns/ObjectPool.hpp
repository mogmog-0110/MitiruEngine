#pragma once

/// @file ObjectPool.hpp
/// @brief 固定サイズオブジェクトプール
///
/// 頻繁な生成・破棄が発生するオブジェクト（弾、パーティクル等）の
/// メモリアロケーションを回避するために使用する。

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace sgc
{

/// @brief 固定容量のオブジェクトプール
///
/// 事前に確保されたオブジェクトを再利用し、動的アロケーションを削減する。
///
/// @tparam T プールするオブジェクトの型
///
/// @code
/// sgc::ObjectPool<Bullet> pool(100, []{ return Bullet{}; });
///
/// auto* bullet = pool.acquire();  // プールから取得
/// if (bullet) {
///     bullet->fire(position, direction);
/// }
/// pool.release(bullet);  // プールに返却
/// @endcode
template <typename T>
class ObjectPool
{
public:
	/// @brief プールを構築する
	/// @param capacity 最大オブジェクト数
	/// @param factory オブジェクト生成関数
	explicit ObjectPool(std::size_t capacity, std::function<T()> factory = []{ return T{}; })
	{
		m_pool.reserve(capacity);
		for (std::size_t i = 0; i < capacity; ++i)
		{
			m_pool.push_back(factory());
			m_available.push_back(i);
		}
	}

	/// @brief プールからオブジェクトを取得する
	/// @return オブジェクトへのポインタ。プールが空の場合は nullptr
	[[nodiscard]] T* acquire()
	{
		if (m_available.empty()) return nullptr;

		const auto index = m_available.back();
		m_available.pop_back();
		return &m_pool[index];
	}

	/// @brief オブジェクトをプールに返却する
	/// @param obj 返却するオブジェクトへのポインタ
	void release(T* obj)
	{
		if (!obj) return;

		const auto index = static_cast<std::size_t>(obj - m_pool.data());
		if (index < m_pool.size())
		{
			m_available.push_back(index);
		}
	}

	/// @brief プールの利用可能なオブジェクト数を返す
	[[nodiscard]] std::size_t available() const noexcept { return m_available.size(); }

	/// @brief プールの総容量を返す
	[[nodiscard]] std::size_t capacity() const noexcept { return m_pool.size(); }

	/// @brief 使用中のオブジェクト数を返す
	[[nodiscard]] std::size_t inUse() const noexcept { return m_pool.size() - m_available.size(); }

private:
	std::vector<T> m_pool;
	std::vector<std::size_t> m_available;
};

} // namespace sgc
