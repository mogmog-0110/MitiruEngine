// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <chrono>
#include <string>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>

// ── hud.save / hud.load (セーブスロット) のクラス外定義 ─────────────────

MITIRU_INLINE mitiru::save::SaveSlotStore mitiru::Engine::saveSlotStore() const
{
	save::SaveSlotStore store(m_config.saveDir, m_config.autosaveSlots);
	store.setMirror(m_saveMirror);
	return store;
}

MITIRU_INLINE mitiru::save::MemoryLayout mitiru::Engine::moduleMemoryLayout() const noexcept
{
	return save::MemoryLayout{ m_moduleMemory, m_moduleMemorySize, m_moduleReflection.identity(),
	                           m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount() };
}

MITIRU_INLINE void mitiru::Engine::checkSaveRoundtripIfAsked(const save::SaveSlotStore& store, const std::string& slot)
{
	if (!m_config.saveRoundtripTest) { return; }
	// --save-roundtrip-test: save → 読み戻し → 再 save が bit 一致するか (Factorio FFF #158)。
	// 累積差分に埋もれないよう検出のたびに stderr へ 1 行出す (warnOnce しない)。
	const save::SlotRead written = store.read(slot);
	const auto divergedField = module::save::checkMsavRoundtrip(
		written.payload, m_moduleMemorySize, module::kWireApiVersion, m_moduleReflection.identity(),
		m_moduleReflection.fieldsData(), m_moduleReflection.fieldCount());
	if (divergedField.has_value())
	{
		if (divergedField->empty())
		{
			console::noticef("スロット %s で、セーブしてロードしてもう一度セーブした中身が最初と違います。", slot.c_str());
		}
		else
		{
			console::noticef("スロット %s で、セーブしてロードしてもう一度セーブした中身が最初と違います (field %s)。"
				"この field の保存と読み込みを確かめてください。", slot.c_str(), divergedField->c_str());
		}
	}
}

MITIRU_INLINE bool mitiru::Engine::saveModuleMemory(const std::string& slot, std::string_view chapter)
{
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0)
	{
		debug::warnOnce("save.no-memory",
			"GameMemory の大きさ (memorySize) が 0 なので、hud.save でセーブできません。"
			"MITIRU_GAME の GameMemory 型に状態を持たせてください。");
		return false;
	}
	save::SlotMeta meta;
	meta.savedAtUnixSec = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	meta.playtimeSec = m_playtimeSec;
	meta.gameVersion = m_config.gameVersion;
	meta.chapter = std::string(chapter);
	const auto pixels = capture();
	const save::SlotThumbnail thumb = save::makeSlotThumbnail(pixels, captureWidth(), captureHeight());
	meta.thumbWidth = thumb.width;
	meta.thumbHeight = thumb.height;

	// GameMemory の外に持つ状態 (ADR 0054) は GameMemory の後ろに続けて書く。保存できなければセーブも失敗にする
	// (GameMemory だけのセーブは、ロードした時に状態が食い違う)。
	if (!captureModuleSideState(m_sideScratch, true)) { return false; }
	auto store = saveSlotStore();
	const save::SlotWrite w = save::saveMemoryToSlot(store, slot, moduleMemoryLayout(),
	                                                 module::kWireApiVersion, meta, thumb.png, m_sideScratch);
	if (!w.ok)
	{
		debug::warnOnce("save.write." + slot, "hud.save でセーブの書き込みに失敗しました (" + w.error
			+ ")。セーブの置き場に書き込めるか、空き容量があるかを確かめてください。");
		return false;
	}
	checkSaveRoundtripIfAsked(store, w.slot);
	return true;
}

MITIRU_INLINE bool mitiru::Engine::loadModuleMemory(const std::string& slot)
{
	const save::MemoryLoad r = save::loadMemoryFromSlot(saveSlotStore(), slot, moduleMemoryLayout(), effectiveSaveMigrator());
	if (!save::memoryLoaded(r))
	{
		debug::warnOnce("load.reject." + slot, "hud.load でスロット " + slot + " を読めません (" + r.message + ")。");
		return false;
	}
	// 窓口を持つ game は image まで続けて渡す。image の無い記録・形の番号が違う記録は rewindModuleMemory が
	// 何も書き換えずに断り、理由を warnOnce で出す。窓口を持たない game は image を使わない
	std::vector<std::uint8_t> bytes = r.memory;
	if (moduleHasSideState()) { bytes.insert(bytes.end(), r.sideImage.begin(), r.sideImage.end()); }
	if (!rewindModuleMemory(bytes.data(), static_cast<std::uint32_t>(bytes.size())))
	{
		debug::warnOnce("load.reject.side." + slot, "hud.load でスロット " + r.slot
			+ " をロードしませんでした。MITIRU_SIDE_STATE の状態を記録から戻せません (理由はひとつ前の知らせにあります)。");
		return false;
	}
	if (r.kind != save::MemoryLoadKind::Exact)
	{
		const std::string text = "hud.load でスロット " + r.slot + " を読みました (" + r.message + ")。";
		if (r.kind == save::MemoryLoadKind::FieldsMoved) { debug::warnOnce("load.migrate." + slot, text); }
		else { debug::verboseOnce("load.migrate." + slot, text); }
	}
	if (r.slotStatus == save::SlotStatus::RestoredFromBackup || r.slotStatus == save::SlotStatus::RestoredFromMirror)
	{
		const char* const from = (r.slotStatus == save::SlotStatus::RestoredFromBackup) ? "予備" : "クラウドの写し";
		debug::warnOnce("load.restored." + slot, "hud.load でスロット " + r.slot + " のセーブが読めなかったので、"
			+ std::string(from) + "を使いました。");
	}
	m_playtimeSec = r.meta.playtimeSec;
	return true;
}
