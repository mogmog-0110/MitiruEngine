#pragma once

/// @file SaveWatch.hpp
/// @brief セーブとロードが「このフレームで終わった」ことと、その成否を知るための小さな POD。
/// @details GameMemory に 1 つ置き、update の最初に `poll` を呼ぶ。`in.saveSucceeded()` は直前の結果を
///          持ち続けるので、2 回目のセーブが終わったフレームを区別できない。ロードが成功すると GameMemory は
///          セーブした時点の中身に置き換わるので、待ち状態を GameMemory に置くだけでは「ロードが終わった」
///          と「セーブが終わった」も区別できない (セーブした中身には、セーブを待つ印が入っている)。
///
///          区別には host が数えるスロット一覧の番号 (`in.slotListSerial()`、答えるたびに 1 増え、1 フレームに
///          高々 1 回) を使う。ロードは一覧を頼んで 1 フレーム待ってから、もう一度一覧を頼みつつ出す。すると
///          ロード後の最初の `poll` では、セーブした時点から番号が 2 以上進んでいる。セーブの直後は高々 1 しか
///          進まない。ロードの結果はセーブより 1 フレーム遅れて届く。
///
///          状態はすべてこの struct にあるので、巻き戻し・リプレイ・決定論の見張り (同じ update の再実行) で
///          同じ結果になる。ロードで読むセーブも SaveWatch::save で書いたものにする (hud.save で直接書いた
///          セーブは、ロードしても loaded() が立たない)。

#include <cstdint>
#include <cstring>

#include <mitiru/module/Game.hpp>

namespace mitiru
{

enum class SaveResult : std::uint8_t { None = 0, Ok = 1, Failed = 2 };

struct SaveWatch
{
	enum Phase : std::uint8_t { Idle = 0, Saving = 1, LoadQueued = 2, Loading = 3 };
	/// 待ちの段階とスロット名は inspector に並べても読む人がいないので、反射では中を見せない
	using MitiruOpaqueReflect = void;

	std::uint8_t  phase = Idle;
	SaveResult    savedNow = SaveResult::None;
	SaveResult    loadedNow = SaveResult::None;
	std::uint8_t  _pad = 0;
	std::uint32_t seenSerial = 0;    ///< 前の poll で見た in.slotListSerial()
	char          loadSlot[28] = {}; ///< ロードを 1 フレーム待たせる間のスロット名 (FrameIntents::loadSlot と同じ長さ)

	/// @brief このフレームでセーブが終わっていれば Ok / Failed、そうでなければ None
	[[nodiscard]] SaveResult saved() const noexcept { return savedNow; }
	/// @brief このフレームでロードが終わっていれば Ok / Failed、そうでなければ None
	[[nodiscard]] SaveResult loaded() const noexcept { return loadedNow; }
	/// @brief セーブかロードの結果を待っている間は true。この間の save / load は受け付けない
	[[nodiscard]] bool busy() const noexcept { return phase != Idle; }

	/// @brief セーブを頼む。chapter を渡すとスロットの一覧に章の名前が出る。待っている間は false
	bool save(Hud hud, const char* slot = "slot0", const char* chapter = nullptr) noexcept
	{
		if (busy()) { return false; }
		if (chapter != nullptr) { hud.saveSlot(slot, chapter); }
		else { hud.save(slot); }
		phase = Saving;
		return true;
	}

	/// @brief ロードを頼む。結果は 2 フレーム後の loaded() に出る。待っている間は false
	bool load(Hud hud, const char* slot = "slot0") noexcept
	{
		if (busy()) { return false; }
		std::memset(loadSlot, 0, sizeof(loadSlot));
		for (std::size_t i = 0; i + 1 < sizeof(loadSlot) && slot[i] != '\0'; ++i) { loadSlot[i] = slot[i]; }
		hud.listSlots();
		phase = LoadQueued;
		return true;
	}

	/// @brief update の最初に毎フレーム 1 回呼ぶ (呼ばないフレームがあると番号の進みを数え違える)
	void poll(const Input& in, Hud hud) noexcept
	{
		savedNow = loadedNow = SaveResult::None;
		const std::uint32_t serial = in.slotListSerial();
		const std::uint32_t advanced = serial - seenSerial;
		seenSerial = serial;
		switch (phase)
		{
		case Saving:
			// 2 以上進んでいれば、この中身はロードで読んだセーブ (セーブした時点の印が残っている)
			if (advanced >= 2) { loadedNow = SaveResult::Ok; }
			else { savedNow = in.raw()->lastSaveResult == 1 ? SaveResult::Ok : SaveResult::Failed; }
			phase = Idle;
			break;
		case LoadQueued:
			hud.load(loadSlot);
			hud.listSlots();
			phase = Loading;
			break;
		case Loading:
			// 成功していれば GameMemory は置き換わっていてここへは来ない。来たのは読めなかったとき
			loadedNow = in.raw()->lastLoadResult == 1 ? SaveResult::Ok : SaveResult::Failed;
			phase = Idle;
			break;
		default:
			break;
		}
	}
};

static_assert(sizeof(SaveWatch) == 36, "SaveWatch は詰め物の無い 36 byte (GameMemory を byte で比べるため)");

}  // namespace mitiru
