#pragma once

/// @file SaveSlots.hpp
/// @brief セーブスロットの置き場 (1 スロット = 1 個の .mslot)。一覧・削除・自動セーブの輪番・クラウドへの写し。
/// @details 書き込みは AtomicFile で差し替え、前の版を .bak に残す。読む時に本命が化けていれば .bak に戻り、
///          そのことを状態で知らせる (黙って古い版を返さない)。自動セーブは auto_0 .. auto_{n-1} を輪番で使い、
///          一番新しいものが化けていれば 1 つ前の自動セーブへ戻る。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <mitiru/save/AtomicFile.hpp>
#include <mitiru/save/SlotFile.hpp>

namespace mitiru::save
{

enum class SlotStatus : std::uint8_t
{
	Ok,
	RestoredFromBackup,  ///< 本命が化けていたので 1 つ前の版 (.bak) を返した
	RestoredFromMirror,  ///< 手元に無かったのでクラウドの写しを返した
	Missing,
	Corrupt,             ///< 本命も .bak も読めない
	NewerFormat,         ///< 新しいエンジンが書いた形式
	InvalidName,
	WriteFailed,
};

[[nodiscard]] constexpr const char* slotStatusText(SlotStatus s) noexcept
{
	switch (s)
	{
	case SlotStatus::Ok:                 return "ok";
	case SlotStatus::RestoredFromBackup: return "restored-from-backup";
	case SlotStatus::RestoredFromMirror: return "restored-from-cloud";
	case SlotStatus::Missing:            return "missing";
	case SlotStatus::Corrupt:            return "corrupt";
	case SlotStatus::NewerFormat:        return "newer-format";
	case SlotStatus::InvalidName:        return "invalid-name";
	case SlotStatus::WriteFailed:        return "write-failed";
	}
	return "unknown";
}

[[nodiscard]] constexpr bool slotReadable(SlotStatus s) noexcept
{
	return s == SlotStatus::Ok || s == SlotStatus::RestoredFromBackup || s == SlotStatus::RestoredFromMirror;
}

struct SlotRead
{
	std::string slot;
	SlotStatus status = SlotStatus::Missing;
	SlotMeta meta;
	std::vector<std::uint8_t> thumbnailPng;
	std::vector<std::uint8_t> payload;
};

struct SlotWrite
{
	std::string slot;
	bool ok = false;
	std::string error;
};

struct SlotInfo
{
	std::string slot;
	SlotStatus status = SlotStatus::Missing;
	SlotMeta meta;
	bool hasThumbnail = false;
};

/// @brief スロットのファイルを別の置き場 (Steam Cloud など) へ写す口。名前は "<slot>.mslot"。
class ISaveMirror
{
public:
	virtual ~ISaveMirror() = default;
	virtual void pushSlot(std::string_view fileName, std::span<const std::uint8_t> bytes) = 0;
	virtual void removeSlot(std::string_view fileName) = 0;
	[[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> pullSlot(std::string_view fileName) = 0;
};

/// @brief スロット名に使えるのは [A-Za-z0-9_-] の 1〜27 文字 (FrameIntents::saveSlot に収まる長さ)。
[[nodiscard]] inline bool isValidSlotName(std::string_view slot) noexcept
{
	if (slot.empty() || slot.size() > 27) { return false; }
	return std::all_of(slot.begin(), slot.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	});
}

class SaveSlotStore
{
public:
	/// hud.save / hud.load でこの名前を使うと、自動セーブの輪番になる。
	static constexpr std::string_view kAutosaveSlot = "auto";
	static constexpr std::string_view kExtension = ".mslot";

	explicit SaveSlotStore(std::filesystem::path dir, int autosaveCount = 3)
		: m_dir(std::move(dir)), m_autosaveCount(std::clamp(autosaveCount, 1, 16)) {}

	[[nodiscard]] const std::filesystem::path& dir() const noexcept { return m_dir; }
	[[nodiscard]] int autosaveCount() const noexcept { return m_autosaveCount; }
	void setMirror(ISaveMirror* mirror) noexcept { m_mirror = mirror; }

	[[nodiscard]] std::filesystem::path pathOf(std::string_view slot) const
	{
		return m_dir / (std::string(slot) + std::string(kExtension));
	}

	[[nodiscard]] SlotWrite write(std::string_view slot, std::span<const std::uint8_t> payload, const SlotMeta& meta,
	                              std::span<const std::uint8_t> thumbnailPng = {})
	{
		SlotWrite out;
		out.slot = std::string(slot);
		if (!isValidSlotName(slot))
		{
			out.error = "スロット名が不正 (使えるのは A-Z a-z 0-9 _ - の 1〜27 文字): " + out.slot;
			return out;
		}
		const auto bytes = encodeSlotFile(meta, thumbnailPng, payload);
		// 本命が化けている時は控えへ写さない。写すと、読める 1 つ前の版 (.bak) を化けた版で上書きする
		SlotRead current;
		const bool primaryGood = readInto(pathOf(slot), current) == SlotStatus::Ok;
		out.ok = writeFileAtomic(pathOf(slot), bytes, primaryGood, &out.error);
		if (out.ok && m_mirror != nullptr) { m_mirror->pushSlot(fileNameOf(slot), bytes); }
		return out;
	}

	[[nodiscard]] SlotRead read(std::string_view slot) const
	{
		SlotRead out;
		out.slot = std::string(slot);
		if (!isValidSlotName(slot)) { out.status = SlotStatus::InvalidName; return out; }
		const auto path = pathOf(slot);
		const SlotStatus primary = readInto(path, out);
		// 新しい形式の本命を古い .bak で置き換えて見せると、その後の保存で進行が巻き戻る
		if (slotReadable(primary) || primary == SlotStatus::NewerFormat) { out.status = primary; return out; }
		if (readInto(withSuffix(path, ".bak"), out) == SlotStatus::Ok)
		{
			out.status = SlotStatus::RestoredFromBackup;
			return out;
		}
		if (primary == SlotStatus::Missing && pullFromMirror(slot, out)) { return out; }
		out.status = primary;
		return out;
	}

	/// @brief 置き場にある全スロット (新しく保存した順)。化けたスロットも状態付きで並べる。
	[[nodiscard]] std::vector<SlotInfo> list() const
	{
		std::vector<SlotInfo> out;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(m_dir, ec))
		{
			const auto& p = entry.path();
			if (p.extension() != kExtension || !entry.is_regular_file(ec)) { continue; }
			const std::string slot = p.stem().string();
			if (!isValidSlotName(slot)) { continue; }
			const SlotRead r = read(slot);
			out.push_back(SlotInfo{ slot, r.status, r.meta, !r.thumbnailPng.empty() });
		}
		std::sort(out.begin(), out.end(), [](const SlotInfo& a, const SlotInfo& b) {
			if (a.meta.savedAtUnixSec != b.meta.savedAtUnixSec) { return a.meta.savedAtUnixSec > b.meta.savedAtUnixSec; }
			return a.slot < b.slot;
		});
		return out;
	}

	bool remove(std::string_view slot)
	{
		if (!isValidSlotName(slot)) { return false; }
		std::error_code ec;
		const auto path = pathOf(slot);
		const bool removed = std::filesystem::remove(path, ec);
		std::filesystem::remove(withSuffix(path, ".bak"), ec);
		if (m_mirror != nullptr) { m_mirror->removeSlot(fileNameOf(slot)); }
		return removed;
	}

	[[nodiscard]] static std::string autosaveName(int index) { return "auto_" + std::to_string(index); }

	/// @brief 次に自動セーブを書く枠。空いた枠と読めない枠が先、無ければ一番古い枠。
	///        読める自動セーブを、化けた枠より先に上書きしないため。
	[[nodiscard]] std::string autosaveTarget() const
	{
		int best = 0;
		std::int64_t bestTime = 0;
		for (int i = 0; i < m_autosaveCount; ++i)
		{
			SlotRead r;
			if (readInto(pathOf(autosaveName(i)), r) != SlotStatus::Ok) { return autosaveName(i); }
			if (i == 0 || r.meta.savedAtUnixSec < bestTime) { best = i; bestTime = r.meta.savedAtUnixSec; }
		}
		return autosaveName(best);
	}

	[[nodiscard]] SlotWrite writeAutosave(std::span<const std::uint8_t> payload, const SlotMeta& meta,
	                                      std::span<const std::uint8_t> thumbnailPng = {})
	{
		return write(autosaveTarget(), payload, meta, thumbnailPng);
	}

	/// @brief 読める自動セーブのうち一番新しいもの。全部読めなければ最後に見た失敗を返す。
	[[nodiscard]] SlotRead readLatestAutosave() const
	{
		SlotRead best;
		best.slot = std::string(kAutosaveSlot);
		bool found = false;
		for (int i = 0; i < m_autosaveCount; ++i)
		{
			SlotRead r = read(autosaveName(i));
			if (!slotReadable(r.status))
			{
				if (!found && r.status != SlotStatus::Missing) { best.status = r.status; }
				continue;
			}
			if (!found || r.meta.savedAtUnixSec > best.meta.savedAtUnixSec) { best = std::move(r); found = true; }
		}
		return best;
	}

private:
	[[nodiscard]] static std::string fileNameOf(std::string_view slot)
	{
		return std::string(slot) + std::string(kExtension);
	}

	[[nodiscard]] static SlotStatus readInto(const std::filesystem::path& path, SlotRead& out)
	{
		const auto bytes = readWholeFile(path);
		if (!bytes) { return SlotStatus::Missing; }
		SlotDecoded d = decodeSlotFile(*bytes);
		if (d.status == SlotDecodeStatus::NewerFormat) { return SlotStatus::NewerFormat; }
		if (d.status != SlotDecodeStatus::Ok) { return SlotStatus::Corrupt; }
		out.meta = std::move(d.meta);
		out.thumbnailPng = std::move(d.thumbnailPng);
		out.payload = std::move(d.payload);
		return SlotStatus::Ok;
	}

	bool pullFromMirror(std::string_view slot, SlotRead& out) const
	{
		if (m_mirror == nullptr) { return false; }
		const auto bytes = m_mirror->pullSlot(fileNameOf(slot));
		if (!bytes) { return false; }
		SlotDecoded d = decodeSlotFile(*bytes);
		if (d.status != SlotDecodeStatus::Ok) { return false; }
		out.meta = std::move(d.meta);
		out.thumbnailPng = std::move(d.thumbnailPng);
		out.payload = std::move(d.payload);
		out.status = SlotStatus::RestoredFromMirror;
		return true;
	}

	std::filesystem::path m_dir;
	int m_autosaveCount = 3;
	ISaveMirror* m_mirror = nullptr;
};

}  // namespace mitiru::save
