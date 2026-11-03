#pragma once

/// @file HostBoundary63.hpp
/// @brief ABI v63 (ADR 0112) の host 側の小さな定数と知らせ。v62 の HostBoundary62.hpp と同じ作り

#include <cstdint>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/detail/HostBoundary62.hpp>

namespace mitiru::module::detail
{

/// Screen::takePendingV62 の bit (v62 の bit 1..2 の続き)。書体の無い画面でルビの組版を頼まれた
inline constexpr std::uint32_t kPendingV63Ruby = 1u << 3;

/// 入口だけの機能が使われたときの warnOnce の key (試験が発火を確かめる)
inline constexpr const char* kWarnRuby               = "abi63.ruby";
inline constexpr const char* kWarnLightLayers        = "abi63.light_layers";
inline constexpr const char* kWarnMaterialBackground = "abi63.material_background";

/// v62 と v63 の印をまとめて、使われた機能ごとに 1 度だけ知らせる
inline void reportPendingV63(std::uint32_t bits)
{
	reportPendingV62(bits);
	if ((bits & kPendingV63Ruby) != 0)
	{
		debug::warnOnce(kWarnRuby, "ルビ (drawTextRuby) は、この描画では書体を使えないので記法ごと横書きで描きます");
	}
}

}  // namespace mitiru::module::detail
