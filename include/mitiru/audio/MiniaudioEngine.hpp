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

#include <mitiru/audio/AudioMeter.hpp>
#include <mitiru/debug/WarnOnce.hpp>

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
		, m_musicPaused(other.m_musicPaused)
		, m_oneShots(std::move(other.m_oneShots)) {
		other.m_initialized = false;
		other.m_musicActive = false;
		other.m_musicPaused = false;
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
			m_musicPaused = other.m_musicPaused;
			m_oneShots = std::move(other.m_oneShots);
			other.m_initialized = false;
			other.m_musicActive = false;
			other.m_musicPaused = false;
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
		playSoundEx(path, volume, 1.0f, 0.0f);
	}

	/// @brief pitch / fade-in を指定して one-shot SE を再生する (#19/#20)。
	/// @param pitchScale 1.0=normal、0.5=半音域低、2.0=高。<=0 は 1.0 とみなす。
	/// @param fadeInSec  0 = fade-in なし、> 0 で 0→volume へ fade-in。
	void playSoundEx(const std::string& path, float volume, float pitchScale, float fadeInSec) {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS) {
			// 黙った無音は原因不明になるので path 単位で初回のみ警告 (R-01 級)
			mitiru::debug::warnOnce("audio.se:" + path,
				"音声ファイルが見つからない/読めない: " + path);
			return;
		}
		ma_sound_set_volume(snd.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) {
			ma_sound_set_pitch(snd.get(), pitchScale);
		}
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				snd.get(), 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(snd.get());
		m_oneShots.push_back(std::move(snd));
	}

	/// @brief 効果音を「マスタークロック上の絶対時刻 atSec」にサンプル精度で予約再生する (v19)。
	/// @details ma_engine のグローバルクロックが atSec に達した瞬間に backend がミックスを開始する
	///          (フレーム量子化なし)。atSec <= 現在時刻 なら即時再生される。
	void playSoundScheduled(const std::string& path, double atSec, float volume, float pitchScale) {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS) {
			mitiru::debug::warnOnce("audio.se:" + path,
				"音声ファイルが見つからない/読めない: " + path);
			return;
		}
		ma_sound_set_volume(snd.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) {
			ma_sound_set_pitch(snd.get(), pitchScale);
		}
		const ma_uint32 sr = ma_engine_get_sample_rate(&m_engine);
		if (atSec > 0.0 && sr > 0) {
			ma_sound_set_start_time_in_pcm_frames(
				snd.get(), static_cast<ma_uint64>(atSec * static_cast<double>(sr)));
		}
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

	/// @brief マスター再生クロック (秒)。デバイスが再生した PCM フレーム位置 / サンプルレート。
	/// @details リズムゲーム等が判定の基準時刻に使う (ADR 0008 拡張)。ma_engine の global time は
	///          サウンドの有無に関わらずデバイス稼働中ずっと進む。未初期化時は 0。
	[[nodiscard]] double masterTimeSec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		const ma_uint32 sr = ma_engine_get_sample_rate(e);
		if (sr == 0) return 0.0;
		return static_cast<double>(ma_engine_get_time_in_pcm_frames(e)) / static_cast<double>(sr);
	}

	/// @brief 出力レイテンシ (秒)。デバイスの内部バッファ (period × periodSize) / sampleRate。
	/// @details masterTimeSec() はデバイスへ送った位置なので、実際に耳へ届くのはこの値だけ後。
	///          リズムゲームが判定窓を耳基準へ補正するのに使う。device 固定値で毎フレーム同じ。
	[[nodiscard]] double outputLatencySec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		ma_device* dev = ma_engine_get_device(e);
		if (dev == nullptr) { return 0.0; }
		const ma_uint32 sr = dev->playback.internalSampleRate;
		if (sr == 0) { return 0.0; }
		const ma_uint64 bufFrames =
			static_cast<ma_uint64>(dev->playback.internalPeriodSizeInFrames) *
			static_cast<ma_uint64>(dev->playback.internalPeriods);
		return static_cast<double>(bufFrames) / static_cast<double>(sr);
	}

	/// @brief BGM を再生する（ループ・音量指定）
	/// @details 永続 ma_sound を 1 つだけ持ち、再生のたびに前の BGM を停止・破棄する。
	///          ストリーミング再生 (MA_SOUND_FLAG_STREAM) で長尺ファイルでも省メモリ。
	/// @param path ファイルパス
	/// @param volume ボリューム [0.0, 1.0]
	/// @param loop ループ再生するか
	void playMusic(const std::string& path, float volume, bool loop) {
		playMusicEx(path, volume, loop, 0.0f);
	}

	/// @brief fade-in 指定で BGM を再生する (#20)。fadeInSec > 0 で 0→volume へ。
	void playMusicEx(const std::string& path, float volume, bool loop, float fadeInSec) {
		if (!m_initialized) { return; }
		stopMusic();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_STREAM,
		                            nullptr, nullptr, &m_music) != MA_SUCCESS) {
			// 黙った無音は原因不明になるので path 単位で初回のみ警告 (R-01 級)
			mitiru::debug::warnOnce("audio.music:" + path,
				"音声ファイルが見つからない/読めない: " + path);
			return;
		}
		ma_sound_set_looping(&m_music, loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_volume(&m_music, volume);
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				&m_music, 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(&m_music);
		m_musicActive = true;
		m_musicPaused = false;
	}

	/// @brief 毎フレームの定期メンテナンス (#51)。
	/// @details (a) 終了した one-shot voice を毎フレーム回収する (従来は次の SE 再生時
	///          まで遅延 → 静かな区間で ended voice が滞留した)。(b) stopMusicFade で
	///          仕掛けた fade-out の残フレームを減算し、完了したら music voice を uninit
	///          する (従来は uninit されず、無音のまま loop voice が走り続けていた)。
	///          固定ステップ (~60Hz) の cadence で呼ばれる前提。
	void update() {
		if (!m_initialized) { return; }
		// #52: 毎フレーム reap は uninit 頻度が高く (特に --speed 倍速 headless)、間欠
		// クラッシュ (0xC0000005) の容疑となった。~30 フレームに 1 回へ間引く (静かな
		// 区間でも ~0.5s で回収・churn は 1/30)。終了済み voice のみ uninit する点は不変。
		if (++m_reapTick >= 30) { m_reapTick = 0; reapFinishedOneShots(); }
		if (m_musicFadeOutFrames > 0 && --m_musicFadeOutFrames == 0) { stopMusic(); }
	}

	/// @brief BGM を停止する
	void stopMusic() {
		if (m_musicActive) {
			ma_sound_stop(&m_music);
			ma_sound_uninit(&m_music);
			m_musicActive = false;
		}
		m_musicFadeOutFrames = 0;
		m_musicPaused = false;
	}

	/// @brief fade-out しながら BGM を停止する (#20)。
	/// @details fadeOutSec > 0 のとき volume→0 の fade-out を仕掛けるが、miniaudio の
	///          fade はサウンド寿命と独立なので、十分時間が経過したら通常 stop を呼ぶ前提。
	///          シンプルさのため v1 では fade を仕掛けて即 stop しない (呼び側が次フレームで
	///          通常 playMusic / stopMusic を呼べる)。即「次の BGM へ切替」の crossfade は
	///          stopMusicFade(prevFadeOut) → playMusicEx(newId, vol, loop, newFadeIn) で実現。
	void stopMusicFade(float fadeOutSec) {
		if (!m_musicActive) { return; }
		if (fadeOutSec <= 0.0f) { stopMusic(); return; }
		ma_sound_set_fade_in_milliseconds(
			&m_music, ma_sound_get_volume(&m_music), 0.0f,
			static_cast<ma_uint64>(fadeOutSec * 1000.0f));
		// fade 終了後の自動 uninit は miniaudio が直接提供しないため、update() が残フレームを
		// 数えて完了時に stopMusic() する (#51)。これが無いと無音の loop voice が走り続けた。
		// +6 frame の余裕で fade を確実に鳴らし切る。約 60Hz 前提。
		m_musicFadeOutFrames = static_cast<int>(fadeOutSec * 60.0f) + 6;
	}

	/// @brief BGM を一時的に mul 倍へ下げ、durSec かけて元の音量へ戻す (#34、ducking heuristic)。
	/// @details 大きな SE 再生中だけ BGM を引っ込めてインパクトを上げる用途。BGM 未再生時は no-op。
	void duckMusic(float mul, float durSec) {
		if (!m_initialized || !m_musicActive) { return; }
		if (mul <= 0.0f || mul >= 1.0f || durSec <= 0.0f) { return; }
		const float current = ma_sound_get_volume(&m_music);
		const float ducked  = current * mul;
		// 即 ducked にし、durSec かけて current へ fade in する → 一瞬下がってじわっと戻る。
		ma_sound_set_volume(&m_music, ducked);
		ma_sound_set_fade_in_milliseconds(
			&m_music, ducked, current, static_cast<ma_uint64>(durSec * 1000.0f));
	}

	/// @brief 再生中の BGM を一時停止する (v19)。ma_sound_stop は再生位置を保持するので、
	///        resumeMusic() で続きから鳴る (uninit する stopMusic() とは別物)。
	void pauseMusic() {
		if (!m_initialized || !m_musicActive || m_musicPaused) { return; }
		ma_sound_stop(&m_music);
		m_musicPaused = true;
	}

	/// @brief pauseMusic() で止めた BGM を続きから再開する (v19)。
	void resumeMusic() {
		if (!m_initialized || !m_musicActive || !m_musicPaused) { return; }
		ma_sound_start(&m_music);
		m_musicPaused = false;
	}

	/// @brief 再生中の BGM を指定位置 (秒) へシークする (v19)。一時停止中でも位置だけ移せる。
	void seekMusic(double positionSec) {
		if (!m_initialized || !m_musicActive) { return; }
		if (positionSec < 0.0) { positionSec = 0.0; }
		ma_sound_seek_to_second(&m_music, static_cast<float>(positionSec));
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

	/// @brief 再生中チャンネルのメーター読みを列挙する
	/// @details BGM (m_music) + 終了前の one-shot SE を、それぞれの設定実効音量で
	///          報告する。mitiru_mixer 窓の per-channel VU 用 (host が host_module 経由で読む)。
	/// @return チャンネルごとの { 種別, レベル } の配列
	[[nodiscard]] std::vector<ChannelMeter> meterChannels() const {
		std::vector<ChannelMeter> out;
		if (!m_initialized) { return out; }
		if (m_musicActive && ma_sound_is_playing(&m_music)) {
			out.push_back(ChannelMeter{"music", ma_sound_get_volume(&m_music)});
		}
		for (const auto& s : m_oneShots) {
			if (!ma_sound_at_end(s.get())) {
				out.push_back(ChannelMeter{"se", ma_sound_get_volume(s.get())});
			}
		}
		return out;
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
	bool m_musicPaused = false;      ///< pauseMusic() 中か (resumeMusic() で false。v19)
	int  m_musicFadeOutFrames = 0;  ///< >0 の間 update() が減算し、0 で music を uninit (#51)
	int  m_reapTick = 0;            ///< update() の reap 間引きカウンタ (#52)
	std::vector<std::unique_ptr<ma_sound>> m_oneShots;
};

} // namespace mitiru::audio
