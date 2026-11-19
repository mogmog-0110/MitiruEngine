#pragma once
/// @file ModernTrackState.hpp
/// @brief 拡張文法 (X パラメーター、和音、アルペジエーター、Q) を、イベントに展開する
/// @details MmlEventBuilder が、拡張を有効にしたトラックでだけ使う。チップの知識は持たない。
///          イベントの出力先は、(tick, type, channel, a, b, c) を受ける関数 emit として渡す。

#include <mitiru_mml/ModernRng.hpp>
#include <mitiru_mml/MmlExtensions.hpp>
#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace mitiru_mml
{

/// @brief 拡張を有効にしたトラック 1 本の、MML を読み進める間の状態
class ModernTrackState
{
public:
	explicit ModernTrackState(std::uint32_t seed = 1) noexcept
		: m_seed(seed)
	{
	}

	/// @brief Q 値 (1〜8)。8 が最も長く、小さいほど短く切る
	void setQuantize(int q) noexcept
	{
		m_quantize = std::clamp(q, 1, 8);
	}

	/// @brief X コマンドを展開する
	/// @details アルペジエーターの設定は状態に保存し、イベントにしない。ほかは Param イベントにする。
	///          ランプは 1 tick ごとの Param イベントにする。Driver が値をなめらかにつなぐ。
	template <class Emit>
	void applyParam(Emit&& emit, int tick, int channel, const MmlCommand& cmd)
	{
		const int id = cmd.value;
		const ModernParamInfo* info = findModernParam(id);
		if (info == nullptr)
		{
			return;
		}
		const int to = std::clamp(cmd.extra, info->minValue, info->maxValue);
		if (isArpParam(id))
		{
			setArp(id, to);
			return;
		}

		int from = (cmd.extra2 == modern_param::FROM_CURRENT) ? lastValue(*info) : cmd.extra2;
		from = std::clamp(from, info->minValue, info->maxValue);
		if (cmd.duration <= 0)
		{
			emit(tick, SeqEventType::Param, channel, id, to, 0);
			m_last[id] = to;
			return;
		}

		const int ticks = lengthTicks(cmd.duration, cmd.dotted);
		int previous = std::numeric_limits<int>::min();
		for (int k = 0; k <= ticks; ++k)
		{
			const double t = static_cast<double>(k) / static_cast<double>(ticks);
			const int v = (k == ticks) ? to : interpolate(*info, from, to, t);
			if (v != previous)
			{
				emit(tick + k, SeqEventType::Param, channel, id, v, 0);
				previous = v;
			}
		}
		m_last[id] = to;
	}

	/// @brief 音符 1 つをイベントにする (和音とアルペジエーターを含む)
	/// @param midi 最初の音の MIDI ノート番号
	/// @param toneMask 最初の音より上の音をビットで表した集合 (和音でなければ 0)
	/// @param mainTicks 音符の長さ (tick)
	template <class Emit>
	void emitNote(Emit&& emit, int tick, int channel, int midi, int toneMask, int volume, int mainTicks)
	{
		const int gate = gateTicks(mainTicks);
		std::vector<int> tones;
		tones.push_back(midi);
		for (int bit = 1; bit <= 30; ++bit)
		{
			if ((toneMask & (1 << bit)) != 0)
			{
				tones.push_back(midi + bit);
			}
		}

		if (m_arpMode == 0)
		{
			for (const int key : tones)
			{
				emit(tick, SeqEventType::NoteOn, channel, clampKey(key), volume, 0);
			}
			emit(tick + gate, SeqEventType::NoteOff, channel, 0, 0, 0);
			return;
		}

		const std::vector<int> pattern = arpPattern(tones);
		const int step = lengthTicks(m_arpRate, false);
		const int steps = std::max(1, (mainTicks + step - 1) / step);
		ModernRng rng(mixSeed(m_seed, static_cast<std::uint32_t>(tick), static_cast<std::uint32_t>(midi)));
		int last = -1;
		for (int s = 0; s < steps; ++s)
		{
			const int at = tick + s * step;
			const int remain = mainTicks - s * step;
			if (remain <= 0)
			{
				break;
			}
			std::size_t index = static_cast<std::size_t>(s) % pattern.size();
			if (m_arpMode == 4)
			{
				index = rng.nextU32() % pattern.size();
				if (pattern.size() > 1 && static_cast<int>(index) == last)
				{
					index = (index + 1) % pattern.size();
				}
				last = static_cast<int>(index);
			}
			const int length = std::min(step, remain);
			const int on = std::max(1, std::min(length - 1, length * m_arpGate / 100));
			emit(at, SeqEventType::NoteOn, channel, clampKey(pattern[index]), volume, 0);
			emit(at + on, SeqEventType::NoteOff, channel, 0, 0, 0);
		}
	}

	/// @brief 音符の発音の長さ (tick)。Q8 は従来と同じく 1 tick 短く、Q が小さいほど短い
	[[nodiscard]] int gateTicks(int mainTicks) const noexcept
	{
		if (m_quantize >= 8)
		{
			return std::max(1, mainTicks - 1);
		}
		return std::max(1, mainTicks * m_quantize / 8);
	}

private:
	static int lengthTicks(int lengthValue, bool dotted) noexcept
	{
		const int base = (TickClock::TICKS_PER_QUARTER * 4) / std::max(1, lengthValue);
		return std::max(1, dotted ? base * 3 / 2 : base);
	}

	static int clampKey(int key) noexcept
	{
		return std::clamp(key, 0, 127);
	}

	static bool isArpParam(int id) noexcept
	{
		return id == modern_param::ARP || id == modern_param::ARPRATE
			|| id == modern_param::ARPOCT || id == modern_param::ARPGATE;
	}

	void setArp(int id, int value) noexcept
	{
		switch (id)
		{
		case modern_param::ARP: m_arpMode = value; break;
		case modern_param::ARPRATE: m_arpRate = value; break;
		case modern_param::ARPOCT: m_arpOctaves = value; break;
		case modern_param::ARPGATE: m_arpGate = value; break;
		default: break;
		}
	}

	[[nodiscard]] int lastValue(const ModernParamInfo& info) const
	{
		const auto it = m_last.find(info.id);
		return it == m_last.end() ? info.defaultValue : it->second;
	}

	[[nodiscard]] static int interpolate(const ModernParamInfo& info, int from, int to, double t)
	{
		if (info.logScale && from > 0 && to > 0)
		{
			return static_cast<int>(std::lround(static_cast<double>(from)
				* std::pow(static_cast<double>(to) / static_cast<double>(from), t)));
		}
		return static_cast<int>(std::lround(static_cast<double>(from) + (static_cast<double>(to - from)) * t));
	}

	/// @brief アルペジエーターが鳴らす音の並び (1 周ぶん)
	[[nodiscard]] std::vector<int> arpPattern(const std::vector<int>& tones) const
	{
		std::vector<int> up;
		for (int octave = 0; octave < m_arpOctaves; ++octave)
		{
			for (const int key : tones)
			{
				up.push_back(key + 12 * octave);
			}
		}
		if (m_arpMode == 2)
		{
			std::reverse(up.begin(), up.end());
			return up;
		}
		if (m_arpMode == 3 && up.size() > 2)
		{
			// 両端を重ねずに戻る
			for (std::size_t i = up.size() - 2; i >= 1; --i)
			{
				up.push_back(up[i]);
				if (i == 1) break;
			}
		}
		return up;
	}

	std::uint32_t m_seed;
	int m_quantize = 8;
	int m_arpMode = 0;
	int m_arpRate = 16;
	int m_arpOctaves = 1;
	int m_arpGate = 70;
	std::map<int, int> m_last;
};

} // namespace mitiru_mml
