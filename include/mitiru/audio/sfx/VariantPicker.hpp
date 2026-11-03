#pragma once

/// @file VariantPicker.hpp
/// @brief 効果音の変種を、直前と同じものを避けて選ぶ仕組み（sounds.json の avoidRepeat / shuffle、ADR 0113）
/// @details 選ぶ変種は seed、id、同じ id を鳴らした通算回数 seq で決まり、フレームやフレーム内の順番には依らない。
/// 巻き戻すと、その時点より後の選択を履歴から落として seq を戻すため、同じ鳴らし方なら同じ並びになる。
/// 履歴は id ごとに kDepth 本まで持ち、それより深く戻すと並びが最初からに戻る。
/// avoidRepeat も shuffle もない音は履歴を作らず、従来の rollSfx の選び方を使う。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include <mitiru/audio/sfx/SfxBank.hpp>

namespace mitiru::audio::sfx
{

/// @brief 変種が 1 周するまで重ならない並びの seq 番目。周の境目でも同じ変種が続かない。変種が 2 つなら交互になる。
[[nodiscard]] inline std::size_t shuffleVariant(std::uint32_t seed, std::uint64_t idHash, std::uint64_t seq, std::size_t n)
{
	if (n < 2) { return 0; }
	const std::uint64_t base = mixBits(seed ^ mixBits(idHash ^ 0x5AF1EDull));
	if (n == 2) { return static_cast<std::size_t>((base + seq) & 1u); }
	const std::uint64_t bag = seq / n;
	const std::size_t   pos = static_cast<std::size_t>(seq % n);
	auto permutation = [&](std::uint64_t bagNo) {
		std::vector<std::size_t> p(n);
		for (std::size_t i = 0; i < n; ++i) { p[i] = i; }
		std::uint64_t h = mixBits(base ^ mixBits(bagNo));
		for (std::size_t i = n - 1; i > 0; --i)
		{
			h = mixBits(h);
			std::swap(p[i], p[static_cast<std::size_t>(h % (i + 1))]);
		}
		return p;
	};
	std::vector<std::size_t> p = permutation(bag);
	// 前の周の最後。0 番目と 1 番目を入れ替えても、2 番目以降の最後は動かない。
	if (bag > 0 && p[0] == permutation(bag - 1)[n - 1]) { std::swap(p[0], p[1]); }
	return p[pos];
}

class VariantPicker
{
public:
	static constexpr std::size_t kDepth = 512;

	/// @param randomVariant rollSfx が選んだ変種。avoidRepeat も shuffle もない音は、そのまま返す。
	[[nodiscard]] std::size_t pick(const SfxDef& d, std::uint32_t seed, std::uint64_t frame, std::uint64_t idHash,
	                               std::size_t randomVariant)
	{
		const std::size_t n = d.variants.size();
		if (n < 2 || (d.avoidRepeat == 0 && !d.shuffle)) { return randomVariant; }
		std::deque<Pick>& history = m_history[idHash];
		const std::uint64_t seq = history.empty() ? 0 : history.back().seq + 1;
		std::size_t chosen = 0;
		if (d.shuffle)
		{
			chosen = shuffleVariant(seed, idHash, seq, n);
		}
		else
		{
			std::vector<std::size_t> allowed;
			allowed.reserve(n);
			const std::size_t avoid = std::min<std::size_t>(d.avoidRepeat, n - 1);
			for (std::size_t v = 0; v < n; ++v)
			{
				bool recent = false;
				for (std::size_t k = 0; k < avoid && k < history.size(); ++k)
				{
					if (history[history.size() - 1 - k].variant == v) { recent = true; break; }
				}
				if (!recent) { allowed.push_back(v); }
			}
			const std::uint64_t h = mixBits(seed ^ mixBits(idHash ^ mixBits(seq + 0xA5A5ull)));
			chosen = allowed[static_cast<std::size_t>(h % allowed.size())];
		}
		history.push_back({frame, seq, chosen});
		if (history.size() > kDepth) { history.pop_front(); }
		return chosen;
	}

	/// @brief 巻き戻しのため、frame より後の選択を落とす。frame ちょうどの選択は残す。
	void rewindTo(std::uint64_t frame)
	{
		for (auto& entry : m_history)
		{
			std::deque<Pick>& h = entry.second;
			while (!h.empty() && h.back().frame > frame) { h.pop_back(); }
		}
	}

	void clear() { m_history.clear(); }

	/// @brief id の選択を新しい順に返す。テストと観察に使う。
	[[nodiscard]] std::vector<std::size_t> recent(std::uint64_t idHash) const
	{
		std::vector<std::size_t> out;
		if (const auto it = m_history.find(idHash); it != m_history.end())
		{
			for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) { out.push_back(r->variant); }
		}
		return out;
	}

private:
	struct Pick
	{
		std::uint64_t frame = 0;
		std::uint64_t seq = 0;
		std::size_t   variant = 0;
	};

	std::unordered_map<std::uint64_t, std::deque<Pick>> m_history;
};

}  // namespace mitiru::audio::sfx
