#pragma once

/// @file DebugMenu.hpp
/// @brief ゲームがデバッグメニューの項目を host へ渡すための宣言 (ABI v59、ADR 0085)。Input を使うため、Game.hpp の末尾から読み込む。
/// @details ゲームは現在の項目を DebugMenu に並べる関数を 1 つ定義し、`MITIRU_DEBUG_MENU` で登録する。表示、開閉、キーボード、マウス、パッドの操作は host が受け持つ。項目を選ぶと、次の update に "debug.menu" の操作が届く。`debugMenuEvent` で id と値を読み、状態を変える。メニューは値を保持せず、関数が返す現在の値を毎回表示する。ゲームが操作を拒んだ場合、メニューの値も変わらない。

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru
{

/// @brief `MITIRU_DEBUG_MENU` の関数が受け取り、項目を追加する。上限の kMaxDebugMenuItems を超えた項目は追加しない。
class DebugMenu
{
public:
	DebugMenu(module::DebugMenuItem* out, std::int32_t cap, std::uint32_t* flags) noexcept
		: m_out(out), m_cap(cap < 0 ? 0 : cap), m_flags(flags) {}

	void section(const char* label) noexcept { (void)add(module::kDebugMenuSection, 0, label, 0.0f, 0.0f, 0.0f, 0.0f, nullptr); }

	/// 選ぶと id が届く。closeAfter が true なら、ステージへ移る項目などを選んだ後にメニューを閉じる
	void button(std::int32_t id, const char* label, bool closeAfter = false, bool enabled = true) noexcept
	{
		(void)add(module::kDebugMenuButton, id, label, 0.0f, 0.0f, 0.0f, 0.0f, nullptr,
		          static_cast<std::uint8_t>((closeAfter ? module::kDebugItemCloseOnPick : 0) | (enabled ? 0 : module::kDebugItemDisabled)));
	}

	/// 選ぶと反転後の値が 0 または 1 で届く
	void toggle(std::int32_t id, const char* label, bool on, bool enabled = true) noexcept
	{
		(void)add(module::kDebugMenuToggle, id, label, on ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f, nullptr,
		          static_cast<std::uint8_t>(enabled ? 0 : module::kDebugItemDisabled));
	}

	/// 左右で step ずつ変わり、minValue から maxValue の範囲に収めた値が届く
	void number(std::int32_t id, const char* label, float value, float minValue, float maxValue, float step = 1.0f,
	            bool enabled = true) noexcept
	{
		(void)add(module::kDebugMenuNumber, id, label, value, minValue, maxValue, step, nullptr,
		          static_cast<std::uint8_t>(enabled ? 0 : module::kDebugItemDisabled));
	}

	/// names は "Easy|Normal|Hard" のように | で区切り、40 byte まで指定できる。選ぶと次の番号が 0 始まりで届く
	void choice(std::int32_t id, const char* label, std::int32_t index, const char* names, bool enabled = true) noexcept
	{
		(void)add(module::kDebugMenuChoice, id, label, static_cast<float>(index), 0.0f, 0.0f, 0.0f, names,
		          static_cast<std::uint8_t>(enabled ? 0 : module::kDebugItemDisabled));
	}

	/// 表示専用の 1 行。pinned が true なら、現在のステージ名などをメニューを閉じている間も FPS の横に表示する
	void text(const char* label, const char* value, bool pinned = false) noexcept
	{
		(void)add(module::kDebugMenuText, 0, label, 0.0f, 0.0f, 0.0f, 0.0f, value,
		          static_cast<std::uint8_t>(pinned ? module::kDebugItemPinned : 0));
	}

	/// 開発用のキーである host の F7 から F12 とデバッグメニューを無効にする。keepScreenshotKey が true なら F12 だけ残す。何度呼んでもよい。呼ばなければ無効にしない
	void devToolsOff(bool off = true, bool keepScreenshotKey = false) noexcept
	{
		if (m_flags == nullptr) { return; }
		if (off) { *m_flags |= module::kDebugMenuDevToolsOff; } else { *m_flags &= ~module::kDebugMenuDevToolsOff; }
		if (keepScreenshotKey) { *m_flags |= module::kDebugMenuKeepScreenshotKey; }
	}

	[[nodiscard]] std::int32_t count() const noexcept { return m_count; }
	[[nodiscard]] bool full() const noexcept { return m_count >= m_cap; }

private:
	static void copyText(char* dst, std::size_t cap, const char* src) noexcept
	{
		std::size_t n = 0;
		if (src != nullptr) { while (n + 1 < cap && src[n] != '\0') { ++n; } }
		if (n > 0) { std::memcpy(dst, src, n); }
		dst[n] = '\0';
	}

	bool add(std::uint8_t kind, std::int32_t id, const char* label, float value, float minValue, float maxValue,
	         float step, const char* text, std::uint8_t flags = 0) noexcept
	{
		if (m_out == nullptr || m_count >= m_cap) { return false; }
		module::DebugMenuItem& item = m_out[m_count++];
		std::memset(&item, 0, sizeof(item));
		item.id = id;
		item.kind = kind;
		item.flags = flags;
		item.value = value;
		item.minValue = minValue;
		item.maxValue = maxValue;
		item.step = step;
		copyText(item.label, sizeof(item.label), label);
		copyText(item.text, sizeof(item.text), text);
		return true;
	}

	module::DebugMenuItem* m_out;
	std::int32_t           m_cap;
	std::uint32_t*         m_flags;
	std::int32_t           m_count = 0;
};

/// @brief 選ばれた項目 1 件。value は Toggle では 0 または 1、Number では変更後の値、Choice では選んだ番号、Button では 0
struct DebugMenuEvent
{
	std::int32_t id = 0;
	float        value = 0.0f;
};

/// @brief このフレームに届いた "debug.menu" の操作を読む。操作がなければ false を返す。1 フレームに 1 件まで届く
[[nodiscard]] inline bool debugMenuEvent(Input in, DebugMenuEvent& out) noexcept
{
	if (!in.action("debug.menu")) { return false; }
	out.id = in.actionPayloadInt("debug.menu", "id", 0);
	out.value = in.actionPayloadFloat("debug.menu", "v", 0.0f);
	return true;
}

namespace module::detail
{

template <class GameType, class Fn>
std::int32_t debugMenuFor(const void* memory, DebugMenuItem* out, std::int32_t cap, std::uint32_t* flags, Fn&& fn)
{
	if (memory == nullptr || out == nullptr || cap <= 0) { return 0; }
	if (flags != nullptr) { *flags = 0; }
	DebugMenu menu(out, cap, flags);
	fn(*static_cast<const GameType*>(memory), menu);
	return menu.count();
}

}  // namespace module::detail

}  // namespace mitiru

/// ゲームのデバッグメニューの項目を host へ渡す (v59)。fn の形式は `void fn(const GameType&, mitiru::DebugMenu&)` で、現在の項目を並べる。host はメニューが開いている間、毎フレーム呼ぶため、重い処理はしない。ファイルスコープに 1 回だけ書く
#define MITIRU_DEBUG_MENU(GameType, fn)                                                                          \
	extern "C" MITIRU_GAME_EXPORT                                                                                \
	std::int32_t mitiru_module_debug_menu(const void* memory, mitiru::module::DebugMenuItem* out,                \
	                                      std::int32_t cap, std::uint32_t* flags)                                \
	{                                                                                                            \
		return mitiru::module::detail::debugMenuFor<GameType>(memory, out, cap, flags, fn);                      \
	}
