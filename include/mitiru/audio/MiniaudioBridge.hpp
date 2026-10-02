#pragma once

/// @file MiniaudioBridge.hpp
/// @brief MiniaudioEngine とゲームオーディオシステムのブリッジ
/// @details AudioMixer のコールバックを MiniaudioEngine の実 API に接続するアダプタ。
///
/// @code
/// mitiru::audio::MiniaudioEngine engine;
/// mitiru::audio::AudioMixer mixer;
///
/// mitiru::audio::MiniaudioBridge bridge(engine);
/// bridge.setBasePath("assets/audio/");
/// bridge.connectToAudioMixer(mixer);
///
/// bridge.playVoice("voice/ch01_001.ogg");
/// bridge.playSE("se/click.wav");
/// bridge.playBGM("bgm/main_theme.mp3", true);
/// @endcode

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "MiniaudioEngine.hpp"
#include "AudioMixer.hpp"

namespace mitiru::audio
{

/// @brief 拡張子から見た音声ファイルの形式 (PCM の並びを表す AudioStream.hpp の AudioFormat とは別物)
enum class AudioFileFormat : std::uint8_t
{
	Wav,
	Mp3,
	Flac,
	Ogg,
	Unknown,
};

/// @brief MiniaudioEngine とゲームオーディオシステムのブリッジ
/// @details MiniaudioEngine への参照を保持し、AudioMixer が要求する
///          コールバックを実装して接続する。ボイス・SE・BGM の再生ヘルパーも提供する。
class MiniaudioBridge
{
public:
	/// @brief コンストラクタ
	/// @param engine MiniaudioEngine への参照（ライフタイムはブリッジより長いこと）
	explicit MiniaudioBridge(MiniaudioEngine& engine) noexcept
		: m_engine(engine)
	{
	}

	// コピー禁止・ムーブ禁止（参照を保持するため）
	MiniaudioBridge(const MiniaudioBridge&) = delete;
	MiniaudioBridge& operator=(const MiniaudioBridge&) = delete;
	MiniaudioBridge(MiniaudioBridge&&) = delete;
	MiniaudioBridge& operator=(MiniaudioBridge&&) = delete;

	// ── ベースパス設定 ────────────────────────────────────────

	/// @brief オーディオアセットのベースディレクトリを設定する
	/// @param path ディレクトリパス（末尾のスラッシュは自動で補う）
	void setBasePath(std::string_view path)
	{
		m_basePath = std::string(path);
		if (!m_basePath.empty() && m_basePath.back() != '/' && m_basePath.back() != '\\')
		{
			m_basePath += '/';
		}
	}

	/// @brief ベースパスを取得する
	[[nodiscard]] const std::string& basePath() const noexcept { return m_basePath; }

	// ── AudioMixer 接続 ───────────────────────────────────────

	/// @brief AudioMixer にコールバックを接続する
	/// @details ミキサーのチャンネル停止イベントを受け取り、
	///          ミキサーの play 呼び出し時に実際のファイル再生を行う。
	/// @param mixer 接続先の AudioMixer
	void connectToAudioMixer(AudioMixer& mixer)
	{
		mixer.setOnChannelStopped(
			[this](int /*handle*/)
			{
				// チャンネル停止時の処理（必要に応じて拡張可能）
			});

		m_mixer = &mixer;
	}

	// ── 直接再生ヘルパー ─────────────────────────────────────

	/// @brief ボイスを再生する（前のボイスは停止）
	/// @param path ボイスファイルのパス（ベースパスからの相対パス）
	void playVoice(std::string_view path)
	{
		// playVoiceEx (#F6) は BGM/SE と独立したスロットなので、BGM を止めずにボイスだけ差し替わる。
		std::string fullPath = resolveAudioPath(path);
		m_engine.playVoiceEx(fullPath, 1.0f, 1.0f, 0.0f);

		if (m_mixer)
		{
			m_mixer->stopByCategory(SoundCategory::Voice);
			m_mixer->play(path, SoundCategory::Voice, false, 1.0f);
		}
	}

	/// @brief 効果音を再生する（他の再生に影響しない）
	/// @param path SE ファイルのパス（ベースパスからの相対パス）
	void playSE(std::string_view path)
	{
		std::string fullPath = resolveAudioPath(path);
		m_engine.playFile(fullPath);

		if (m_mixer)
		{
			m_mixer->play(path, SoundCategory::Se, false, 1.0f);
		}
	}

	/// @brief BGM を再生する
	/// @param path BGM ファイルのパス（ベースパスからの相対パス）
	/// @param loop ループ再生するか
	void playBGM(std::string_view path, bool loop = true)
	{
		std::string fullPath = resolveAudioPath(path);
		m_engine.playFile(fullPath);

		if (m_mixer)
		{
			m_mixer->play(path, SoundCategory::Bgm, loop, 1.0f);
		}
	}

	/// @brief 全サウンドを停止する
	void stopAll()
	{
		m_engine.stopAll();
		if (m_mixer)
		{
			m_mixer->stopAll();
		}
	}

	// ── フォーマットサポート ─────────────────────────────────

	/// @brief ファイル拡張子からオーディオフォーマットを判定する
	/// @param path ファイルパス
	/// @return 判定されたフォーマット
	[[nodiscard]] static AudioFileFormat detectFormat(std::string_view path) noexcept
	{
		auto dotPos = path.rfind('.');
		if (dotPos == std::string_view::npos)
		{
			return AudioFileFormat::Unknown;
		}
		auto ext = path.substr(dotPos + 1);

		if (ext == "wav" || ext == "WAV") return AudioFileFormat::Wav;
		if (ext == "mp3" || ext == "MP3") return AudioFileFormat::Mp3;
		if (ext == "flac" || ext == "FLAC") return AudioFileFormat::Flac;
		if (ext == "ogg" || ext == "OGG") return AudioFileFormat::Ogg;
		return AudioFileFormat::Unknown;
	}

	/// @brief 指定したフォーマットが miniaudio でサポートされているか
	/// @param format オーディオフォーマット
	/// @return サポートされていれば true
	[[nodiscard]] static bool isFormatSupported(AudioFileFormat format) noexcept
	{
		return format != AudioFileFormat::Unknown;
	}

	/// @brief ファイルパスがサポートされているフォーマットか判定する
	/// @param path ファイルパス
	/// @return サポートされていれば true
	[[nodiscard]] static bool isFileSupported(std::string_view path) noexcept
	{
		return isFormatSupported(detectFormat(path));
	}

	/// @brief サポートされる拡張子の一覧を取得する
	[[nodiscard]] static constexpr std::array<std::string_view, 4> supportedExtensions() noexcept
	{
		return {"wav", "mp3", "flac", "ogg"};
	}

	// ── エンジンアクセス ─────────────────────────────────────

	/// @brief 内部の MiniaudioEngine への参照を取得する
	[[nodiscard]] MiniaudioEngine& engine() noexcept { return m_engine; }
	[[nodiscard]] const MiniaudioEngine& engine() const noexcept { return m_engine; }

	/// @brief エンジンが初期化されているか
	[[nodiscard]] bool isReady() const noexcept { return m_engine.isInitialized(); }

private:
	/// @brief audioId をフルパスに解決する
	/// @param audioId オーディオリソース ID または相対パス
	/// @return ベースパス付きのフルパス
	[[nodiscard]] std::string resolveAudioPath(std::string_view audioId) const
	{
		if (m_basePath.empty())
		{
			return std::string(audioId);
		}

		// 絶対パスの場合はそのまま返す
		if (audioId.size() >= 2 && audioId[1] == ':')
		{
			return std::string(audioId);
		}
		if (!audioId.empty() && (audioId.front() == '/' || audioId.front() == '\\'))
		{
			return std::string(audioId);
		}

		return m_basePath + std::string(audioId);
	}

	MiniaudioEngine& m_engine;                   ///< オーディオバックエンド
	AudioMixer* m_mixer = nullptr;               ///< ミキサー（接続時に設定）
	std::string m_basePath;                      ///< オーディオアセットのベースパス
};

} // namespace mitiru::audio
