#pragma once

/// @file BoundaryLayout.hpp
/// @brief 境界を渡る型の sizeof をまとめた指紋 (ABI v63、ADR 0112)。
/// @details バージョンの番号 (kCurrentApiVersion) が同じでも、途中の commit で Screen や HostView の配置が動くことがある。
///          DLL は registerGame で `ModuleApi::boundaryLayoutHash` に自分の値を書き、host は自分の値との完全一致を要求する。
///          見るのは sizeof と、予約の先頭の offset。バージョンの途中で起きる変更はほぼ末尾への追加か予約の切り出しで、追加は sizeof に、
///          切り出しは予約の先頭の offset に出る (切り出しは sizeof を動かさない)。欄の並べ替えは見えない。
///          Screen はゲーム DLL が inline で持つので、ここに含める。

#include <cstddef>
#include <cstdint>

#include <mitiru/core/Screen.hpp>
#include <mitiru/module/DrawCommands.hpp>
#include <mitiru/module/HostView.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/render/LocalLights.hpp>
#include <mitiru/render/SceneLook.hpp>

namespace mitiru::module::detail
{

/// FNV-1a (32 bit) を size の列に掛ける。順は固定で、足すときは末尾に
[[nodiscard]] constexpr std::uint32_t fnv1aSizes(const std::size_t* sizes, std::size_t count) noexcept
{
	std::uint32_t h = 2166136261u;
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::uint64_t v = static_cast<std::uint64_t>(sizes[i]);
		for (int b = 0; b < 8; ++b)
		{
			h ^= static_cast<std::uint32_t>((v >> (b * 8)) & 0xFFu);
			h *= 16777619u;
		}
	}
	return h;
}

/// 指紋に入れる型。InputSnapshot と FrameIntents は録画と ModuleApi の wire、DrawCommand 系は POD コマンドの経路、
/// Screen は on_draw(Screen&) の経路で DLL が inline で持つ配置、HostView と SceneLook と LocalLight は Screen が写す POD。
/// 後半の offset は、予約から欄を切り出した変更 (sizeof が動かない) を見分けるためで、足すときは末尾に
inline constexpr std::size_t kBoundarySizes[] = {
	sizeof(InputSnapshot),
	sizeof(FrameIntents),
	sizeof(ModuleApi),
	sizeof(DrawContext),
	sizeof(DrawCommand),
	sizeof(DrawCommandBuffer),
	sizeof(HostView),
	sizeof(render::SceneLook),
	sizeof(render::LocalLight),
	sizeof(mitiru::Screen),
	offsetof(InputSnapshot, inputHoldMs),
	offsetof(FrameIntents, reservedV62),
	offsetof(DrawCommandBuffer, reservedV62),
	offsetof(HostView, audioSpectrum),
	offsetof(HostView, reserved),
	offsetof(render::SceneLook, reserved),
	offsetof(render::LocalLight, layers),
	mitiru::Screen::layoutReservedOffset(),
};

inline constexpr std::uint32_t kBoundaryLayoutHash =
	fnv1aSizes(kBoundarySizes, sizeof(kBoundarySizes) / sizeof(kBoundarySizes[0]));

static_assert(kBoundaryLayoutHash != 0, "0 は「DLL が書かなかった」の印に取っておく");
static_assert(kBoundaryLayoutHash != kBoundaryLayoutUnchecked, "「比べない」の印と値が衝突した。kBoundarySizes の並びを見直す");

/// host が DLL の指紋を受け入れるか。「比べない」の印は受け入れる (Screen を持たない最小の DLL 用)
[[nodiscard]] constexpr bool boundaryLayoutAccepts(std::uint32_t dllHash) noexcept
{
	return dllHash == kBoundaryLayoutHash || dllHash == kBoundaryLayoutUnchecked;
}

}  // namespace mitiru::module::detail
