#pragma once

/// @file RingBuffer.hpp
/// @brief 固定サイズリングバッファ
///
/// コンパイル時にサイズが決定される固定容量のリングバッファ。
/// 入力バッファリングやログ記録などに使用する。
///
/// @code
/// sgc::RingBuffer<int, 4> buf;
/// buf.pushBack(1);
/// buf.pushBack(2);
/// buf.pushBack(3);
/// buf.pushBack(4);
/// buf.pushBack(5); // 1が上書きされる
/// // buf: [2, 3, 4, 5]
/// @endcode

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace sgc
{

/// @brief 固定サイズリングバッファ
/// @tparam T 要素型
/// @tparam N バッファ容量（コンパイル時定数）
template <typename T, std::size_t N>
	requires (N > 0)
class RingBuffer
{
public:
	/// @brief 前方イテレータ
	class Iterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = T&;

		Iterator() = default;

		/// @brief コンストラクタ
		/// @param buffer バッファへのポインタ
		/// @param index 論理インデックス
		Iterator(RingBuffer* buffer, std::size_t index)
			: m_buffer(buffer), m_index(index)
		{
		}

		/// @brief 参照外し
		[[nodiscard]] reference operator*() const
		{
			return (*m_buffer)[m_index];
		}

		/// @brief ポインタアクセス
		[[nodiscard]] pointer operator->() const
		{
			return &(*m_buffer)[m_index];
		}

		/// @brief 前置インクリメント
		Iterator& operator++()
		{
			++m_index;
			return *this;
		}

		/// @brief 後置インクリメント
		Iterator operator++(int)
		{
			auto tmp = *this;
			++m_index;
			return tmp;
		}

		/// @brief 等値比較
		[[nodiscard]] bool operator==(const Iterator& rhs) const noexcept = default;

	private:
		RingBuffer* m_buffer{nullptr};
		std::size_t m_index{0};
	};

	/// @brief constイテレータ
	class ConstIterator
	{
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = const T*;
		using reference = const T&;

		ConstIterator() = default;

		/// @brief コンストラクタ
		/// @param buffer バッファへのconstポインタ
		/// @param index 論理インデックス
		ConstIterator(const RingBuffer* buffer, std::size_t index)
			: m_buffer(buffer), m_index(index)
		{
		}

		/// @brief 参照外し
		[[nodiscard]] reference operator*() const
		{
			return (*m_buffer)[m_index];
		}

		/// @brief ポインタアクセス
		[[nodiscard]] pointer operator->() const
		{
			return &(*m_buffer)[m_index];
		}

		/// @brief 前置インクリメント
		ConstIterator& operator++()
		{
			++m_index;
			return *this;
		}

		/// @brief 後置インクリメント
		ConstIterator operator++(int)
		{
			auto tmp = *this;
			++m_index;
			return tmp;
		}

		/// @brief 等値比較
		[[nodiscard]] bool operator==(const ConstIterator& rhs) const noexcept = default;

	private:
		const RingBuffer* m_buffer{nullptr};
		std::size_t m_index{0};
	};

	/// @brief 末尾に要素を追加する（満杯時は先頭を上書き）
	/// @param value 追加する値
	void pushBack(const T& value)
	{
		if (m_size == N)
		{
			// 満杯：先頭を上書きして進める
			m_buffer[m_tail] = value;
			m_tail = (m_tail + 1) % N;
			m_head = (m_head + 1) % N;
		}
		else
		{
			m_buffer[m_tail] = value;
			m_tail = (m_tail + 1) % N;
			++m_size;
		}
	}

	/// @brief 末尾に要素を追加する（ムーブ版）
	/// @param value 追加する値
	void pushBack(T&& value)
	{
		if (m_size == N)
		{
			m_buffer[m_tail] = std::move(value);
			m_tail = (m_tail + 1) % N;
			m_head = (m_head + 1) % N;
		}
		else
		{
			m_buffer[m_tail] = std::move(value);
			m_tail = (m_tail + 1) % N;
			++m_size;
		}
	}

	/// @brief 先頭の要素を削除する
	void popFront()
	{
		assert(!empty() && "popFront on empty RingBuffer");
		m_head = (m_head + 1) % N;
		--m_size;
	}

	/// @brief 末尾の要素を削除する
	void popBack()
	{
		assert(!empty() && "popBack on empty RingBuffer");
		m_tail = (m_tail + N - 1) % N;
		--m_size;
	}

	/// @brief 先頭要素への参照
	/// @return 先頭要素
	[[nodiscard]] T& front()
	{
		assert(!empty() && "front on empty RingBuffer");
		return m_buffer[m_head];
	}

	/// @brief 先頭要素へのconst参照
	/// @return 先頭要素
	[[nodiscard]] const T& front() const
	{
		assert(!empty() && "front on empty RingBuffer");
		return m_buffer[m_head];
	}

	/// @brief 末尾要素への参照
	/// @return 末尾要素
	[[nodiscard]] T& back()
	{
		assert(!empty() && "back on empty RingBuffer");
		return m_buffer[(m_tail + N - 1) % N];
	}

	/// @brief 末尾要素へのconst参照
	/// @return 末尾要素
	[[nodiscard]] const T& back() const
	{
		assert(!empty() && "back on empty RingBuffer");
		return m_buffer[(m_tail + N - 1) % N];
	}

	/// @brief 論理インデックスによるアクセス
	/// @param index 論理インデックス（0 = 先頭）
	/// @return 要素への参照
	[[nodiscard]] T& operator[](std::size_t index)
	{
		assert(index < m_size && "RingBuffer index out of range");
		return m_buffer[(m_head + index) % N];
	}

	/// @brief 論理インデックスによるアクセス（const版）
	/// @param index 論理インデックス（0 = 先頭）
	/// @return 要素へのconst参照
	[[nodiscard]] const T& operator[](std::size_t index) const
	{
		assert(index < m_size && "RingBuffer index out of range");
		return m_buffer[(m_head + index) % N];
	}

	/// @brief バッファが空かどうか
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept { return m_size == 0; }

	/// @brief バッファが満杯かどうか
	/// @return 満杯ならtrue
	[[nodiscard]] bool full() const noexcept { return m_size == N; }

	/// @brief 現在の要素数
	/// @return 要素数
	[[nodiscard]] std::size_t size() const noexcept { return m_size; }

	/// @brief バッファ容量
	/// @return 容量
	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }

	/// @brief 全要素を削除する
	void clear() noexcept
	{
		m_head = 0;
		m_tail = 0;
		m_size = 0;
	}

	/// @brief 先頭イテレータ
	[[nodiscard]] Iterator begin() { return Iterator(this, 0); }

	/// @brief 末尾イテレータ
	[[nodiscard]] Iterator end() { return Iterator(this, m_size); }

	/// @brief const先頭イテレータ
	[[nodiscard]] ConstIterator begin() const { return ConstIterator(this, 0); }

	/// @brief const末尾イテレータ
	[[nodiscard]] ConstIterator end() const { return ConstIterator(this, m_size); }

private:
	std::array<T, N> m_buffer{};   ///< 内部バッファ
	std::size_t m_head{0};         ///< 先頭位置
	std::size_t m_tail{0};         ///< 末尾位置（次の書き込み位置）
	std::size_t m_size{0};         ///< 現在の要素数
};

} // namespace sgc
