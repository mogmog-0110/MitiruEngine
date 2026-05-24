#pragma once

/// @file FreeList.hpp
/// @brief 固定サイズプールアロケータ（FreeList）
///
/// O(1)のアロケート・フリーを実現するプールアロケータ。
/// union ベースのノードで侵入型フリーリストを構築する。
/// ハンドル（インデックス）ベースのAPIを提供。
///
/// @code
/// sgc::containers::FreeList<MyComponent, 256> pool;
/// auto handle = pool.allocate(arg1, arg2);
/// if (handle) {
///     MyComponent& comp = pool.get(*handle);
///     pool.free(*handle);
/// }
/// @endcode

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>

namespace sgc::containers
{

/// @brief 固定サイズプールアロケータ
///
/// コンパイル時にCapacityを指定し、ヒープ確保なしで
/// オブジェクトプールを提供する。フリーリストにより
/// O(1)での確保・解放を実現。
///
/// @tparam T 格納する型（破棄可能かつムーブ構築可能であること）
/// @tparam Capacity プールの最大要素数
template <typename T, std::size_t Capacity>
	requires std::destructible<T> && std::move_constructible<T>
class FreeList
{
public:
	/// @brief ハンドル型（プール内インデックス）
	using Handle = std::size_t;

	/// @brief 無効なハンドルを示す定数
	static constexpr Handle INVALID_HANDLE = static_cast<Handle>(-1);

	/// @brief コンストラクタ（フリーリストを初期化）
	FreeList()
	{
		initFreeList();
	}

	/// @brief デストラクタ（生存中の全要素を破棄）
	~FreeList()
	{
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			if (m_alive[i])
			{
				std::destroy_at(std::launder(reinterpret_cast<T*>(&m_storage[i])));
			}
		}
	}

	/// @brief コピー禁止
	FreeList(const FreeList&) = delete;
	/// @brief コピー代入禁止
	FreeList& operator=(const FreeList&) = delete;

	/// @brief ムーブコンストラクタ
	FreeList(FreeList&& other) noexcept
		: m_freeHead(other.m_freeHead)
		, m_count(other.m_count)
		, m_alive(other.m_alive)
	{
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			if (m_alive[i])
			{
				auto* src = std::launder(reinterpret_cast<T*>(&other.m_storage[i]));
				::new (&m_storage[i]) T(std::move(*src));
				std::destroy_at(src);
			}
			else
			{
				// フリーリストのnextインデックスをコピー
				m_storage[i].next = other.m_storage[i].next;
			}
		}
		other.m_count = 0;
		std::fill(other.m_alive.begin(), other.m_alive.end(), false);
		other.initFreeList();
	}

	/// @brief ムーブ代入禁止（簡潔さのため）
	FreeList& operator=(FreeList&&) = delete;

	/// @brief 要素を構築して確保する
	/// @tparam Args コンストラクタ引数の型
	/// @param args コンストラクタに渡す引数
	/// @return 確保に成功した場合ハンドル、満杯の場合nullopt
	template <typename... Args>
	[[nodiscard]] std::optional<Handle> allocate(Args&&... args)
	{
		if (m_freeHead == INVALID_HANDLE)
		{
			return std::nullopt;
		}
		const Handle handle = m_freeHead;
		m_freeHead = m_storage[handle].next;
		::new (&m_storage[handle]) T(std::forward<Args>(args)...);
		m_alive[handle] = true;
		++m_count;
		return handle;
	}

	/// @brief 要素を解放する
	/// @param handle 解放するハンドル
	/// @return 解放に成功した場合true
	bool free(Handle handle)
	{
		if (handle >= Capacity || !m_alive[handle])
		{
			return false;
		}
		std::destroy_at(std::launder(reinterpret_cast<T*>(&m_storage[handle])));
		m_alive[handle] = false;
		m_storage[handle].next = m_freeHead;
		m_freeHead = handle;
		--m_count;
		return true;
	}

	/// @brief ハンドルから要素への参照を取得する
	/// @param handle 有効なハンドル
	/// @return 要素への参照
	[[nodiscard]] T& get(Handle handle)
	{
		assert(handle < Capacity && m_alive[handle] && "Invalid handle");
		return *std::launder(reinterpret_cast<T*>(&m_storage[handle]));
	}

	/// @brief ハンドルから要素へのconst参照を取得する
	/// @param handle 有効なハンドル
	/// @return 要素へのconst参照
	[[nodiscard]] const T& get(Handle handle) const
	{
		assert(handle < Capacity && m_alive[handle] && "Invalid handle");
		return *std::launder(reinterpret_cast<const T*>(&m_storage[handle]));
	}

	/// @brief ハンドルが有効（生存中）か判定する
	/// @param handle 判定するハンドル
	/// @return 有効な場合true
	[[nodiscard]] bool isAlive(Handle handle) const noexcept
	{
		return handle < Capacity && m_alive[handle];
	}

	/// @brief 現在の使用中要素数を返す
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_count;
	}

	/// @brief プールが空か判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return m_count == 0;
	}

	/// @brief プールが満杯か判定する
	[[nodiscard]] bool full() const noexcept
	{
		return m_count == Capacity;
	}

	/// @brief プールの最大容量を返す
	[[nodiscard]] static constexpr std::size_t capacity() noexcept
	{
		return Capacity;
	}

	/// @brief 全要素を解放してプールをリセットする
	void clear()
	{
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			if (m_alive[i])
			{
				std::destroy_at(std::launder(reinterpret_cast<T*>(&m_storage[i])));
			}
		}
		m_count = 0;
		std::fill(m_alive.begin(), m_alive.end(), false);
		initFreeList();
	}

private:
	/// @brief フリーリストノード
	union Node
	{
		Handle next;                                ///< 次のフリーノードインデックス
		alignas(T) unsigned char data[sizeof(T)];   ///< 要素ストレージ
	};

	/// @brief フリーリストを連鎖的に初期化する
	void initFreeList()
	{
		m_freeHead = 0;
		for (std::size_t i = 0; i < Capacity - 1; ++i)
		{
			m_storage[i].next = i + 1;
		}
		m_storage[Capacity - 1].next = INVALID_HANDLE;
	}

	std::array<Node, Capacity> m_storage{};        ///< ノードストレージ
	std::array<bool, Capacity> m_alive{};          ///< 生存フラグ配列
	Handle m_freeHead = 0;                         ///< フリーリスト先頭
	std::size_t m_count = 0;                       ///< 使用中要素数
};

} // namespace sgc::containers
