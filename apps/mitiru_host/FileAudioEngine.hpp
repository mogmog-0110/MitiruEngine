#pragma once

/// @file FileAudioEngine.hpp
/// @brief 論理 sound id を game の assets/audio/ のファイルに解決し、miniaudio で鳴らす host の IAudioEngine
/// @details game は操作せず SoundIntent を書くだけ。未知の id は黙って失敗させず id ごとに 1 度知らせる。
///          assets/audio/music.json があれば、その区間の id の BGM と、スティンガーの id の SE は
///          曲の規則 (MusicDirector) で鳴らす (docs/ADAPTIVE_MUSIC.md)。assets/audio/mix.json の
///          ダッキングと場所ごとの残響は毎ステップ MiniaudioMixDriver が反映する (docs/AUDIO_MIXING.md)。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/mix/MiniaudioMixDriver.hpp>
#include <mitiru/audio/music/MiniaudioMusicPlayer.hpp>
#include <mitiru/audio/music/MusicClock.hpp>
#include <mitiru/audio/SoundPreloads.hpp>
#include <mitiru/audio/music/MusicManifestJson.hpp>
#include <mitiru/audio/sfx/SfxBankJson.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::host
{

class FileAudioEngine final : public mitiru::audio::IAudioEngine
{
public:
	explicit FileAudioEngine(std::filesystem::path baseDir)
		: m_baseDir(std::move(baseDir)) { loadMix(); loadSfxBank(); loadMusic(false); }

	/// --audio-capture: デバイスを開かず、renderOffline() で読まれた分だけ進む。
	FileAudioEngine(std::filesystem::path baseDir, mitiru::audio::MiniaudioEngine::Offline offline)
		: m_baseDir(std::move(baseDir)), m_engine(offline) { loadMix(); loadSfxBank(); loadMusic(true); }

	~FileAudioEngine() override { m_engine.attachMusicSound(nullptr); }

	ma_uint64 renderOffline(float* out, ma_uint64 frames) { return m_engine.renderOffline(out, frames); }

	void playSound(std::string_view id) override { playSoundEx(id, 1.0f, 1.0f, 0.0f); }
	void playSound(std::string_view id, float vol) override { playSoundEx(id, vol, 1.0f, 0.0f); }
	void stopSound(std::string_view id) override { stopSoundFade(id, 0.0f); }
	bool preloadSound(std::string_view id) override { return m_preloads.preload(m_engine.rawEngine(), resolvePlayPath(id)); }
	bool releaseSound(std::string_view id) override { return m_preloads.release(resolvePlayPath(id)); }
	[[nodiscard]] std::size_t pendingSoundLoads() const override { return m_preloads.pending(); }
	void playMusic(std::string_view id) override { playMusicEx(id, 1.0f, true, 0.0f); }
	void playMusic(std::string_view id, float vol, bool loop) override { playMusicEx(id, vol, loop, 0.0f); }
	void stopMusic() override { stopMusicFade(0.0f); }
	void setVolume(float v) override { m_engine.setMasterVolume(v); }
	[[nodiscard]] bool isPlaying(std::string_view) const override { return false; }

	// v6 拡張 (#19/#20): pitch / fade を渡す。
	void playSoundEx(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		if (playStinger(id)) { return; }
		playByIdEx(id, vol, pitch, fadeIn, /*music=*/false, false);
	}
	void stopSoundFade(std::string_view id, float fadeOutSec) override
	{
		// 停止できるのはループで再生したものだけ。one-shot は id で記録していない。
		m_engine.stopKeyed(instanceKey(0, id), fadeOutSec);
	}

	// v45: 番号付きの再生・パン (番号 0 のループは id で取得)。
	void playSoundInstance(std::string_view id, const mitiru::audio::SoundPlayback& p) override
	{
		if (playStinger(id)) { return; }
		if (p.handle == 0 && !p.loop)
		{
			playByIdEx(id, p.volume, p.pitch, p.fadeInSec, /*music=*/false, false, 0.0, p.pan);
			return;
		}
		const std::string playPath = resolvePlayPath(id);
		if (playPath.empty()) { reportMissing(id); return; }
		m_engine.playKeyed(instanceKey(p.handle, id), playPath,
		                   {p.volume, p.pitch, p.fadeInSec, p.pan, p.loop, p.startSec, p.lowpassHz, p.spatial, p.direction});
		if (!p.loop) { m_mix.onSoundPlayed(mitiru::audio::mix::MixBus::Sfx, p.volume); }  // 大きな効果音で BGM を下げる規則のきっかけ
	}
	void updateSoundInstance(std::uint32_t handle, std::string_view id, float vol, float pitch, float pan) override
	{
		m_engine.updateKeyed(instanceKey(handle, id), vol, pitch, pan);
	}
	void updateSoundInstanceEx(std::uint32_t handle, std::string_view id, const mitiru::audio::SoundPlayback& p) override
	{
		m_engine.updateKeyed(instanceKey(handle, id), p.volume, p.pitch, p.pan, p.lowpassHz, p.spatial ? &p.direction : nullptr);
	}
	/// 効果音の声の寿命に使う。一度測った id は覚えておく (全部を展開して測るので、毎回は測らない)
	[[nodiscard]] double soundLengthSec(std::string_view id) override
	{
		const std::string key(id);
		if (const auto it = m_lengths.find(key); it != m_lengths.end()) { return it->second; }
		const std::string path = resolvePlayPath(id);
		float len = 0.0f;
		ma_engine* engine = m_engine.rawEngine();
		ma_sound probe{};
		if (engine != nullptr && !path.empty()
		    && ma_sound_init_from_file(engine, path.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT,
		                               nullptr, nullptr, &probe) == MA_SUCCESS)
		{
			ma_sound_get_length_in_seconds(&probe, &len);
			ma_sound_uninit(&probe);
		}
		m_lengths.emplace(key, static_cast<double>(len));
		return len;
	}
	void stopSoundInstance(std::uint32_t handle, std::string_view id, float fadeOutSec) override
	{
		m_engine.stopKeyed(instanceKey(handle, id), fadeOutSec);
	}
	void setMusicVolume(float volume) override
	{
		m_engine.setMusicVolume(volume);
		m_musicVolume = volume;
		pushMusicGain();
	}
	void playMusicEx(std::string_view id, float vol, bool loop, float fadeIn) override
	{
		if (const int seg = musicSegment(id); seg >= 0)
		{
			if (m_engine.musicActive()) { m_engine.stopMusicFade(fadeIn); }
			m_musicVolume = vol;
			pushMusicGain();
			m_music.director().request(seg);
			return;
		}
		stopInteractiveMusic(0.0f);
		playByIdEx(id, vol, 1.0f, fadeIn, /*music=*/true, loop);
	}
	void stopMusicFade(float fadeOutSec) override
	{
		m_engine.stopMusicFade(fadeOutSec);
		stopInteractiveMusic(fadeOutSec);
	}
	/// 曲の区間どうしの切り替えは規則で待つので、router に前の BGM を先に止めさせない
	[[nodiscard]] bool ownsMusicTransition(std::string_view id) const override { return musicSegment(id) >= 0; }

	/// 場所で決まる残響 (mix.json の箱) を聞き手の位置で選ぶ
	void setListener(const mitiru::audio::AudioListener& l) override
	{
		m_mix.setListener({l.position.x, l.position.y, l.position.z});
	}

	// 毎フレームの定期的な後処理 (終了した SE voice の回収 + fade-out が完了した music の解放、#51)、
	// ダッキングと残響の反映、曲の命令出し。
	void update() override
	{
		m_engine.update();
		const float duck = m_mix.step(m_engine, kStepSec);
		if (duck != m_musicDuck) { m_musicDuck = duck; pushMusicGain(); }
		m_music.step();
	}

	// 再生中の voice のメーターを miniaudio backend からそのまま渡す (mitiru_mixer 窓用)。
	[[nodiscard]] std::vector<mitiru::audio::ChannelMeter> meterChannels() const override
	{
		std::vector<mitiru::audio::ChannelMeter> out = m_engine.meterChannels();
		for (auto& m : out)
		{
			// backend が把握しているのは path だけ。game が渡した論理 id はここで追加する (K1)。
			if (std::strcmp(m.kind, "voice") == 0)
			{
				mitiru::audio::ChannelMeter::copyTo(m.id, sizeof(m.id), m_voiceId.c_str());
			}
		}
		return out;
	}

	// マスター再生クロック (秒) を miniaudio からそのまま渡す。game が音声クロックを基準に判定するため。
	[[nodiscard]] double masterTimeSec() const noexcept override { return m_engine.masterTimeSec(); }

	// v19: 出力レイテンシ / BGM transport / サンプル精度予約を miniaudio backend へ渡す。
	[[nodiscard]] double outputLatencySec() const noexcept override { return m_engine.outputLatencySec(); }
	void pauseMusic() override { m_engine.pauseMusic(); }
	void resumeMusic() override { m_engine.resumeMusic(); }
	void seekMusic(double positionSec) override { m_engine.seekMusic(positionSec); }
	void setMusicLowPass(float cutoffHz) override { m_engine.setMusicLowPass(cutoffHz); }
	void playSoundScheduled(std::string_view id, double atSec, float vol, float pitch) override
	{
		playByIdEx(id, vol, pitch, 0.0f, /*music=*/false, false, atSec);
	}

	// K1: Voice (category=2) は BGM/SE から独立した 1 本のスロット。id を記録し、
	// meterChannels の "voice" 行に game が渡した名前を載せる (mixer 窓の voice 一覧)。
	void playVoiceEx(std::string_view id, float vol, float pitch, float fadeIn) override
	{
		const std::string playPath = resolvePlayPath(id);
		if (playPath.empty()) { reportMissing(id); return; }
		m_voiceId = std::string(id);
		m_engine.playVoiceEx(playPath, vol, pitch, fadeIn);
	}
	void stopVoiceFade(float fadeOutSec) override { m_engine.stopVoice(fadeOutSec); }

	[[nodiscard]] std::shared_ptr<const mitiru::audio::sfx::SfxBank> sfxBank() const override { return m_sfxBank; }

	void setMusicIntensity(float intensity) override
	{
		if (m_music.loaded()) { m_music.director().setIntensity(intensity); }
	}
	bool forceReverbZone(std::string_view id) override { return m_mix.forceReverbZone(id); }

	/// 命令は先読みのぶん先に出ているので、音声スレッドが読んだ位置まで戻して耳に届いている拍にする
	bool musicClock(mitiru::audio::MusicClockInfo& out) override
	{
		if (!m_music.loaded()) { return false; }
		auto& director = m_music.director();
		const auto p = director.position();
		if (!p.playing || p.segment < 0) { return false; }
		const std::int64_t ahead = std::max<std::int64_t>(director.now() - m_music.renderer().position(), 0);
		const auto& seg = director.manifest().segments[static_cast<std::size_t>(p.segment)];
		out = mitiru::audio::music::musicClockOf(seg, p.segment, p.sampleInSegment - ahead,
		                                         ma_engine_get_sample_rate(m_engine.rawEngine()));
		return true;
	}

	/// @brief 曲の C++ API (強さ・スティンガーなど)。music.json が無ければ loaded() が false
	[[nodiscard]] mitiru::audio::music::MiniaudioMusicPlayer& music() noexcept { return m_music; }
	/// @brief ダッキングと残響の C++ API (残響の箱を名前で決める forceReverbZone など)
	[[nodiscard]] mitiru::audio::mix::MiniaudioMixDriver& mix() noexcept { return m_mix; }

private:
	/// 音の時計で動くデバイスでは、update の間隔と音声スレッドの読みの揺れを越えるだけ先に命令を出す (約 85 ms)
	static constexpr std::int64_t kDeviceMusicLookahead = 4096;
	/// 曲全体の音量の変化はこの時間で滑らかにつなぐ (1 フレームで段差を付けない)
	static constexpr float kMusicGainRampSec = 0.02f;
	/// update() は固定ステップ (60 Hz) で呼ばれる (#51 と同じ前提)
	static constexpr float kStepSec = 1.0f / 60.0f;

	void pushMusicGain()
	{
		if (m_music.loaded()) { m_music.director().setMasterGain(m_musicVolume * m_musicDuck, kMusicGainRampSec); }
	}

	/// assets/audio/sounds.json を読む。無ければ既定の鳴らし方のまま (router の VoiceManager が持つ既定)
	void loadSfxBank()
	{
		const auto text = mitiru::vfs::readGlobal((m_baseDir / "sounds.json").generic_string());
		if (!text) { return; }
		auto parsed = mitiru::audio::sfx::parseSfxBank(std::string_view(
			reinterpret_cast<const char*>(text->data()), text->size()));
		if (!parsed.ok()) { reportBadJson("sounds.json", parsed.error); return; }
		m_sfxBank = std::make_shared<const mitiru::audio::sfx::SfxBank>(std::move(parsed.bank));
	}

	/// assets/audio/mix.json を読む。無ければ既定のダッキング (mix::defaultDuckRules) のまま
	void loadMix()
	{
		const auto text = mitiru::vfs::readGlobal((m_baseDir / "mix.json").generic_string());
		if (!text) { return; }
		auto parsed = mitiru::audio::mix::parseMixConfig(std::string_view(
			reinterpret_cast<const char*>(text->data()), text->size()));
		if (!parsed.ok()) { reportBadJson("mix.json", parsed.error); return; }
		m_mix.configure(std::move(parsed.config));
	}

	[[nodiscard]] int musicSegment(std::string_view id) const
	{
		return m_music.loaded() ? m_musicManifest->segmentIndex(id) : -1;
	}

	bool playStinger(std::string_view id)
	{
		if (!m_music.loaded()) { return false; }
		const int s = m_musicManifest->stingerIndex(id);
		if (s < 0) { return false; }
		m_music.director().playStinger(s);
		return true;
	}

	void stopInteractiveMusic(float fadeOutSec)
	{
		if (m_music.loaded() && m_music.director().currentSegment() >= 0) { m_music.director().stop(fadeOutSec); }
	}

	/// assets/audio/music.json を読み、曲の音源をメモリへ置く。誤りは stderr に出して曲なしで続ける
	void loadMusic(bool offline)
	{
		const auto text = mitiru::vfs::readGlobal((m_baseDir / "music.json").generic_string());
		ma_engine* engine = m_engine.rawEngine();
		if (!text || engine == nullptr) { return; }
		auto parsed = mitiru::audio::music::parseMusicManifest(std::string_view(
			reinterpret_cast<const char*>(text->data()), text->size()));
		if (!parsed.ok()) { reportBadJson("music.json", parsed.error); return; }
		m_musicManifest = std::make_shared<const mitiru::audio::music::MusicManifest>(std::move(parsed.manifest));
		const ma_uint32 rate = ma_engine_get_sample_rate(engine);
		const std::int64_t lookahead = offline ? static_cast<std::int64_t>((rate + 59) / 60) : kDeviceMusicLookahead;
		const std::string err = m_music.load(*engine, m_musicManifest,
			[this](std::string_view file) { return readAudioBytes(file); }, lookahead);
		if (!err.empty()) { mitiru::console::noticef("曲の準備に失敗しました (%s)。曲なしで続けます。", err.c_str()); return; }
		m_engine.attachMusicSound(m_music.sound());
	}

	std::vector<std::uint8_t> readAudioBytes(std::string_view file) const
	{
		for (const char* ext : {".wav", ".ogg", ".mp3", ".flac"})
		{
			if (auto bytes = mitiru::vfs::readGlobal((m_baseDir / (std::string(file) + ext)).generic_string()))
			{
				return std::move(*bytes);
			}
		}
		return {};
	}

	/// 論理 id を、再生に渡す実ファイルパスへ解決する。pack mount 時はパックから取り出した
	/// temp ファイル、dev (未 mount) 時は disk のファイル。見つからなければ空。
	std::string resolvePlayPath(std::string_view id)
	{
		for (const char* ext : {".wav", ".ogg", ".mp3", ".flac"})
		{
			const auto        p   = m_baseDir / (std::string(id) + ext);
			const std::string key = p.generic_string();
			if (mitiru::vfs::hasGlobalMount())
			{
				const std::string packed = materializeFromPack(key, ext);
				if (!packed.empty()) { return packed; }
			}
			else if (std::filesystem::exists(p))
			{
				return p.string();
			}
		}
		return {};
	}

	/// 番号付きは番号、番号のないループは id で取得する (同じ id のループは 1 本、番号付きは何本でも)。
	static std::string instanceKey(std::uint32_t handle, std::string_view id)
	{
		return (handle != 0) ? "#" + std::to_string(handle) : "id:" + std::string(id);
	}

	void reportBadJson(const char* file, const std::string& error) const
	{
		mitiru::console::noticef("%s を読めません。%s。", (m_baseDir / file).string().c_str(), error.c_str());
	}

	void reportMissing(std::string_view id) const
	{
		const std::string name(id);
		mitiru::debug::warnOnce("audio.missing:" + name, "音 " + name + " のファイルが " + m_baseDir.string()
			+ " に見つかりません。" + name + ".wav か .ogg / .mp3 / .flac を置いてください。");
	}

	void playByIdEx(std::string_view id, float volume, float pitch, float fadeIn,
	                bool music, bool loop, double scheduleSec = 0.0, float pan = 0.0f)
	{
		const std::string playPath = resolvePlayPath(id);
		if (playPath.empty()) { reportMissing(id); return; }

		if (music) { m_engine.playMusicEx(playPath, volume, loop, fadeIn); }
		else if (scheduleSec > 0.0)
		{
			// v19: サンプル精度予約 (リズムゲームの「次の拍で鳴らす」)。ducking は予約による再生開始を
			// 事前に把握できないため適用しない (即時 SE 用)。
			m_engine.playSoundScheduled(playPath, scheduleSec, volume, pitch);
		}
		else
		{
			m_engine.playSoundEx(playPath, volume, pitch, fadeIn, pan);
			m_mix.onSoundPlayed(mitiru::audio::mix::MixBus::Sfx, volume);  // 大きな効果音で BGM を下げる規則のきっかけ
		}
	}

	/// pack 内の音声 (key) を %TEMP% に一度だけ取り出し、その path を返す。
	/// 配布物に個別の音声ファイルを置かないための経路。pack に無ければ空。
	std::string materializeFromPack(const std::string& key, const char* ext)
	{
		if (auto it = m_audioTemp.find(key); it != m_audioTemp.end()) { return it->second; }
		const auto bytes = mitiru::vfs::readGlobal(key);
		if (!bytes) { return {}; }
		// key + pack の実体 (size/mtime) の FNV-1a で temp 名を作る。同一の pack なら
		// run をまたいで再利用し、pack の差し替え時は名前を変えて古いバイト列が再生される問題を解消する。
		std::uint64_t h = 14695981039346656037ULL;
		for (unsigned char c : key) { h = (h ^ c) * 1099511628211ULL; }
		const std::uint64_t stamp = packStamp();
		for (int i = 0; i < 8; ++i)
		{
			h = (h ^ ((stamp >> (i * 8)) & 0xFFu)) * 1099511628211ULL;
		}
		char name[64];
		std::snprintf(name, sizeof(name), "mitiru_aud_%016llx%s",
		              static_cast<unsigned long long>(h), ext);
		const auto out = std::filesystem::temp_directory_path() / name;
		std::error_code ec;
		if (!std::filesystem::exists(out, ec))
		{
			std::ofstream f(out, std::ios::binary | std::ios::trunc);
			if (!f) { return {}; }
			if (!bytes->empty())
			{
				f.write(reinterpret_cast<const char*>(bytes->data()),
				        static_cast<std::streamsize>(bytes->size()));
			}
		}
		const std::string s = out.string();
		m_audioTemp.emplace(key, s);
		return s;
	}

	/// pack ファイル (MITIRU_PACK。host が env を統一する前の名残として MITIRU_ASSET_PACK も
	/// 後方互換のため確認する) の size + mtime から作る指紋。temp 名に含め、assets.mtpak の差し替え後に
	/// 古い temp を参照しないようにする。stat は初回のみ。
	std::uint64_t packStamp()
	{
		if (m_packStampInit) { return m_packStamp; }
		m_packStampInit = true;
		const char* packPath = std::getenv("MITIRU_PACK");
		if (packPath == nullptr || packPath[0] == '\0') { packPath = std::getenv("MITIRU_ASSET_PACK"); }
		if (packPath != nullptr && packPath[0] != '\0')
		{
			std::error_code ec;
			const std::filesystem::path p(packPath);
			if (const auto size = std::filesystem::file_size(p, ec); !ec)
			{
				m_packStamp = static_cast<std::uint64_t>(size) * 1099511628211ULL;
			}
			if (const auto mtime = std::filesystem::last_write_time(p, ec); !ec)
			{
				m_packStamp ^= static_cast<std::uint64_t>(mtime.time_since_epoch().count());
			}
		}
		return m_packStamp;
	}

	std::filesystem::path                        m_baseDir;
	std::string m_voiceId;  ///< 直近に鳴らした voice の論理 id (meterChannels 用、K1)
	mitiru::audio::MiniaudioEngine               m_engine;
	std::shared_ptr<const mitiru::audio::music::MusicManifest> m_musicManifest;
	mitiru::audio::music::MiniaudioMusicPlayer   m_music;  ///< m_engine より後に置き、先に壊す
	mitiru::audio::mix::MiniaudioMixDriver       m_mix;
	float m_musicVolume = 1.0f;  ///< 曲 (music.json) の音量。バス音量を掛けた値
	float m_musicDuck   = 1.0f;  ///< ダッキングで曲に掛ける音量
	std::unordered_map<std::string, std::string> m_audioTemp;  ///< pack→temp 取り出しキャッシュ
	std::unordered_map<std::string, double>      m_lengths;    ///< soundLengthSec で測った長さ
	std::shared_ptr<const mitiru::audio::sfx::SfxBank> m_sfxBank;  ///< sounds.json (無ければ空)
	std::uint64_t m_packStamp     = 0;      ///< pack 指紋 (size/mtime FNV 混合)
	bool          m_packStampInit = false;  ///< packStamp 計算済みか
	mitiru::audio::SoundPreloads m_preloads;  ///< m_engine より後に置き、先に壊す
};

}  // namespace mitiru::host
