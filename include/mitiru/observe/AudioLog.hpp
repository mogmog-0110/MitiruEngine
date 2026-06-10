#pragma once

/// @file AudioLog.hpp
/// @brief AI 観測用の音イベントログ (/api/ai/audio)
/// @details host が SoundIntent を audio engine へ流す瞬間に 1 エントリ記録する固定リング。
///          常時 on でもコストは struct コピー 1 本 (音 intent は毎フレーム高々数件)。

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::observe
{

/// @brief 音イベント 1 件 (SoundIntent が audio engine に適用された記録)
struct AudioLogEntry
{
	std::uint64_t frame = 0;   ///< 発火フレーム
	char id[64] = {};          ///< 論理サウンド id (null 終端)
	std::uint8_t category = 0; ///< 0=SE, 1=BGM, 2=Voice
	std::uint8_t loop = 0;     ///< 1=ループ再生
	std::uint8_t stop = 0;     ///< 1=停止要求
	float volume = 0.0f;       ///< 0.0–1.0
	float pitchScale = 0.0f;   ///< 0=未指定 (=1.0)
};

/// @brief 音イベントの固定リングバッファ
/// @details 容量超過は古いものから上書き。ヒープ確保なし。
class AudioLog
{
public:
	/// @brief 1 イベントを記録する
	void push(std::uint64_t frame, const char* id, std::uint8_t category,
	          std::uint8_t loop, std::uint8_t stop, float volume, float pitchScale) noexcept
	{
		AudioLogEntry& e = m_buf[m_count % kCap];
		e.frame = frame;
		std::strncpy(e.id, id != nullptr ? id : "", sizeof(e.id) - 1);
		e.id[sizeof(e.id) - 1] = '\0';
		e.category = category;
		e.loop = loop;
		e.stop = stop;
		e.volume = volume;
		e.pitchScale = pitchScale;
		++m_count;
	}

	/// @brief 記録済みイベント総数 (上書き分も含む累積)
	[[nodiscard]] std::size_t totalCount() const noexcept { return m_count; }

	/// @brief 最新 max 件を古い順の JSON 配列にする
	[[nodiscard]] std::string toJson(std::size_t max = 64) const
	{
		const std::size_t held = m_count < kCap ? m_count : kCap;
		const std::size_t n = max < held ? max : held;
		std::string out = "[";
		for (std::size_t i = 0; i < n; ++i)
		{
			// 末尾 n 件を古い順に走査する
			const std::size_t idx = (m_count - n + i) % kCap;
			const AudioLogEntry& e = m_buf[idx];
			if (i > 0) { out += ","; }
			out += "{\"frame\":" + std::to_string(e.frame) +
			       ",\"id\":\"" + jsonEscape(e.id) + "\"" +
			       ",\"category\":" + std::to_string(e.category) +
			       ",\"loop\":" + std::to_string(e.loop) +
			       ",\"stop\":" + std::to_string(e.stop) +
			       ",\"volume\":" + std::to_string(e.volume) +
			       ",\"pitch\":" + std::to_string(e.pitchScale) + "}";
		}
		out += "]";
		return out;
	}

private:
	static constexpr std::size_t kCap = 256; ///< リング容量
	std::array<AudioLogEntry, kCap> m_buf{};
	std::size_t m_count = 0; ///< 累積 push 数 (次の書き込み位置 = m_count % kCap)
};

} // namespace mitiru::observe
