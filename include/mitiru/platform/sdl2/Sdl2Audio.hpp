#pragma once

/// @file Sdl2Audio.hpp
/// @brief SDL2オーディオ出力
/// @details SDL2のオーディオサブシステムを使用した音声出力管理。
///          MITIRU_HAS_SDL2が定義されている場合のみコンパイルされる。

#include <cstdint>
#include <string>

namespace mitiru
{

/// @brief オーディオサンプルフォーマット
enum class AudioSampleFormat : std::uint8_t
{
	Int16 = 0,   ///< 符号付き16bit整数
	Float32      ///< 32bit浮動小数点
};

/// @brief オーディオデバイス設定
/// @details SDL2オーディオデバイスの初期化パラメータを保持する。
///          実際のSDL2ライブラリに依存せず、テストで使用可能。
struct AudioDeviceConfig
{
	std::uint32_t sampleRate = 44100;                    ///< サンプリングレート（Hz）
	std::uint8_t channels = 2;                           ///< チャンネル数（1=モノ, 2=ステレオ）
	AudioSampleFormat format = AudioSampleFormat::Float32; ///< サンプルフォーマット
	std::uint16_t bufferSize = 4096;                     ///< バッファサイズ（サンプル数）
	std::string deviceName;                              ///< デバイス名（空=デフォルト）

	/// @brief デフォルト設定を取得する（44100Hz, ステレオ, Float32）
	/// @return 標準的なオーディオ設定
	[[nodiscard]] static AudioDeviceConfig defaults() noexcept
	{
		return AudioDeviceConfig{};
	}

	/// @brief 低レイテンシ設定を取得する（48000Hz, ステレオ, Float32, 小バッファ）
	/// @return 低レイテンシ用設定
	[[nodiscard]] static AudioDeviceConfig lowLatency() noexcept
	{
		AudioDeviceConfig config;
		config.sampleRate = 48000;
		config.bufferSize = 512;
		return config;
	}

	/// @brief 1サンプルあたりのバイト数を取得する
	/// @return バイト数
	[[nodiscard]] std::uint32_t bytesPerSample() const noexcept
	{
		switch (format)
		{
		case AudioSampleFormat::Int16:
			return 2;
		case AudioSampleFormat::Float32:
			return 4;
		}
		return 4;
	}

	/// @brief バッファの合計バイト数を取得する
	/// @return バッファサイズ（バイト）
	[[nodiscard]] std::uint32_t bufferSizeBytes() const noexcept
	{
		return static_cast<std::uint32_t>(bufferSize)
			* static_cast<std::uint32_t>(channels)
			* bytesPerSample();
	}
};

/// @brief 実際に取得されたオーディオフォーマット情報
/// @details SDL_AudioSpecのobtained値に対応するフォーマット情報。
struct ObtainedAudioFormat
{
	std::uint32_t sampleRate = 0;     ///< 実際のサンプリングレート
	std::uint8_t channels = 0;        ///< 実際のチャンネル数
	AudioSampleFormat format = AudioSampleFormat::Float32; ///< 実際のフォーマット
	std::uint16_t bufferSize = 0;     ///< 実際のバッファサイズ

	/// @brief 有効かどうかを判定する
	/// @return サンプリングレートが0より大きければtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return sampleRate > 0 && channels > 0;
	}
};

#ifdef MITIRU_HAS_SDL2

#include <SDL2/SDL.h>

/// @brief SDL2オーディオデバイスのRAIIラッパー
/// @details コールバックベースのオーディオミキシングを提供する。
///
/// @code
/// auto config = AudioDeviceConfig::defaults();
/// Sdl2Audio audio(config, [](float* buffer, int frames) {
///     // オーディオバッファの生成
/// });
/// audio.start();
/// // ... ゲームループ ...
/// audio.stop();
/// @endcode
class Sdl2Audio
{
public:
	/// @brief オーディオコールバック型
	/// @details バッファポインタとフレーム数を受け取る。
	using AudioCallback = void(*)(void* userdata, std::uint8_t* stream, int len);

	/// @brief コンストラクタ
	/// @param config オーディオ設定
	/// @param callback オーディオコールバック関数
	/// @param userdata コールバックに渡すユーザーデータ
	explicit Sdl2Audio(
		const AudioDeviceConfig& config,
		AudioCallback callback = nullptr,
		void* userdata = nullptr)
		: m_config(config)
	{
		if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
		{
			SDL_InitSubSystem(SDL_INIT_AUDIO);
		}

		SDL_AudioSpec desired;
		SDL_zero(desired);
		desired.freq = static_cast<int>(config.sampleRate);
		desired.channels = config.channels;
		desired.samples = config.bufferSize;
		desired.callback = callback;
		desired.userdata = userdata;

		switch (config.format)
		{
		case AudioSampleFormat::Int16:
			desired.format = AUDIO_S16SYS;
			break;
		case AudioSampleFormat::Float32:
			desired.format = AUDIO_F32SYS;
			break;
		}

		SDL_AudioSpec obtained;
		const char* devName = config.deviceName.empty()
			? nullptr
			: config.deviceName.c_str();

		m_deviceId = SDL_OpenAudioDevice(
			devName, 0, &desired, &obtained,
			SDL_AUDIO_ALLOW_FORMAT_CHANGE);

		if (m_deviceId > 0)
		{
			m_obtained.sampleRate = static_cast<std::uint32_t>(obtained.freq);
			m_obtained.channels = obtained.channels;
			m_obtained.bufferSize = obtained.samples;

			if (obtained.format == AUDIO_F32SYS)
			{
				m_obtained.format = AudioSampleFormat::Float32;
			}
			else
			{
				m_obtained.format = AudioSampleFormat::Int16;
			}
		}
	}

	/// @brief デストラクタ
	~Sdl2Audio()
	{
		if (m_deviceId > 0)
		{
			SDL_CloseAudioDevice(m_deviceId);
			m_deviceId = 0;
		}
	}

	/// コピー禁止
	Sdl2Audio(const Sdl2Audio&) = delete;
	Sdl2Audio& operator=(const Sdl2Audio&) = delete;

	/// ムーブ禁止
	Sdl2Audio(Sdl2Audio&&) = delete;
	Sdl2Audio& operator=(Sdl2Audio&&) = delete;

	/// @brief オーディオ再生を開始する
	void start() noexcept
	{
		if (m_deviceId > 0)
		{
			SDL_PauseAudioDevice(m_deviceId, 0);
			m_playing = true;
		}
	}

	/// @brief オーディオ再生を停止する
	void stop() noexcept
	{
		if (m_deviceId > 0)
		{
			SDL_PauseAudioDevice(m_deviceId, 1);
			m_playing = false;
		}
	}

	/// @brief 再生中かどうかを判定する
	/// @return 再生中ならtrue
	[[nodiscard]] bool isPlaying() const noexcept
	{
		return m_playing;
	}

	/// @brief デバイスが正常にオープンされたかを判定する
	/// @return オープン成功ならtrue
	[[nodiscard]] bool isOpen() const noexcept
	{
		return m_deviceId > 0;
	}

	/// @brief 実際に取得されたフォーマットを取得する
	/// @return ObtainedAudioFormatへのconst参照
	[[nodiscard]] const ObtainedAudioFormat& obtainedFormat() const noexcept
	{
		return m_obtained;
	}

	/// @brief 設定を取得する
	/// @return AudioDeviceConfigへのconst参照
	[[nodiscard]] const AudioDeviceConfig& config() const noexcept
	{
		return m_config;
	}

private:
	AudioDeviceConfig m_config;              ///< 要求したオーディオ設定
	ObtainedAudioFormat m_obtained;          ///< 実際のオーディオフォーマット
	SDL_AudioDeviceID m_deviceId = 0;        ///< SDL2オーディオデバイスID
	bool m_playing = false;                  ///< 再生中フラグ
};

#endif // MITIRU_HAS_SDL2

} // namespace mitiru
