#pragma once
/// @file MiniaudioEngine.hpp
/// @brief miniaudio ベースのオーディオエンジン
/// @details クロスプラットフォーム対応のオーディオ再生バックエンド。
///          WAV/MP3/FLAC等のファイル再生をサポートする。
///          miniaudio implementation は src/miniaudio_impl.cpp で定義。

#include <miniaudio.h>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioMeter.hpp>
#include <mitiru/debug/WarnOnce.hpp>

#if defined(MA_HAS_WASAPI) && defined(_WIN32)
// outputLatencySec() が OS ミキサ側のレイテンシを IAudioClient へ直接尋ねるため。
#include <audioclient.h>
#endif

namespace mitiru::audio {

/// @brief miniaudioベースのオーディオエンジン
/// @details ma_engineをラップし、サウンドファイルの再生・ボリューム制御を提供する。
///          IAudioEngineインターフェースとは独立したスタンドアロン実装。
class MiniaudioEngine {
public:
	/// @param periodMs デバイスへ 1 回に積むフレーム数 (ミリ秒)。0 = backend の既定。
	/// @details 既定の 10ms は 3 期ぶんで 30ms の遅れになる。音に合わせて叩く遊びでは、
	///          押した手応えも音の出も丸ごとその分遅れる。ハードウェアの下限は 2ms 前後
	///          あるので、4ms あれば取りこぼしの余裕を残したまま 12ms まで詰められる。
	explicit MiniaudioEngine(ma_uint32 periodMs = 4) {
		ma_engine_config config = ma_engine_config_init();
		config.periodSizeInMilliseconds = periodMs;
		if (ma_engine_init(&config, &m_engine) == MA_SUCCESS) {
			m_initialized = true;
		}
	}

	~MiniaudioEngine() {
		if (m_initialized) {
			releaseLoops();
			releaseVoices();
			ma_engine_uninit(&m_engine);
		}
	}

	// コピー・ムーブ禁止。ma_engine / ma_sound は自己参照ポインタを持ち、稼働中の
	// device thread が旧アドレスを参照し続けるため byte-copy による move は成立しない。
	MiniaudioEngine(const MiniaudioEngine&) = delete;
	MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;
	MiniaudioEngine(MiniaudioEngine&&) = delete;
	MiniaudioEngine& operator=(MiniaudioEngine&&) = delete;

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
	/// @details ma_sound を 1 つ生成して再生し、終了済みのものは retire 経由で遅延解放する。
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

	/// @brief 効果音をループ再生する。停止するまで鳴り続ける。
	/// @details one-shot と違い path で覚えておき、stopSoundLoop で止める。長押しの
	///          「押している間ずっと」のように、長さが入力で決まる音に使う。短い音を
	///          継ぎ足して伸ばすと継ぎ目が聴こえ、離した瞬間にぶつっと切れる。
	///          同じ path が鳴っている間の再呼び出しは、頭から鳴らし直さず音量と
	///          ピッチだけを寄せる (BGM の同 id 冪等と同じ扱い)。鳴らしながら音量を
	///          調整する用途で、呼ぶたびに曲が頭へ戻ると調整にならないため。
	void playSoundLoop(const std::string& path, float volume, float pitchScale, float fadeInSec) {
		if (!m_initialized) { return; }
		if (auto it = m_loops.find(path); it != m_loops.end()) {
			// 音量は短い ramp で寄せる。即値で変えるとザッというズレ音が乗る。
			ma_sound_set_fade_in_milliseconds(it->second.get(), -1.0f, volume, 40);
			ma_sound_set_pitch(it->second.get(), (pitchScale > 0.0f) ? pitchScale : 1.0f);
			return;
		}
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS) {
			mitiru::debug::warnOnce("audio.loop:" + path,
				"音声ファイルが見つからない/読めない: " + path);
			return;
		}
		ma_sound_set_looping(snd.get(), MA_TRUE);
		ma_sound_set_volume(snd.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) {
			ma_sound_set_pitch(snd.get(), pitchScale);
		}
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				snd.get(), 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(snd.get());
		m_loops[path] = std::move(snd);
	}

	/// @brief ループ再生中の効果音を止める。fadeOutSec > 0 で減衰させてから止める。
	/// @details 減衰させる場合も ma_sound はここで解放せず、次の update() で回収する。
	///          鳴っている最中に uninit すると device thread が解放済みを触る。
	void stopSoundLoop(const std::string& path, float fadeOutSec) {
		auto it = m_loops.find(path);
		if (it == m_loops.end()) { return; }
		if (fadeOutSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(it->second.get(), -1.0f, 0.0f,
				static_cast<ma_uint64>(fadeOutSec * 1000.0f));
			ma_sound_set_stop_time_in_pcm_frames(
				it->second.get(),
				ma_engine_get_time_in_pcm_frames(&m_engine)
					+ static_cast<ma_uint64>(fadeOutSec * ma_engine_get_sample_rate(&m_engine)));
			ma_sound_set_looping(it->second.get(), MA_FALSE);
			m_fading.push_back(std::move(it->second));
		} else {
			ma_sound_stop(it->second.get());
			ma_sound_uninit(it->second.get());
		}
		m_loops.erase(it);
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
	/// @details リズムゲーム等が判定の基準時刻に使う。ma_engine の global time は
	///          サウンドの有無に関わらずデバイス稼働中ずっと進む。未初期化時は 0。
	[[nodiscard]] double masterTimeSec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		const ma_uint32 sr = ma_engine_get_sample_rate(e);
		if (sr == 0) return 0.0;
		return static_cast<double>(ma_engine_get_time_in_pcm_frames(e)) / static_cast<double>(sr);
	}

	/// @brief 出力レイテンシ (秒)。masterTimeSec() はデバイスへ送った位置なので、耳へ届くのはこの値だけ後。
	/// @details 自前のバッファ (period × periodSize) の先に、OS のミキサがもう一段ある。
	///          共有モードの WASAPI では後者が 10ms 前後あり、無視すると判定窓が実際より
	///          手前に来る。プレイヤーは音に合わせて叩くので、そのずれがそのまま「遅く
	///          叩いている」判定になる。デバイスへ直接尋ねて足す。
	[[nodiscard]] double outputLatencySec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		ma_device* dev = ma_engine_get_device(e);
		if (dev == nullptr) { return 0.0; }
		const ma_uint32 sr = dev->playback.internalSampleRate;
		if (sr == 0) { return 0.0; }
		return static_cast<double>(deviceBufferFrames(dev)) / static_cast<double>(sr)
		     + backendLatencySec(dev);
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
		m_musicBaseVolume = volume;  // duck の復帰先として本来の音量を記憶
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				&m_music, 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(&m_music);
		m_musicActive = true;
		m_musicPaused = false;
	}

	/// @brief 毎フレームの定期メンテナンス (#51)。
	/// @details (a) 終了した one-shot voice を retire リストへ移し、kRetireDelay 経過後に
	///          uninit する (#52 根治: device thread が mix 中に触れうる期間を確実に過ぎて
	///          から解放する遅延解放)。(b) stopMusicFade で仕掛けた fade-out の残フレームを
	///          減算し、完了したら music voice を uninit する。uninit しないと、無音のまま
	///          loop voice が走り続ける。固定ステップ (~60Hz) の cadence 前提。
	void update() {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		// 減衰させて止めたループ音を回収する。鳴り終わってから uninit する。
		for (std::size_t i = m_fading.size(); i-- > 0;) {
			if (ma_sound_at_end(m_fading[i].get()) || !ma_sound_is_playing(m_fading[i].get())) {
				ma_sound_uninit(m_fading[i].get());
				m_fading.erase(m_fading.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
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
		// 復帰先は「本来の音量 (base)」であって「現在の音量」ではない。current を基準に
		// すると、SE 連打で duck 中に再 duck され base より小さい値へ ×mul が重なり、
		// BGM が雪だるま式に 0 へ落ちて消える (実機バグ)。base 基準なら何度押しても
		// 「base×mul へ落として base へ戻す」で一定に保たれる。
		const float base   = m_musicBaseVolume;
		const float ducked = base * mul;
		// 即 ducked にし、durSec かけて base へ fade in する → 一瞬下がってじわっと戻る。
		ma_sound_set_volume(&m_music, ducked);
		ma_sound_set_fade_in_milliseconds(
			&m_music, ducked, base, static_cast<ma_uint64>(durSec * 1000.0f));
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
	/// @brief 実際に確保されたデバイスバッファ (フレーム)。
	/// @details WASAPI の共有モードでは、要求した period × periods がそのまま通るとは
	///          限らない。internalPeriodSizeInFrames は要求値のままなので、確保された
	///          側の値がある場合はそちらを使う。
	[[nodiscard]] static ma_uint64 deviceBufferFrames(ma_device* dev) noexcept {
#if defined(MA_HAS_WASAPI) && defined(_WIN32)
		if (dev->pContext != nullptr && dev->pContext->backend == ma_backend_wasapi
		    && dev->wasapi.actualBufferSizeInFramesPlayback > 0) {
			return dev->wasapi.actualBufferSizeInFramesPlayback;
		}
#endif
		return static_cast<ma_uint64>(dev->playback.internalPeriodSizeInFrames) *
		       static_cast<ma_uint64>(dev->playback.internalPeriods);
	}

	/// @brief backend が自前バッファの外側に持つレイテンシ (秒)。分からない backend は 0。
	/// @details WASAPI の共有モードは OS 側のミキサを経由し、その 1 期ぶんが後段に乗る。
	///          GetStreamLatency はドライバによっては 0 を返すので、返らない場合は
	///          GetDevicePeriod の既定値で埋める。
	[[nodiscard]] static double backendLatencySec(ma_device* dev) noexcept {
#if defined(MA_HAS_WASAPI) && defined(_WIN32)
		if (dev->pContext == nullptr || dev->pContext->backend != ma_backend_wasapi) { return 0.0; }
		auto* client = static_cast<IAudioClient*>(dev->wasapi.pAudioClientPlayback);
		if (client == nullptr) { return 0.0; }
		REFERENCE_TIME rt = 0;
		if (SUCCEEDED(client->GetStreamLatency(&rt)) && rt > 0) {
			return static_cast<double>(rt) * 1e-7;   // REFERENCE_TIME は 100ns 単位
		}
		REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
		if (SUCCEEDED(client->GetDevicePeriod(&defaultPeriod, &minPeriod)) && defaultPeriod > 0) {
			return static_cast<double>(defaultPeriod) * 1e-7;
		}
		return 0.0;
#else
		(void)dev;
		return 0.0;
#endif
	}

	/// @brief 解放待ちの one-shot voice (#52)。
	/// @details ma_sound_at_end 検出後も device thread が同一 mix 周期内で voice に
	///          触れている可能性があるため、即 uninit せず retiredAt から待機させる。
	struct RetiredVoice {
		std::unique_ptr<ma_sound> snd;
		std::chrono::steady_clock::time_point retiredAt;
	};

	/// 遅延解放の待機時間。device の 1 mix 周期 (数十 ms) を確実に上回る値 (#52)。
	static constexpr std::chrono::milliseconds kRetireDelay{400};

	/// @brief 終了済み one-shot を retire へ移し、待機を終えた retire を解放する (#52)
	void reapFinishedOneShots() {
		retireFinishedOneShots();
		drainRetired();
	}

	/// @brief 終了済みの one-shot を retire リストへ移す (uninit はまだしない)
	void retireFinishedOneShots() {
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_oneShots.begin(); it != m_oneShots.end();) {
			if (ma_sound_at_end(it->get())) {
				m_retired.push_back(RetiredVoice{std::move(*it), now});
				it = m_oneShots.erase(it);
			} else {
				++it;
			}
		}
	}

	/// @brief retire から kRetireDelay 経過した voice を uninit する
	void drainRetired() {
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_retired.begin(); it != m_retired.end();) {
			if (now - it->retiredAt >= kRetireDelay) {
				ma_sound_uninit(it->snd.get());
				it = m_retired.erase(it);
			} else {
				++it;
			}
		}
	}

	/// @brief 待機中の retire を待たず同期的に全 uninit する (shutdown 専用)
	void flushRetired() {
		for (auto& r : m_retired) { ma_sound_uninit(r.snd.get()); }
		m_retired.clear();
	}

	/// @brief 全 voice (BGM + one-shot + retire 待ち) を解放する (shutdown 専用)
	/// @brief 鳴っているループ音をすべて解放する。
	void releaseLoops() {
		for (auto& kv : m_loops) { ma_sound_stop(kv.second.get()); ma_sound_uninit(kv.second.get()); }
		m_loops.clear();
		for (auto& s2 : m_fading) { ma_sound_uninit(s2.get()); }
		m_fading.clear();
	}

	void releaseVoices() {
		stopMusic();
		for (auto& s : m_oneShots) { ma_sound_uninit(s.get()); }
		m_oneShots.clear();
		flushRetired();
	}

	ma_engine m_engine{};
	bool m_initialized = false;
	ma_sound m_music{};
	bool m_musicActive = false;
	float m_musicBaseVolume = 1.0f;  ///< duck していない本来の BGM 音量。duck の復帰先 (#34 の雪だるま化防止)
	bool m_musicPaused = false;      ///< pauseMusic() 中か (resumeMusic() で false。v19)
	int  m_musicFadeOutFrames = 0;  ///< >0 の間 update() が減算し、0 で music を uninit (#51)
	std::unordered_map<std::string, std::unique_ptr<ma_sound>> m_loops;  ///< 鳴らしっぱなしのループ音 (path で引く)
	std::vector<std::unique_ptr<ma_sound>> m_fading;                   ///< 減衰させて止めた最中のループ音
	std::vector<std::unique_ptr<ma_sound>> m_oneShots;
	std::vector<RetiredVoice> m_retired;  ///< 遅延解放待ち (#52)
};

} // namespace mitiru::audio
