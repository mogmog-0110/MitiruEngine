#pragma once

/// @file FixedVec.hpp
/// @brief flat POD な固定容量コンテナ。GameMemory に入れても trivially_copyable を保つ。
/// @details
/// GameMemory は flat POD (= trivially_copyable) でなければならない (ADR 0017、host が
/// bytes を memcpy で記録・rewind するため)。`std::vector` / `std::string` は内部 pointer
/// を持つので使えない。代わりにこの固定容量コンテナを使う:
///
/// @code
///   struct GameMemory {
///       mitiru::FixedVec<Enemy, 64> enemies;   // std::vector<Enemy> の代わり
///       mitiru::FixedString<32>     name;      // std::string の代わり
///   };
///   static_assert(std::is_trivially_copyable_v<GameMemory>);  // MITIRU_GAME が自動で確認
/// @endcode
///
/// 上限を型に焼くことで「敵は最大何体か」を設計時に意識させる (hot-path discipline とも
/// 一貫: 毎フレーム確保しない)。push_back / at / size / clear は vector の手触りに寄せる。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief 固定容量 N の flat POD ベクタ。例外を投げない。
/// @tparam T 要素型 (これ自身も trivially_copyable であること)
/// @tparam N 最大要素数
template <typename T, std::size_t N>
struct FixedVec
{
	static_assert(std::is_trivially_copyable_v<T>,
	              "FixedVec<T,N>: 要素 T も flat POD (trivially_copyable) であること");

	T             data[N]{};
	std::uint32_t count = 0;

	[[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool        empty() const noexcept { return count == 0; }
	[[nodiscard]] constexpr bool        full() const noexcept { return count >= N; }
	constexpr void                      clear() noexcept { count = 0; }

	/// @brief 末尾追加。容量超過時は捨てて false を返す (例外なし)。
	constexpr bool push_back(const T& v) noexcept
	{
		if (count >= N) { return false; }
		data[count++] = v;
		return true;
	}

	/// @brief swap-remove (順序非保持・O(1))。範囲外は no-op。
	constexpr void removeAt(std::size_t i) noexcept
	{
		if (i < count) { data[i] = data[--count]; }
	}

	[[nodiscard]] constexpr T&       operator[](std::size_t i) noexcept { return data[i]; }
	[[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return data[i]; }

	[[nodiscard]] constexpr T*       begin() noexcept { return data; }
	[[nodiscard]] constexpr T*       end() noexcept { return data + count; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return data; }
	[[nodiscard]] constexpr const T* end() const noexcept { return data + count; }
};

/// @brief 固定容量 N の flat POD 文字列 (null 終端、std::string の代替)。
/// @tparam N バッファ長 (終端含む)
template <std::size_t N>
struct FixedString
{
	static_assert(N >= 1, "FixedString<N>: N は 1 以上");

	char chars[N]{};

	/// @brief C 文字列をコピー (切り詰め、null 終端を保証)。null は空文字列。
	constexpr void set(const char* s) noexcept
	{
		std::size_t i = 0;
		if (s != nullptr)
		{
			for (; s[i] != '\0' && i + 1 < N; ++i) { chars[i] = s[i]; }
		}
		chars[i] = '\0';
	}

	[[nodiscard]] constexpr const char* c_str() const noexcept { return chars; }
	[[nodiscard]] constexpr bool        empty() const noexcept { return chars[0] == '\0'; }
};

}  // namespace mitiru
