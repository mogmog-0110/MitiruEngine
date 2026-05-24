#pragma once
/// @file Sequencer.hpp
/// @brief 複数トラック同時再生シーケンサー
/// @details 複数のMML文字列を各トラックに割り当て、
///          ミキシングして1つのPCMバッファにする。
///
/// @code
/// mitiru_mml::Sequencer seq;
/// seq.addTrack("T120 O4 L8 @2 CDEFGAB>C");   // メロディ（正弦波）
/// seq.addTrack("T120 O3 L4 @0 C2E2G2>C2");   // ベース（矩形波）
/// seq.addTrack("T120 L8 @4 V8 CCCCCCCC");     // リズム（ノイズ）
/// auto pcm = seq.render();
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/Synthesizer.hpp>
#include <mitiru_mml/Track.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief 複数トラック同時再生シーケンサー
class Sequencer
{
public:
	/// @brief コンストラクタ
	/// @param config オーディオ設定
	explicit Sequencer(const AudioConfig& config = {})
		: m_config(config)
		, m_synth(config)
	{
	}

	/// @brief MML文字列からトラックを追加する
	/// @param mml MML文字列
	void addTrack(std::string_view mml)
	{
		m_trackMml.emplace_back(mml);
	}

	/// @brief 全トラックをクリアする
	void clear()
	{
		m_trackMml.clear();
	}

	/// @brief トラック数を返す
	[[nodiscard]] int trackCount() const noexcept
	{
		return static_cast<int>(m_trackMml.size());
	}

	/// @brief 全トラックをレンダリングしてミキシングする
	/// @return ミキシング済みPCMバッファ（16bit モノラル）
	[[nodiscard]] PcmBuffer render() const
	{
		if (m_trackMml.empty()) return {};

		// 各トラックをレンダリングする
		std::vector<PcmBuffer> trackBuffers;
		std::size_t maxLen = 0;

		Track track(m_synth);
		for (const auto& mml : m_trackMml)
		{
			auto cmds = MmlParser::parse(mml);
			auto buf = track.render(cmds);
			maxLen = std::max(maxLen, buf.size());
			trackBuffers.push_back(std::move(buf));
		}

		// ミキシング（加算合成 + クリッピング）
		PcmBuffer mixed(maxLen, 0);
		for (const auto& buf : trackBuffers)
		{
			for (std::size_t i = 0; i < buf.size(); ++i)
			{
				int val = static_cast<int>(mixed[i]) + static_cast<int>(buf[i]);
				mixed[i] = static_cast<std::int16_t>(
					std::clamp(val, -32767, 32767));
			}
		}

		return mixed;
	}

	/// @brief 推定再生時間を返す（秒）
	[[nodiscard]] float estimateDuration() const
	{
		const auto pcm = render();
		return static_cast<float>(pcm.size()) / static_cast<float>(m_config.sampleRate);
	}

	/// @brief サンプルレートを取得する
	[[nodiscard]] std::uint32_t sampleRate() const noexcept
	{
		return m_config.sampleRate;
	}

private:
	AudioConfig m_config;
	Synthesizer m_synth;
	std::vector<std::string> m_trackMml;
};

} // namespace mitiru_mml
