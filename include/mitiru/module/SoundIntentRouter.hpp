#pragma once

/// @file SoundIntentRouter.hpp
/// @brief SoundIntent (DLL → host の intent) を IAudioEngine 操作へ写像する host 専用 glue
/// @details game は audio mixer を持たず SoundIntent を書くだけ。host がこの関数で intent を解釈し、
/// 自分が所有する IAudioEngine を駆動する。この header は **host 側のみ** が include する。DLL は
/// ModuleApi.hpp だけを include するので、audio 依存が DLL 側へ漏れない。
///
/// SoundIntent のフィールド解釈:
///   - category: 0=SE, 1=BGM, 2=Voice (BGM は playMusic 経路、Voice は playVoice 経路 (同時 1 本の
///               台詞スロット、stop は id 不要)、SE は playSound 経路)
///   - stop:     1 なら再生でなく停止 (BGM→stopMusic / SE→stopSound、handle 付きはその 1 本だけ)
///   - loop:     ループ再生するか (BGM / SE 共通)
///   - volume:   0.0 から 1.0。0 (zero-init の既定) は「未指定 = 既定音量」とみなし 1.0 とする。
///               FrameIntents は毎フレーム zero-init されるため、音量を設定しない game が
///               無音化する footgun を防ぐ。
///   - bus / spatial / pan / position (v45): 音量にバスの音量と全体の音量を掛け、spatial=1 なら
///               聞き手 (FrameIntents::listener*) から見た距離で減衰させ左右に振る (audio/SpatialAudio.hpp)。

#include <algorithm>
#include <array>
#include <cstring>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/SpatialAudio.hpp>
#include <mitiru/audio/mix/BusDucker.hpp>
#include <mitiru/audio/sfx/VoiceManager.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module
{

static_assert(static_cast<int>(audio::mix::MixBus::Music) == kSoundBusMusic, "MixBus は kSoundBus* と同じ番号");
static_assert(static_cast<int>(audio::mix::MixBus::Sfx) == kSoundBusSfx, "MixBus は kSoundBus* と同じ番号");
static_assert(static_cast<int>(audio::mix::MixBus::Voice) == kSoundBusVoice, "MixBus は kSoundBus* と同じ番号");
static_assert(audio::mix::kMixBusCount == kSoundBusCount, "MixBus は kSoundBus* と同じ数");

/// @brief host が保持するバス音量と聞き手。FrameIntents の busVolume / listener* で変わる。
/// @details userBusVolume は利用者の設定ファイルの音量で、ゲームが決める busVolume に掛け合わせる。
///          ゲームのオプション画面と host の設定のどちらで下げても、両方の値が効く。
struct SoundMixState
{
	std::array<float, kSoundBusCount> busVolume{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	std::array<float, kSoundBusCount> userBusVolume{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
	audio::SpatialAudio spatial;
};

/// @brief SoundIntent::bus の 0 を category から決めた番号にする。
[[nodiscard]] inline std::uint8_t resolveSoundBus(const SoundIntent& s) noexcept
{
	if (s.bus != 0 && s.bus < kSoundBusCount) { return s.bus; }
	if (s.category == 1) { return kSoundBusMusic; }
	if (s.category == 2) { return kSoundBusVoice; }
	return kSoundBusSfx;
}

/// @brief 実際に鳴らす音量とパン。
struct SoundMix
{
	float volume = 1.0f;
	float pan = 0.0f;
};

/// @brief intent の音量 (0 = 既定 1.0) にバスと全体の音量、3D の減衰を掛け、パンを決める。
[[nodiscard]] inline SoundMix mixSound(const SoundIntent& s, const SoundMixState& mix) noexcept
{
	SoundMix out;
	const float base = (s.volume > 0.0f) ? s.volume : 1.0f;
	const std::uint8_t bus = resolveSoundBus(s);
	out.volume = base * mix.busVolume[bus] * mix.busVolume[kSoundBusMaster]
	           * mix.userBusVolume[bus] * mix.userBusVolume[kSoundBusMaster];
	if (s.spatial != 0)
	{
		const auto r = mix.spatial.calculate(audio::AudioVec3{s.position[0], s.position[1], s.position[2]});
		out.volume *= r.volume;
		out.pan = r.pan;
	}
	else
	{
		out.pan = std::clamp(s.pan, -1.0f, 1.0f);
	}
	return out;
}

namespace detail
{

inline const SoundMixState& identitySoundMix() noexcept
{
	static const SoundMixState mix{};
	return mix;
}

inline void applySoundStop(audio::IAudioEngine& engine, const SoundIntent& s)
{
	// fade 0 は素の stop として扱う (backend によって fade(0) の扱いが違うことがあるため)
	if (s.category == 1)
	{
		if (s.fadeOutSec > 0.0f) { engine.stopMusicFade(s.fadeOutSec); }
		else                     { engine.stopMusic(); }
	}
	else if (s.category == 2)
	{
		engine.stopVoiceFade(s.fadeOutSec);
	}
	else if (s.handle != 0)
	{
		engine.stopSoundInstance(s.handle, s.id, s.fadeOutSec);
	}
	else if (s.id[0] != '\0')
	{
		if (s.fadeOutSec > 0.0f) { engine.stopSoundFade(s.id, s.fadeOutSec); }
		else                     { engine.stopSound(s.id); }
	}
}

inline void applySoundEffect(audio::IAudioEngine& engine, const SoundIntent& s, const SoundMix& m, float pitch)
{
	if (s.loop != 0 || s.handle != 0 || m.pan != 0.0f)
	{
		audio::SoundPlayback p;
		p.handle = s.handle; p.volume = m.volume; p.pitch = pitch;
		p.fadeInSec = s.fadeInSec; p.pan = m.pan; p.loop = (s.loop != 0);
		engine.playSoundInstance(s.id, p);
	}
	else if (s.scheduleSec > 0.0) { engine.playSoundScheduled(s.id, s.scheduleSec, m.volume, pitch); }  // v19 サンプル精度予約
	else                          { engine.playSoundEx(s.id, m.volume, pitch, s.fadeInSec); }
}

}  // namespace detail

/// @brief SoundIntent 1 件を IAudioEngine 操作へ写像する (stateless 版)
/// @param mix バス音量と聞き手 (host の SoundIntentRouter が保持する値)。省くと全部 1.0・原点の聞き手。
/// BGM の同 id 連打を冪等化したい場合は SoundIntentRouter::apply を使う (host はそちら)。
inline void applySoundIntent(audio::IAudioEngine& engine, const SoundIntent& s,
                             const SoundMixState& mix = detail::identitySoundMix())
{
	const bool isMusic = (s.category == 1);  // 1 = BGM

	// v19: BGM transport (pause/resume/seek)。id 不要。再生中の BGM に作用する。
	// play/stop より先に判定する (transport intent は id 空・stop 0 で来る)。
	if (isMusic && s.transport != 0)
	{
		switch (s.transport)
		{
			case 1: engine.pauseMusic();         break;  // pause
			case 2: engine.resumeMusic();        break;  // resume
			case 3: engine.seekMusic(s.seekSec); break;  // seek
			default:                             break;
		}
		return;
	}
	if (s.stop != 0) { detail::applySoundStop(engine, s); return; }
	if (s.id[0] == '\0') { return; }

	const SoundMix m = mixSound(s, mix);
	const float pitch = (s.pitchScale > 0.0f) ? s.pitchScale : 1.0f;  // 0 = 未指定 → 1.0
	if (isMusic)                { engine.playMusicEx(s.id, m.volume, s.loop != 0, s.fadeInSec); }
	else if (s.category == 2)   { engine.playVoiceEx(s.id, m.volume, pitch, s.fadeInSec); }  // K1 台詞スロット
	else                        { detail::applySoundEffect(engine, s, m, pitch); }
}

/// @brief SoundIntent の host 側 router。BGM (category=1) の同 id 連打を冪等化し、バス音量と聞き手を保持する。
/// @details 「直前に開始した music id と同一 & その後 stop 無し & loop / volume も同じ」の play intent は
///          skip する。game が update() 内で毎フレーム `hud.music("bgm")` と書いても BGM は毎フレーム
///          再スタートしない (仕様として保証)。loop / volume が変わった場合は skip せず適用し、stopMusic 後の
///          同 id 再生も skip しない。別 id への切替で fadeInSec > 0 なら、新曲の前に stopMusicFade(同じ秒数)
///          で前の曲をフェードアウトさせる (crossfade)。効果音 (予約再生を除く) は audio::sfx::VoiceManager が
///          受け、同時に鳴らす数・優先度・距離での減り方・揺らぎ (assets/audio/sounds.json) を決めてから鳴らす。
///          ボイスは applySoundIntent に委譲する。バス音量か聞き手が変わったフレームは BGM の音量を掛け直し、
///          効果音は毎フレーム VoiceManager::step が掛け直す (ゲームが鳴らし直さなくても、オプション画面の音量や
///          カメラの移動が反映される)。DLL は再生状態を知らないので、この記憶は host 側にしか置けない。
class SoundIntentRouter
{
public:
	/// @brief フレームの聞き手とバス音量を取り込む。そのフレームの SoundIntent より先に呼ぶ。
	void applyMix(audio::IAudioEngine& engine, const FrameIntents& intents)
	{
		bool changed = false;
		if (intents.listenerSet != 0)
		{
			m_mix.spatial.updateFromCamera(intents.listenerPosition, intents.listenerForward, intents.listenerUp);
			engine.setListener(m_mix.spatial.listener());
			m_voices.setListener(m_mix.spatial.listener());
			changed = true;
		}
		for (int b = 0; b < kSoundBusCount; ++b)
		{
			if ((intents.busVolumeMask & (1u << b)) == 0) { continue; }
			m_mix.busVolume[static_cast<std::size_t>(b)] = std::clamp(intents.busVolume[b], 0.0f, 1.0f);
			changed = true;
		}
		if (auto bank = engine.sfxBank(); bank && bank != m_bank)
		{
			m_bank = bank;
			m_voices.setBank(engine, std::move(bank));
		}
		m_voices.setBusGains(effectiveBusGains());
		m_voices.step(engine, kStepSec);
		if (changed && m_lastMusicId[0] != '\0') { engine.setMusicVolume(mixSound(m_lastMusic, m_mix).volume); }
	}

	/// @brief 利用者の設定の音量を差し替え、鳴っているループ SE と BGM に掛け直す。engine が無ければ覚えるだけ。
	void setUserBusVolumes(audio::IAudioEngine* engine, const std::array<float, kSoundBusCount>& volumes)
	{
		for (std::size_t b = 0; b < volumes.size(); ++b) { m_mix.userBusVolume[b] = std::clamp(volumes[b], 0.0f, 1.0f); }
		m_voices.setBusGains(effectiveBusGains());
		if (engine != nullptr && m_lastMusicId[0] != '\0') { engine->setMusicVolume(mixSound(m_lastMusic, m_mix).volume); }
	}

	/// @brief 声に掛けるバスの音量。ゲームの決めた音量に利用者の設定を掛ける
	[[nodiscard]] std::array<float, kSoundBusCount> effectiveBusGains() const noexcept
	{
		std::array<float, kSoundBusCount> g{};
		for (std::size_t b = 0; b < g.size(); ++b) { g[b] = m_mix.busVolume[b] * m_mix.userBusVolume[b]; }
		return g;
	}

	/// @brief intent 1 件を適用する。music dedupe で skip したら false を返す。
	bool apply(audio::IAudioEngine& engine, const SoundIntent& s)
	{
		if (s.category == 1 && !acceptMusic(engine, s)) { return false; }
		if (s.category == 0 && s.scheduleSec <= 0.0) { applyEffect(engine, s); return true; }
		applySoundIntent(engine, s, m_mix);
		return true;
	}

	[[nodiscard]] const SoundMixState& mix() const noexcept { return m_mix; }

	/// @brief 効果音の声 (sounds.json の差し替え、遮蔽の C++ API、観察)
	[[nodiscard]] audio::sfx::VoiceManager& voices() noexcept { return m_voices; }
	[[nodiscard]] const audio::sfx::VoiceManager& voices() const noexcept { return m_voices; }

private:
	/// applyMix は固定ステップ (60 Hz) ごとに 1 回呼ばれる (audio の update と同じ前提)
	static constexpr float kStepSec = 1.0f / 60.0f;

	void applyEffect(audio::IAudioEngine& engine, const SoundIntent& s)
	{
		if (s.stop != 0) { m_voices.stop(engine, s.handle, s.id, s.fadeOutSec); return; }
		if (s.id[0] == '\0') { return; }
		audio::sfx::SfxPlay p;
		p.id = std::string_view(s.id, static_cast<std::size_t>(std::find(s.id, s.id + sizeof(s.id), 0) - s.id));
		p.handle = s.handle;
		p.volume = (s.volume > 0.0f) ? s.volume : 1.0f;
		p.pitch = (s.pitchScale > 0.0f) ? s.pitchScale : 1.0f;
		p.pan = s.pan;
		p.fadeInSec = s.fadeInSec;
		p.loop = s.loop != 0;
		p.spatial = s.spatial != 0;
		p.position = audio::AudioVec3{s.position[0], s.position[1], s.position[2]};
		p.bus = resolveSoundBus(s);
		p.priority = s.priority;
		p.occlusion = static_cast<float>(s.occlusion) / 255.0f;
		m_voices.play(engine, p);
	}

	bool acceptMusic(audio::IAudioEngine& engine, const SoundIntent& s)
	{
		if (s.stop != 0)
		{
			m_lastMusicId[0] = '\0';  // stop 後の同 id は再び再生される
			return true;
		}
		if (s.id[0] == '\0') { return true; }
		// 比較は「ゲームが渡した値」で行う (volume 0 = 未指定 → 1.0)。バス音量は applyMix が掛け直す。
		const float        vol  = (s.volume > 0.0f) ? s.volume : 1.0f;
		const std::uint8_t loop = (s.loop != 0) ? 1 : 0;
		const bool sameId = std::strncmp(m_lastMusicId, s.id, sizeof(m_lastMusicId)) == 0;
		if (sameId && loop == m_lastLoop && vol == m_lastVolume) { return false; }
		if (!sameId && m_lastMusicId[0] != '\0' && s.fadeInSec > 0.0f && !engine.ownsMusicTransition(s.id))
		{
			engine.stopMusicFade(s.fadeInSec);
		}
		std::memcpy(m_lastMusicId, s.id, sizeof(m_lastMusicId));
		m_lastMusicId[sizeof(m_lastMusicId) - 1] = '\0';
		m_lastLoop   = loop;
		m_lastVolume = vol;
		m_lastMusic  = s;
		return true;
	}

	SoundMixState m_mix;
	char          m_lastMusicId[sizeof(SoundIntent::id)] = {};   ///< 直前に開始した music id
	std::uint8_t  m_lastLoop   = 0;                              ///< その loop フラグ
	float         m_lastVolume = 0.0f;                           ///< その volume (0→1.0 解決後、バスを掛ける前)
	SoundIntent   m_lastMusic{};                                 ///< 直前に開始した music の intent (バスの掛け直し用)
	audio::sfx::VoiceManager m_voices;
	std::shared_ptr<const audio::sfx::SfxBank> m_bank;           ///< m_voices に渡した鳴らし方 (差し替わったかを見る)
};

}  // namespace mitiru::module
