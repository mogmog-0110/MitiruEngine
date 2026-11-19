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
	offsetof(FrameIntents, mmlCount),
	offsetof(DrawCommandBuffer, reservedV62),
	offsetof(HostView, audioSpectrum),
	offsetof(HostView, reserved),
	offsetof(render::SceneLook, reserved),
	offsetof(render::LocalLight, layers),
	mitiru::Screen::layoutReservedOffset(),
	// ここから v64 (ADR 0164)。v63 の DLL が書く指紋は、ここより前の 18 個だけで計算されている
	offsetof(DrawCommandBuffer, bulkPool),
	offsetof(FrameIntents, mml),
	offsetof(FrameIntents, reservedV64),
	sizeof(MmlIntent),
};

inline constexpr std::size_t kBoundarySizesCount = sizeof(kBoundarySizes) / sizeof(kBoundarySizes[0]);
/// v63 の指紋に入っていた個数。v64 は後ろへ足しただけなので、先頭のこの個数が v63 の並び
inline constexpr std::size_t kBoundarySizesCountV63 = 18;

inline constexpr std::uint32_t kBoundaryLayoutHash = fnv1aSizes(kBoundarySizes, kBoundarySizesCount);

/// v63 の DLL が書く指紋 (ADR 0164)。v64 が v63 から動かした型は DrawCommandBuffer の sizeof だけなので、その 1 つを v63 の値に置いて同じ式で計算する。
/// コンパイル時に計算するので、Debug と RelWithDebInfo の sizeof(Screen) の違いも、DLL が書く値と一致する
inline constexpr std::size_t kDrawCommandBufferSizeV63 = 417872;
[[nodiscard]] constexpr std::uint32_t layoutHashV63() noexcept
{
	std::size_t sizes[kBoundarySizesCountV63]{};
	for (std::size_t i = 0; i < kBoundarySizesCountV63; ++i) { sizes[i] = kBoundarySizes[i]; }
	sizes[5] = kDrawCommandBufferSizeV63;   // kBoundarySizes[5] は sizeof(DrawCommandBuffer)
	return fnv1aSizes(sizes, kBoundarySizesCountV63);
}
inline constexpr std::uint32_t kBoundaryLayoutHashPredecessor = layoutHashV63();
static_assert(kBoundarySizes[5] == sizeof(DrawCommandBuffer), "layoutHashV63 は kBoundarySizes[5] が DrawCommandBuffer の sizeof であることに頼っている");
static_assert(kBoundaryLayoutHashPredecessor != kBoundaryLayoutHash, "v64 の指紋は v63 と違わなければ、v63 の DLL と区別できない");

static_assert(kBoundaryLayoutHash != 0, "0 は「DLL が書かなかった」の印に取っておく");
static_assert(kBoundaryLayoutHash != kBoundaryLayoutUnchecked, "「比べない」の印と値が衝突した。kBoundarySizes の並びを見直す");

/// host が DLL の指紋を受け入れるか。「比べない」の印は受け入れる (Screen を持たない最小の DLL 用)
[[nodiscard]] constexpr bool boundaryLayoutAccepts(std::uint32_t dllHash) noexcept
{
	return dllHash == kBoundaryLayoutHash || dllHash == kBoundaryLayoutUnchecked;
}

/// DLL のバージョンに対して host が期待する指紋 (拒否のメッセージに出す)
[[nodiscard]] constexpr std::uint32_t expectedLayoutHash(std::uint32_t dllWireVersion) noexcept
{
	return wireVersionIsPredecessor(dllWireVersion) ? kBoundaryLayoutHashPredecessor : kBoundaryLayoutHash;
}

/// host が DLL を受け入れるか。今のバージョンは番号と指紋の完全一致、直前のバージョンは (番号とビルドの構成が v63、指紋が v63 の値か「比べない」のマーカー)。
/// DLL が v63 のときだけ、指紋は v63 の値と比べる (v64 の指紋を書いた DLL が番号だけ古い、という食い違いを受け入れない)
[[nodiscard]] constexpr bool moduleAccepted(std::uint32_t dllWireVersion, std::uint32_t dllHash) noexcept
{
	if (dllWireVersion == kWireApiVersion) { return boundaryLayoutAccepts(dllHash); }
	if (wireVersionIsPredecessor(dllWireVersion))
	{
		return dllHash == kBoundaryLayoutHashPredecessor || dllHash == kBoundaryLayoutUnchecked;
	}
	return false;
}

}  // namespace mitiru::module::detail
