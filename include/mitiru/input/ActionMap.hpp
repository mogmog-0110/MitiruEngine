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

enum class SourceKind : std::uint8_t { None, Key, Mouse, PadButton, PadAxis, Wheel };

/// InputSource::mods のビット。キーボードの修飾キー (Ctrl+K のような組)。パッドの入力には付けない。
inline constexpr std::uint8_t kModCtrl = 1;
inline constexpr std::uint8_t kModShift = 2;
inline constexpr std::uint8_t kModAlt = 4;

/// @brief 入力 1 つ。Key は VK、Mouse は 0=左 1=右 2=中 3=X1 4=X2、PadButton は module::gamepad のビット、
///        PadAxis は module::gamepad::Axis の添字。PadAxis の sign は、ボタンとして読む時の向き (+1 / -1) か、
///        軸として読む時の反転 (-1)。0 は軸をそのまま読む。
///        Wheel は code 0=縦 1=横、sign +1=奥/右 -1=手前/左 (押した瞬間だけの入力で、回した 1 フレームが押下)。
///        mods は Key / Mouse / Wheel だけに付き、全部を押している間だけ押下になる。
struct InputSource
{
	SourceKind kind = SourceKind::None;
	std::uint16_t code = 0;
	std::int8_t sign = 0;
	std::uint8_t mods = 0;

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

namespace detail
{
struct WheelName
{
	std::string_view name;
	std::uint16_t code;
	std::int8_t sign;
};
inline constexpr WheelName kWheelNames[] = {
	{ "Up", 0, 1 }, { "Down", 0, -1 }, { "Right", 1, 1 }, { "Left", 1, -1 },
};

/// "ctrl+" / "shift+" / "alt+" を先頭から外し、ビットにして返す。軸の符号の '+' は名前の最後にしか来ないので紛れない
[[nodiscard]] inline std::uint8_t stripModifierPrefix(std::string_view& text) noexcept
{
	std::uint8_t mods = 0;
	for (bool again = true; again;)
	{
		again = false;
		const auto plus = text.find('+');
		if (plus == std::string_view::npos || plus == 0) { break; }
		const std::string_view word = text.substr(0, plus);
		std::uint8_t bit = 0;
		if (equalsIgnoreCase(word, "ctrl") || equalsIgnoreCase(word, "control")) { bit = kModCtrl; }
		else if (equalsIgnoreCase(word, "shift")) { bit = kModShift; }
		else if (equalsIgnoreCase(word, "alt")) { bit = kModAlt; }
		if (bit == 0) { break; }
		mods = static_cast<std::uint8_t>(mods | bit);
		text.remove_prefix(plus + 1);
		again = true;
	}
	return mods;
}
}  // namespace detail

/// @brief "key:Space" / "mouse:Left" / "pad:A" / "axis:LeftStickY" / "axis:LeftStickY-" / "wheel:Up" を読む。
///        Key、Mouse、Wheel は "ctrl+shift+key:K" のように修飾キーを前に付けられる。
[[nodiscard]] inline std::optional<InputSource> parseInputSource(std::string_view text)
{
	const std::uint8_t mods = detail::stripModifierPrefix(text);
	const auto colon = text.find(':');
	if (colon == std::string_view::npos) { return std::nullopt; }
	const std::string_view kind = text.substr(0, colon);
	std::string_view name = text.substr(colon + 1);
	InputSource s;
	if (equalsIgnoreCase(kind, "wheel"))
	{
		for (const detail::WheelName& w : detail::kWheelNames)
		{
			if (equalsIgnoreCase(w.name, name)) { s = { SourceKind::Wheel, w.code, w.sign, 0 }; }
		}
		if (s.kind == SourceKind::None) { return std::nullopt; }
	}
	else if (equalsIgnoreCase(kind, "key"))
	{
		const int vk = vkFromKeyName(name);
		if (vk <= 0 || vk > 255) { return std::nullopt; }
		s = { SourceKind::Key, static_cast<std::uint16_t>(vk), 0, 0 };
	}
	else if (equalsIgnoreCase(kind, "mouse") || equalsIgnoreCase(kind, "pad"))
	{
		const bool mouse = equalsIgnoreCase(kind, "mouse");
		const auto code = mouse ? codeByName(kMouseButtonNames, name) : codeByName(kPadButtonNames, name);
		if (!code) { return std::nullopt; }
		s = { mouse ? SourceKind::Mouse : SourceKind::PadButton, static_cast<std::uint16_t>(*code), 0, 0 };
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
		s = { SourceKind::PadAxis, static_cast<std::uint16_t>(*code), sign, 0 };
	}
	else
	{
		return std::nullopt;
	}
	if (mods != 0 && (s.kind == SourceKind::PadButton || s.kind == SourceKind::PadAxis)) { return std::nullopt; }
	s.mods = mods;
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

[[nodiscard]] inline std::string modifierPrefix(std::uint8_t mods)
{
	std::string out;
	if (mods & kModCtrl) { out += "ctrl+"; }
	if (mods & kModShift) { out += "shift+"; }
	if (mods & kModAlt) { out += "alt+"; }
	return out;
}

[[nodiscard]] inline std::string formatInputSource(const InputSource& s)
{
	switch (s.kind)
	{
	case SourceKind::Key:       return modifierPrefix(s.mods) + "key:" + keyNameFromVk(s.code);
	case SourceKind::Mouse:     return modifierPrefix(s.mods) + "mouse:" + std::string(nameByCode(kMouseButtonNames, s.code));
	case SourceKind::PadButton: return "pad:" + std::string(nameByCode(kPadButtonNames, s.code));
	case SourceKind::PadAxis:
		return "axis:" + std::string(nameByCode(kPadAxisNames, s.code)) + (s.sign > 0 ? "+" : (s.sign < 0 ? "-" : ""));
	case SourceKind::Wheel:
		for (const detail::WheelName& w : detail::kWheelNames)
		{
			if (w.code == s.code && w.sign == s.sign) { return modifierPrefix(s.mods) + "wheel:" + std::string(w.name); }
		}
		break;
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

/// @brief パッドの入力か (キーボードとマウスの入力の反対)。割り当てを機器ごとに分けて扱うのに使う
[[nodiscard]] constexpr bool isPadSource(const InputSource& s) noexcept
{
	return s.kind == SourceKind::PadButton || s.kind == SourceKind::PadAxis;
}

/// @brief 2 つの入力が、同じ物理の操作で両方とも鳴るか。同じ入力どうしか、同じ入力で一方の修飾キーがもう一方に含まれる組
///        (W と Shift+W は、Shift+W を押すと両方が押下になる)。
[[nodiscard]] constexpr bool sourcesCollide(const InputSource& a, const InputSource& b) noexcept
{
	if (a.kind == SourceKind::None || a.kind != b.kind || a.code != b.code || a.sign != b.sign) { return false; }
	const bool aInB = (a.mods & b.mods) == a.mods;
	const bool bInA = (a.mods & b.mods) == b.mods;
	return aInB || bInA;
}

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

/// "defaults" は配列 (キーボードとパッドを混ぜて並べる) か、{ "keyboard": [...], "pad": [...] } のどちらでも書ける。
/// 機器ごとに分けた形は、設定画面でキーボード側とパッド側を別々に初期値へ戻すときの境目が、書いた人の意図どおりになる。
[[nodiscard]] inline std::vector<InputSource> parseDefaults(const nlohmann::json& j, const ActionDef& a,
                                                            const std::string& where, std::vector<std::string>& errors)
{
	const auto it = j.find("defaults");
	if (it == j.end()) { return parseSources(nlohmann::json::array({ formatInputSource(a.slot) }), where, errors); }
	if (!it->is_object()) { return parseSources(*it, where, errors); }
	std::vector<InputSource> out = parseSources(it->value("keyboard", nlohmann::json::array()), where, errors);
	const std::size_t keyboardCount = out.size();
	const std::vector<InputSource> pad = parseSources(it->value("pad", nlohmann::json::array()), where, errors);
	out.insert(out.end(), pad.begin(), pad.end());
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		if (isPadSource(out[i]) == (i < keyboardCount))
		{
			errors.push_back(where + ": defaults." + (i < keyboardCount ? "keyboard" : "pad") + " に "
			                 + (i < keyboardCount ? "パッド" : "キーボードとマウス") + "の入力 " + formatInputSource(out[i]) + " がある");
		}
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
	a.defaults = parseDefaults(j, a, where, errors);
	for (const InputSource& r : a.reads)
	{
		if (r.mods != 0 || r.kind == SourceKind::Wheel)
		{
			errors.push_back(where + ": reads と slot には修飾キーとホイールを書けない (ゲームが読むキーの宣言なので、割り当てる側 defaults だけに書く)");
			break;
		}
	}
	if (a.slot.mods != 0) { errors.push_back(where + ": slot に修飾キーは付けられない"); }
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
				const auto hit = std::find_if(bb.begin(), bb.end(), [&](const InputSource& t) { return sourcesCollide(s, t); });
				if (hit == bb.end()) { continue; }
				// 両方が鳴るのは修飾キーの多い方を押したとき。宣言の順で表示が変わらないよう、その方を出す
				const auto bits = [](std::uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1); };
				out.push_back({ a.id, b.id, bits(hit->mods) > bits(s.mods) ? *hit : s });
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

/// @brief 1 つの操作の、片方の機器 (pad=true ならパッド、false ならキーボードとマウス) の割り当てだけを初期値へ戻す。
///        もう片方の割り当てと、並びは変えない (上限 kMaxBindingsPerAction に収まらなければ、もう片方の末尾から外す)。
///        戻した結果が初期値と同じなら overrides から消す。
[[nodiscard]] inline BindingOverrides resetDeviceBinding(BindingOverrides o, const ActionDef& def, bool pad)
{
	std::vector<InputSource> restored;
	for (const InputSource& s : def.defaults)
	{
		if (isPadSource(s) == pad) { restored.push_back(s); }
	}
	// 戻すと言った側の初期値は必ず残す。もう片方が上限いっぱいなら、その末尾を詰める
	const std::size_t room = restored.size() >= kMaxBindingsPerAction ? 0 : kMaxBindingsPerAction - restored.size();
	std::vector<InputSource> next;
	for (const InputSource& s : effectiveBindings(def, o))
	{
		if (isPadSource(s) != pad && next.size() < room) { next.push_back(s); }
	}
	for (const InputSource& s : restored)
	{
		if (next.size() < kMaxBindingsPerAction) { next.push_back(s); }
	}
	if (next == def.defaults) { o.erase(def.id); }
	else { o[def.id] = std::move(next); }
	return o;
}

}  // namespace mitiru::input
