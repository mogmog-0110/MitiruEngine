#pragma once

/// @file FrameMotionHistory.hpp
/// @brief 前フレームの同じ物の描画を探す (動きベクトル用)
/// @details 即時描画の API には物の ID が無いので、(鍵, その鍵のフレーム内での出現順) で前フレームと
///          突き合わせる。鍵はメッシュのアドレスなど、同じ物なら毎フレーム同じになる値を使う。
///          同じメッシュを何個も描くゲームで途中の 1 個が消えると、後ろの物が 1 フレームだけ隣の物の
///          姿勢と対になる。動きベクトルが 1 フレーム外れるだけで、描画の結果は崩れない。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace mitiru::render
{

template <typename Payload>
class FrameMotionHistory
{
public:
	struct Entry
	{
		const void* key = nullptr;    ///< 比べるだけで参照はしない (前フレームの鍵は解放済みのこともある)
		std::uint32_t sequence = 0;   ///< フレーム内で積んだ順
		std::uint32_t ordinal = 0;    ///< 同じ鍵の中での順。seal で決まる
		Payload payload{};
	};

	/// @brief 今フレームの記録を前フレームへ回し、今フレームを空にする。容量は使い回す
	void beginFrame()
	{
		seal();
		std::swap(m_current, m_previous);
		m_current.clear();
		m_sealed = false;
	}

	void add(const void* key, const Payload& payload)
	{
		m_current.push_back(Entry{key, static_cast<std::uint32_t>(m_current.size()), 0, payload});
		m_sealed = false;
	}

	/// @brief 鍵ごとに積んだ順へ並べ、出現順を振る。match の前に 1 回呼ぶ
	void seal()
	{
		if (m_sealed) { return; }
		std::sort(m_current.begin(), m_current.end(), [](const Entry& a, const Entry& b) {
			if (a.key != b.key) { return std::less<const void*>{}(a.key, b.key); }
			return a.sequence < b.sequence;
		});
		for (std::size_t i = 0; i < m_current.size(); ++i)
		{
			const bool sameKey = i > 0 && m_current[i - 1].key == m_current[i].key;
			m_current[i].ordinal = sameKey ? m_current[i - 1].ordinal + 1 : 0;
		}
		m_sealed = true;
	}

	/// @brief 前フレームに対がある今フレームの記録ごとに fn(今, 前) を呼ぶ
	template <typename Fn>
	void match(Fn&& fn) const
	{
		std::size_t p = 0;
		for (const Entry& cur : m_current)
		{
			while (p < m_previous.size() && before(m_previous[p], cur)) { ++p; }
			if (p < m_previous.size() && m_previous[p].key == cur.key && m_previous[p].ordinal == cur.ordinal)
			{
				fn(cur.payload, m_previous[p].payload);
			}
		}
	}

	[[nodiscard]] std::span<const Entry> current() const noexcept { return m_current; }
	[[nodiscard]] std::span<const Entry> previous() const noexcept { return m_previous; }

	/// @brief 両フレームの記録を捨てる (履歴を作り直すとき)
	void clear() noexcept
	{
		m_current.clear();
		m_previous.clear();
		m_sealed = false;
	}

private:
	[[nodiscard]] static bool before(const Entry& a, const Entry& b) noexcept
	{
		if (a.key != b.key) { return std::less<const void*>{}(a.key, b.key); }
		return a.ordinal < b.ordinal;
	}

	std::vector<Entry> m_current;
	std::vector<Entry> m_previous;
	bool m_sealed = false;
};

} // namespace mitiru::render
