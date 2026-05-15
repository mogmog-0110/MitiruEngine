#pragma once
/// @file MiniaudioEngine.hpp
/// @brief miniaudio ベースのオーディオエンジン
/// @details クロスプラットフォーム対応のオーディオ再生バックエンド。
///          WAV/MP3/FLAC等のファイル再生をサポートする。
///          miniaudio implementation は src/miniaudio_impl.cpp で定義。

#include <miniaudio.h>

#include <string>
#include <string_view>

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
			ma_engine_uninit(&m_engine);
		}
	}

	// Non-copyable
	MiniaudioEngine(const MiniaudioEngine&) = delete;
	MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;

	// Movable
	MiniaudioEngine(MiniaudioEngine&& other) noexcept
		: m_engine(other.m_engine), m_initialized(other.m_initialized) {
		other.m_initialized = false;
	}

	MiniaudioEngine& operator=(MiniaudioEngine&& other) noexcept {
		if (this != &other) {
			if (m_initialized) {
				ma_engine_uninit(&m_engine);
			}
			m_engine = other.m_engine;
			m_initialized = other.m_initialized;
			other.m_initialized = false;
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
	ma_engine m_engine{};
	bool m_initialized = false;
};

} // namespace mitiru::audio
