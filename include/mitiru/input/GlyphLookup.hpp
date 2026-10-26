#pragma once

/// @file GlyphLookup.hpp
/// @brief UI に出すボタンの絵柄を、操作の名前と今使っている機器から決める。
/// @details RML の `<img src="glyph:jump"/>` は、操作 jump の割り当てのうち今使っている機器 (キーボードとマウス、
///          またはパッド) の最初の入力を、パッドの機種の書き方の絵柄にする。`glyph:pad:A` のように入力の名前を
///          直に書けば、割り当てを通さずにその入力の絵柄になる。

#include <cstdint>
#include <string>
#include <string_view>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/input/ActionMap.hpp>
#include <mitiru/input/InputDeviceKind.hpp>
#include <mitiru/input/PadGlyphs.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

[[nodiscard]] constexpr bool belongsTo(const InputSource& s, InputDevice d) noexcept
{
	const bool padSource = s.kind == SourceKind::PadButton || s.kind == SourceKind::PadAxis;
	return s.kind != SourceKind::None && padSource == (d == InputDevice::Gamepad);
}

/// @brief "glyph:" の後ろの名前の絵柄の名前 ("xbox_a"、"key_space" など)。今の機器に割り当てが無ければ空。
/// @details ゲーム DLL が自分の HUD に描く時は in.inputDevice() と in.padFamily() (ABI v50) を渡す。利用者の
///          割り当て (overrides) は host の設定にあって DLL からは見えないので、DLL は {} を渡して表の既定で引く。
[[nodiscard]] inline std::string glyphFor(std::string_view name, const ActionManifest& actions,
                                          const BindingOverrides& overrides, InputDevice device, PadFamily family,
                                          KeyLabelFn keyLabel = nullptr)
{
	if (name.find(':') != std::string_view::npos)
	{
		const auto source = parseInputSource(name);
		if (!source && ambiguousDigitSource(name) >= 0)
		{
			debug::verboseOnce("glyph.ambiguous." + std::string(name), "glyph:" + std::string(name) + " の "
				+ ambiguousDigitMessage(name.substr(name.find(':') + 1)));
		}
		return source ? inputGlyph(*source, family, keyLabel).glyph : std::string();
	}
	const ActionDef* def = actions.find(name);
	if (def == nullptr) { return {}; }
	// 絵のある入力を先に探す。Ctrl+K のような組やホイールには絵が無く、HUD に透明な絵を出してしまう
	const auto& list = effectiveBindings(*def, overrides);
	for (const InputSource& s : list)
	{
		if (belongsTo(s, device) && s.mods == 0 && s.kind != SourceKind::Wheel) { return inputGlyph(s, family, keyLabel).glyph; }
	}
	for (const InputSource& s : list)
	{
		if (belongsTo(s, device)) { return inputGlyph(s, family, keyLabel).glyph; }
	}
	return {};
}

[[nodiscard]] inline std::string glyphFor(std::string_view name, const ActionManifest& actions,
                                          const BindingOverrides& overrides, InputDevice device, PadKind kind,
                                          KeyLabelFn keyLabel = nullptr)
{
	return glyphFor(name, actions, overrides, device, padFamilyOf(kind), keyLabel);
}

}  // namespace mitiru::input
