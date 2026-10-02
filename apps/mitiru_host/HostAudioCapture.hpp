#pragma once

/// @file HostAudioCapture.hpp
/// @brief --audio-capture: headless 実行の音を固定ステップごとに WAV へ書き、鳴らした音を隣の JSONL に残す
/// @details WAV の n ステップ目の区間 [n/60, (n+1)/60) 秒には、n ステップ目の update で積んだ音が頭から入る。
///          JSONL の frame も同じステップ番号なので、「鳴らしたフレーム」と「WAV で鳴り始めた時刻」を
///          そのまま突き合わせられる (tools/audio_report.py --events)。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/audio/OfflineMixCapture.hpp>
#include <mitiru/core/Engine.hpp>

namespace mitiru::host
{

class HostAudioCapture final : public mitiru::IFrameListener
{
public:
	static constexpr std::uint32_t kSampleRate = 48000;
	static constexpr std::uint32_t kChannels   = 2;

	explicit HostAudioCapture(mitiru::audio::OfflineMixCapture::Source source)
		: m_capture(std::move(source), kSampleRate, kChannels, 60) {}

	/// @brief out.wav と out.events.jsonl を開く。どちらかが開けなければ false
	[[nodiscard]] bool open(const std::string& wavPath)
	{
		if (!m_capture.open(wavPath)) { return false; }
		m_eventsPath = eventsPathFor(wavPath);
		m_events.open(m_eventsPath, std::ios::trunc);
		return m_events.is_open();
	}

	void onAfterUpdate(mitiru::Engine& engine) override
	{
		const std::uint64_t step = m_capture.steps();
		m_capture.step();
		writeNewEvents(engine.audioLog(), step);
		if (const auto* intents = engine.lastModuleIntents()) { writeMarks(*intents, step); }
	}

	void close()
	{
		m_capture.close();
		m_events.close();
	}

	[[nodiscard]] std::uint64_t steps() const noexcept { return m_capture.steps(); }
	[[nodiscard]] const std::string& eventsPath() const noexcept { return m_eventsPath; }

	[[nodiscard]] static std::string eventsPathFor(const std::string& wavPath)
	{
		return std::filesystem::path(wavPath).replace_extension(".events.jsonl").string();
	}

private:
	/// AudioLog は 256 件のリングなので、1 ステップで増えた分だけを古い順に取り出せば取りこぼさない。
	void writeNewEvents(const mitiru::observe::AudioLog& log, std::uint64_t step)
	{
		const std::size_t total = log.totalCount();
		if (total == m_seen || !m_events.is_open()) { return; }
		const std::size_t fresh = std::min<std::size_t>(total - m_seen, 256);
		m_seen = total;
		const nlohmann::json entries = nlohmann::json::parse(log.toJson(fresh), nullptr, false);
		if (!entries.is_array()) { return; }
		for (nlohmann::json e : entries)
		{
			e["frame"]   = step;
			e["timeSec"] = static_cast<double>(step) / 60.0;
			m_events << e.dump() << '\n';
		}
	}

	/// hud.mark (ABI v48) の印を音と同じ行の形で残す。音の行と分けられるよう "mark" を持ち "id" を持たない
	void writeMarks(const mitiru::module::FrameIntents& intents, std::uint64_t step)
	{
		const int n = std::clamp(intents.markCount, 0, mitiru::module::kMaxFrameMarks);
		for (int i = 0; i < n && m_events.is_open(); ++i)
		{
			const nlohmann::json e{{"frame", step}, {"timeSec", static_cast<double>(step) / 60.0},
			                       {"mark", std::string(intents.marks[i], mitiru::module::detail::boundedLen(intents.marks[i]))}};
			m_events << e.dump() << '\n';
		}
	}

	mitiru::audio::OfflineMixCapture m_capture;
	std::ofstream                    m_events;
	std::string                      m_eventsPath;
	std::size_t                      m_seen = 0;
};

}  // namespace mitiru::host
