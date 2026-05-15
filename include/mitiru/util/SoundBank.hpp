#pragma once

/// @file SoundBank.hpp
/// @brief 簡易効果音バンク
/// @details 名前付きトーン定義を登録し、play()で実際に音を鳴らす。
///          Windows環境ではPlaySound(SND_MEMORY|SND_ASYNC)で非同期再生する。
///          非Windows/ヘッドレスではイベント記録のみ。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace mitiru::util
{

/// @brief トーン定義
struct ToneDef
{
	float frequency = 440.0f;   ///< 周波数 (Hz)
	float duration = 0.1f;      ///< 持続時間（秒）
	float volume = 0.5f;        ///< ボリューム [0,1]
};

/// @brief 再生イベント（テスト検証用）
struct PlayEvent
{
	std::string name;   ///< トーン名
	ToneDef tone;       ///< トーン定義
};

/// @brief 簡易効果音バンク
/// @details ゲーム用の効果音をトーン定義として登録し、名前で再生する。
///          Windows環境ではメモリ上にWAVを生成しPlaySoundで非同期再生する。
///
/// @code
/// mitiru::util::SoundBank sounds;
/// sounds.define("hit", {880.0f, 0.05f, 0.6f});
/// sounds.define("score", {1200.0f, 0.1f, 0.4f});
/// sounds.setEnabled(true);
/// sounds.play("hit");  // 実際に音が鳴る
/// @endcode
class SoundBank
{
public:
	/// @brief トーンを定義する
	/// @param name トーン名
	/// @param tone トーン定義
	void define(std::string_view name, const ToneDef& tone)
	{
		m_tones[std::string(name)] = tone;
	}

	/// @brief 音声出力を有効/無効にする
	/// @param enabled trueで実際に音を出す
	void setEnabled(bool enabled) noexcept { m_enabled = enabled; }

	/// @brief 音声出力が有効か
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief 定義済みトーンを再生する
	/// @param name トーン名
	void play(std::string_view name)
	{
		const auto it = m_tones.find(std::string(name));
		if (it != m_tones.end())
		{
			m_events.push_back({std::string(name), it->second});
			++m_totalPlayCount;
			if (m_enabled)
			{
				outputTone(it->second);
			}
		}
	}

	/// @brief 未定義でもトーンを即座に再生する
	/// @param name トーン名
	/// @param freq 周波数
	/// @param duration 持続時間
	/// @param volume ボリューム
	void playTone(std::string_view name, float freq, float duration = 0.1f, float volume = 0.5f)
	{
		ToneDef tone{freq, duration, volume};
		m_events.push_back({std::string(name), tone});
		++m_totalPlayCount;
		if (m_enabled)
		{
			outputTone(tone);
		}
	}

	/// @brief フレーム開始時にイベントをクリアする
	void beginFrame() noexcept
	{
		m_events.clear();
	}

	/// @brief 今フレームの再生イベント一覧
	[[nodiscard]] const std::vector<PlayEvent>& events() const noexcept { return m_events; }

	/// @brief 今フレームの再生イベント数
	[[nodiscard]] int frameEventCount() const noexcept { return static_cast<int>(m_events.size()); }

	/// @brief 累計再生回数
	[[nodiscard]] int playCount() const noexcept { return m_totalPlayCount; }

	/// @brief 最後の再生イベント
	[[nodiscard]] const PlayEvent& lastEvent() const { return m_events.back(); }

	/// @brief 定義済みトーン数
	[[nodiscard]] int definedCount() const noexcept { return static_cast<int>(m_tones.size()); }

	/// @brief 累計カウントをリセットする
	void resetCounts() noexcept { m_totalPlayCount = 0; }

private:
	/// @brief トーンを実際に音声出力する
	/// @param tone トーン定義
	void outputTone(const ToneDef& tone)
	{
#ifdef _WIN32
		/// メモリ上にWAVデータを生成してPlaySoundで非同期再生する
		constexpr std::uint32_t SAMPLE_RATE = 22050;
		constexpr std::uint16_t BITS = 16;
		constexpr std::uint16_t CHANNELS = 1;

		const auto numSamples = static_cast<std::uint32_t>(
			tone.duration * static_cast<float>(SAMPLE_RATE));
		if (numSamples == 0) return;

		const std::uint32_t dataSize = numSamples * (BITS / 8) * CHANNELS;
		const std::uint32_t fileSize = 44 + dataSize;

		/// WAVバッファを生成する（staticで保持してPlaySoundの再生中に解放されないようにする）
		static std::vector<std::uint8_t> wavBuf;
		wavBuf.resize(fileSize);
		auto* p = wavBuf.data();

		/// RIFFヘッダー
		std::memcpy(p, "RIFF", 4); p += 4;
		writeU32(p, fileSize - 8); p += 4;
		std::memcpy(p, "WAVE", 4); p += 4;

		/// fmtチャンク
		std::memcpy(p, "fmt ", 4); p += 4;
		writeU32(p, 16); p += 4;                          // chunk size
		writeU16(p, 1); p += 2;                            // PCM
		writeU16(p, CHANNELS); p += 2;
		writeU32(p, SAMPLE_RATE); p += 4;
		writeU32(p, SAMPLE_RATE * CHANNELS * (BITS / 8)); p += 4;
		writeU16(p, CHANNELS * (BITS / 8)); p += 2;
		writeU16(p, BITS); p += 2;

		/// dataチャンク
		std::memcpy(p, "data", 4); p += 4;
		writeU32(p, dataSize); p += 4;

		/// 正弦波PCMデータを生成する
		constexpr float PI2 = 6.28318530717958647692f;
		const float amplitude = std::clamp(tone.volume, 0.0f, 1.0f) * 20000.0f;

		for (std::uint32_t i = 0; i < numSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(SAMPLE_RATE);
			/// フェードアウト（最後の20%）でプチノイズを防止する
			const float progress = static_cast<float>(i) / static_cast<float>(numSamples);
			const float envelope = (progress > 0.8f)
				? (1.0f - progress) / 0.2f
				: 1.0f;
			const float sample = std::sin(PI2 * tone.frequency * t) * amplitude * envelope;
			const auto s16 = static_cast<std::int16_t>(
				std::clamp(sample, -32767.0f, 32767.0f));
			writeS16(p, s16);
			p += 2;
		}

		/// 非同期再生する（SND_ASYNCで即座に戻る）
		::PlaySoundA(
			reinterpret_cast<LPCSTR>(wavBuf.data()),
			NULL,
			SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
		static_cast<void>(tone);
#endif
	}

	/// @brief リトルエンディアンで32bit値を書き込む
	static void writeU32(std::uint8_t* dst, std::uint32_t val) noexcept
	{
		dst[0] = static_cast<std::uint8_t>(val & 0xFF);
		dst[1] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
		dst[2] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
		dst[3] = static_cast<std::uint8_t>((val >> 24) & 0xFF);
	}

	/// @brief リトルエンディアンで16bit値を書き込む
	static void writeU16(std::uint8_t* dst, std::uint16_t val) noexcept
	{
		dst[0] = static_cast<std::uint8_t>(val & 0xFF);
		dst[1] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
	}

	/// @brief リトルエンディアンで符号付き16bit値を書き込む
	static void writeS16(std::uint8_t* dst, std::int16_t val) noexcept
	{
		writeU16(dst, static_cast<std::uint16_t>(val));
	}

	std::unordered_map<std::string, ToneDef> m_tones;   ///< トーン定義
	std::vector<PlayEvent> m_events;                      ///< 今フレームのイベント
	int m_totalPlayCount = 0;                             ///< 累計再生回数
	bool m_enabled = false;                               ///< 音声出力有効フラグ（デフォルト無効）
};

} // namespace mitiru::util
