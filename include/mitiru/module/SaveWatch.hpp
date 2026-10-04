#pragma once

/// @file SaveWatch.hpp
/// @brief セーブとロードの完了と成否を監視する SaveWatch の宣言。
/// @details GameMemory に 1 つ置き、update の最初に `poll` を呼ぶ。`in.saveSucceeded()` は直前の結果を
/// 持ち続けるので、2 回目のセーブが終わったフレームを区別できない。そこで host が結果と同じ snapshot で
/// 進める番号 (`in.saveResultSerial()` / `in.loadResultSerial()`) を、前回の `poll` で見た値と比べる。
///
/// ロードに成功すると GameMemory はセーブ時の状態に戻るが、ロードの番号は 1 進むため、戻った値と必ず異なる。
/// hud.save で直接書いたセーブでも同じ。ロードと同じフレームのセーブは番号の差から判別できないため無視し、
/// `saved()` を立てない。
///
/// 状態をすべてこの struct に置くので、巻き戻し・リプレイ・同じ update の再実行でも結果は変わらない。
/// GameMemory を作った直後 (初回・restart) の `poll` は番号を覚えるだけで、結果を出さない。

#include <cstdint>

#include <mitiru/module/Game.hpp>

namespace mitiru
{

enum class SaveResult : std::uint8_t { None = 0, Ok = 1, Failed = 2 };

struct SaveWatch
{
	enum Phase : std::uint8_t { Idle = 0, Saving = 1, Loading = 2 };
	/// 待ちの段階と番号は inspector で見る必要がないため、反射では中を見せない
	using MitiruOpaqueReflect = void;

	std::uint8_t  phase = Idle;
	SaveResult    savedNow = SaveResult::None;
	SaveResult    loadedNow = SaveResult::None;
	std::uint8_t  primed = 0;          ///< 1 = 一度 poll した。0 なら GameMemory が新しく作られたばかり
	std::uint32_t seenSaveSerial = 0;  ///< 前の poll で見た in.saveResultSerial()
	std::uint32_t seenLoadSerial = 0;  ///< 前の poll で見た in.loadResultSerial()

	/// @brief このフレームでセーブが終わっていれば Ok / Failed、そうでなければ None
	[[nodiscard]] SaveResult saved() const noexcept { return savedNow; }
	/// @brief このフレームでロードが終わっていれば Ok / Failed、そうでなければ None
	[[nodiscard]] SaveResult loaded() const noexcept { return loadedNow; }
	/// @brief セーブかロードの結果を待っている間は true。この間は save / load を受け付けない
	[[nodiscard]] bool busy() const noexcept { return phase != Idle; }

	/// @brief セーブを頼む。chapter を渡すとスロットの一覧に章の名前が出る。結果は次のフレームの saved() に出る。
///        結果待ちの間は false を返す
	bool save(Hud hud, const char* slot = "slot0", const char* chapter = nullptr) noexcept
	{
		if (busy()) { return false; }
		if (chapter != nullptr) { hud.saveSlot(slot, chapter); }
		else { hud.save(slot); }
		phase = Saving;
		return true;
	}

	/// @brief ロードを頼む。結果は次のフレームの loaded() に出る。結果待ちの間は false を返す
	bool load(Hud hud, const char* slot = "slot0") noexcept
	{
		if (busy()) { return false; }
		hud.load(slot);
		phase = Loading;
		return true;
	}

	/// @brief update の最初に毎フレーム 1 回呼ぶ。hud は呼び方をそろえるために受け取るだけで、ここでは使わない
	void poll(const Input& in, Hud /*hud*/) noexcept
	{
		savedNow = loadedNow = SaveResult::None;
		const std::uint32_t saveSerial = in.saveResultSerial();
		const std::uint32_t loadSerial = in.loadResultSerial();
		const bool fresh = primed == 0;
		const bool loadedThisFrame = !fresh && loadSerial != seenLoadSerial;
		const bool savedThisFrame = !fresh && !loadedThisFrame && saveSerial != seenSaveSerial;
		primed = 1;
		seenSaveSerial = saveSerial;
		seenLoadSerial = loadSerial;
		if (loadedThisFrame) { loadedNow = in.loadSucceeded() ? SaveResult::Ok : SaveResult::Failed; }
		if (savedThisFrame) { savedNow = in.saveSucceeded() ? SaveResult::Ok : SaveResult::Failed; }
		if (fresh || loadedThisFrame || savedThisFrame) { phase = Idle; }
	}
};

static_assert(sizeof(SaveWatch) == 12, "SaveWatch は詰め物の無い 12 byte (GameMemory を byte で比べるため)");

}  // namespace mitiru
