// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Boundary.hpp
/// @brief ABI v48 (ADR 0056) で開いた入口の host 側。InputSnapshot の末尾を埋め、FrameIntents の末尾を実行する。
/// @details 録画に乗るのは InputSnapshot だけなので、ディスクや音の時計から読む値 (スロットの一覧、曲の拍) も
///          ここで snapshot に書けば再生では記録の値に置き換わる。パッドへの出力と印は録画に乗らない。

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::module::detail
{

template <std::size_t N>
inline void copyCStr(char (&dst)[N], std::string_view src) noexcept
{
	const std::size_t n = std::min(src.size(), N - 1);
	std::memcpy(dst, src.data(), n);
	dst[n] = '\0';
}

/// @brief セーブスロットの一覧を snapshot の形へ詰める (新しい順に最大 8 個)。
inline std::uint8_t fillSlotSummaries(const std::vector<::mitiru::save::SlotInfo>& slots, SlotSummary (&out)[kMaxSlotSummaries]) noexcept
{
	const std::size_t n = std::min<std::size_t>(slots.size(), kMaxSlotSummaries);
	for (std::size_t i = 0; i < n; ++i)
	{
		SlotSummary s{};
		copyCStr(s.slot, slots[i].slot);
		s.status = static_cast<std::uint8_t>(slots[i].status);
		s.hasThumbnail = slots[i].hasThumbnail ? 1 : 0;
		s.savedAtUnixSec = slots[i].meta.savedAtUnixSec;
		s.playtimeSec = slots[i].meta.playtimeSec;
		copyCStr(s.chapter, slots[i].meta.chapter);
		out[i] = s;
	}
	return static_cast<std::uint8_t>(n);
}

/// @brief 曲の拍を snapshot の形へ詰める。
[[nodiscard]] inline MusicClock toMusicClock(bool playing, const ::mitiru::audio::MusicClockInfo& c) noexcept
{
	MusicClock m{};
	if (!playing) { m.segment = -1; return m; }
	m.playing = 1;
	m.beatInBar = static_cast<std::uint8_t>(std::clamp(c.beatInBar, 0, 255));
	m.beatsPerBar = static_cast<std::uint8_t>(std::clamp(c.beatsPerBar, 0, 255));
	m.segment = c.segment;
	m.bar = c.bar;
	m.beatPhase = c.beatPhase;
	m.sec = c.sec;
	return m;
}

/// @brief パッドの出力 1 台分を SdlGamepadInput へ送る。出力だけの演出なので録画には乗らない。
inline void sendPadOut(::mitiru::input::SdlGamepadInput& pads, int slot, const PadOutIntent& p) noexcept
{
	const auto ms = static_cast<std::uint32_t>(std::max(p.rumbleSec, 0.0f) * 1000.0f);
	if ((p.set & kPadOutMotion) != 0) { (void)pads.setMotionEnabled(slot, p.motionEnabled != 0); }
	if ((p.set & kPadOutLight) != 0) { (void)pads.setLightbar(slot, p.light[0], p.light[1], p.light[2]); }
	for (int side = 0; side < 2; ++side)
	{
		if ((p.set & (side == 0 ? kPadOutTriggerLeft : kPadOutTriggerRight)) == 0) { continue; }
		const PadTriggerOut& t = p.trigger[side];
		const ::mitiru::input::TriggerEffect e = (t.mode == kPadTriggerResistance) ? ::mitiru::input::TriggerEffect::resistance(t.start, t.strength)
			: (t.mode == kPadTriggerVibration) ? ::mitiru::input::TriggerEffect::vibration(t.start, t.strength, t.frequency)
			: ::mitiru::input::TriggerEffect::off();
		(void)pads.setTriggerEffect(slot, side == 0 ? ::mitiru::input::TriggerSide::Left : ::mitiru::input::TriggerSide::Right, e);
	}
	if ((p.set & kPadOutRumble) != 0) { (void)pads.rumble(slot, p.rumbleLow, p.rumbleHigh, ms); }
	if ((p.set & kPadOutTriggerRumble) != 0) { (void)pads.rumbleTriggers(slot, p.triggerRumble[0], p.triggerRumble[1], ms); }
}

/// @brief 印 (FrameIntents::marks) を JSON の文字列の配列にする。印が無ければ空文字列。
[[nodiscard]] inline std::string marksJson(const FrameIntents& intents)
{
	const int n = std::clamp(intents.markCount, 0, kMaxFrameMarks);
	if (n == 0) { return {}; }
	nlohmann::json arr = nlohmann::json::array();
	for (int i = 0; i < n; ++i) { arr.push_back(std::string(intents.marks[i], boundedLen(intents.marks[i]))); }
	return arr.dump();
}

}  // namespace mitiru::module::detail

MITIRU_INLINE const char* mitiru::Engine::moduleActionManifestJson() const
{
	if (!m_moduleHost) { return nullptr; }
	const auto fn = m_moduleHost->actionManifestFn();
	return (fn != nullptr) ? fn() : nullptr;
}

MITIRU_INLINE mitiru::save::MemoryMigrator mitiru::Engine::effectiveSaveMigrator() const
{
	if (m_saveMigrator || !m_moduleHost) { return m_saveMigrator; }
	const auto fn = m_moduleHost->migrateFn();
	if (fn == nullptr) { return {}; }
	return [fn](const module::save::MsavView& old, const ::mitiru::save::MemoryLayout& now)
		-> std::optional<std::vector<std::uint8_t>>
	{
		const auto* base = static_cast<const std::uint8_t*>(now.current);
		std::vector<std::uint8_t> out(base, base + now.size);
		const std::int32_t ok = fn(old.memory.data(), old.memory.size(), old.header.layoutHash, out.data(), out.size());
		if (ok == 0) { return std::nullopt; }
		return out;
	};
}

MITIRU_INLINE void mitiru::Engine::answerSlotList(module::InputSnapshot& snap)
{
	if (!m_pendingSlotList) { return; }
	m_pendingSlotList = false;
	snap.slotCount = module::detail::fillSlotSummaries(saveSlotStore().list(), snap.slots);
	snap.slotListSerial = ++m_slotListSerial;
}

MITIRU_INLINE void mitiru::Engine::fillModuleSnapshotV48(module::InputSnapshot& snap)
{
	snap.settingsChangedMask = m_pendingSettingsMask;
	m_pendingSettingsMask = 0;
	module::detail::copyCStr(snap.language, m_config.language);
	if (m_pendingDeleteResult != 0) { snap.lastDeleteResult = m_pendingDeleteResult; m_pendingDeleteResult = 0; }
	answerSlotList(snap);
	audio::MusicClockInfo clock;
	const bool playing = (m_audioEngine != nullptr) && m_audioEngine->musicClock(clock);
	snap.music = module::detail::toMusicClock(playing, clock);
}

MITIRU_INLINE void mitiru::Engine::applyPadOutIntents(const module::FrameIntents& intents)
{
	for (int slot = 0; slot < 4; ++slot)
	{
		if (intents.padOut[slot].set != 0) { module::detail::sendPadOut(m_gamepads, slot, intents.padOut[slot]); }
	}
}

MITIRU_INLINE void mitiru::Engine::drainModuleIntentsV48(const module::FrameIntents& intents)
{
	if (intents.cameraCut != 0 && m_renderer3D) { m_renderer3D->resetTemporalHistory(); }
	applyPadOutIntents(intents);
	if (intents.slotListRequest != 0) { m_pendingSlotList = true; }
	if (intents.deleteSlotRequest != 0)
	{
		const std::string slot = module::save::sanitizeSlot(intents.deleteSlot);
		// 再生中は結果を記録の snapshot が持っているので、ファイルを消さない
		const bool replaying = static_cast<bool>(m_saveLoadOverride);
		const bool ok = !slot.empty() && (replaying || saveSlotStore().remove(slot));
		m_pendingDeleteResult = ok ? 1u : 2u;
	}
	if (m_audioEngine)
	{
		if (intents.musicIntensitySet != 0) { m_audioEngine->setMusicIntensity(std::clamp(intents.musicIntensity, 0.0f, 1.0f)); }
		if (intents.reverbZoneSet != 0)
		{
			const std::string_view id{intents.reverbZone, module::detail::boundedLen(intents.reverbZone)};
			if (!m_audioEngine->forceReverbZone(id) && !id.empty())
			{
				debug::warnOnce("hud.reverbZone." + std::string(id),
					"hud.reverbZone: mix.json に残響の場所 \"" + std::string(id) + "\" が無い (または音を書き出す host でない)");
			}
		}
	}
}
