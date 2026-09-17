#pragma once
/// @file MiniaudioEngine.hpp
/// @brief miniaudio ベースのオーディオエンジン
/// @details クロスプラットフォーム対応のオーディオ再生バックエンド。
///          WAV/MP3/FLAC等のファイル再生をサポートする。
///          miniaudio implementation は src/miniaudio_impl.cpp で定義。

#include <miniaudio.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioMeter.hpp>
#include <mitiru/audio/AudioTransportClock.hpp>
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
		// device が実際にコールバックへ渡すフレーム数を transportDataCallback 経由で
		// 積算するため、engine 生成前に &m_engine をレジストリへ登録しておく
		// (ma_engine_init は成功すると device thread を即座に起動しうる、#F1)。
		{
			std::lock_guard<std::mutex> lock(registryMutex());
			registry()[&m_engine] = this;
		}
		ma_engine_config config = ma_engine_config_init();
		config.periodSizeInMilliseconds = periodMs;
		config.dataCallback = &MiniaudioEngine::transportDataCallback;
		if (ma_engine_init(&config, &m_engine) == MA_SUCCESS) {
			m_initialized = true;
		} else {
			std::lock_guard<std::mutex> lock(registryMutex());
			registry().erase(&m_engine);
		}
	}

	~MiniaudioEngine() {
		if (m_initialized) {
			releaseLoops();
			releaseVoices();
			ma_engine_uninit(&m_engine);
		}
		std::lock_guard<std::mutex> lock(registryMutex());
		registry().erase(&m_engine);
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
			mitiru::debug::warnOnceFix("audio.se:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
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
			mitiru::debug::warnOnceFix("audio.loop:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
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
			mitiru::debug::warnOnceFix("audio.se:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
			return;
		}
		ma_sound_set_volume(snd.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) {
			ma_sound_set_pitch(snd.get(), pitchScale);
		}
		const ma_uint32 sr = ma_engine_get_sample_rate(&m_engine);
		if (atSec > 0.0 && sr > 0) {
			// atSec は masterTimeSec() = m_transportClock 基準。ma_sound の開始時刻は
			// engine 自身の内部クロックで判定されるため、両者の「今」の差分を
			// engine クロックへ足し戻して変換する (2 つのクロックが乖離しうるため)。
			const double targetTransportFrames = atSec * static_cast<double>(sr);
			const double deltaFrames = targetTransportFrames - static_cast<double>(m_transportClock.frames());
			const double targetEngineFrames =
				static_cast<double>(ma_engine_get_time_in_pcm_frames(&m_engine)) + deltaFrames;
			ma_sound_set_start_time_in_pcm_frames(
				snd.get(), static_cast<ma_uint64>(targetEngineFrames > 0.0 ? targetEngineFrames : 0.0));
		}
		ma_sound_start(snd.get());
		m_oneShots.push_back(std::move(snd));
	}

	/// @brief playSoundScheduled が atSec をサンプル精度で honoring するか (#F2)。
	/// @details ma_sound_set_start_time_in_pcm_frames による実装なので常に true。
	[[nodiscard]] bool supportsScheduledPlayback() const noexcept { return true; }

	/// @brief path のサウンドをあらかじめ poolSize 個ぶんプールする (#F3)。
	/// @details playPooled() の hot path (拍ごと) で make_unique<ma_sound> と
	///          ファイル再読込を発生させないための事前確保。同じ decode 済みデータを
	///          ma_sound_init_copy で共有するのでメモリ増は voice の管理領域のみ。
	///          既にプール済みの path は no-op で true を返す。
	/// @return プール作成に成功したか (ファイルが読めない場合は false)
	bool preloadPooled(const std::string& path, std::size_t poolSize) {
		if (!m_initialized || poolSize == 0) { return false; }
		if (m_pools.find(path) != m_pools.end()) { return true; }
		auto pool = std::make_unique<SoundPool>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(),
		                            MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT,
		                            nullptr, nullptr, &pool->templateSound) != MA_SUCCESS) {
			mitiru::debug::warnOnceFix("audio.pool:" + path,
				"プール用音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
			return false;
		}
		pool->voices.reserve(poolSize);
		for (std::size_t i = 0; i < poolSize; ++i) {
			auto voice = std::make_unique<ma_sound>();
			if (ma_sound_init_copy(&m_engine, &pool->templateSound, 0, nullptr, voice.get()) != MA_SUCCESS) {
				for (auto& v : pool->voices) { ma_sound_uninit(v.get()); }
				ma_sound_uninit(&pool->templateSound);
				return false;
			}
			pool->voices.push_back(std::move(voice));
		}
		m_pools.emplace(path, std::move(pool));
		return true;
	}

	/// @brief プール済み SE を鳴らす (#F3)。未プールの path は playSoundEx にフォールバックする。
	/// @details 再生中でない voice を探し、無ければ最古の voice (round robin) を奪って
	///          頭から鳴らし直す。拍ごとに呼ばれる hot path でも allocation が発生しない。
	void playPooled(const std::string& path, float volume, float pitchScale) {
		if (!m_initialized) { return; }
		auto it = m_pools.find(path);
		if (it == m_pools.end()) { playSoundEx(path, volume, pitchScale, 0.0f); return; }
		SoundPool& pool = *it->second;
		ma_sound* target = nullptr;
		for (auto& v : pool.voices) {
			if (!ma_sound_is_playing(v.get())) { target = v.get(); break; }
		}
		if (target == nullptr) {
			target = pool.voices[pool.nextIndex].get();
			pool.nextIndex = (pool.nextIndex + 1) % pool.voices.size();
		}
		ma_sound_stop(target);
		ma_sound_seek_to_pcm_frame(target, 0);
		ma_sound_set_volume(target, volume);
		if (pitchScale > 0.0f) { ma_sound_set_pitch(target, pitchScale); }
		ma_sound_start(target);
	}

	/// @brief メモリ上のエンコード済み音声データから one-shot SE を再生する (#F4)。
	/// @details %TEMP% への一時ファイル書き出しを経由せず、埋め込みリソース (RCDATA 等)
	///          を直接鳴らすための経路。decoder はバイト列を直接参照し続けるため、
	///          再生終了までバイト列自体もここでコピー保持する。
	void playFromMemory(const void* data, std::size_t len, float volume, float pitchScale) {
		if (!m_initialized || data == nullptr || len == 0) { return; }
		reapFinishedMemorySounds();
		auto mem = std::make_unique<MemorySound>();
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		mem->bytes.assign(bytes, bytes + len);
		ma_decoder_config decCfg = ma_decoder_config_init(ma_format_unknown, 0, 0);
		if (ma_decoder_init_memory(mem->bytes.data(), mem->bytes.size(), &decCfg, &mem->decoder) != MA_SUCCESS) {
			mitiru::debug::warnOnceFix("audio.mem.decode", "メモリ音声データのデコードに失敗しました",
				"埋め込みバイト列が壊れているか、対応していないエンコード形式",
				"元データのエンコード形式 (wav/ogg/mp3 等) を確認する");
			return;
		}
		mem->sound = std::make_unique<ma_sound>();
		if (ma_sound_init_from_data_source(&m_engine, &mem->decoder, 0, nullptr, mem->sound.get()) != MA_SUCCESS) {
			ma_decoder_uninit(&mem->decoder);
			mitiru::debug::warnOnceFix("audio.mem.sound", "メモリ音声データの初期化に失敗しました",
				"decoder は初期化できたが ma_sound への割り当てが失敗した (未対応チャンネル構成等)",
				"埋め込み音声データのフォーマット (サンプルレート/チャンネル数) を確認する");
			return;
		}
		ma_sound_set_volume(mem->sound.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) { ma_sound_set_pitch(mem->sound.get(), pitchScale); }
		ma_sound_start(mem->sound.get());
		m_memorySounds.push_back(std::move(mem));
	}

	/// @brief ボイス（同時1トラック）を再生する (#F6)。既存ボイスは頭出しせず即差し替える。
	/// @details BGM と独立したスロットに持つ。meterChannels() が "voice" 種別として
	///          報告するので、mixer.html は BGM (M) / ボイス (V) / SE (S) を区別できる。
	void playVoiceEx(const std::string& path, float volume, float pitchScale, float fadeInSec) {
		if (!m_initialized) { return; }
		stopVoice(0.0f);
		m_voice = std::make_unique<ma_sound>();
		m_voicePath = path;   // meterChannels が asset 名を出すため (K1)
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, m_voice.get()) != MA_SUCCESS) {
			mitiru::debug::warnOnceFix("audio.voice:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
			m_voice.reset();
			return;
		}
		ma_sound_set_volume(m_voice.get(), volume);
		if (pitchScale > 0.0f && pitchScale != 1.0f) { ma_sound_set_pitch(m_voice.get(), pitchScale); }
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				m_voice.get(), 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(m_voice.get());
	}

	/// @brief 再生中のボイスを止める (#F6)。fadeOutSec > 0 で減衰させてから止める。
	void stopVoice(float fadeOutSec) {
		if (!m_voice) { return; }
		if (fadeOutSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(m_voice.get(), -1.0f, 0.0f,
				static_cast<ma_uint64>(fadeOutSec * 1000.0f));
			m_fading.push_back(std::move(m_voice));
		} else {
			ma_sound_stop(m_voice.get());
			ma_sound_uninit(m_voice.get());
		}
		m_voice.reset();
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

	/// @brief マスター再生クロック (秒)。device data callback が要求したフレーム数の積算 / サンプルレート。
	/// @details リズムゲーム等が判定の基準時刻に使う (#F1)。ma_engine 自身の global time
	///          (ma_engine_get_time_in_pcm_frames) は鳴っている音が無い区間で進まないことがある。
	///          m_transportClock は device が実際に出力を要求したフレーム数だけを数えるので、
	///          無音でも単調増加し、device が停止すれば呼ばれなくなり自然に止まる。未初期化時は 0。
	[[nodiscard]] double masterTimeSec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		const ma_uint32 sr = ma_engine_get_sample_rate(e);
		return m_transportClock.seconds(sr);
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
			mitiru::debug::warnOnceFix("audio.music:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
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
		reapFinishedMemorySounds();
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
	/// @details BGM (m_music) + ボイス (m_voice、#F6) + 終了前の one-shot SE を、
	///          それぞれの設定実効音量で報告する。mitiru_mixer 窓の per-channel VU 用
	///          (host が host_module 経由で読む)。
	/// @return チャンネルごとの { 種別, レベル } の配列 (voice は asset / pan / 残り秒も入る)
	[[nodiscard]] std::vector<ChannelMeter> meterChannels() const {
		std::vector<ChannelMeter> out;
		if (!m_initialized) { return out; }
		if (m_musicActive && ma_sound_is_playing(&m_music)) {
			out.push_back(ChannelMeter{"music", ma_sound_get_volume(&m_music)});
		}
		if (m_voice && ma_sound_is_playing(m_voice.get())) {
			out.push_back(voiceMeter());
		}
		for (const auto& s : m_oneShots) {
			if (!ma_sound_at_end(s.get())) {
				out.push_back(ChannelMeter{"se", ma_sound_get_volume(s.get())});
			}
		}
		return out;
	}

private:
	/// @brief 再生中ボイスのメーター 1 件 (K1: mixer 窓の「voice 一覧」用)。
	/// @details 残り秒は length - cursor。MA_SOUND_FLAG_DECODE で丸ごと展開しているので
	///          length は O(1)。ループ指定や長さ不明なら負のまま (= 不明)。
	[[nodiscard]] ChannelMeter voiceMeter() const {
		ChannelMeter m{"voice", ma_sound_get_volume(m_voice.get())};
		m.pan = ma_sound_get_pan(m_voice.get());
		float len = 0.0f, cur = 0.0f;
		if (!ma_sound_is_looping(m_voice.get())
		    && ma_sound_get_length_in_seconds(m_voice.get(), &len) == MA_SUCCESS
		    && ma_sound_get_cursor_in_seconds(m_voice.get(), &cur) == MA_SUCCESS) {
			m.remainingSec = (len > cur) ? (len - cur) : 0.0f;
		}
		const std::size_t slash = m_voicePath.find_last_of("/\\");
		const char* name = m_voicePath.c_str() + (slash == std::string::npos ? 0 : slash + 1);
		ChannelMeter::copyTo(m.asset, sizeof(m.asset), name);
		return m;
	}

	/// @brief この engine インスタンス群のレジストリ (ma_engine* → MiniaudioEngine*)。
	/// @details transportDataCallback は device thread から ma_engine* しか受け取れないため、
	///          そこから呼び出し元インスタンスへ戻すために使う。関数内 static はインライン
	///          関数 (クラス内定義) のため TU をまたいで単一実体になる。
	static std::unordered_map<const ma_engine*, MiniaudioEngine*>& registry() {
		static std::unordered_map<const ma_engine*, MiniaudioEngine*> r;
		return r;
	}
	static std::mutex& registryMutex() {
		static std::mutex m;
		return m;
	}

	/// @brief ma_engine_config.dataCallback として登録する device data callback (#F1)。
	/// @details 通常経路 (ma_engine_data_callback_internal 相当) のミックスを行った上で、
	///          device が要求したフレーム数を無条件に m_transportClock へ積算する。
	///          ミックス内容 (無音か否か) に関わらず device 呼び出しそのものを数えるので、
	///          engine 内部クロックが無音区間で止まる場合でも連続して進む。
	static void transportDataCallback(ma_device* pDevice, void* pFramesOut, const void* pFramesIn, ma_uint32 frameCount) {
		(void)pFramesIn;
		auto* engine = static_cast<ma_engine*>(pDevice->pUserData);
		ma_engine_read_pcm_frames(engine, pFramesOut, frameCount, nullptr);
		std::lock_guard<std::mutex> lock(registryMutex());
		if (auto it = registry().find(engine); it != registry().end()) {
			it->second->m_transportClock.addFrames(frameCount);
		}
	}

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

	/// @brief preloadPooled() で作った、鳴らすたびに使い回す voice の集合 (#F3)。
	struct SoundPool {
		ma_sound templateSound{};                        ///< ma_sound_init_copy の元
		std::vector<std::unique_ptr<ma_sound>> voices;    ///< templateSound の複製
		std::size_t nextIndex = 0;                        ///< 全 voice 使用中のときに奪う順番
	};

	/// @brief playFromMemory() 用の再生インスタンス (#F4)。
	/// @details decoder はバイト列を直接参照するため、再生が終わるまで両方を同じ寿命で持つ。
	struct MemorySound {
		std::vector<std::uint8_t> bytes;
		ma_decoder decoder{};
		std::unique_ptr<ma_sound> sound;
	};

	/// @brief kRetireDelay 経過を待ってから MemorySound を解放するための保持 (#F4、#52 と同じ理由)。
	struct RetiredMemorySound {
		std::unique_ptr<MemorySound> mem;
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

	/// @brief 終了済みの MemorySound を retire へ移し、待機を終えたものを解放する (#F4)
	void reapFinishedMemorySounds() {
		const auto now = std::chrono::steady_clock::now();
		for (auto it = m_memorySounds.begin(); it != m_memorySounds.end();) {
			if (ma_sound_at_end((*it)->sound.get())) {
				m_retiredMemory.push_back(RetiredMemorySound{std::move(*it), now});
				it = m_memorySounds.erase(it);
			} else {
				++it;
			}
		}
		for (auto it = m_retiredMemory.begin(); it != m_retiredMemory.end();) {
			if (now - it->retiredAt >= kRetireDelay) {
				ma_sound_uninit(it->mem->sound.get());
				ma_decoder_uninit(&it->mem->decoder);
				it = m_retiredMemory.erase(it);
			} else {
				++it;
			}
		}
	}

	/// @brief 待機中の MemorySound retire を待たず同期的に全解放する (shutdown 専用)
	void flushMemorySounds() {
		for (auto& m : m_memorySounds) { ma_sound_uninit(m->sound.get()); ma_decoder_uninit(&m->decoder); }
		m_memorySounds.clear();
		for (auto& r : m_retiredMemory) { ma_sound_uninit(r.mem->sound.get()); ma_decoder_uninit(&r.mem->decoder); }
		m_retiredMemory.clear();
	}

	/// @brief preloadPooled() で作った全プールを解放する (shutdown 専用)
	void releasePools() {
		for (auto& kv : m_pools) {
			for (auto& v : kv.second->voices) { ma_sound_uninit(v.get()); }
			ma_sound_uninit(&kv.second->templateSound);
		}
		m_pools.clear();
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
		stopVoice(0.0f);
		for (auto& s : m_oneShots) { ma_sound_uninit(s.get()); }
		m_oneShots.clear();
		flushRetired();
		flushMemorySounds();
		releasePools();
	}

	ma_engine m_engine{};
	bool m_initialized = false;
	AudioTransportClock m_transportClock;  ///< device 出力フレーム数の連続積算クロック (#F1)
	ma_sound m_music{};
	std::unique_ptr<ma_sound> m_voice;  ///< 再生中のボイス（同時1トラック、#F6）
	std::string m_voicePath;            ///< m_voice の元ファイル (meterChannels の asset 名、K1)
	bool m_musicActive = false;
	float m_musicBaseVolume = 1.0f;  ///< duck していない本来の BGM 音量。duck の復帰先 (#34 の雪だるま化防止)
	bool m_musicPaused = false;      ///< pauseMusic() 中か (resumeMusic() で false。v19)
	int  m_musicFadeOutFrames = 0;  ///< >0 の間 update() が減算し、0 で music を uninit (#51)
	std::unordered_map<std::string, std::unique_ptr<ma_sound>> m_loops;  ///< 鳴らしっぱなしのループ音 (path で引く)
	std::vector<std::unique_ptr<ma_sound>> m_fading;                   ///< 減衰させて止めた最中のループ音
	std::vector<std::unique_ptr<ma_sound>> m_oneShots;
	std::vector<RetiredVoice> m_retired;  ///< 遅延解放待ち (#52)
	std::unordered_map<std::string, std::unique_ptr<SoundPool>> m_pools;  ///< preloadPooled() 済みプール (#F3)
	std::vector<std::unique_ptr<MemorySound>> m_memorySounds;  ///< playFromMemory() 再生中 (#F4)
	std::vector<RetiredMemorySound> m_retiredMemory;           ///< 同、遅延解放待ち (#F4)
};

} // namespace mitiru::audio
