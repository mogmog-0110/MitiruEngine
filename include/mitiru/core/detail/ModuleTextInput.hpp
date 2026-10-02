#pragma once

/// @file ModuleTextInput.hpp
/// @brief IME とゲームの間の受け渡し (ADR 0048 段階 1): 変換中の文字列を InputSnapshot へ、
///        FrameIntents::textInputRect (ゲームの画面座標) を窓のクライアント座標へ。

#include <cmath>
#include <cstring>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/platform/win32/ImeComposition.hpp>

namespace mitiru::detail
{

struct ClientRect
{
	long left = 0, top = 0, right = 0, bottom = 0;
};

/// @brief 入力欄 (x, y, w, h、論理解像度の座標) をクライアント座標へ。マウスの論理化
///        (Engine::tickMouseScalingPhase、client × screen / window) の逆。
[[nodiscard]] inline ClientRect textInputRectToClient(const float (&rect)[4], float screenW, float screenH,
                                                      float windowW, float windowH) noexcept
{
	const float sx = (screenW > 0.0f) ? windowW / screenW : 1.0f;
	const float sy = (screenH > 0.0f) ? windowH / screenH : 1.0f;
	ClientRect r;
	r.left   = std::lround(rect[0] * sx);
	r.top    = std::lround(rect[1] * sy);
	r.right  = std::lround((rect[0] + rect[2]) * sx);
	r.bottom = std::lround((rect[1] + rect[3]) * sy);
	return r;
}

/// @brief 変換中の文字列を snapshot へ写す。収まらない分は文字の切れ目で切り、キャレットもその中に収める。
inline void fillSnapshotIme(const platform::ImeCompositionUtf8& c, module::InputSnapshot& snap) noexcept
{
	const std::size_t cap = sizeof(snap.imeComposition) - 1;  // null 終端分
	const std::size_t n = platform::utf8PrefixWithin(c.text, cap);
	std::memcpy(snap.imeComposition, c.text.data(), n);
	std::memset(snap.imeComposition + n, 0, sizeof(snap.imeComposition) - n);
	snap.imeCompositionLen = static_cast<std::uint8_t>(n);
	snap.imeCursor = static_cast<std::uint8_t>((c.cursorBytes < n) ? c.cursorBytes : n);
}

}  // namespace mitiru::detail
