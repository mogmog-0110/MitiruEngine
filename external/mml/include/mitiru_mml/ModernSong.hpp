#pragma once
/// @file ModernSong.hpp
/// @brief 現代的な曲を、トラックのグループ (バス) ごとに描いて、ステムとミックスを返す
/// @details トラックはグループに属する。グループは自分のチップ (シンセ、ドラムシンセ、SF2 サンプラー) を持ち、
///          別のグループとは何も共有しないので、グループごとに描いた結果 (ステム) の和が、そのままミックスになる。
///          ステムは、グループのゲインとダッキングを掛けたあと、マスターのゲイン、幅、リミッターの手前の音。
///          流れ: MML -> イベント (スウィングを掛ける) -> グループごとにチップを鳴らす -> ダッキング -> 足す -> 幅 -> リミッター。
///          乱数は音ごとにシードが決まっているので、同じ MML から毎回同じ PCM になる。
///
/// @code
/// mitiru_mml::ModernSong song;
/// song.addTrack({"kick", "drums", ModernSong::ChipKind::Drums, -1, 0, false, true, "T128 O4 [C4]16"});
/// song.addTrack({"bass", "bass", ModernSong::ChipKind::Synth, 0, -1, false, true, "T128 O2 [CC8>C8<]8"});
/// song.addDuck({{"bass"}, "kick", {-10.0f, 4.0f, 0.0f, 160.0f}});
/// auto result = song.render();
/// @endcode

#include <mitiru_mml/BoxDecimator.hpp>
#include <mitiru_mml/ChipCatalog.hpp>
#include <mitiru_mml/ChipDriver.hpp>
#include <mitiru_mml/Dynamics.hpp>
#include <mitiru_mml/DrumSynthDriver.hpp>
#include <mitiru_mml/EventTransforms.hpp>
#include <mitiru_mml/MmlEventBuilder.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/OpnaDriver.hpp>
#include <mitiru_mml/SamplerDriver.hpp>
#include <mitiru_mml/SendBus.hpp>
#include <mitiru_mml/Sf2File.hpp>
#include <mitiru_mml/StereoBuffer.hpp>
#include <mitiru_mml/SynthDriver.hpp>
#include <mitiru_mml/Timeline.hpp>
#include <mitiru_mml/WavWriter.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mitiru_mml
{

class ModernSongStream;

/// @brief 現代的な曲 (グループ、トラック、ダッキング、リミッター)
class ModernSong
{
	friend class ModernSongStream;

public:
	/// @brief トラックを鳴らすチップ
	enum class ChipKind : int
	{
		Synth = 0,    ///< 減算シンセ。voice = 組み込みの音色の番号 (-1 なら patch)
		Drums = 1,    ///< ドラムシンセ。drum = 0〜7 で種類、-1 で GM の割り当て (1 本の MML で曲全体のドラム)
		Sampler = 2,  ///< SF2。voice = GM のプログラム番号。drum >= 0 のとき、パーカッション (チャンネル 9)
		Opna = 3,     ///< OPNA (YM2608)。voice = FM の音色の番号。ssg が true なら SSG、rhythm が true ならドラム
		Opm = 4,      ///< YM2151。voice = OPM の音色の番号
		Opl = 5,      ///< YM3526
		Opl2 = 6,     ///< YM3812
		Opl3 = 7,     ///< YMF262
		Sn76489 = 8,  ///< SN76489。ノイズは channel = Sn76489Driver::NOISE_CHANNEL
		Ay8910 = 9,   ///< AY-3-8910 / YM2149
		Nes = 10,     ///< NES の APU
	};

	static constexpr std::size_t CHIP_KIND_COUNT = 11;

	/// @brief トラック 1 本
	struct TrackSpec
	{
		std::string name;
		std::string group = "main";
		ChipKind chip = ChipKind::Synth;
		int voice = -1;
		int drum = -1;
		bool rhythm = false;     ///< MML を既存のリズム文法 (B S H F Y I) として読む
		bool ssg = false;        ///< Opna のとき、FM でなく SSG のチャンネルに置く
		bool swing = true;       ///< 曲のスウィングを掛ける
		std::string mml;
		std::optional<SynthPatch> patch;
		int channel = -1;        ///< Driver のチャンネル番号を決める。-1 は自動 (グループの中で先着順)
		int pan = -1;            ///< 0 左、1 中央、2 右。-1 は MML に任せる (MML の先頭に P を足すのと同じ)
		int volume = -1;         ///< 0〜15。-1 は MML に任せる (MML の先頭に V を足すのと同じ)
		/// @brief エフェクトへの送り量。SendBus::REVERB、DELAY、CHORUS の順に 0〜1 で指定し、0 は送らない
		/// @details グループのゲインとダッキングを掛けたあとに送り、トラックの dry とは別に加える。dry は変えない。
		/// 送りのあるトラックは、そのトラックだけをもう 1 度描く。
		SendBus::Amounts sends{};
	};

	/// @brief グループ (バス) の設定
	struct GroupSpec
	{
		std::string name;
		float gainDb = 0.0f;
	};

	/// @brief ダッキングの規則
	struct DuckRule
	{
		std::vector<std::string> targets;  ///< 下げるグループ
		std::string by;                    ///< 合図のトラック名かグループ名。signal のときはグループ名
		DuckSettings duck;
		bool signal = false;               ///< true なら by のグループの音を検出に使う (サイドチェーン)
		SidechainSettings sidechain;
	};

	/// @brief 曲全体の設定
	struct Options
	{
		std::uint32_t sampleRate = 44100;
		double tailSeconds = 1.5;          ///< 最後のイベントのあとも、リリースと残響のために描く時間
		int loopCount = 1;
		float masterGainDb = 0.0f;
		float stereoWidth = 1.0f;
		bool limiter = true;
		LimiterSettings limit;
		bool dither = true;                ///< 16bit にするときにディザを掛ける
		bool parallelGroups = false;       ///< グループごとに別スレッドで描く (結果は変わらない)
		float swingRatio = 0.5f;           ///< 0.5 でスウィングなし
		int swingGrid = 8;
		int tempo = 0;                     ///< 0 より大きいと、曲の最初のテンポ (0 なら 120)。MML の T はこの上に重なる
		/// @brief 送り先のエフェクト。送り量は 100% とし、wet の大きさを戻りの量に使う
		Reverb sendReverb;
		Delay sendDelay;
		Chorus sendChorus = Chorus().setMix(0.7f);
	};

	/// @brief グループ 1 つぶんの音
	struct Stem
	{
		std::string name;
		StereoBuffer audio;
	};

	/// @brief 描いた結果
	struct Result
	{
		StereoBuffer mix;              ///< マスターのあとの音 (リミッターを通ったもの)
		std::vector<Stem> stems;       ///< グループごとの音 (ゲインとダッキングのあと、マスターの手前)
		float limiterReductionDb = 0.0f;
		bool ditherDefault = true;

		/// @brief 16bit ステレオ PCM に直す
		[[nodiscard]] PcmBuffer toPcm16(bool dither) const
		{
			return toPcm16StereoDithered(mix, dither);
		}
	};

	ModernSong() = default;
	explicit ModernSong(const Options& options) : m_options(options) {}

	[[nodiscard]] Options& options() noexcept
	{
		return m_options;
	}

	[[nodiscard]] const Options& options() const noexcept
	{
		return m_options;
	}

	/// @brief SF2 を渡す (Sampler のトラックだけが使う)
	void setSoundFont(std::shared_ptr<const Sf2File> soundFont)
	{
		m_soundFont = std::move(soundFont);
	}

	/// @brief グループを作る (ゲインを決めたいときに使う。トラックに書いた名前のグループは自動で作られる)
	void addGroup(const GroupSpec& group)
	{
		const int index = findGroup(group.name);
		if (index >= 0)
		{
			m_groups[static_cast<std::size_t>(index)].gainDb = group.gainDb;
			return;
		}
		m_groups.push_back(group);
	}

	void addTrack(const TrackSpec& track)
	{
		if (findGroup(track.group) < 0)
		{
			m_groups.push_back({track.group, 0.0f});
		}
		m_tracks.push_back(track);
	}

	void addDuck(const DuckRule& rule)
	{
		m_ducks.push_back(rule);
	}

	[[nodiscard]] int trackCount() const noexcept
	{
		return static_cast<int>(m_tracks.size());
	}

	[[nodiscard]] const std::vector<GroupSpec>& groups() const noexcept
	{
		return m_groups;
	}

	/// @brief 全トラックを描く
	/// @throws std::invalid_argument チャンネルが足りない、ダッキングの対象が見つからないなど
	[[nodiscard]] Result render() const
	{
		const Prepared prep = prepare();
		const auto& plan = prep.plan;
		const auto& trackEvents = prep.trackEvents;
		const auto& shared = prep.shared;
		const auto& groupEvents = prep.groupEvents;
		const std::size_t groupCount = m_groups.size();

		// 2. グループごとに鳴らす
		std::vector<StereoBuffer> raw(groupCount);
		if (m_options.parallelGroups && groupCount > 1)
		{
			std::vector<std::future<StereoBuffer>> futures;
			for (std::size_t g = 0; g < groupCount; ++g)
			{
				futures.push_back(std::async(std::launch::async,
					[this, g, &groupEvents, &plan]() { return renderGroup(g, groupEvents[g], plan); }));
			}
			for (std::size_t g = 0; g < groupCount; ++g)
			{
				raw[g] = futures[g].get();
			}
		}
		else
		{
			for (std::size_t g = 0; g < groupCount; ++g)
			{
				raw[g] = renderGroup(g, groupEvents[g], plan);
			}
		}

		std::size_t frames = 0;
		for (const auto& b : raw)
		{
			frames = std::max(frames, b.frames());
		}
		for (auto& b : raw)
		{
			b.sampleRate = m_options.sampleRate;
			b.growTo(frames);
		}

		// 3. グループのゲイン。ダッキングの検出は、ゲインを掛けたあと、ダッキングの前の音を使う
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			const float gain = dbToLinear(m_groups[g].gainDb);
			for (float& s : raw[g].samples)
			{
				s *= gain;
			}
		}
		const std::vector<StereoBuffer> keys = raw;

		// 4. ダッキング
		std::map<std::string, std::vector<std::uint64_t>> triggerCache;
		const auto applyDucks = [&](StereoBuffer& buffer, std::size_t g)
		{
			for (const auto& rule : m_ducks)
			{
				for (const auto& target : rule.targets)
				{
					if (static_cast<std::size_t>(findGroup(target)) != g)
					{
						continue;
					}
					if (rule.signal)
					{
						applySidechainCompressor(buffer, keys[static_cast<std::size_t>(findGroup(rule.by))], rule.sidechain);
					}
					else
					{
						if (triggerCache.find(rule.by) == triggerCache.end())
						{
							triggerCache[rule.by] = triggerPositions(trackEvents, shared, rule.by);
						}
						applyGainCurve(buffer, buildDuckCurve(frames, triggerCache[rule.by], m_options.sampleRate, rule.duck));
					}
				}
			}
		};
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			applyDucks(raw[g], g);
		}

		// 5. エフェクトへの送り。送りのあるトラックだけをもう 1 度描いてバスに加える
		const SendPlan sendPlan = planSends();
		std::optional<SendBus> bus;
		if (sendPlan.any())
		{
			bus.emplace(m_options.sendReverb, m_options.sendDelay, m_options.sendChorus, m_options.sampleRate, sendPlan.used);
			bus->begin(frames);
			for (const std::size_t t : sendPlan.tracks)
			{
				StereoBuffer tap = renderIsolatedTrack(t, prep);
				tap.sampleRate = m_options.sampleRate;
				tap.growTo(frames);
				const std::size_t g = static_cast<std::size_t>(findGroup(m_tracks[t].group));
				const float gain = dbToLinear(m_groups[g].gainDb);
				for (float& s : tap.samples)
				{
					s *= gain;
				}
				applyDucks(tap, g);
				bus->addTap(tap.samples.data(), frames, m_tracks[t].sends);
			}
		}

		// 6. グループ順、送りの戻りの順に加算。幅、リミッター
		Result result;
		result.ditherDefault = m_options.dither;
		result.mix.sampleRate = m_options.sampleRate;
		result.mix.growTo(frames);
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			result.stems.push_back({m_groups[g].name, raw[g]});
			result.mix.addScaled(raw[g], 1.0f);
		}
		if (bus)
		{
			bus->process(result.mix.samples.data(), frames);
			for (std::size_t e = 0; e < SendBus::EFFECT_COUNT; ++e)
			{
				if (!bus->used(e))
				{
					continue;
				}
				StereoBuffer stem;
				stem.sampleRate = m_options.sampleRate;
				stem.samples.resize(frames * 2);
				for (std::size_t i = 0; i < frames; ++i)
				{
					stem.samples[i * 2] = bus->returns(e)[i];
					stem.samples[i * 2 + 1] = bus->returns(e)[i];
				}
				result.stems.push_back({std::string("send_") + SendBus::effectName(e), std::move(stem)});
			}
		}
		const float master = dbToLinear(m_options.masterGainDb);
		for (float& s : result.mix.samples)
		{
			s *= master;
		}
		if (std::fabs(m_options.stereoWidth - 1.0f) > 1.0e-6f)
		{
			applyStereoWidth(result.mix, m_options.stereoWidth);
		}
		if (m_options.limiter)
		{
			result.limiterReductionDb = applyLimiter(result.mix, m_options.limit);
		}
		return result;
	}

	/// @brief ステムとミックスを WAV に書く
	/// @param directory 書き出し先 (無ければ作る)
	/// @param baseName ファイル名の先頭。<baseName>_mix.wav と <baseName>_<グループ名>.wav ができる
	/// @return 書いたファイルの数
	static int exportStems(const Result& result, const std::filesystem::path& directory,
		const std::string& baseName, bool dither = true)
	{
		std::filesystem::create_directories(directory);
		int written = 0;
		const auto write = [&](const std::string& suffix, const StereoBuffer& audio)
		{
			const auto pcm = toPcm16StereoDithered(audio, dither);
			const auto wav = WavWriter::toWav(pcm, audio.sampleRate, 2);
			std::ofstream ofs(directory / (baseName + "_" + suffix + ".wav"), std::ios::binary);
			if (ofs)
			{
				ofs.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
				if (ofs.good())
				{
					++written;
				}
			}
		};
		write("mix", result.mix);
		for (const auto& stem : result.stems)
		{
			write(stem.name, stem.audio);
		}
		return written;
	}

private:
	/// @brief トラックを割り当てるチャンネル
	struct Placement
	{
		int channel = 0;
		int rhythmBase = -1;  ///< リズム MML のとき、ドラム 0 のチャンネル
	};

	static constexpr int SYNTH_FIRST_FREE = 0;

	/// @brief 曲の最初のテンポ。#tempo の指定があればそれ、無ければ 120。MML の T は、その上に重なる
	[[nodiscard]] int initialTempo() const noexcept
	{
		return m_options.tempo > 0 ? m_options.tempo : 120;
	}

	[[nodiscard]] int findGroup(const std::string& name) const
	{
		for (std::size_t i = 0; i < m_groups.size(); ++i)
		{
			if (m_groups[i].name == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void validate() const
	{
		for (const TrackSpec& track : m_tracks)
		{
			for (const float amount : track.sends)
			{
				if (!(amount >= 0.0f && amount <= 1.0f))
				{
					throw std::invalid_argument("ModernSong: track '" + track.name + "' has a send outside 0 to 1");
				}
			}
		}
		// 戻りのステム名は send_<エフェクト名>。同名のグループがあると書き出すファイルが重なる
		const SendPlan sends = planSends();
		for (std::size_t e = 0; e < SendBus::EFFECT_COUNT; ++e)
		{
			const std::string stemName = std::string("send_") + SendBus::effectName(e);
			if (sends.used[e] && findGroup(stemName) >= 0)
			{
				throw std::invalid_argument("ModernSong: group name '" + stemName + "' is reserved for the return stem of the send");
			}
		}
		for (const auto& rule : m_ducks)
		{
			for (const auto& target : rule.targets)
			{
				if (findGroup(target) < 0)
				{
					throw std::invalid_argument("ModernSong: duck target group '" + target + "' does not exist");
				}
			}
			if (rule.signal)
			{
				if (findGroup(rule.by) < 0)
				{
					throw std::invalid_argument("ModernSong: signal duck needs a group name, but '" + rule.by + "' is not a group");
				}
			}
			else if (!isTrackOrGroup(rule.by))
			{
				throw std::invalid_argument("ModernSong: duck source '" + rule.by + "' is neither a track nor a group");
			}
		}
	}

	[[nodiscard]] bool isTrackOrGroup(const std::string& name) const
	{
		if (findGroup(name) >= 0)
		{
			return true;
		}
		return std::any_of(m_tracks.begin(), m_tracks.end(), [&](const TrackSpec& t) { return t.name == name; });
	}

	/// @brief 同じチップの中でチャンネルを取り合うトラックの区分 (OPNA の FM と SSG は別)
	[[nodiscard]] static int slotOf(const TrackSpec& spec) noexcept
	{
		const int kind = static_cast<int>(spec.chip);
		return (spec.chip == ChipKind::Opna && spec.ssg) ? kind + 10 : kind;
	}

	/// @brief ch= で明示できるチャンネルの範囲。古いチップの範囲は buildEvents が Driver に問い合わせる
	[[nodiscard]] static std::pair<int, int> explicitSpan(const TrackSpec& spec) noexcept
	{
		switch (spec.chip)
		{
		case ChipKind::Synth: return {0, SynthDevice::CHANNELS - 1};
		case ChipKind::Drums: return {0, DrumSynthDriver::KIT_CHANNEL};
		case ChipKind::Sampler: return {0, SamplerDevice::CHANNELS - 1};
		case ChipKind::Opna:
			return spec.ssg ? std::make_pair(OpnaDriver::SSG_CHANNEL_BASE, OpnaDriver::DRUM_CHANNEL_BASE - 1)
				: std::make_pair(OpnaDriver::FM_CHANNEL_BASE, OpnaDriver::SSG_CHANNEL_BASE - 1);
		default: return {0, 1 << 20};
		}
	}

	/// @brief 古いチップ (OPM から NES まで) を ChipCatalog の種類に直す
	[[nodiscard]] static mitiru_mml::ChipKind catalogKindOf(ChipKind kind) noexcept
	{
		switch (kind)
		{
		case ChipKind::Opm: return mitiru_mml::ChipKind::Opm;
		case ChipKind::Opl: return mitiru_mml::ChipKind::Opl;
		case ChipKind::Opl2: return mitiru_mml::ChipKind::Opl2;
		case ChipKind::Opl3: return mitiru_mml::ChipKind::Opl3;
		case ChipKind::Sn76489: return mitiru_mml::ChipKind::Sn76489;
		case ChipKind::Ay8910: return mitiru_mml::ChipKind::Ay8910;
		case ChipKind::Nes: return mitiru_mml::ChipKind::Nes;
		default: return mitiru_mml::ChipKind::OpnaFm;
		}
	}

	[[nodiscard]] static bool isLegacyChip(ChipKind kind) noexcept
	{
		return kind != ChipKind::Synth && kind != ChipKind::Drums && kind != ChipKind::Sampler;
	}

	/// @brief 各トラックのチャンネルを決める (グループごと、チップごとに、先着順。ch= を書いたトラックの分は避ける)
	[[nodiscard]] std::vector<Placement> planTracks() const
	{
		std::vector<Placement> plan(m_tracks.size());
		using Slot = std::pair<int, int>; // (グループ, 区分)
		std::map<Slot, std::set<int>> taken;
		// OPNA のリズム (ドラム) のトラックがあるグループでは、ドラムが FM のチャンネル 3〜5 を使う
		std::set<int> rhythmGroups;
		for (const TrackSpec& spec : m_tracks)
		{
			if (spec.chip == ChipKind::Opna && spec.rhythm)
			{
				rhythmGroups.insert(findGroup(spec.group));
			}
		}
		const auto fmLast = [&](const TrackSpec& spec)
		{
			return rhythmGroups.count(findGroup(spec.group)) != 0 ? OpnaDriver::FM_CHANNEL_BASE + 2 : OpnaDriver::SSG_CHANNEL_BASE - 1;
		};
		for (const TrackSpec& spec : m_tracks)
		{
			if (spec.channel < 0)
			{
				continue;
			}
			if (spec.chip == ChipKind::Sampler && spec.drum < 0 && !spec.rhythm && spec.channel == SamplerDevice::PERCUSSION_CHANNEL)
			{
				throw std::invalid_argument("ModernSong: track '" + spec.name + "' asks for channel 9, which is the GM percussion channel (use drum=)");
			}
			if (spec.chip == ChipKind::Opna && !spec.ssg && !spec.rhythm && spec.channel > fmLast(spec) && spec.channel < OpnaDriver::SSG_CHANNEL_BASE)
			{
				throw std::invalid_argument("ModernSong: track '" + spec.name + "' asks for FM channel " + std::to_string(spec.channel)
					+ ", which the OPNA rhythm track of group '" + spec.group + "' uses");
			}
			if (spec.rhythm)
			{
				throw std::invalid_argument("ModernSong: track '" + spec.name + "' is a rhythm track and cannot take ch=");
			}
			const auto span = explicitSpan(spec);
			if (spec.channel < span.first || spec.channel > span.second)
			{
				throw std::invalid_argument("ModernSong: track '" + spec.name + "' asks for channel " + std::to_string(spec.channel)
					+ ", which this chip does not have");
			}
			if (!taken[{findGroup(spec.group), slotOf(spec)}].insert(spec.channel).second)
			{
				throw std::invalid_argument("ModernSong: channel " + std::to_string(spec.channel) + " is already used in group '"
					+ spec.group + "' (track '" + spec.name + "')");
			}
		}

		std::map<Slot, int> nextChannel;
		const auto pick = [&](const TrackSpec& spec, int first, int last, bool skipPercussion) -> int
		{
			const Slot slot{findGroup(spec.group), slotOf(spec)};
			int& next = nextChannel.try_emplace(slot, first).first->second;
			const std::set<int>& used = taken[slot];
			while (next <= last && (used.count(next) != 0 || (skipPercussion && next == SamplerDevice::PERCUSSION_CHANNEL)))
			{
				++next;
			}
			if (next > last)
			{
				throw std::invalid_argument("ModernSong: group '" + spec.group + "' has no free channel left on this chip (track '"
					+ spec.name + "')");
			}
			return next++;
		};

		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			const TrackSpec& spec = m_tracks[t];
			Placement p;
			if (spec.chip == ChipKind::Drums)
			{
				if (spec.rhythm)
				{
					p.channel = 0;
					p.rhythmBase = 0;
				}
				else if (spec.channel >= 0)
				{
					p.channel = spec.channel;
				}
				else
				{
					p.channel = (spec.drum >= 0) ? spec.drum : DrumSynthDriver::KIT_CHANNEL;
				}
			}
			else if (spec.chip == ChipKind::Sampler)
			{
				if (spec.rhythm)
				{
					p.channel = SamplerDriver::RHYTHM_CHANNEL_BASE;
					p.rhythmBase = SamplerDriver::RHYTHM_CHANNEL_BASE;
				}
				else if (spec.channel >= 0)
				{
					p.channel = spec.channel;
				}
				else if (spec.drum >= 0)
				{
					p.channel = SamplerDevice::PERCUSSION_CHANNEL;
				}
				else
				{
					p.channel = pick(spec, 0, SamplerDevice::CHANNELS - 1, true);
				}
			}
			else if (spec.chip == ChipKind::Opna)
			{
				if (spec.rhythm)
				{
					p.channel = OpnaDriver::DRUM_CHANNEL_BASE;
					p.rhythmBase = OpnaDriver::DRUM_CHANNEL_BASE;
				}
				else if (spec.channel >= 0)
				{
					p.channel = spec.channel;
				}
				else if (spec.ssg)
				{
					p.channel = pick(spec, OpnaDriver::SSG_CHANNEL_BASE, OpnaDriver::DRUM_CHANNEL_BASE - 1, false);
				}
				else
				{
					p.channel = pick(spec, OpnaDriver::FM_CHANNEL_BASE, fmLast(spec), false);
				}
			}
			else if (spec.chip == ChipKind::Synth)
			{
				p.channel = (spec.channel >= 0) ? spec.channel : pick(spec, SYNTH_FIRST_FREE, SynthDevice::CHANNELS - 1, false);
			}
			else
			{
				if (spec.rhythm)
				{
					throw std::invalid_argument("ModernSong: track '" + spec.name + "' cannot use rhythm on this chip");
				}
				const chip_catalog::ChannelRange range = chip_catalog::autoRange(catalogKindOf(spec.chip), false);
				p.channel = (spec.channel >= 0) ? spec.channel : pick(spec, range.first, range.last, false);
			}
			plan[t] = p;
		}
		return plan;
	}

	/// @brief トラック 1 本のイベント (拡張文法を有効にして、スウィングを掛けたもの)
	[[nodiscard]] std::vector<SeqEvent> buildEvents(std::size_t t, const Placement& place) const
	{
		const TrackSpec& spec = m_tracks[t];
		TrackTarget target;
		target.track = static_cast<std::uint32_t>(t);
		target.chip = static_cast<std::uint32_t>(spec.chip);
		target.channel = place.channel;
		target.caps.valid = true;
		target.caps.volumeEvents = true;
		target.extended = !isLegacyChip(spec.chip);
		if (isLegacyChip(spec.chip))
		{
			// 古いチップは今までの文法のまま。レガートやポルタメントが使えるかは、チャンネルの能力を見て決める
			const std::unique_ptr<ChipDriver> probe = (spec.chip == ChipKind::Opna)
				? std::unique_ptr<ChipDriver>(std::make_unique<OpnaDriver>())
				: chip_catalog::makeDriver(catalogKindOf(spec.chip));
			if (place.channel < 0 || place.channel >= probe->channelCount() || !probe->channelCaps(place.channel).valid)
			{
				throw std::invalid_argument("ModernSong: track '" + spec.name + "' uses channel " + std::to_string(place.channel)
					+ ", which this chip does not have");
			}
			target.caps = probe->channelCaps(place.channel);
		}
		// pan= と vol= は、MML の先頭に P と V を書くのと同じ (リズム文法には付けない)
		std::string mml;
		if (!spec.rhythm)
		{
			if (spec.pan >= 0)
			{
				mml += "P" + std::to_string(spec.pan) + " ";
			}
			if (spec.volume >= 0)
			{
				mml += "V" + std::to_string(spec.volume) + " ";
			}
		}
		mml += spec.mml;
		std::vector<SeqEvent> events = spec.rhythm
			? MmlEventBuilder::fromRhythm(expandTextLoops(mml), target, place.rhythmBase)
			: MmlEventBuilder::fromMelodic(MmlEventBuilder::expandLoops(MmlParser::parse(mml)), target);
		if (spec.swing)
		{
			applySwing(events, m_options.swingGrid, m_options.swingRatio);
		}
		return events;
	}

	/// @brief 描く前の準備: チャンネルの割り当て、トラックのイベント、全グループに配るテンポとループ、グループごとのイベント
	/// @details render() と ModernSongStream が同じものを使う。
	struct Prepared
	{
		std::vector<Placement> plan;
		std::vector<std::vector<SeqEvent>> trackEvents;
		std::vector<SeqEvent> shared;
		std::vector<std::vector<SeqEvent>> groupEvents;
	};

	[[nodiscard]] Prepared prepare() const
	{
		validate();
		Prepared prep;
		prep.plan = planTracks();
		const std::size_t groupCount = m_groups.size();
		const auto& plan = prep.plan;
		auto& trackEvents = prep.trackEvents;
		auto& shared = prep.shared;
		auto& groupEvents = prep.groupEvents;

		// 1. トラックごとのイベント (スウィングは MML の tick に掛ける)
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			trackEvents.push_back(buildEvents(t, plan[t]));
		}

		// 全グループが同じ時間軸になるよう、全トラックのテンポとループの位置を全グループへ配る。
		// 同じ tick に別のテンポがあれば、(トラック番号、通し番号) が後ろのものが効く
		for (const auto& list : trackEvents)
		{
			for (const auto& ev : list)
			{
				if (ev.type == SeqEventType::Tempo || ev.type == SeqEventType::LoopPoint)
				{
					shared.push_back(ev);
				}
			}
		}

		groupEvents.resize(groupCount);
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			const auto g = static_cast<std::size_t>(findGroup(m_tracks[t].group));
			groupEvents[g].insert(groupEvents[g].end(), trackEvents[t].begin(), trackEvents[t].end());
		}
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			for (const auto& ev : shared)
			{
				bool own = false;
				for (std::size_t t = 0; t < m_tracks.size(); ++t)
				{
					if (ev.track == t && static_cast<std::size_t>(findGroup(m_tracks[t].group)) == g)
					{
						own = true;
					}
				}
				if (!own)
				{
					groupEvents[g].push_back(ev);
				}
			}
			sortSeqEvents(groupEvents[g]);
		}
		return prep;
	}

	/// @brief エフェクトへの送りがあるトラックと、使用するエフェクト
	struct SendPlan
	{
		std::vector<std::size_t> tracks;
		std::array<bool, SendBus::EFFECT_COUNT> used{};

		[[nodiscard]] bool any() const noexcept
		{
			return !tracks.empty();
		}
	};

	[[nodiscard]] SendPlan planSends() const
	{
		SendPlan plan;
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			bool has = false;
			for (std::size_t e = 0; e < SendBus::EFFECT_COUNT; ++e)
			{
				if (m_tracks[t].sends[e] > 0.0f)
				{
					has = true;
					plan.used[e] = true;
				}
			}
			if (has)
			{
				plan.tracks.push_back(t);
			}
		}
		return plan;
	}

	static constexpr std::uint32_t NO_CHIP = 0xFFFFFFFFu;

	/// @brief トラック t だけが鳴るイベント
	/// @details グループのイベントを写し、ほかのトラックの発音は一致しないチップ番号に変える。
	/// Tempo、LoopPoint、全イベントの最後の tick は残し、ループの 2 周目を含む時間軸をグループと揃える
	[[nodiscard]] std::vector<SeqEvent> isolatedEvents(std::size_t t, const Prepared& prep) const
	{
		std::vector<SeqEvent> events = prep.groupEvents[static_cast<std::size_t>(findGroup(m_tracks[t].group))];
		for (SeqEvent& ev : events)
		{
			if (ev.track != static_cast<std::uint32_t>(t) && ev.type != SeqEventType::Tempo && ev.type != SeqEventType::LoopPoint)
			{
				ev.chip = NO_CHIP;
			}
		}
		return events;
	}

	[[nodiscard]] StereoBuffer renderIsolatedTrack(std::size_t t, const Prepared& prep) const
	{
		DriverSet drivers = makeTrackDrivers(t, prep.plan);
		return renderDrivers(drivers, isolatedEvents(t, prep));
	}

	/// @brief 合図のトラック (またはグループのトラック全部) の発音の、サンプル位置
	[[nodiscard]] std::vector<std::uint64_t> triggerPositions(
		const std::vector<std::vector<SeqEvent>>& trackEvents, const std::vector<SeqEvent>& shared,
		const std::string& by) const
	{
		std::set<std::uint32_t> sources;
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			if (m_tracks[t].name == by || m_tracks[t].group == by)
			{
				sources.insert(static_cast<std::uint32_t>(t));
			}
		}
		std::vector<SeqEvent> all = shared;
		for (const auto& list : trackEvents)
		{
			for (const auto& ev : list)
			{
				if (ev.type != SeqEventType::Tempo && ev.type != SeqEventType::LoopPoint)
				{
					all.push_back(ev);
				}
			}
		}
		// shared には各トラックのテンポが入っている。全トラックのイベントと合わせても (track, order) は一意
		std::sort(all.begin(), all.end(), seqEventBefore);
		all.erase(std::unique(all.begin(), all.end(), [](const SeqEvent& a, const SeqEvent& b)
			{ return a.track == b.track && a.order == b.order; }), all.end());

		std::vector<std::uint64_t> positions;
		TickClock clock(m_options.sampleRate, TickClock::Rounding::CarryRemainder, initialTempo());
		playTimeline(all, m_options.loopCount, clock,
			[&](const SeqEvent& ev)
			{
				if (ev.type == SeqEventType::NoteOn && sources.count(ev.track) != 0)
				{
					const std::uint64_t pos = clock.position();
					if (positions.empty() || positions.back() != pos)
					{
						positions.push_back(pos);
					}
				}
			},
			[](std::uint32_t) {});
		return positions;
	}

	using DriverSet = std::array<std::unique_ptr<ChipDriver>, CHIP_KIND_COUNT>;

	/// @brief グループのトラックが使うチップの Driver を作り、reset と音色の設定まで済ませる (ChipKind の番号の位置に入る)
	[[nodiscard]] DriverSet makeGroupDrivers(std::size_t group, const std::vector<Placement>& plan) const
	{
		return makeDrivers([&](std::size_t t) { return static_cast<std::size_t>(findGroup(m_tracks[t].group)) == group; }, plan);
	}

	/// @brief 送りのために、トラック 1 本だけを鳴らす Driver を作る
	[[nodiscard]] DriverSet makeTrackDrivers(std::size_t track, const std::vector<Placement>& plan) const
	{
		return makeDrivers([&](std::size_t t) { return t == track; }, plan);
	}

	/// @param include トラック番号を受け取り、そのトラックの Driver を作るかを返す
	template <class Include>
	[[nodiscard]] DriverSet makeDrivers(const Include& include, const std::vector<Placement>& plan) const
	{
		DriverSet drivers;
		SynthDriver* synth = nullptr;
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			const TrackSpec& spec = m_tracks[t];
			if (!include(t))
			{
				continue;
			}
			const auto kind = static_cast<std::size_t>(spec.chip);
			if (!drivers[kind])
			{
				switch (spec.chip)
				{
				case ChipKind::Synth:
				{
					auto d = std::make_unique<SynthDriver>(m_options.sampleRate);
					synth = d.get();
					drivers[kind] = std::move(d);
					break;
				}
				case ChipKind::Drums:
					drivers[kind] = std::make_unique<DrumSynthDriver>(m_options.sampleRate);
					break;
				case ChipKind::Sampler:
				{
					auto d = std::make_unique<SamplerDriver>(m_options.sampleRate);
					d->setSoundFont(m_soundFont);
					drivers[kind] = std::move(d);
					break;
				}
				case ChipKind::Opna:
					drivers[kind] = std::make_unique<OpnaDriver>();
					break;
				default:
					drivers[kind] = chip_catalog::makeDriver(catalogKindOf(spec.chip));
					break;
				}
			}
			if (spec.chip == ChipKind::Synth && spec.patch.has_value())
			{
				synth->setPatch(plan[t].channel, *spec.patch);
			}
		}

		for (auto& d : drivers)
		{
			if (d)
			{
				d->reset();
			}
		}
		for (std::size_t t = 0; t < m_tracks.size(); ++t)
		{
			const TrackSpec& spec = m_tracks[t];
			if (include(t) && spec.voice >= 0
				&& !(spec.chip == ChipKind::Synth && spec.patch.has_value()))
			{
				drivers[static_cast<std::size_t>(spec.chip)]->setVoice(plan[t].channel, spec.voice);
			}
		}
		return drivers;
	}

	/// @brief グループ 1 つを鳴らして、ステレオの音を返す
	/// @details チップごとに自分のネイティブレートで最後まで描き (OPNA は約 998 kHz)、出力レートに落としてから、
	///          チップの並びの順に足す。
	[[nodiscard]] StereoBuffer renderGroup(std::size_t group, const std::vector<SeqEvent>& events,
		const std::vector<Placement>& plan) const
	{
		DriverSet drivers = makeGroupDrivers(group, plan);
		return renderDrivers(drivers, events);
	}

	/// @brief Driver をチップ順に加え、ステレオの音にする
	[[nodiscard]] StereoBuffer renderDrivers(DriverSet& drivers, const std::vector<SeqEvent>& events) const
	{
		StereoBuffer out;
		out.sampleRate = m_options.sampleRate;

		for (std::size_t k = 0; k < drivers.size(); ++k)
		{
			if (!drivers[k])
			{
				continue;
			}
			const std::vector<float> chipOut = renderChip(*drivers[k], static_cast<std::uint32_t>(k), events);
			out.growTo(chipOut.size() / 2);
			for (std::size_t i = 0; i < chipOut.size(); ++i)
			{
				out.samples[i] += chipOut[i];
			}
		}
		return out;
	}

	/// @brief チップ 1 つを、そのネイティブレートの時間軸で最後まで (リリースの尾も含めて) 描き、出力レートで返す
	[[nodiscard]] std::vector<float> renderChip(ChipDriver& driver, std::uint32_t chipIndex,
		const std::vector<SeqEvent>& events) const
	{
		ChipDevice& device = driver.device();
		const std::uint32_t native = device.nativeRate();
		const std::uint32_t outRate = m_options.sampleRate;
		// ネイティブレートが出力より低いチップは、同じフレームを繰り返して出力以上に揃えてから落とす
		const std::uint32_t repeat = (native >= outRate) ? 1u : (outRate + native - 1) / native;
		const std::uint32_t inRate = native * repeat;
		const bool direct = (inRate == outRate);

		TickClock clock(native, TickClock::Rounding::CarryRemainder, initialTempo());
		BoxDecimatorStereo decimator(inRate, outRate);
		std::vector<float> out;
		std::vector<float> block;
		std::vector<float> held;
		const auto renderFrames = [&](std::uint32_t frames)
		{
			block.resize(static_cast<std::size_t>(frames) * 2);
			device.generate(block.data(), frames);
			if (direct)
			{
				out.insert(out.end(), block.begin(), block.end());
			}
			else if (repeat == 1)
			{
				decimator.push(block.data(), frames, out);
			}
			else
			{
				held.resize(static_cast<std::size_t>(frames) * repeat * 2);
				for (std::size_t i = 0; i < frames; ++i)
				{
					for (std::uint32_t r = 0; r < repeat; ++r)
					{
						held[(i * repeat + r) * 2] = block[i * 2];
						held[(i * repeat + r) * 2 + 1] = block[i * 2 + 1];
					}
				}
				decimator.push(held.data(), static_cast<std::size_t>(frames) * repeat, out);
			}
		};

		playTimeline(events, m_options.loopCount, clock,
			[&](const SeqEvent& ev)
			{
				if (ev.chip == chipIndex)
				{
					applySeqEvent(driver, ev);
				}
			},
			renderFrames);

		// 最後のイベントのあとの、リリースの尾
		std::uint64_t tail = static_cast<std::uint64_t>(m_options.tailSeconds * static_cast<double>(native));
		while (tail > 0)
		{
			const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(tail, 4096));
			renderFrames(chunk);
			tail -= chunk;
		}
		if (!direct)
		{
			decimator.finish(out);
		}
		return out;
	}

	Options m_options;
	std::vector<GroupSpec> m_groups;
	std::vector<TrackSpec> m_tracks;
	std::vector<DuckRule> m_ducks;
	std::shared_ptr<const Sf2File> m_soundFont;
};

} // namespace mitiru_mml
