#pragma once

/// @file ActionMap.hpp
/// @brief キー割り当ての表 (input_actions.json) と、利用者が変えた割り当て。
/// @details ゲームは入力を Binding 表 (Game.hpp) で読む。読み口は DLL の中にあり host から見えないので、
///          ゲームが「どの論理操作を、どのキーとボタンで読んでいるか」を JSON で宣言する。
///          - slot: 割り当てを変えた時に host が押下を書き込む先 (ゲームが読むキーの 1 つ)
///          - reads: その操作のためにゲームが読むキーとボタンの全部 (slot を含む)。host はここを空にしてから
///            slot へ書くので、割り当てから外したキーで操作が漏れない
///          - defaults: 初期の割り当て
///          同じ context (空は全 context) の操作どうしで reads が重なると、片方の割り当てがもう片方に漏れる。
///          parseActionManifest はそれを誤りとして返す。

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/input/KeyNames.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

/// 1 つの表に載せられる操作の数と、1 つの操作に割り当てられる入力の数 (host は毎フレーム固定長の配列で組み替える)。
inline constexpr std::size_t kMaxActions = 64;
inline constexpr std::size_t kMaxBindingsPerAction = 8;

enum class SourceKind : std::uint8_t { None, Key, Mouse, PadButton, PadAxis };

/// @brief 入力 1 つ。Key は VK、Mouse は 0=左 1=右 2=中 3=X1 4=X2、PadButton は module::gamepad のビット、
///        PadAxis は module::gamepad::Axis の添字。PadAxis の sign は、ボタンとして読む時の向き (+1 / -1) か、
///        軸として読む時の反転 (-1)。0 は軸をそのまま読む。
struct InputSource
{
	SourceKind kind = SourceKind::None;
	std::uint16_t code = 0;
	std::int8_t sign = 0;

	[[nodiscard]] bool operator==(const InputSource&) const noexcept = default;
};

struct NamedCode
{
	std::string_view name;
	std::uint32_t code;
};

inline constexpr NamedCode kPadButtonNames[] = {
	{ "A", module::gamepad::A }, { "B", module::gamepad::B }, { "X", module::gamepad::X }, { "Y", module::gamepad::Y },
	{ "LB", module::gamepad::LB }, { "RB", module::gamepad::RB }, { "Start", module::gamepad::Start },
	{ "Back", module::gamepad::Back }, { "LS", module::gamepad::LS }, { "RS", module::gamepad::RS },
	{ "DPadUp", module::gamepad::DPadUp }, { "DPadDown", module::gamepad::DPadDown },
	{ "DPadLeft", module::gamepad::DPadLeft }, { "DPadRight", module::gamepad::DPadRight },
};

inline constexpr NamedCode kPadAxisNames[] = {
	{ "LeftStickX", module::gamepad::LeftStickX }, { "LeftStickY", module::gamepad::LeftStickY },
	{ "RightStickX", module::gamepad::RightStickX }, { "RightStickY", module::gamepad::RightStickY },
	{ "LeftTrigger", module::gamepad::LeftTrigger }, { "RightTrigger", module::gamepad::RightTrigger },
};

inline constexpr NamedCode kMouseButtonNames[] = {
	{ "Left", 0 }, { "Right", 1 }, { "Middle", 2 }, { "X1", 3 }, { "X2", 4 },
};

template <std::size_t N>
[[nodiscard]] std::optional<std::uint32_t> codeByName(const NamedCode (&table)[N], std::string_view name) noexcept
{
	for (const NamedCode& n : table)
	{
		if (equalsIgnoreCase(n.name, name)) { return n.code; }
	}
	return std::nullopt;
}

template <std::size_t N>
[[nodiscard]] std::string_view nameByCode(const NamedCode (&table)[N], std::uint32_t code) noexcept
{
	for (const NamedCode& n : table)
	{
		if (n.code == code) { return n.name; }
	}
	return {};
}

/// @brief "key:Space" / "mouse:Left" / "pad:A" / "axis:LeftStickY" / "axis:LeftStickY-" を読む。
[[nodiscard]] inline std::optional<InputSource> parseInputSource(std::string_view text)
{
	const auto colon = text.find(':');
	if (colon == std::string_view::npos) { return std::nullopt; }
	const std::string_view kind = text.substr(0, colon);
	std::string_view name = text.substr(colon + 1);
	InputSource s;
	if (equalsIgnoreCase(kind, "key"))
	{
		const int vk = vkFromKeyName(name);
		if (vk <= 0 || vk > 255) { return std::nullopt; }
		s = { SourceKind::Key, static_cast<std::uint16_t>(vk), 0 };
	}
	else if (equalsIgnoreCase(kind, "mouse") || equalsIgnoreCase(kind, "pad"))
	{
		const bool mouse = equalsIgnoreCase(kind, "mouse");
		const auto code = mouse ? codeByName(kMouseButtonNames, name) : codeByName(kPadButtonNames, name);
		if (!code) { return std::nullopt; }
		s = { mouse ? SourceKind::Mouse : SourceKind::PadButton, static_cast<std::uint16_t>(*code), 0 };
	}
	else if (equalsIgnoreCase(kind, "axis"))
	{
		std::int8_t sign = 0;
		if (!name.empty() && (name.back() == '+' || name.back() == '-'))
		{
			sign = name.back() == '+' ? 1 : -1;
			name.remove_suffix(1);
		}
		const auto code = codeByName(kPadAxisNames, name);
		if (!code) { return std::nullopt; }
		s = { SourceKind::PadAxis, static_cast<std::uint16_t>(*code), sign };
	}
	else
	{
		return std::nullopt;
	}
	return s;
}

/// @brief "key:Num0".."key:Num9" (どちらの数字のキーか決まらない名前) なら数字 0..9、そうでなければ -1
[[nodiscard]] inline int ambiguousDigitSource(std::string_view text) noexcept
{
	const auto colon = text.find(':');
	if (colon == std::string_view::npos || !equalsIgnoreCase(text.substr(0, colon), "key")) { return -1; }
	return ambiguousDigitName(text.substr(colon + 1));
}

/// @brief parseInputSource が読めなかった text について、何が悪いかの文
[[nodiscard]] inline std::string inputSourceError(std::string_view text)
{
	if (ambiguousDigitSource(text) >= 0) { return ambiguousDigitMessage(text.substr(text.find(':') + 1)); }
	return "入力の名前が分からない: \"" + std::string(text) + "\"";
}

[[nodiscard]] inline std::string formatInputSource(const InputSource& s)
{
	switch (s.kind)
	{
	case SourceKind::Key:       return "key:" + keyNameFromVk(s.code);
	case SourceKind::Mouse:     return "mouse:" + std::string(nameByCode(kMouseButtonNames, s.code));
	case SourceKind::PadButton: return "pad:" + std::string(nameByCode(kPadButtonNames, s.code));
	case SourceKind::PadAxis:
		return "axis:" + std::string(nameByCode(kPadAxisNames, s.code)) + (s.sign > 0 ? "+" : (s.sign < 0 ? "-" : ""));
	case SourceKind::None:      break;
	}
	return "none";
}

enum class ActionKind : std::uint8_t { Button, Axis };

struct ActionDef
{
	std::string id;
	std::string label;       ///< 設定画面に出す名前 (UTF-8)
	std::string context;     ///< 同時に使わない操作の組 (例 "menu" と "field")。空は全部と重なる
	ActionKind kind = ActionKind::Button;
	InputSource slot;
	std::vector<InputSource> reads;
	std::vector<InputSource> defaults;
	bool toggleable = false; ///< 押し続ける操作を「押すたびに切り替え」にできるか
};

struct ActionManifest
{
	std::vector<ActionDef> actions;

	[[nodiscard]] const ActionDef* find(std::string_view id) const noexcept
	{
		const auto it = std::find_if(actions.begin(), actions.end(), [&](const ActionDef& a) { return a.id == id; });
		return it == actions.end() ? nullptr : &*it;
	}
};

struct ManifestLoad
{
	ActionManifest manifest;
	std::vector<std::string> errors;  ///< 空なら使える
};

/// @brief 利用者が変えた割り当て (操作 id → 入力の並び)。載っていない操作は defaults のまま。
using BindingOverrides = std::map<std::string, std::vector<InputSource>, std::less<>>;

[[nodiscard]] inline const std::vector<InputSource>& effectiveBindings(const ActionDef& a, const BindingOverrides& o)
{
	const auto it = o.find(a.id);
	return it == o.end() ? a.defaults : it->second;
}

[[nodiscard]] inline bool sharesContext(const ActionDef& a, const ActionDef& b) noexcept
{
	return a.context.empty() || b.context.empty() || a.context == b.context;
}

namespace detail
{

[[nodiscard]] inline std::vector<InputSource> parseSources(const nlohmann::json& arr, const std::string& where,
                                                           std::vector<std::string>& errors)
{
	std::vector<InputSource> out;
	if (!arr.is_array()) { return out; }
	for (const auto& v : arr)
	{
		const std::string text = v.is_string() ? v.get<std::string>() : std::string();
		if (const auto s = parseInputSource(text)) { out.push_back(*s); }
		else { errors.push_back(where + ": " + inputSourceError(text)); }
	}
	return out;
}

[[nodiscard]] inline bool slotFitsKind(const ActionDef& a) noexcept
{
	if (a.kind == ActionKind::Axis) { return a.slot.kind == SourceKind::PadAxis && a.slot.sign == 0; }
	return a.slot.kind == SourceKind::Key || a.slot.kind == SourceKind::Mouse || a.slot.kind == SourceKind::PadButton;
}

[[nodiscard]] inline ActionDef parseAction(const nlohmann::json& j, std::vector<std::string>& errors)
{
	ActionDef a;
	a.id = j.value("id", std::string());
	a.label = j.value("label", a.id);
	a.context = j.value("context", std::string());
	a.kind = j.value("kind", std::string("button")) == "axis" ? ActionKind::Axis : ActionKind::Button;
	a.toggleable = j.value("toggleable", false);
	const std::string where = "action \"" + a.id + "\"";
	if (const auto s = parseInputSource(j.value("slot", std::string()))) { a.slot = *s; }
	else { errors.push_back(where + ": slot が無いか読めない (" + inputSourceError(j.value("slot", std::string())) + ")"); }
	if (!slotFitsKind(a)) { errors.push_back(where + ": slot の種類が kind と合わない (axis は axis:<名前>)"); }
	a.reads = parseSources(j.value("reads", nlohmann::json::array()), where, errors);
	if (std::find(a.reads.begin(), a.reads.end(), a.slot) == a.reads.end()) { a.reads.insert(a.reads.begin(), a.slot); }
	a.defaults = parseSources(j.value("defaults", nlohmann::json::array({ formatInputSource(a.slot) })), where, errors);
	if (a.defaults.size() > kMaxBindingsPerAction)
	{
		errors.push_back(where + ": defaults は " + std::to_string(kMaxBindingsPerAction) + " 個まで");
	}
	return a;
}

inline void checkOverlaps(const ActionManifest& m, std::vector<std::string>& errors)
{
	for (std::size_t i = 0; i < m.actions.size(); ++i)
	{
		for (std::size_t k = i + 1; k < m.actions.size(); ++k)
		{
			const ActionDef& a = m.actions[i];
			const ActionDef& b = m.actions[k];
			if (a.id == b.id) { errors.push_back("action \"" + a.id + "\" が 2 回ある"); continue; }
			if (!sharesContext(a, b)) { continue; }
			for (const InputSource& s : a.reads)
			{
				if (std::find(b.reads.begin(), b.reads.end(), s) == b.reads.end()) { continue; }
				errors.push_back("action \"" + a.id + "\" と \"" + b.id + "\" が同じ context で " + formatInputSource(s)
				                 + " を読む (割り当てを変えると片方に漏れる。context を分けるか reads を分ける)");
			}
		}
	}
}

}  // namespace detail

/// @brief input_actions.json を読む。形は docs/SAVE_AND_SETTINGS.md の「キー割り当て」。
[[nodiscard]] inline ManifestLoad parseActionManifest(std::string_view json)
{
	ManifestLoad out;
	std::string syntaxError;
	const auto j = data::parseJsonText(json, "input_actions.json", syntaxError);
	if (j.is_discarded() || !j.is_object() || !j.contains("actions") || !j["actions"].is_array())
	{
		out.errors.push_back(syntaxError.empty() ? "input_actions.json の形が違う ({ \"actions\": [ ... ] } が要る)" : syntaxError);
		return out;
	}
	for (const auto& a : j["actions"])
	{
		ActionDef def = detail::parseAction(a, out.errors);
		if (def.id.empty()) { out.errors.push_back("id の無い action がある"); continue; }
		out.manifest.actions.push_back(std::move(def));
	}
	if (out.manifest.actions.size() > kMaxActions)
	{
		out.errors.push_back("action は " + std::to_string(kMaxActions) + " 個まで (" + std::to_string(out.manifest.actions.size()) + " 個ある)");
	}
	detail::checkOverlaps(out.manifest, out.errors);
	return out;
}

struct BindingConflict
{
	std::string actionA;
	std::string actionB;
	InputSource source;
};

/// @brief 同じ context の 2 つの操作に同じ入力が割り当たっている組を全部返す (設定画面で警告する用)。
[[nodiscard]] inline std::vector<BindingConflict> findBindingConflicts(const ActionManifest& m, const BindingOverrides& o)
{
	std::vector<BindingConflict> out;
	for (std::size_t i = 0; i < m.actions.size(); ++i)
	{
		for (std::size_t k = i + 1; k < m.actions.size(); ++k)
		{
			const ActionDef& a = m.actions[i];
			const ActionDef& b = m.actions[k];
			if (!sharesContext(a, b)) { continue; }
			const auto& bb = effectiveBindings(b, o);
			for (const InputSource& s : effectiveBindings(a, o))
			{
				if (std::find(bb.begin(), bb.end(), s) != bb.end()) { out.push_back({ a.id, b.id, s }); }
			}
		}
	}
	return out;
}

/// @brief 1 つの操作の割り当てを初期値へ戻す (overrides から消す)。
[[nodiscard]] inline BindingOverrides resetBinding(BindingOverrides o, std::string_view actionId)
{
	if (const auto it = o.find(actionId); it != o.end()) { o.erase(it); }
	return o;
}

}  // namespace mitiru::input
