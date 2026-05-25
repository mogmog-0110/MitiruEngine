#pragma once
/// @file MiniaudioEngine.hpp
/// @brief miniaudio ベースのオーディオエンジン
/// @details クロスプラットフォーム対応のオーディオ再生バックエンド。
///          WAV/MP3/FLAC等のファイル再生をサポートする。
///          miniaudio implementation は src/miniaudio_impl.cpp で定義。

#include <miniaudio.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::audio {

/// @brief miniaudioベースのオーディオエンジン
/// @details ma_engineをラップし、サウンドファイルの再生・ボリューム制御を提供する。
///          IAudioEngineインターフェースとは独立したスタンドアロン実装。
class MiniaudioEngine {
public:
	MiniaudioEngine() {
		ma_engine_config config = ma_engine_config_init();
		if (ma_engine_init(&config, &m_engine) == MA_SUCCESS) {
			m_initialized = true;
		}
	}

	~MiniaudioEngine() {
		if (m_initialized) {
			releaseVoices();
			ma_engine_uninit(&m_engine);
		}
	}

	// コピー禁止
	MiniaudioEngine(const MiniaudioEngine&) = delete;
	MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;

	// ムーブ可能。ma_engine / ma_sound は内部に自己参照ポインタを持つため、move 後の
	// 旧オブジェクトは必ず無効化する (m_initialized=false)。
	MiniaudioEngine(MiniaudioEngine&& other) noexcept
		: m_engine(other.m_engine)
		, m_initialized(other.m_initialized)
		, m_music(other.m_music)
		, m_musicActive(other.m_musicActive)
		, m_oneShots(std::move(other.m_oneShots)) {
		other.m_initialized = false;
		other.m_musicActive = false;
	}

	MiniaudioEngine& operator=(MiniaudioEngine&& other) noexcept {
		if (this != &other) {
			if (m_initialized) {
				releaseVoices();
				ma_engine_uninit(&m_engine);
			}
			m_engine = other.m_engine;
			m_initialized = other.m_initialized;
			m_music = other.m_music;
			m_musicActive = other.m_musicActive;
			m_oneShots = std::move(other.m_oneShots);
			other.m_initialized = false;
			other.m_musicActive = false;
		}
		return *this;
	}

	/// @brief 初期化成功したか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief サウンドファイルを再生する（WAV/MP3/FLAC対応）
	/// @param path ファイルパス
	void playFile(const std::string& path) {
		if (!m_initialized) return;
		ma_engine_play_sound(&m_engine, path.c_str(), nullptr);
	}

	/// @brief サウンドファイルを再生する（string_view版）
	/// @param path ファイルパス
	void playFile(std::string_view path) {
		playFile(std::string(path));
	}

	/// @brief 音量指定で one-shot SE を再生する
	/// @details ma_sound を 1 つ生成して再生し、終了済みのものは次回再生時に reap する。
	///          ma_engine_play_sound はマスター音量のみで per-sound 音量を扱えないため、
	///          per-sound 音量が要る場合はこちらを使う。
	/// @param path ファイルパス
	/// @param volume ボリューム [0.0, 1.0]
	void playSoundVolume(const std::string& path, float volume) {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS) {
			return;
		}
		ma_sound_set_volume(snd.get(), volume);
		ma_sound_start(snd.get());
		m_oneShots.push_back(std::move(snd));
	}

	/// @brief マスターボリュームを設定する
	/// @param volume ボリューム [0.0, 1.0]
	void setMasterVolume(float volume) {
		if (!m_initialized) return;
		ma_engine_set_volume(&m_engine, volume);
	}

	/// @brief マスターボリュームを取得する
	/// @return 現在のボリューム
	[[nodiscard]] float getMasterVolume() const {
		if (!m_initialized) return 0.0f;
		return ma_engine_get_volume(const_cast<ma_engine*>(&m_engine));
	}

	/// @brief BGM を再生する（ループ・音量指定）
	/// @details 永続 ma_sound を 1 つだけ持ち、再生のたびに前の BGM を停止・破棄する。
	///          ストリーミング再生 (MA_SOUND_FLAG_STREAM) で長尺ファイルでも省メモリ。
	/// @param path ファイルパス
	/// @param volume ボリューム [0.0, 1.0]
	/// @param loop ループ再生するか
	void playMusic(const std::string& path, float volume, bool loop) {
		if (!m_initialized) { return; }
		stopMusic();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_STREAM,
		                            nullptr, nullptr, &m_music) != MA_SUCCESS) {
			return;
		}
		ma_sound_set_looping(&m_music, loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_volume(&m_music, volume);
		ma_sound_start(&m_music);
		m_musicActive = true;
	}

	/// @brief BGM を停止する
	void stopMusic() {
		if (m_musicActive) {
			ma_sound_stop(&m_music);
			ma_sound_uninit(&m_music);
			m_musicActive = false;
		}
	}

	/// @brief 全サウンドを停止する
	void stopAll() {
		if (!m_initialized) return;
		ma_engine_stop(&m_engine);
	}

	/// @brief エンジンを再開する（stopAll後に使用）
	void resume() {
		if (!m_initialized) return;
		ma_engine_start(&m_engine);
	}

private:
	/// @brief 終了済みの one-shot ma_sound を破棄する
	void reapFinishedOneShots() {
		for (auto it = m_oneShots.begin(); it != m_oneShots.end();) {
			if (ma_sound_at_end(it->get())) {
				ma_sound_uninit(it->get());
				it = m_oneShots.erase(it);
			} else {
				++it;
			}
		}
	}

	/// @brief 全 voice (BGM + one-shot) を解放する
	void releaseVoices() {
		stopMusic();
		for (auto& s : m_oneShots) { ma_sound_uninit(s.get()); }
		m_oneShots.clear();
	}

	ma_engine m_engine{};
	bool m_initialized = false;
	ma_sound m_music{};
	bool m_musicActive = false;
	std::vector<std::unique_ptr<ma_sound>> m_oneShots;
};

} // namespace mitiru::audio
