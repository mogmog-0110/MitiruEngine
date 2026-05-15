#pragma once

/// @file ObjectPool.hpp
/// @brief オブジェクトプール
/// @details 事前確保されたオブジェクト群の再利用により、
///          ゲームプレイ中のヒープ割り当てを排除する。

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <vector>

namespace mitiru
{

/// @brief スレッドセーフティポリシー: ロックなし
struct NoLock
{
	void lock() noexcept {}
	void unlock() noexcept {}
};

/// @brief スレッドセーフティポリシー: mutex使用
struct MutexLock
{
	void lock() { m_mutex.lock(); }
	void unlock() { m_mutex.unlock(); }
private:
	std::mutex m_mutex;
};

/// @brief 固定容量オブジェクトプール
/// @tparam T プールするオブジェクトの型
/// @tparam LockPolicy スレッドセーフティポリシー（NoLock または MutexLock）
/// @details acquire() でオブジェクトを取得し、release() で返却する。
///          ゲームプレイ中の動的メモリ確保を回避する。
///
/// @code
/// mitiru::ObjectPool<Bullet> pool(256);
/// auto* bullet = pool.acquire();
/// if (bullet) {
///     bullet->position = {10.0f, 20.0f};
///     // ... 使用 ...
///     pool.release(bullet);
/// }
/// @endcode
///
/// @code
/// // スレッドセーフ版
/// mitiru::ObjectPool<Particle, mitiru::MutexLock> pool(1024);
/// @endcode
template <typename T, typename LockPolicy = NoLock>
class ObjectPool
{
public:
	/// @brief コンストラクタ — N個のオブジェクトを事前確保する
	/// @param capacity プールの容量
	explicit ObjectPool(std::size_t capacity)
		: m_capacity(capacity)
	{
		assert(capacity > 0);

		// アライメントを考慮した生メモリ確保
		m_storage.resize(capacity * sizeof(T));
		m_freeList.reserve(capacity);

		// フリーリストを逆順で初期化（先頭から取得できるように）
		for (std::size_t i = capacity; i > 0; --i)
		{
			m_freeList.push_back(objectAt(i - 1));
		}
	}

	/// @brief デストラクタ — アクティブなオブジェクトがあればデストラクタを呼ぶ
	~ObjectPool()
	{
		// フリーリストに含まれない = アクティブなオブジェクトを破棄
		// （ユーザーがreleaseし忘れた場合の安全策）
		// 注: Tがtrivially destructibleなら不要だが、安全側に倒す
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (std::size_t i = 0; i < m_capacity; ++i)
			{
				T* ptr = objectAt(i);
				if (!isInFreeList(ptr))
				{
					ptr->~T();
				}
			}
		}
	}

	/// @brief コピー禁止
	ObjectPool(const ObjectPool&) = delete;
	ObjectPool& operator=(const ObjectPool&) = delete;

	/// @brief ムーブ禁止（ポインタの安定性を保証するため）
	ObjectPool(ObjectPool&&) = delete;
	ObjectPool& operator=(ObjectPool&&) = delete;

	/// @brief オブジェクトをプールから取得する
	/// @return 初期化済みオブジェクトへのポインタ（容量不足時はnullptr）
	[[nodiscard]] T* acquire()
	{
		std::lock_guard<LockPolicy> guard(m_lock);

		if (m_freeList.empty())
		{
			return nullptr;
		}

		T* ptr = m_freeList.back();
		m_freeList.pop_back();

		// placement new でデフォルト構築
		::new (static_cast<void*>(ptr)) T();
		return ptr;
	}

	/// @brief オブジェクトをプールに返却する
	/// @param ptr release対象のポインタ（このプールからacquireしたもの）
	void release(T* ptr) noexcept
	{
		if (!ptr) return;
		assert(owns(ptr) && "released pointer does not belong to this pool");

		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			ptr->~T();
		}

		std::lock_guard<LockPolicy> guard(m_lock);
		m_freeList.push_back(ptr);
	}

	/// @brief 指定ポインタがこのプールの所有物か判定する
	/// @param ptr 判定対象のポインタ
	/// @return true: このプールの管轄内
	[[nodiscard]] bool owns(const T* ptr) const noexcept
	{
		const auto* raw = reinterpret_cast<const unsigned char*>(ptr);
		const auto* begin = m_storage.data();
		const auto* end = begin + m_storage.size();
		if (raw < begin || raw >= end) return false;

		const auto offset = static_cast<std::size_t>(raw - begin);
		return (offset % sizeof(T)) == 0;
	}

	/// @brief プールの総容量
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return m_capacity;
	}

	/// @brief 現在利用可能なオブジェクト数
	[[nodiscard]] std::size_t available() const noexcept
	{
		return m_freeList.size();
	}

	/// @brief 現在使用中のオブジェクト数
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		return m_capacity - m_freeList.size();
	}

private:
	std::size_t m_capacity;
	std::vector<unsigned char> m_storage;
	std::vector<T*> m_freeList;
	mutable LockPolicy m_lock;

	/// @brief インデックスからオブジェクトポインタを取得する
	[[nodiscard]] T* objectAt(std::size_t index) noexcept
	{
		return reinterpret_cast<T*>(m_storage.data() + index * sizeof(T));
	}

	/// @brief ポインタがフリーリストに含まれるか判定する（デストラクタ用）
	[[nodiscard]] bool isInFreeList(const T* ptr) const noexcept
	{
		for (const auto* p : m_freeList)
		{
			if (p == ptr) return true;
		}
		return false;
	}
};

} // namespace mitiru
