#pragma once

/// @file MiniaudioBridge.hpp
/// @brief MiniaudioEngineとVN/ゲームオーディオシステムのブリッジ
/// @details DynamicBGMControllerやAudioMixerのコールバックを
///          MiniaudioEngineの実APIに接続するアダプタ。
///          1行のセットアップでVNオーディオを動作させる。
///
/// @code
/// mitiru::audio::MiniaudioEngine engine;
/// mitiru::vn::DynamicBGMController bgm;
/// mitiru::audio::AudioMixer mixer;
///
/// mitiru::audio::MiniaudioBridge bridge(engine);
/// bridge.setBasePath("assets/audio/");
/// bridge.connectToDynamicBGM(bgm);
/// bridge.connectToAudioMixer(mixer);
///
/// // VN voice/SE playback
/// bridge.playVoice("voice/ch01_001.ogg");
/// bridge.playSE("se/click.wav");
/// bridge.playBGM("bgm/main_theme.mp3", true);
/// @endcode

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>

#include "MiniaudioEngine.hpp"
#include "AudioMixer.hpp"
#include "../vn/DynamicBGM.hpp"

namespace mitiru::audio
{

/// @brief サポートするオーディオフォーマット
enum class AudioFormat : std::uint8_t
{
	Wav,
	Mp3,
	Flac,
	Ogg,
	Unknown,
};

/// @brief MiniaudioEngineとVN/ゲームオーディオシステムのブリッジ
/// @details MiniaudioEngineへの参照を保持し、DynamicBGMControllerや
///          AudioMixerが要求するコールバックを実装して接続する。
///          ボイス・SE・BGMの再生ヘルパーも提供する。
class MiniaudioBridge
{
public:
	/// @brief コンストラクタ
	/// @param engine MiniaudioEngineへの参照（ライフタイムはブリッジより長いこと）
	explicit MiniaudioBridge(MiniaudioEngine& engine) noexcept
		: m_engine(engine)
	{
	}

	// Non-copyable, non-movable（参照を保持するため）
	MiniaudioBridge(const MiniaudioBridge&) = delete;
	MiniaudioBridge& operator=(const MiniaudioBridge&) = delete;
	MiniaudioBridge(MiniaudioBridge&&) = delete;
	MiniaudioBridge& operator=(MiniaudioBridge&&) = delete;

	// ── ベースパス設定 ────────────────────────────────────────

	/// @brief オーディオアセットのベースディレクトリを設定する
	/// @param path ディレクトリパス（末尾スラッシュは自動補完）
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

	// ── DynamicBGMController接続 ─────────────────────────────

	/// @brief DynamicBGMControllerにコールバックを接続する
	/// @param bgm 接続先のDynamicBGMController
	void connectToDynamicBGM(vn::DynamicBGMController& bgm)
	{
		bgm.setPlayCallback(
			[this](const std::string& audioId, float volume, float /*fadeDuration*/)
			{
				std::string fullPath = resolveAudioPath(audioId);
				m_engine.setMasterVolume(volume);
				m_engine.playFile(fullPath);
			});

		bgm.setStopCallback(
			[this](const std::string& /*audioId*/, float /*fadeDuration*/)
			{
				m_engine.stopAll();
			});

		bgm.setVolumeCallback(
			[this](const std::string& /*audioId*/, float volume)
			{
				m_engine.setMasterVolume(std::clamp(volume, 0.0f, 1.0f));
			});
	}

	// ── AudioMixer接続 ───────────────────────────────────────

	/// @brief AudioMixerにコールバックを接続する
	/// @details ミキサーのチャンネル停止イベントを受け取り、
	///          ミキサーのplay呼び出し時に実際のファイル再生を行う。
	/// @param mixer 接続先のAudioMixer
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
	/// @param path ボイスファイルパス（ベースパスからの相対パス）
	void playVoice(std::string_view path)
	{
		// 前のボイスが再生中なら停止してから再開
		m_engine.stopAll();
		m_engine.resume();

		std::string fullPath = resolveAudioPath(path);
		m_engine.playFile(fullPath);

		if (m_mixer)
		{
			m_mixer->stopByCategory(SoundCategory::Voice);
			m_mixer->play(path, SoundCategory::Voice, false, 1.0f);
		}
	}

	/// @brief 効果音を再生する（他の再生に影響しない）
	/// @param path SEファイルパス（ベースパスからの相対パス）
	void playSE(std::string_view path)
	{
		std::string fullPath = resolveAudioPath(path);
		m_engine.playFile(fullPath);

		if (m_mixer)
		{
			m_mixer->play(path, SoundCategory::Se, false, 1.0f);
		}
	}

	/// @brief BGMを再生する
	/// @param path BGMファイルパス（ベースパスからの相対パス）
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
	[[nodiscard]] static AudioFormat detectFormat(std::string_view path) noexcept
	{
		auto dotPos = path.rfind('.');
		if (dotPos == std::string_view::npos)
		{
			return AudioFormat::Unknown;
		}
		auto ext = path.substr(dotPos + 1);

		if (ext == "wav" || ext == "WAV") return AudioFormat::Wav;
		if (ext == "mp3" || ext == "MP3") return AudioFormat::Mp3;
		if (ext == "flac" || ext == "FLAC") return AudioFormat::Flac;
		if (ext == "ogg" || ext == "OGG") return AudioFormat::Ogg;
		return AudioFormat::Unknown;
	}

	/// @brief 指定フォーマットがminiaudioでサポートされているか
	/// @param format オーディオフォーマット
	/// @return サポートされていればtrue
	[[nodiscard]] static bool isFormatSupported(AudioFormat format) noexcept
	{
		return format != AudioFormat::Unknown;
	}

	/// @brief ファイルパスがサポートされたフォーマットか判定する
	/// @param path ファイルパス
	/// @return サポートされていればtrue
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

	/// @brief 内部のMiniaudioEngineへの参照を取得する
	[[nodiscard]] MiniaudioEngine& engine() noexcept { return m_engine; }
	[[nodiscard]] const MiniaudioEngine& engine() const noexcept { return m_engine; }

	/// @brief エンジンが初期化されているか
	[[nodiscard]] bool isReady() const noexcept { return m_engine.isInitialized(); }

private:
	/// @brief audioIdをフルパスに解決する
	/// @param audioId オーディオリソースIDまたは相対パス
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
