#pragma once
/// @file MiniaudioEngine.hpp
/// @brief miniaudio ベースのオーディオエンジン
/// @details クロスプラットフォーム対応のオーディオ再生バックエンド。
///          WAV/MP3/FLAC 等のファイル再生をサポートする。
///          miniaudio の実装は src/miniaudio_impl.cpp で定義する。

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

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
#include <mitiru/audio/MusicLowPassBus.hpp>
#include <mitiru/audio/detail/MiniaudioDeviceLatency.hpp>
#include <mitiru/audio/detail/MiniaudioKeyedSounds.hpp>
#include <mitiru/debug/WarnOnce.hpp>


namespace mitiru::audio {

/// @brief miniaudio ベースのオーディオエンジン
/// @details ma_engine をラップし、サウンドファイルの再生・ボリューム制御を提供する。
///          IAudioEngine インターフェースとは独立したスタンドアロン実装。
class MiniaudioEngine {
public:
	/// @param periodMs デバイスへ 1 回に積むフレーム数 (ミリ秒)。0 = backend の既定。
	/// @details 既定の 10ms は 3 期分で 30ms の遅れになる。音に合わせて叩く遊びでは、
	///          押した手応えも音が出るタイミングも、その分だけ遅れる。ハードウェアの下限は 2ms 前後
	///          なので、4ms あれば取りこぼしを防ぐ余裕を残したまま 12ms まで短縮できる。
	explicit MiniaudioEngine(ma_uint32 periodMs = 4) {
		ma_engine_config config = ma_engine_config_init();
		config.periodSizeInMilliseconds = periodMs;
		config.dataCallback = &MiniaudioEngine::transportDataCallback;
		init(config);
	}

	/// @brief デバイスを開かない engine。renderOffline() で呼び出し側がミックス結果を取得する (テスト・書き出し用)
	struct Offline { ma_uint32 sampleRate = 48000; ma_uint32 channels = 2; };
	explicit MiniaudioEngine(Offline offline) {
		ma_engine_config config = ma_engine_config_init();
		config.noDevice = MA_TRUE;
		config.sampleRate = offline.sampleRate;
		config.channels = offline.channels;
		init(config);
	}

	~MiniaudioEngine() {
		if (m_initialized) {
			releaseLoops();
			releaseVoices();
			m_musicBus.uninit();
			ma_engine_uninit(&m_engine);
		}
		std::lock_guard<std::mutex> lock(registryMutex());
		registry().erase(&m_engine);
	}

	// コピー・ムーブ禁止。ma_engine / ma_sound は自己参照ポインタを持ち、稼働中の
	// device thread が移動前のアドレスを参照し続けるため、byte-copy による move は成立しない。
	MiniaudioEngine(const MiniaudioEngine&) = delete;
	MiniaudioEngine& operator=(const MiniaudioEngine&) = delete;
	MiniaudioEngine(MiniaudioEngine&&) = delete;
	MiniaudioEngine& operator=(MiniaudioEngine&&) = delete;

	/// @brief 初期化に成功したか
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief Offline で作った engine のミックスを frames 分進めて out へ書く
	/// @return 書いたフレーム数 (Offline でない engine では 0。device thread と競合するため)
	ma_uint64 renderOffline(float* out, ma_uint64 frames) {
		if (!m_initialized || ma_engine_get_device(&m_engine) != nullptr) { return 0; }
		ma_uint64 read = 0;
		ma_engine_read_pcm_frames(&m_engine, out, frames, &read);
		return read;
	}

	/// @brief BGM だけに low-pass を掛ける。cutoffHz <= 0 で外す
	/// @details 効果音・ボイスには掛けない。再生中の BGM にもその場で反映し、次の BGM にも引き継ぐ。
	void setMusicLowPass(float cutoffHz) {
		if (!m_initialized) { return; }
		m_musicBus.setCutoff(cutoffHz);
		if (m_musicActive) { m_musicBus.route(m_music); }
	}

	/// @brief サウンドファイルを再生する (WAV/MP3/FLAC 対応)
	/// @param path ファイルパス
	void playFile(const std::string& path) {
		if (!m_initialized) return;
		ma_engine_play_sound(&m_engine, path.c_str(), nullptr);
	}

	/// @brief サウンドファイルを再生する（string_view 版）
	/// @param path ファイルパス
	void playFile(std::string_view path) {
		playFile(std::string(path));
	}

	/// @brief 音量を指定して one-shot SE を再生する
	/// @details ma_sound を 1 つ生成して再生し、終了済みのものは retire を経由して遅延解放する。
	///          ma_engine_play_sound はマスター音量のみを扱い、per-sound 音量を扱えないため、
	///          per-sound 音量が必要な場合はこちらを使う。
	/// @param path ファイルパス
	/// @param volume ボリューム [0.0, 1.0]
	void playSoundVolume(const std::string& path, float volume) {
		playSoundEx(path, volume, 1.0f, 0.0f);
	}

	/// @brief pitch / fade-in を指定して one-shot SE を再生する (#19/#20)。
	/// @param pitchScale 1.0=normal、0.5=半音域低、2.0=高。<=0 は 1.0 とみなす。
	/// @param fadeInSec  0 = fade-in なし、> 0 で 0→volume へ fade-in。
	/// @param pan        -1 (左) .. 1 (右)。
	void playSoundEx(const std::string& path, float volume, float pitchScale, float fadeInSec, float pan = 0.0f) {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		auto snd = std::make_unique<ma_sound>();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_DECODE,
		                            nullptr, nullptr, snd.get()) != MA_SUCCESS) {
			// 無音のままでは原因が分からないので、path 単位で初回のみ警告する (R-01 級)
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
		if (pan != 0.0f) { ma_sound_set_pan(snd.get(), pan); }
		if (fadeInSec > 0.0f) {
			ma_sound_set_fade_in_milliseconds(
				snd.get(), 0.0f, volume, static_cast<ma_uint64>(fadeInSec * 1000.0f));
		}
		ma_sound_start(snd.get());
		m_oneShots.push_back(std::move(snd));
	}

	/// @brief key (ループ音なら id、番号付きなら番号) で取得できる音を鳴らす。同じ key が鳴っていれば先頭から
	///        鳴らし直さず、音量・ピッチ・パンだけを変える。長押しの「押している間ずっと」のように長さが
	///        入力で決まる音や、同じ音を何本も鳴らして 1 本ずつ止めたい音 (ABI v45 の handle) に使う。
	void playKeyed(const std::string& key, const std::string& path, const detail::MiniaudioKeyedSounds::Params& p) {
		if (!m_initialized) { return; }
		m_keyed.play(m_engine, key, path, p);
	}

	/// @brief key の音の音量・ピッチ・パンを変える (鳴っていなければ何もしない)。
	void updateKeyed(const std::string& key, float volume, float pitchScale, float pan) {
		m_keyed.update(key, volume, pitchScale, pan);
	}

	/// @brief key の音を止める。fadeOutSec > 0 で減衰させてから止める (解放は update() が行う)。
	void stopKeyed(const std::string& key, float fadeOutSec) {
		if (!m_initialized) { return; }
		m_keyed.stop(m_engine, key, fadeOutSec);
	}

	/// @brief key の音が鳴っているか。
	[[nodiscard]] bool keyedPlaying(const std::string& key) const { return m_keyed.playing(key); }

	/// @brief 効果音を「マスタークロック上の絶対時刻 atSec」にサンプル精度で予約再生する (v19)。
	/// @details ma_engine のグローバルクロックが atSec に達した瞬間に backend がミックスを開始する
	///          (フレーム量子化なし)。atSec <= 現在時刻なら即時再生される。
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
			// atSec は masterTimeSec() = m_transportClock が基準。ma_sound の開始時刻は
			// engine 自身の内部クロックで判定されるため、両者の「今」の差分を
			// engine クロックへ加えて変換する (2 つのクロックが乖離しうるため)。
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

	/// @brief playSoundScheduled が atSec をサンプル精度で守るか (#F2)。
	/// @details ma_sound_set_start_time_in_pcm_frames による実装なので、常に true。
	[[nodiscard]] bool supportsScheduledPlayback() const noexcept { return true; }

	/// @brief path のサウンドをあらかじめ poolSize 個分プールする (#F3)。
	/// @details playPooled() の hot path (拍ごと) で make_unique<ma_sound> と
	///          ファイルの再読み込みを発生させないため、事前に確保する。同じ decode 済みデータを
	///          ma_sound_init_copy で共有するので、増えるメモリは voice の管理領域のみ。
	///          すでにプール済みの path では何もせず true を返す。
	/// @return プールの作成に成功したか (ファイルが読めない場合は false)
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

	/// @brief プール済みの SE を鳴らす (#F3)。未プールの path は playSoundEx にフォールバックする。
	/// @details 再生中でない voice を探し、なければ最古の voice (round robin) の再生を止めて
	///          先頭から鳴らし直す。拍ごとに呼ばれる hot path でも allocation は発生しない。
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
	/// @details %TEMP% への一時ファイルの書き出しを経由せず、埋め込みリソース (RCDATA 等)
	///          を直接鳴らすための経路。decoder はバイト列を直接参照し続けるため、
	///          再生が終わるまでバイト列自体もここでコピーして保持する。
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

	/// @brief ボイス (同時 1 トラック) を再生する (#F6)。既存のボイスは先頭から再生せず、即座に差し替える。
	/// @details BGM とは独立したスロットに保持する。meterChannels() が "voice" 種別として
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
			m_keyed.retire(std::move(m_voice));
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
	///          (ma_engine_get_time_in_pcm_frames) は、鳴っている音がない区間で進まないことがある。
	///          m_transportClock は device が実際に出力を要求したフレーム数だけを数えるので、
	///          無音でも単調に増加し、device が停止すれば呼ばれなくなり自然に止まる。未初期化時は 0。
	[[nodiscard]] double masterTimeSec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		const ma_uint32 sr = ma_engine_get_sample_rate(e);
		return m_transportClock.seconds(sr);
	}

	/// @brief 出力レイテンシ (秒)。masterTimeSec() はデバイスへ送った位置なので、耳へ届くのはこの値の分だけ後になる。
	/// @details 自前のバッファ (period × periodSize) の先に、OS のミキサがもう一段ある。
	///          共有モードの WASAPI では後者が 10ms 前後あり、無視すると判定窓が実際より
	///          手前に来る。プレイヤーは音に合わせて叩くので、そのずれがそのまま「遅く
	///          叩いている」という判定になる。デバイスへ直接問い合わせて加える。
	[[nodiscard]] double outputLatencySec() const noexcept {
		if (!m_initialized) return 0.0;
		auto* e = const_cast<ma_engine*>(&m_engine);
		ma_device* dev = ma_engine_get_device(e);
		if (dev == nullptr) { return 0.0; }
		const ma_uint32 sr = dev->playback.internalSampleRate;
		if (sr == 0) { return 0.0; }
		return static_cast<double>(detail::deviceBufferFrames(dev)) / static_cast<double>(sr)
		     + detail::backendLatencySec(dev);
	}

	/// @brief BGM を再生する (ループ・音量指定)
	/// @details 永続的な ma_sound を 1 つだけ持ち、再生するたびに前の BGM を停止・破棄する。
	///          ストリーミング再生 (MA_SOUND_FLAG_STREAM) により、長尺ファイルでもメモリ使用量を抑える。
	/// @param path ファイルパス
	/// @param volume ボリューム [0.0, 1.0]
	/// @param loop ループ再生するか
	void playMusic(const std::string& path, float volume, bool loop) {
		playMusicEx(path, volume, loop, 0.0f);
	}

	/// @brief fade-in を指定して BGM を再生する (#20)。fadeInSec > 0 で 0→volume へ変化させる。
	void playMusicEx(const std::string& path, float volume, bool loop, float fadeInSec) {
		if (!m_initialized) { return; }
		stopMusic();
		if (ma_sound_init_from_file(&m_engine, path.c_str(), MA_SOUND_FLAG_STREAM,
		                            nullptr, nullptr, &m_music) != MA_SUCCESS) {
			// 無音のままでは原因が分からないので、path 単位で初回のみ警告する (R-01 級)
			mitiru::debug::warnOnceFix("audio.music:" + path,
				"音声ファイル " + path + " が見つからない/読めない",
				"パスが assets 相対で間違っているか、対応フォーマット外",
				"パスを確認し、対応フォーマット (wav/ogg/mp3 等) に変換する");
			return;
		}
		m_musicBus.route(m_music);
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

	/// @brief 毎フレーム行う定期メンテナンス (#51)。
	/// @details (a) 終了した one-shot voice を retire リストへ移し、kRetireDelay の経過後に
	///          uninit する (#52 根治: device thread が mix 中に参照しうる期間を確実に過ぎて
	///          から解放する遅延解放)。(b) stopMusicFade で設定した fade-out の残りフレームを
	///          減らし、完了したら music voice を uninit する。uninit しないと、無音のまま
	///          loop voice が動き続ける。固定ステップ (~60Hz) の cadence が前提。
	void update() {
		if (!m_initialized) { return; }
		reapFinishedOneShots();
		reapFinishedMemorySounds();
		m_keyed.reap();  // 減衰させて止めた音と、鳴り終わった番号付きの one-shot を回収する
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
	/// @details fadeOutSec > 0 のとき volume→0 の fade-out を設定するが、miniaudio の
	///          fade はサウンドの寿命と独立しているので、十分に時間が経過したら通常の stop を呼ぶ前提。
	///          簡単にするため、v1 では fade を設定してもすぐには stop しない (呼び出し側が次のフレームで
	///          通常の playMusic / stopMusic を呼べる)。すぐに「次の BGM へ切替」する crossfade は
	///          stopMusicFade(prevFadeOut) → playMusicEx(newId, vol, loop, newFadeIn) で実現する。
	void stopMusicFade(float fadeOutSec) {
		if (!m_musicActive) { return; }
		if (fadeOutSec <= 0.0f) { stopMusic(); return; }
		ma_sound_set_fade_in_milliseconds(
			&m_music, ma_sound_get_volume(&m_music), 0.0f,
			static_cast<ma_uint64>(fadeOutSec * 1000.0f));
		// fade の終了後に自動で uninit する機能は miniaudio が直接提供しないため、update() が残りフレームを
		// 数え、完了時に stopMusic() を呼ぶ (#51)。これがないと、無音の loop voice が動き続けた。
		// +6 frame の余裕を設け、fade を確実に最後まで再生する。約 60Hz が前提。
		m_musicFadeOutFrames = static_cast<int>(fadeOutSec * 60.0f) + 6;
	}

	/// @brief 鳴っている BGM の音量を変える (バス音量を掛け直す)。duck 後に戻す音量もこの値になる。
	void setMusicVolume(float volume) {
		m_musicBaseVolume = volume;
		if (!m_initialized || !m_musicActive) { return; }
		ma_sound_set_fade_in_milliseconds(&m_music, -1.0f, volume, 40);
	}

	/// @brief BGM を一時的に mul 倍まで下げ、durSec かけて元の音量へ戻す (#34、ducking heuristic)。
	/// @details 大きな SE の再生中だけ BGM を下げてインパクトを強める用途。BGM の未再生時は何もしない。
	void duckMusic(float mul, float durSec) {
		if (!m_initialized || !m_musicActive) { return; }
		if (mul <= 0.0f || mul >= 1.0f || durSec <= 0.0f) { return; }
		// 戻す音量は「本来の音量 (base)」であり、「現在の音量」ではない。current を基準に
		// すると、SE の連打で duck 中に再び duck され、base より小さい値に ×mul が重なり、
		// BGM が段階的に 0 まで下がって聞こえなくなる (実機バグ)。base 基準なら何度押しても
		// 「base×mul まで下げて base へ戻す」動作で一定に保たれる。
		const float base   = m_musicBaseVolume;
		const float ducked = base * mul;
		// すぐに ducked へ変更し、durSec かけて base へ fade in することで、一瞬下がって徐々に戻る。
		ma_sound_set_volume(&m_music, ducked);
		ma_sound_set_fade_in_milliseconds(
			&m_music, ducked, base, static_cast<ma_uint64>(durSec * 1000.0f));
	}

	/// @brief 再生中の BGM を一時停止する (v19)。ma_sound_stop は再生位置を保持するので、
	///        resumeMusic() で続きから再生する (uninit する stopMusic() とは別の処理)。
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

	/// @brief すべてのサウンドを停止する
	void stopAll() {
		if (!m_initialized) return;
		ma_engine_stop(&m_engine);
	}

	/// @brief エンジンを再開する（stopAll 後に使用）
	void resume() {
		if (!m_initialized) return;
		ma_engine_start(&m_engine);
	}

	/// @brief 再生中のチャンネルのメーター値を列挙する
	/// @details BGM (m_music) + ボイス (m_voice、#F6) + 終了前の one-shot SE を、
	///          それぞれに設定された実効音量で報告する。mitiru_mixer 窓の per-channel VU 用
	///          (host が host_module 経由で読み取る)。
	/// @return チャンネルごとの { 種別, レベル } の配列 (voice には asset / pan / 残り秒も入る)
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
	void init(const ma_engine_config& config) {
		// device が実際にコールバックへ渡すフレーム数を transportDataCallback 経由で
		// 積算するため、engine を生成する前に &m_engine をレジストリへ登録しておく
		// (ma_engine_init は成功すると device thread を即座に起動しうる、#F1)。
		{
			std::lock_guard<std::mutex> lock(registryMutex());
			registry()[&m_engine] = this;
		}
		if (ma_engine_init(&config, &m_engine) != MA_SUCCESS) {
			std::lock_guard<std::mutex> lock(registryMutex());
			registry().erase(&m_engine);
			return;
		}
		m_initialized = true;
		m_musicBus.init(m_engine);
	}

	/// @brief 再生中のボイスのメーター 1 件 (K1: mixer 窓の「voice 一覧」用)。
	/// @details 残り秒は length - cursor。MA_SOUND_FLAG_DECODE ですべて展開しているので、
	///          length は O(1)。ループ指定または長さが不明なら、負のまま (= 不明)。
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
	///          そこから呼び出し元のインスタンスへ戻すために使う。関数内の static はインライン
	///          関数 (クラス内定義) のため、TU をまたいで単一の実体になる。
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
	///          device が要求したフレーム数を無条件に m_transportClock へ加算する。
	///          ミックス内容 (無音か否か) にかかわらず device の呼び出しそのものを数えるので、
	///          engine の内部クロックが無音区間で止まる場合でも連続して進む。
	static void transportDataCallback(ma_device* pDevice, void* pFramesOut, const void* pFramesIn, ma_uint32 frameCount) {
		(void)pFramesIn;
		auto* engine = static_cast<ma_engine*>(pDevice->pUserData);
		ma_engine_read_pcm_frames(engine, pFramesOut, frameCount, nullptr);
		std::lock_guard<std::mutex> lock(registryMutex());
		if (auto it = registry().find(engine); it != registry().end()) {
			it->second->m_transportClock.addFrames(frameCount);
		}
	}

	/// @brief 解放待ちの one-shot voice (#52)。
	/// @details ma_sound_at_end の検出後も device thread が同じ mix 周期内で voice を
	///          参照している可能性があるため、すぐには uninit せず retiredAt から待機させる。
	struct RetiredVoice {
		std::unique_ptr<ma_sound> snd;
		std::chrono::steady_clock::time_point retiredAt;
	};

	/// @brief preloadPooled() で作った、再生するたびに使い回す voice の集合 (#F3)。
	struct SoundPool {
		ma_sound templateSound{};                        ///< ma_sound_init_copy の元
		std::vector<std::unique_ptr<ma_sound>> voices;    ///< templateSound の複製
		std::size_t nextIndex = 0;                        ///< 全 voice 使用中のときに奪う順番
	};

	/// @brief playFromMemory() 用の再生インスタンス (#F4)。
	/// @details decoder はバイト列を直接参照するため、再生が終わるまで両方を同じ寿命で保持する。
	struct MemorySound {
		std::vector<std::uint8_t> bytes;
		ma_decoder decoder{};
		std::unique_ptr<ma_sound> sound;
	};

	/// @brief kRetireDelay の経過を待ってから MemorySound を解放するための保持 (#F4、#52 と同じ理由)。
	struct RetiredMemorySound {
		std::unique_ptr<MemorySound> mem;
		std::chrono::steady_clock::time_point retiredAt;
	};

	/// 遅延解放の待機時間。device の 1 mix 周期 (数十 ms) を確実に上回る値 (#52)。
	static constexpr std::chrono::milliseconds kRetireDelay{400};

	/// @brief 終了済みの one-shot を retire へ移し、待機を終えた retire を解放する (#52)
	void reapFinishedOneShots() {
		retireFinishedOneShots();
		drainRetired();
	}

	/// @brief 終了済みの one-shot を retire リストへ移す (uninit はまだ行わない)
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

	/// @brief retire から kRetireDelay が経過した voice を uninit する
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

	/// @brief 待機中の retire を待たず、同期的にすべて uninit する (shutdown 専用)
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

	/// @brief 待機中の MemorySound retire を待たず、同期的にすべて解放する (shutdown 専用)
	void flushMemorySounds() {
		for (auto& m : m_memorySounds) { ma_sound_uninit(m->sound.get()); ma_decoder_uninit(&m->decoder); }
		m_memorySounds.clear();
		for (auto& r : m_retiredMemory) { ma_sound_uninit(r.mem->sound.get()); ma_decoder_uninit(&r.mem->decoder); }
		m_retiredMemory.clear();
	}

	/// @brief preloadPooled() で作ったすべてのプールを解放する (shutdown 専用)
	void releasePools() {
		for (auto& kv : m_pools) {
			for (auto& v : kv.second->voices) { ma_sound_uninit(v.get()); }
			ma_sound_uninit(&kv.second->templateSound);
		}
		m_pools.clear();
	}

	/// @brief すべての voice (BGM + one-shot + retire 待ち) を解放する (shutdown 専用)
	/// @brief key で取得できる音 (ループ音・番号付きの音・減衰中の音) をすべて解放する。
	void releaseLoops() { m_keyed.releaseAll(); }

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
	MusicLowPassBus m_musicBus;            ///< m_music の出力先 (low-pass 経由 / 直結)
	ma_sound m_music{};
	std::unique_ptr<ma_sound> m_voice;  ///< 再生中のボイス（同時1トラック、#F6）
	std::string m_voicePath;            ///< m_voice の元ファイル (meterChannels の asset 名、K1)
	bool m_musicActive = false;
	float m_musicBaseVolume = 1.0f;  ///< duck していない本来の BGM 音量。duck の復帰先 (#34 の雪だるま化防止)
	bool m_musicPaused = false;      ///< pauseMusic() 中か (resumeMusic() で false。v19)
	int  m_musicFadeOutFrames = 0;  ///< >0 の間 update() が減算し、0 で music を uninit (#51)
	detail::MiniaudioKeyedSounds m_keyed;  ///< key で引ける音 (ループ音・番号付きの音) と減衰中の音
	std::vector<std::unique_ptr<ma_sound>> m_oneShots;
	std::vector<RetiredVoice> m_retired;  ///< 遅延解放待ち (#52)
	std::unordered_map<std::string, std::unique_ptr<SoundPool>> m_pools;  ///< preloadPooled() 済みプール (#F3)
	std::vector<std::unique_ptr<MemorySound>> m_memorySounds;  ///< playFromMemory() 再生中 (#F4)
	std::vector<RetiredMemorySound> m_retiredMemory;           ///< 同、遅延解放待ち (#F4)
};

} // namespace mitiru::audio
