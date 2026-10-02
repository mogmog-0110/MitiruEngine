#pragma once

/// @file VoiceManager.hpp
/// @brief 効果音の同時発声数を抑え、優先度と聞こえる大きさから実際に鳴らす声を選ぶ
/// @details 1 回の再生につき 1 つの声を持つ。実際に鳴らす声 (real) は SfxBank::maxReal 本までとし、優先度、聞こえる大きさ、新しさの順に選ぶ。
/// 選ばれなかった声と聞こえないほど小さい声は、音を出さずに時間だけ進めておき (virtual)、もう一度選ばれたら
/// 経過した位置から鳴らす。
/// 再生位置はステップごとに「経過時間 × そのステップのピッチ」を足して求め、音の長さはファイルから測る。終了はこの時間で判定するため、デバイスの有無で結果が変わらない。
/// 距離、遮蔽、ドップラー、バス音量を反映してから、IAudioEngine の番号付き再生へ渡す。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/SpatialAudio.hpp>
#include <mitiru/audio/sfx/SfxBank.hpp>

namespace mitiru::audio::sfx
{

/// @brief SoundIntent の SE から router が作る、1 本分の再生指定。volume と pitch は、0 を 1 に直してから渡す
struct SfxPlay
{
	std::string_view id;
	std::uint32_t    handle = 0;
	float            volume = 1.0f;
	float            pitch = 1.0f;
	float            pan = 0.0f;
	float            fadeInSec = 0.0f;
	bool             loop = false;
	bool             spatial = false;
	AudioVec3        position{};
	std::uint8_t     bus = 2;
	std::uint8_t     priority = 0;      ///< 0 = sounds.json の priority。鳴らし直しのたびに変えられる
	float            occlusion = 0.0f;  ///< 0..1。鳴らし直しのたびに置き換わる (ゲームがレイの結果を毎フレーム送る)
};

/// @brief テストと観察に使う、声の現在の状態
struct VoiceView
{
	bool          real = false;
	std::uint32_t handle = 0;
	const char*   id = "";
	const char*   file = "";
	float         gain = 0.0f;
	float         pan = 0.0f;
	float         pitch = 1.0f;
	float         lowpassHz = 0.0f;
};

class VoiceManager
{
public:
	static constexpr std::size_t kMaxVoices = 256;
	static constexpr float kSwapFadeSec = 0.03f;       ///< 鳴らす・止めるを入れ替えるときのつなぎ (ぷつっと鳴らさない)
	static constexpr double kUnknownLengthSec = 2.0;   ///< 長さが測れないファイルの寿命
	static constexpr double kEndGraceSec = 0.1;        ///< 鳴っている声は、終わってからこれだけ待って止める (音声スレッドの遅れの分)

	VoiceManager() : m_bank(std::make_shared<const SfxBank>()) { m_busGain.fill(1.0f); }

	/// @brief 鳴らし方を差し替え、古い定義を参照する声が残らないよう、鳴っている声をすべて止める
	void setBank(IAudioEngine& engine, std::shared_ptr<const SfxBank> bank)
	{
		for (Voice& v : m_voices) { if (v.used) { kill(engine, v, 0.0f); } }
		m_bank = std::move(bank);
	}
	[[nodiscard]] const SfxBank& bank() const noexcept { return *m_bank; }

	/// @brief バスごとの音量 (添字は kSoundBus*、0 は全体)
	void setBusGains(const std::array<float, 8>& gains) noexcept { m_busGain = gains; }

	void setListener(const AudioListener& listener) noexcept
	{
		AudioListener l = listener;
		l.velocity = m_spatial.listener().velocity;
		m_spatial.setListener(l);
	}

	void play(IAudioEngine& engine, const SfxPlay& p)
	{
		if (Voice* v = findLive(p); v != nullptr) { retune(engine, *v, p); return; }
		const SfxDef& def = m_bank->find(p.id);
		Voice fresh = makeVoice(engine, def, p);
		fresh.audibility = audibility(fresh);
		if (!makeRoomForInstance(engine, def, fresh)) { return; }
		Voice* slot = freeSlot(engine, fresh);
		if (slot == nullptr) { return; }
		Voice& placed = slot[0];
		placed = fresh;
		admit(engine, placed);
	}

	/// @brief handle が 0 でなければその声、0 なら id の声 (番号なし) をすべて止める
	void stop(IAudioEngine& engine, std::uint32_t handle, std::string_view id, float fadeOutSec)
	{
		for (Voice& v : m_voices)
		{
			if (!v.used) { continue; }
			const bool match = (handle != 0) ? (v.handle == handle) : (v.handle == 0 && id == v.id);
			if (match) { kill(engine, v, fadeOutSec); }
		}
	}

	/// @brief handle で指定した声の遮蔽。0 は遮蔽なし、1 は完全な遮蔽を表し、声がなければ false を返す
	bool setOcclusion(std::uint32_t handle, float occlusion) noexcept
	{
		for (Voice& v : m_voices)
		{
			if (v.used && handle != 0 && v.handle == handle) { v.occlusion = std::clamp(occlusion, 0.0f, 1.0f); return true; }
		}
		return false;
	}

	/// @brief 1 ステップ進め、寿命、距離、遮蔽から鳴らす声を選び、変更分だけ engine へ伝える
	void step(IAudioEngine& engine, float dt)
	{
		advanceClock(dt);
		const float inv = (dt > 0.0f) ? 1.0f / dt : 0.0f;
		for (Voice& v : m_voices)
		{
			if (!v.used) { continue; }
			v.filePos += static_cast<double>(dt) * static_cast<double>(v.mix.pitch);
			if (!v.loop && v.filePos >= v.fileSec + (v.real ? kEndGraceSec : 0.0)) { kill(engine, v, 0.0f); continue; }
			v.velocity = scale(sub(v.position, v.prevPosition), inv);
			v.prevPosition = v.position;
			v.audibility = audibility(v);
		}
		reselect(engine);
	}

	[[nodiscard]] std::size_t realCount() const noexcept { return count(true); }
	[[nodiscard]] std::size_t activeCount() const noexcept { return count(false); }

	template <class F>
	void forEach(F&& f) const
	{
		for (const Voice& v : m_voices)
		{
			if (v.used) { f(VoiceView{v.real, v.handle, v.id, v.file, v.mix.gain, v.mix.pan, v.mix.pitch, v.mix.lowpassHz}); }
		}
	}

private:
	struct Mix
	{
		float     gain = 0.0f;
		float     pan = 0.0f;
		float     pitch = 1.0f;
		float     lowpassHz = 0.0f;
		AudioVec3 direction{0.0f, 0.0f, -1.0f};  ///< 聞き手から見た向き (HRTF を持つ backend が使う)
	};

	static bool sameMix(const Mix& a, const Mix& b) noexcept
	{
		return std::fabs(a.gain - b.gain) < 1e-4f && a.pan == b.pan && a.pitch == b.pitch && a.lowpassHz == b.lowpassHz
		    && a.direction.x == b.direction.x && a.direction.y == b.direction.y && a.direction.z == b.direction.z;
	}

	struct Voice
	{
		bool          used = false;
		bool          real = false;
		bool          loop = false;
		bool          spatial = false;
		std::uint8_t  bus = 2;
		std::uint8_t  priority = 128;
		std::uint32_t key = 0;
		std::uint32_t handle = 0;
		std::uint64_t order = 0;
		const SfxDef* def = nullptr;
		char          id[64]{};
		char          file[64]{};
		float         volume = 1.0f;     ///< ゲームの音量 × 揺らぎ
		float         pitch = 1.0f;      ///< ゲームのピッチ × 揺らぎ
		float         rollGain = 1.0f;
		float         rollPitch = 1.0f;
		float         pan = 0.0f;
		float         fadeInSec = 0.0f;
		float         occlusion = 0.0f;
		float         audibility = 0.0f;
		AudioVec3     position{};
		AudioVec3     prevPosition{};
		AudioVec3     velocity{};
		double        filePos = 0.0;      ///< ファイルのどこまで進んだか (秒、原音のピッチで)
		double        fileSec = 0.0;      ///< ファイルの長さ (原音のピッチで)
		Mix           mix;
		Mix           sent;
	};

	struct Ordinal
	{
		std::uint64_t hash = 0;
		std::uint32_t count = 0;
	};

	static AudioVec3 sub(const AudioVec3& a, const AudioVec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	static AudioVec3 scale(const AudioVec3& a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
	static void copyId(char (&dst)[64], std::string_view src) noexcept
	{
		const std::size_t n = std::min(src.size(), sizeof(dst) - 1);
		std::memcpy(dst, src.data(), n);
		dst[n] = '\0';
	}

	void advanceClock(float dt) noexcept
	{
		AudioListener next = m_spatial.listener();
		const float inv = (dt > 0.0f) ? 1.0f / dt : 0.0f;
		next.velocity = m_hasListenerPos ? scale(sub(next.position, m_prevListener), inv) : AudioVec3{};
		m_prevListener = next.position;
		m_hasListenerPos = true;
		m_spatial.setListener(next);
		++m_frame;
		m_ordinalCount = 0;
	}

	[[nodiscard]] std::size_t count(bool realOnly) const noexcept
	{
		return static_cast<std::size_t>(std::count_if(m_voices.begin(), m_voices.end(),
			[&](const Voice& v) { return v.used && (!realOnly || v.real); }));
	}

	Voice* findLive(const SfxPlay& p) noexcept
	{
		for (Voice& v : m_voices)
		{
			if (!v.used) { continue; }
			if (p.handle != 0 && v.handle == p.handle) { return &v; }
			if (p.handle == 0 && p.loop && v.loop && v.handle == 0 && p.id == v.id) { return &v; }
		}
		return nullptr;
	}

	/// 同じ番号の声は新しく鳴らさず、音量、ピッチ、位置だけを変える。番号なしのループは同じ id を同じ声とみなす
	void retune(IAudioEngine& engine, Voice& v, const SfxPlay& p)
	{
		retuneFields(v, p);
		v.audibility = audibility(v);
		if (v.real) { sendUpdate(engine, v); }
	}

	static void retuneFields(Voice& v, const SfxPlay& p) noexcept
	{
		v.volume = p.volume * v.rollGain;
		v.pitch = p.pitch * v.rollPitch;
		v.pan = p.pan;
		v.spatial = p.spatial;
		v.position = p.position;
		v.bus = p.bus;
		v.occlusion = std::clamp(p.occlusion, 0.0f, 1.0f);
		if (p.priority != 0) { v.priority = p.priority; }
	}

	std::uint32_t ordinal(std::uint64_t idHash) noexcept
	{
		for (std::size_t i = 0; i < m_ordinalCount; ++i)
		{
			if (m_ordinals[i].hash == idHash) { return ++m_ordinals[i].count; }
		}
		if (m_ordinalCount < m_ordinals.size()) { m_ordinals[m_ordinalCount++] = Ordinal{idHash, 0}; }
		return 0;
	}

	Voice makeVoice(IAudioEngine& engine, const SfxDef& def, const SfxPlay& p)
	{
		const std::uint64_t h = hashId(p.id);
		const SfxRoll roll = rollSfx(def, m_bank->seed, m_frame, h, ordinal(h), p.handle);
		Voice v;
		v.used = true;
		v.loop = p.loop;
		v.handle = p.handle;
		v.def = &def;
		v.priority = def.priority;
		copyId(v.id, p.id);
		copyId(v.file, def.variants.empty() ? p.id : std::string_view(def.variants[roll.variant]));
		v.rollGain = roll.gain;
		v.rollPitch = roll.pitch;
		v.fadeInSec = p.fadeInSec;
		retuneFields(v, p);
		v.prevPosition = v.position;
		const double len = engine.soundLengthSec(v.file);
		v.fileSec = (len > 0.0) ? len : kUnknownLengthSec;
		v.order = m_order++;
		v.key = nextKey();
		return v;
	}

	std::uint32_t nextKey() noexcept
	{
		if (++m_key == 0) { m_key = 1; }
		return m_key;
	}

	[[nodiscard]] Mix computeMix(const Voice& v) const noexcept
	{
		Mix m;
		const std::size_t bus = (v.bus < m_busGain.size()) ? v.bus : 2;
		m.gain = v.volume * m_busGain[bus] * m_busGain[0];
		m.pan = std::clamp(v.pan, -1.0f, 1.0f);
		m.pitch = v.pitch;
		if (v.spatial)
		{
			SpatialSourceConfig cfg;
			cfg.dopplerFactor = v.def->dopplerFactor;
			const SpatialResult r = m_spatial.calculate(v.position, cfg, v.velocity);
			m.gain *= attenuate(v.def->attenuation, m_spatial.listener().position.distanceTo(v.position));
			m.pan = r.pan;
			m.pitch *= r.doppler;
			m.direction = m_spatial.toListenerSpace(v.position);
		}
		m.gain *= occlusionGain(*v.def, v.occlusion);
		m.lowpassHz = occlusionLowpass(*v.def, v.occlusion);
		return m;
	}

	float audibility(Voice& v) const noexcept
	{
		v.mix = computeMix(v);
		return v.mix.gain;
	}

	/// 同時数が maxInstances を超える場合は、規則で選んだ 1 本を止める。新しい音を鳴らさない場合は false を返す
	bool makeRoomForInstance(IAudioEngine& engine, const SfxDef& def, const Voice& fresh)
	{
		if (def.maxInstances <= 0) { return true; }
		Voice* victim = nullptr;
		int n = 0;
		for (Voice& v : m_voices)
		{
			if (!v.used || std::strcmp(v.id, fresh.id) != 0) { continue; }
			++n;
			const bool pick = (def.steal == StealMode::Quietest) ? (victim == nullptr || v.audibility < victim->audibility)
			                                                      : (victim == nullptr || v.order < victim->order);
			if (pick) { victim = &v; }
		}
		if (n < def.maxInstances) { return true; }
		if (def.steal == StealMode::None) { return false; }
		if (def.steal == StealMode::Quietest && victim->audibility >= fresh.audibility) { return false; }
		kill(engine, *victim, kSwapFadeSec);
		return true;
	}

	/// a が b より先に手放してよい声なら true
	static bool lessImportant(const Voice& a, const Voice& b) noexcept
	{
		if (a.priority != b.priority) { return a.priority < b.priority; }
		if (a.audibility != b.audibility) { return a.audibility < b.audibility; }
		return a.order < b.order;
	}

	Voice* freeSlot(IAudioEngine& engine, const Voice& fresh)
	{
		const std::size_t cap = std::min<std::size_t>(kMaxVoices, static_cast<std::size_t>(std::max(m_bank->maxVirtual, 1)));
		Voice* weakest = nullptr;
		for (std::size_t i = 0; i < cap; ++i)
		{
			if (!m_voices[i].used) { return &m_voices[i]; }
			if (weakest == nullptr || lessImportant(m_voices[i], *weakest)) { weakest = &m_voices[i]; }
		}
		if (weakest == nullptr || lessImportant(fresh, *weakest)) { return nullptr; }
		kill(engine, *weakest, kSwapFadeSec);
		return weakest;
	}

	[[nodiscard]] float threshold() const noexcept { return std::pow(10.0f, m_bank->virtualBelowDb / 20.0f); }

	/// 空きがあるか、鳴っている声のうち最も重要度が低い声より重要なら、新しい声をすぐ鳴らす
	void admit(IAudioEngine& engine, Voice& v)
	{
		if (v.audibility < threshold()) { return; }
		Voice* weakestReal = nullptr;
		std::size_t reals = 0;
		for (Voice& o : m_voices)
		{
			if (!o.used || !o.real || &o == &v) { continue; }
			++reals;
			if (weakestReal == nullptr || lessImportant(o, *weakestReal)) { weakestReal = &o; }
		}
		if (reals >= static_cast<std::size_t>(m_bank->maxReal))
		{
			if (weakestReal == nullptr || lessImportant(v, *weakestReal)) { return; }
			demote(engine, *weakestReal);
		}
		startReal(engine, v, 0.0);
	}

	void reselect(IAudioEngine& engine)
	{
		std::size_t n = 0;
		for (std::size_t i = 0; i < m_voices.size(); ++i)
		{
			if (m_voices[i].used) { m_sortIndex[n++] = static_cast<std::uint16_t>(i); }
		}
		std::sort(m_sortIndex.begin(), m_sortIndex.begin() + static_cast<std::ptrdiff_t>(n),
			[this](std::uint16_t a, std::uint16_t b) { return lessImportant(m_voices[b], m_voices[a]); });
		const float th = threshold();
		std::size_t chosen = 0;
		for (std::size_t k = 0; k < n; ++k)
		{
			Voice& v = m_voices[m_sortIndex[k]];
			const bool want = v.audibility >= th && chosen < static_cast<std::size_t>(m_bank->maxReal);
			if (want) { ++chosen; }
			if (want && !v.real) { promote(engine, v); }
			else if (!want && v.real) { demote(engine, v); }
			else if (v.real) { sendUpdate(engine, v); }
		}
	}

	void promote(IAudioEngine& engine, Voice& v)
	{
		if (!v.loop && v.fileSec - v.filePos < kSwapFadeSec) { return; }
		startReal(engine, v, v.loop ? std::fmod(v.filePos, v.fileSec) : v.filePos);
	}

	void startReal(IAudioEngine& engine, Voice& v, double offsetSec)
	{
		SoundPlayback p;
		p.handle = v.key;
		p.volume = v.mix.gain;
		p.pitch = v.mix.pitch;
		p.pan = v.mix.pan;
		p.loop = v.loop;
		p.fadeInSec = (offsetSec > 0.0) ? kSwapFadeSec : v.fadeInSec;
		p.startSec = static_cast<float>(offsetSec);
		p.lowpassHz = v.mix.lowpassHz;
		p.spatial = v.spatial;
		p.direction = v.mix.direction;
		engine.playSoundInstance(v.file, p);
		v.real = true;
		v.sent = v.mix;
	}

	void demote(IAudioEngine& engine, Voice& v)
	{
		engine.stopSoundInstance(v.key, v.file, kSwapFadeSec);
		v.real = false;
		v.key = nextKey();  // 減衰しながら止まる音と番号を分けるため、次に鳴らすときは新しい番号を使う
	}

	void kill(IAudioEngine& engine, Voice& v, float fadeOutSec)
	{
		if (v.real) { engine.stopSoundInstance(v.key, v.file, fadeOutSec); }
		v.used = false;
		v.real = false;
	}

	void sendUpdate(IAudioEngine& engine, Voice& v)
	{
		const Mix& m = v.mix;
		if (sameMix(m, v.sent)) { return; }
		SoundPlayback p;
		p.handle = v.key;
		p.volume = m.gain;
		p.pitch = m.pitch;
		p.pan = m.pan;
		p.loop = v.loop;
		p.lowpassHz = m.lowpassHz;
		p.spatial = v.spatial;
		p.direction = m.direction;
		engine.updateSoundInstanceEx(v.key, v.file, p);
		v.sent = m;
	}

	std::shared_ptr<const SfxBank>          m_bank;
	std::vector<Voice>                      m_voices = std::vector<Voice>(kMaxVoices);  // 鳴らすたびに確保しないよう最初に作る
	std::vector<std::uint16_t>              m_sortIndex = std::vector<std::uint16_t>(kMaxVoices);
	std::array<float, 8>                    m_busGain{};
	std::array<Ordinal, 32>                 m_ordinals{};
	std::size_t                             m_ordinalCount = 0;
	SpatialAudio                            m_spatial;
	AudioVec3                               m_prevListener{};
	bool                                    m_hasListenerPos = false;
	std::uint64_t                           m_frame = 0;
	std::uint64_t                           m_order = 0;
	std::uint32_t                           m_key = 0;
};

}  // namespace mitiru::audio::sfx
