#pragma once

/// @file NarrativeVars.hpp
/// @brief 会話・クエスト・カットシーンが読み書きする旗と数の表 (flat POD、GameMemory に置く)。
/// @details 名前は FNV-1a の 32 bit の hash で引く。名前から番号を振る表をアセットの読み込み順で作ると、台本を
///          書き換えたりビルドを替えたりしただけで番号がずれ、セーブの値が別の旗として読まれる。hash なら名前が同じ限り
///          どの版でも同じ鍵になる。違う名前の hash がぶつかる組は、読み込み (NarrativeNames) が名前つきで断る。

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::narrative
{

/// @brief 名前の鍵。0 は「無い」に使うので、hash が 0 になる名前は 1 にする
[[nodiscard]] constexpr std::uint32_t nameHash(std::string_view name) noexcept
{
	std::uint32_t h = 2166136261u;
	for (const char c : name)
	{
		h ^= static_cast<std::uint8_t>(c);
		h *= 16777619u;
	}
	return h == 0 ? 1u : h;
}

/// @brief 旗と数の表。値は int32 (真偽は 0 / 1)。書いたことの無い名前は 0 と読む
struct NarrativeVars
{
	static constexpr int kCapacity = 128;

	std::uint32_t key[kCapacity]{};
	std::int32_t  value[kCapacity]{};
	std::uint32_t count = 0;

	[[nodiscard]] std::int32_t get(std::uint32_t k) const noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (key[i] == k) { return value[i]; }
		}
		return 0;
	}
	[[nodiscard]] std::int32_t get(std::string_view name) const noexcept { return get(nameHash(name)); }

	/// @brief 書く。初めての名前は末尾に足す (書いた順に並ぶので、同じ入力から同じ bytes になる)。満杯なら false
	bool set(std::uint32_t k, std::int32_t v) noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (key[i] == k) { value[i] = v; return true; }
		}
		if (count >= static_cast<std::uint32_t>(kCapacity)) { return false; }
		key[count]   = k;
		value[count] = v;
		++count;
		return true;
	}
	bool set(std::string_view name, std::int32_t v) noexcept { return set(nameHash(name), v); }
};

/// @brief アセットに出てくる名前を集め、hash のぶつかりを見つける (読み込みの時だけ使う)
class NarrativeNames
{
public:
	/// @brief name を登録して鍵を返す。別の名前と鍵がぶつかったら 0 を返し、error() に両方の名前を置く
	std::uint32_t intern(std::string_view name)
	{
		const std::uint32_t k = nameHash(name);
		const auto [it, added] = m_names.emplace(k, std::string(name));
		if (!added && it->second != name)
		{
			m_error = "名前の鍵がぶつかった: \"" + it->second + "\" と \"" + std::string(name) + "\" (どちらかを改名する)";
			return 0;
		}
		return k;
	}

	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief 登録した名前を辞書順で返す (ツール窓が鍵を名前で出すために台本と一緒に持つ)
	[[nodiscard]] std::vector<std::string> sortedNames() const
	{
		std::vector<std::string> out;
		out.reserve(m_names.size());
		for (const auto& [key, name] : m_names) { out.push_back(name); }
		std::sort(out.begin(), out.end());
		return out;
	}

private:
	std::unordered_map<std::uint32_t, std::string> m_names;
	std::string m_error;
};

}  // namespace mitiru::narrative
