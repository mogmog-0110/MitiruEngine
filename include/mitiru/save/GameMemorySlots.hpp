#pragma once

/// @file GameMemorySlots.hpp
/// @brief GameMemory をセーブスロットへ書き、読み戻す (hud.save / hud.load の中身)。
/// @details 中身は .msav 形式 (GameMemory + layout hash + フィールド表)。読む時に layout が今の module と
///          違えば、黙って memcpy せずに次の順で扱い、どれを通ったかを結果で返す。
///          1. 同じ layout ならそのまま
///          2. 移行関数 (ゲームが登録したもの) が返した bytes
///          3. 名前と形が一致するフィールドだけを移す (module::save::migrateMsav)
///          4. どれも駄目なら拒否
///          スロット (.mslot) が無ければ、同じ名前の .msav ファイル (スロットに包まない形) を同じ手順で読む。
///          GameMemory の外に持つ状態 (窓口、ADR 0054) の image は .msav と同じく GameMemory の後ろに続け、
///          読む時は sideImage に分けて返す。GameMemory → on_rebuild → 窓口の順に戻すのと、窓口の形が合わない記録を
///          断るのは Engine::rewindModuleMemory の仕事 (GameMemory と image を続けて渡す)。

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/module/GameMemorySave.hpp>
#include <mitiru/save/SaveSlots.hpp>

namespace mitiru::save
{

/// @brief 現在の module の GameMemory の形。
struct MemoryLayout
{
	const void* current = nullptr;          ///< 今の GameMemory (移行の土台)
	std::uint32_t size = 0;
	std::uint64_t layoutHash = 0;
	const module::FieldDescriptor* fields = nullptr;
	std::int32_t fieldCount = 0;
};

/// @brief 層が変わった記録を今の層へ移す関数。移せなければ nullopt。
using MemoryMigrator = std::function<std::optional<std::vector<std::uint8_t>>(
	const module::save::MsavView& old, const MemoryLayout& now)>;

enum class MemoryLoadKind : std::uint8_t
{
	Exact,          ///< 同じ layout
	Migrated,       ///< 移行関数で移した
	FieldsMoved,    ///< 名前と形が一致するフィールドだけ移した
	Refused,        ///< 形が合わず移せない
	Unreadable,     ///< スロットが無い・化けている・新しい形式
};

struct MemoryLoad
{
	MemoryLoadKind kind = MemoryLoadKind::Unreadable;
	SlotStatus slotStatus = SlotStatus::Missing;
	std::string slot;          ///< 実際に読んだスロット (auto は auto_N になる)
	std::vector<std::uint8_t> memory;
	std::int32_t movedFields = 0;
	SlotMeta meta;
	std::vector<std::uint8_t> sideImage;  ///< 記録にあった窓口の image (無ければ空)
	std::string message;       ///< 何が起きたかの 1 行 (成功でも移行時は入る)
	bool msavFile = false;     ///< スロットに包まない .msav ファイルから読んだ
};

[[nodiscard]] inline bool memoryLoaded(const MemoryLoad& r) noexcept
{
	return r.kind == MemoryLoadKind::Exact || r.kind == MemoryLoadKind::Migrated
	    || r.kind == MemoryLoadKind::FieldsMoved;
}

/// @brief GameMemory をスロットへ書く。slot が "auto" なら自動セーブの輪番に書く。
/// @param sideImage GameMemory の外に持つ状態の image (Engine::captureModuleSideState の結果)。無い game は空
[[nodiscard]] inline SlotWrite saveMemoryToSlot(SaveSlotStore& store, std::string_view slot,
                                                const MemoryLayout& layout, std::uint32_t abiVersion,
                                                SlotMeta meta, std::span<const std::uint8_t> thumbnailPng = {},
                                                std::span<const std::uint8_t> sideImage = {})
{
	const auto blob = module::save::encodeMsav(layout.current, layout.size, abiVersion, layout.layoutHash,
	                                           layout.fields, layout.fieldCount, sideImage);
	if (blob.empty())
	{
		SlotWrite w;
		w.slot = std::string(slot);
		w.error = "GameMemory が未申告 (memorySize=0)";
		return w;
	}
	meta.layoutHash = layout.layoutHash;
	if (slot == SaveSlotStore::kAutosaveSlot) { return store.writeAutosave(blob, meta, thumbnailPng); }
	return store.write(slot, blob, meta, thumbnailPng);
}

namespace detail
{

[[nodiscard]] inline MemoryLoad decodeMemory(std::span<const std::uint8_t> blob, const MemoryLayout& now,
                                             const MemoryMigrator& migrator, MemoryLoad out)
{
	const auto view = module::save::parseMsav(blob);
	if (!view)
	{
		out.kind = MemoryLoadKind::Unreadable;
		out.message = "中身が GameMemory の形式ではない";
		return out;
	}
	out.sideImage.assign(view->tail.begin(), view->tail.end());
	if (auto exact = module::save::decodeMsav(blob, now.size, now.layoutHash))
	{
		out.kind = MemoryLoadKind::Exact;
		out.memory = std::move(*exact);
		return out;
	}
	if (migrator)
	{
		if (auto migrated = migrator(*view, now); migrated && migrated->size() == now.size)
		{
			out.kind = MemoryLoadKind::Migrated;
			out.memory = std::move(*migrated);
			out.message = "GameMemory の層が変わっていたので、ゲームの移行関数で移した";
			return out;
		}
	}
	if (auto moved = module::save::migrateMsav(blob, now.current, now.size, now.fields, now.fieldCount,
	                                           &out.movedFields))
	{
		out.kind = MemoryLoadKind::FieldsMoved;
		out.memory = std::move(*moved);
		out.message = "GameMemory の層が変わっていたので、名前と形が一致する "
		            + std::to_string(out.movedFields) + " 個のフィールドだけ移した (他は初期値)";
		return out;
	}
	out.kind = MemoryLoadKind::Refused;
	out.message = "GameMemory の層が変わっていて移せない (記録 " + std::to_string(view->header.memorySize)
	            + " byte / 今 " + std::to_string(now.size) + " byte、MITIRU_REFLECT のフィールドも一致しない)";
	return out;
}

}  // namespace detail

/// @brief スロットから GameMemory を読む。slot が "auto" なら読める中で一番新しい自動セーブ。
[[nodiscard]] inline MemoryLoad loadMemoryFromSlot(const SaveSlotStore& store, std::string_view slot,
                                                   const MemoryLayout& now, const MemoryMigrator& migrator = {})
{
	const SlotRead r = (slot == SaveSlotStore::kAutosaveSlot) ? store.readLatestAutosave() : store.read(slot);
	MemoryLoad out;
	out.slot = r.slot;
	out.slotStatus = r.status;
	out.meta = r.meta;
	if (slotReadable(r.status)) { return detail::decodeMemory(r.payload, now, migrator, std::move(out)); }

	if (r.status == SlotStatus::Missing && isValidSlotName(slot))
	{
		if (const auto msav = readWholeFile(store.dir() / (std::string(slot) + ".msav")))
		{
			out.msavFile = true;
			out.slotStatus = SlotStatus::Ok;
			return detail::decodeMemory(*msav, now, migrator, std::move(out));
		}
	}
	out.kind = MemoryLoadKind::Unreadable;
	out.message = std::string("スロットを読めない (") + slotStatusText(r.status) + ")";
	return out;
}

}  // namespace mitiru::save
