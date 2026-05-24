#pragma once

/// @file BitSet.hpp
/// @brief 固定サイズビットセット
///
/// コンパイル時サイズ決定のビットセット。constexprで全操作が可能。
/// ECSのコンポーネントマスクやフラグ管理に適している。
///
/// @code
/// sgc::containers::BitSet<64> flags;
/// flags.set(0);
/// flags.set(3);
/// assert(flags.count() == 2);
/// assert(flags.test(0));
/// @endcode

#include <array>
#include <cstddef>
#include <cstdint>

namespace sgc::containers
{

/// @brief 固定サイズビットセット
/// @tparam N ビット数
template <size_t N>
class BitSet
{
public:
	/// @brief デフォルトコンストラクタ（全ビット0）
	constexpr BitSet() noexcept = default;

	/// @brief 指定インデックスのビットをセットする
	/// @param index セットするビットのインデックス
	constexpr void set(size_t index) noexcept
	{
		if (index >= N) return;
		m_words[index / BITS_PER_WORD] |= (uint64_t{1} << (index % BITS_PER_WORD));
	}

	/// @brief 指定インデックスのビットをクリアする
	/// @param index クリアするビットのインデックス
	constexpr void clear(size_t index) noexcept
	{
		if (index >= N) return;
		m_words[index / BITS_PER_WORD] &= ~(uint64_t{1} << (index % BITS_PER_WORD));
	}

	/// @brief 指定インデックスのビットをトグルする
	/// @param index トグルするビットのインデックス
	constexpr void toggle(size_t index) noexcept
	{
		if (index >= N) return;
		m_words[index / BITS_PER_WORD] ^= (uint64_t{1} << (index % BITS_PER_WORD));
	}

	/// @brief 指定インデックスのビットを検査する
	/// @param index 検査するビットのインデックス
	/// @return ビットがセットされていればtrue
	[[nodiscard]] constexpr bool test(size_t index) const noexcept
	{
		if (index >= N) return false;
		return (m_words[index / BITS_PER_WORD] & (uint64_t{1} << (index % BITS_PER_WORD))) != 0;
	}

	/// @brief 全ビットをセットする
	constexpr void setAll() noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			m_words[i] = ~uint64_t{0};
		}
		clearExtraBits();
	}

	/// @brief 全ビットをクリアする
	constexpr void clearAll() noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			m_words[i] = uint64_t{0};
		}
	}

	/// @brief セットされているビット数を返す
	/// @return セットされているビットの数
	[[nodiscard]] constexpr size_t count() const noexcept
	{
		size_t result = 0;
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			result += popcount(m_words[i]);
		}
		return result;
	}

	/// @brief ビットセットのサイズ（ビット数）を返す
	/// @return ビット数 N
	[[nodiscard]] constexpr size_t size() const noexcept { return N; }

	/// @brief 全ビットがクリアされているか
	/// @return 全ビットが0ならtrue
	[[nodiscard]] constexpr bool none() const noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			if (m_words[i] != 0) return false;
		}
		return true;
	}

	/// @brief いずれかのビットがセットされているか
	/// @return 1つ以上のビットがセットされていればtrue
	[[nodiscard]] constexpr bool any() const noexcept { return !none(); }

	/// @brief 全ビットがセットされているか
	/// @return 全ビットがセットされていればtrue
	[[nodiscard]] constexpr bool all() const noexcept
	{
		if constexpr (WORD_COUNT == 0) return true;

		// 最後以外のワードは全ビットセット確認
		for (size_t i = 0; i + 1 < WORD_COUNT; ++i)
		{
			if (m_words[i] != ~uint64_t{0}) return false;
		}
		// 最後のワードは有効ビットのみ確認
		constexpr size_t extraBits = N % BITS_PER_WORD;
		if constexpr (extraBits == 0)
		{
			return m_words[WORD_COUNT - 1] == ~uint64_t{0};
		}
		else
		{
			constexpr uint64_t mask = (uint64_t{1} << extraBits) - 1;
			return m_words[WORD_COUNT - 1] == mask;
		}
	}

	// ── ビット演算子 ──────────────────────────────────────────

	/// @brief AND演算
	[[nodiscard]] constexpr BitSet operator&(const BitSet& other) const noexcept
	{
		BitSet result;
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			result.m_words[i] = m_words[i] & other.m_words[i];
		}
		return result;
	}

	/// @brief OR演算
	[[nodiscard]] constexpr BitSet operator|(const BitSet& other) const noexcept
	{
		BitSet result;
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			result.m_words[i] = m_words[i] | other.m_words[i];
		}
		return result;
	}

	/// @brief XOR演算
	[[nodiscard]] constexpr BitSet operator^(const BitSet& other) const noexcept
	{
		BitSet result;
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			result.m_words[i] = m_words[i] ^ other.m_words[i];
		}
		return result;
	}

	/// @brief NOT演算（有効ビットのみ反転）
	[[nodiscard]] constexpr BitSet operator~() const noexcept
	{
		BitSet result;
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			result.m_words[i] = ~m_words[i];
		}
		result.clearExtraBits();
		return result;
	}

	/// @brief AND代入演算
	constexpr BitSet& operator&=(const BitSet& other) noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			m_words[i] &= other.m_words[i];
		}
		return *this;
	}

	/// @brief OR代入演算
	constexpr BitSet& operator|=(const BitSet& other) noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			m_words[i] |= other.m_words[i];
		}
		return *this;
	}

	/// @brief XOR代入演算
	constexpr BitSet& operator^=(const BitSet& other) noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			m_words[i] ^= other.m_words[i];
		}
		return *this;
	}

	/// @brief 等値比較
	constexpr bool operator==(const BitSet& other) const noexcept = default;

	/// @brief 最初にセットされたビットのインデックスを返す
	/// @return 最初にセットされたビットのインデックス。なければ-1
	[[nodiscard]] constexpr int32_t firstSet() const noexcept
	{
		for (size_t i = 0; i < WORD_COUNT; ++i)
		{
			if (m_words[i] != 0)
			{
				const int32_t bit = countTrailingZeros(m_words[i]);
				const int32_t index = static_cast<int32_t>(i * BITS_PER_WORD) + bit;
				return (static_cast<size_t>(index) < N) ? index : -1;
			}
		}
		return -1;
	}

private:
	static constexpr size_t BITS_PER_WORD = 64;
	static constexpr size_t WORD_COUNT = (N + BITS_PER_WORD - 1) / BITS_PER_WORD;

	std::array<uint64_t, WORD_COUNT> m_words{};

	/// @brief N境界を超える余分なビットをクリアする
	constexpr void clearExtraBits() noexcept
	{
		constexpr size_t extraBits = N % BITS_PER_WORD;
		if constexpr (extraBits != 0)
		{
			m_words[WORD_COUNT - 1] &= (uint64_t{1} << extraBits) - 1;
		}
	}

	/// @brief constexpr対応のpopcount
	/// @param x 対象の64ビット整数
	/// @return セットされたビット数
	[[nodiscard]] static constexpr size_t popcount(uint64_t x) noexcept
	{
		// Hamming weight アルゴリズム
		x = x - ((x >> 1) & 0x5555555555555555ULL);
		x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
		x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
		return static_cast<size_t>((x * 0x0101010101010101ULL) >> 56);
	}

	/// @brief constexpr対応のctz（末尾ゼロカウント）
	/// @param x 対象の64ビット整数（0でないこと）
	/// @return 末尾のゼロビット数
	[[nodiscard]] static constexpr int32_t countTrailingZeros(uint64_t x) noexcept
	{
		if (x == 0) return 64;
		int32_t count = 0;
		if ((x & 0x00000000FFFFFFFFULL) == 0) { count += 32; x >>= 32; }
		if ((x & 0x000000000000FFFFULL) == 0) { count += 16; x >>= 16; }
		if ((x & 0x00000000000000FFULL) == 0) { count += 8;  x >>= 8;  }
		if ((x & 0x000000000000000FULL) == 0) { count += 4;  x >>= 4;  }
		if ((x & 0x0000000000000003ULL) == 0) { count += 2;  x >>= 2;  }
		if ((x & 0x0000000000000001ULL) == 0) { count += 1; }
		return count;
	}
};

} // namespace sgc::containers
