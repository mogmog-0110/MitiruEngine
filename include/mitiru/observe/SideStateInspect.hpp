#pragma once

/// @file SideStateInspect.hpp
/// @brief GameMemory の外に持つ状態の窓口 (ADR 0054) を、ツール窓 side_state が読む snapshot の節にする。
/// @details 窓口ごとの名前・形の番号・1 フレームの bytes・hash と、リングの埋まり具合を出す。replay の照合中は、
///          窓口の hash が記録と食い違ったフレームを窓口ごとに覚えて添える。照合は host が最初の食い違いで
///          失敗を決めた後も続けるので、どの窓口がどのフレームからずれ続けたかが見える。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/SideStateHost.hpp>
#include <mitiru/observe/SideStateImage.hpp>
#include <mitiru/observe/SideStateRing.hpp>

namespace mitiru::observe
{

/// @brief replay の照合で窓口の hash が食い違ったフレームの記録。窓口の並びは記録側の image の順。
class SideStateReplayMarks
{
public:
	static constexpr std::size_t kMaxMarks = 256;

	void reset()
	{
		m_marks.clear();
		m_channels.clear();
		m_compared = 0;
		m_first = m_last = 0;
		m_dropped = 0;
	}

	/// @brief 1 フレーム分の照合。live に無い窓口や形の番号の違いも食い違いとして数える。
	void note(std::uint32_t frame, const SideImageView& recorded, const SideImageView& live)
	{
		if (m_compared == 0) { m_first = frame; }
		m_last = frame;
		++m_compared;
		std::uint32_t mask = 0;
		for (std::uint32_t i = 0; i < recorded.count; ++i)
		{
			const SideImageEntry& r = recorded.items[i].entry;
			const std::string_view name(r.name, ::strnlen(r.name, sizeof(r.name)));
			const std::uint32_t bit = channelBit(name);
			const SideImageItem* l = live.find(name);
			if (l == nullptr || l->entry.version != r.version || l->entry.hash != r.hash) { mask |= bit; }
		}
		if (live.count != recorded.count)
		{
			// 窓口の数が違えば全部の窓口が食い違い。live にだけある窓口も名前を覚えて印に入れる。
			for (std::uint32_t i = 0; i < live.count; ++i)
			{
				const SideImageEntry& l = live.items[i].entry;
				(void)channelBit(std::string_view(l.name, ::strnlen(l.name, sizeof(l.name))));
			}
			mask = static_cast<std::uint32_t>((1ull << m_channels.size()) - 1u);
		}
		if (mask == 0) { return; }
		if (m_marks.size() >= kMaxMarks) { ++m_dropped; return; }
		m_marks.push_back(Mark{ frame, mask });
	}

	[[nodiscard]] bool active() const noexcept { return m_compared > 0; }

	[[nodiscard]] nlohmann::json toJson() const
	{
		nlohmann::json marks = nlohmann::json::array();
		for (const Mark& m : m_marks)
		{
			nlohmann::json names = nlohmann::json::array();
			for (std::size_t c = 0; c < m_channels.size(); ++c)
			{
				if ((m.mask >> c) & 1u) { names.push_back(m_channels[c]); }
			}
			marks.push_back({ { "frame", m.frame }, { "channels", std::move(names) } });
		}
		return { { "compared", m_compared }, { "firstFrame", m_first }, { "lastFrame", m_last },
		         { "marks", std::move(marks) }, { "dropped", m_dropped } };
	}

private:
	struct Mark
	{
		std::uint32_t frame = 0;
		std::uint32_t mask = 0;
	};

	std::uint32_t channelBit(std::string_view name)
	{
		for (std::size_t i = 0; i < m_channels.size(); ++i) { if (m_channels[i] == name) { return 1u << i; } }
		if (m_channels.size() >= static_cast<std::size_t>(module::kMaxSideStateChannels)) { return 0; }
		m_channels.emplace_back(name);
		return 1u << (m_channels.size() - 1);
	}

	std::vector<Mark> m_marks;
	std::vector<std::string> m_channels;
	std::uint32_t m_compared = 0;
	std::uint32_t m_first = 0;
	std::uint32_t m_last = 0;
	std::uint32_t m_dropped = 0;
};

/// @brief host の replay 照合 (mitiru_host の --replay) が書き、snapshot の節が読む 1 つだけの記録。
inline SideStateReplayMarks& sideStateReplayMarks() noexcept
{
	static SideStateReplayMarks marks;
	return marks;
}

/// @brief hash は 64 bit を落とさずに見せるため 16 桁の 16 進の文字にする。
[[nodiscard]] inline std::string sideStateHashText(std::uint64_t h)
{
	char buf[24];
	std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
	return buf;
}

/// @brief snapshot の "sideState" 節。窓口が無い game でも節を出し、ページが「窓口なし」と言えるようにする。
[[nodiscard]] inline nlohmann::json sideStateSection(const module::SideStateHost& host, const SideStateRing& ring,
                                                     const SideStateReplayMarks& marks)
{
	SideImageView latest;
	std::size_t len = 0;
	const std::uint8_t* image = ring.size() > 0 ? ring.at(0, len) : nullptr;
	const bool haveImage = image != nullptr && parseSideImage(image, len, latest);

	nlohmann::json channels = nlohmann::json::array();
	for (std::int32_t i = 0; i < host.count(); ++i)
	{
		const module::SideStateChannel& c = host.channel(i);
		const std::string name(c.name, ::strnlen(c.name, sizeof(c.name)));
		nlohmann::json row{ { "name", name }, { "version", c.version },
		                    { "coversScene", (c.flags & module::kSideStateCoversScene) != 0 } };
		if (const SideImageItem* item = haveImage ? latest.find(name) : nullptr)
		{
			row["bytes"] = item->entry.size;
			row["hash"] = sideStateHashText(item->entry.hash);
		}
		channels.push_back(std::move(row));
	}
	const std::size_t frames = ring.size();
	nlohmann::json state{
		{ "channels", std::move(channels) },
		{ "ring", { { "frames", frames }, { "capacity", ring.capacity() }, { "usedBytes", ring.usedBytes() },
		            { "allocatedBytes", ring.allocatedBytes() },
		            { "bytesPerFrame", frames > 0 ? ring.usedBytes() / frames : 0 } } },
	};
	if (marks.active()) { state["replay"] = marks.toJson(); }
	return { { "title", "GameMemory の外の状態" }, { "state", std::move(state) } };
}

}  // namespace mitiru::observe
