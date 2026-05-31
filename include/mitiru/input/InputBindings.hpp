#pragma once

/// @file InputBindings.hpp
/// @brief 「action 名 → 入力ソース」のリバインド辞書。
/// @details game の設定画面が動的にキー/ボタン割り当てを編集できるよう、engine は raw 入力を
///          提供し、本ヘルパが action 名で問い合わせる layer を提供する。analog (gamepadAxes)
///          は v5 で既に float で取れるので、本ヘルパは button / key の binding に焦点。

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

/// @brief 1 つの binding ターゲット。種別 enum + 値。
struct Bind
{
	enum Kind : std::uint8_t {
		None = 0,
		GamepadButton,  ///< value = mitiru::module::gamepad::* のビットマスク (例 gamepad::A)
		GamepadAxis,    ///< value = gamepadAxes インデックス (例 LeftStickX) / extra = + or -
		Key,            ///< value = VK_* (Windows) または game の key enum
	};

	Kind        kind  = None;
	std::uint32_t value = 0;
	std::int8_t  axisSign = 0;  ///< GamepadAxis のみ: +1=正側、-1=負側 (押下判定の閾値)
};

/// @brief action 名 → 1 つ以上の Bind。複数 bind があれば OR (どれか押されてれば true)。
class InputBindings
{
public:
	/// @brief action に bind を追加 (既存に追加する。リセットは clear)。
	void add(std::string action, Bind b) { m_map[std::move(action)].push_back(b); }
	void clear(std::string_view action)
	{
		const auto it = m_map.find(std::string{action});
		if (it != m_map.end()) { it->second.clear(); }
	}
	void clearAll() { m_map.clear(); }

	[[nodiscard]] const std::vector<Bind>* bindsOf(std::string_view action) const
	{
		const auto it = m_map.find(std::string{action});
		return (it != m_map.end()) ? &it->second : nullptr;
	}

	/// @brief action が「現在押されているか」。複数 bind は OR。
	[[nodiscard]] bool isPressed(const mitiru::module::InputSnapshot& s,
	                             std::string_view action) const
	{
		const auto* bs = bindsOf(action);
		if (!bs) { return false; }
		for (const auto& b : *bs)
		{
			if (matchPressed(s, b)) { return true; }
		}
		return false;
	}

	/// @brief action が「このフレーム押下開始されたか」。複数 bind は OR。
	[[nodiscard]] bool isJustPressed(const mitiru::module::InputSnapshot& s,
	                                 std::string_view action) const
	{
		const auto* bs = bindsOf(action);
		if (!bs) { return false; }
		for (const auto& b : *bs)
		{
			if (matchJustPressed(s, b)) { return true; }
		}
		return false;
	}

private:
	static constexpr float kAxisThresh = 0.5f;

	static bool matchPressed(const mitiru::module::InputSnapshot& s, const Bind& b)
	{
		switch (b.kind)
		{
		case Bind::GamepadButton:
			return s.gamepadConnected && (s.gamepadButtonsDown & b.value) != 0u;
		case Bind::GamepadAxis:
			if (!s.gamepadConnected || b.value >= 6) { return false; }
			return (b.axisSign >= 0)
				? (s.gamepadAxes[b.value] >=  kAxisThresh)
				: (s.gamepadAxes[b.value] <= -kAxisThresh);
		case Bind::Key:
			// engine 側 keyboard state は本 InputSnapshot に含まれないため、game が key
			// state を別途持って本ヘルパに渡すケースを想定 (現状は no-op で false)。
			return false;
		default:
			return false;
		}
	}

	static bool matchJustPressed(const mitiru::module::InputSnapshot& s, const Bind& b)
	{
		switch (b.kind)
		{
		case Bind::GamepadButton:
			return s.gamepadConnected && (s.gamepadButtonsJustPressed & b.value) != 0u;
		case Bind::GamepadAxis:
			// axis は前フレーム値を持たない (InputSnapshot は stateless view) ため
			// just-pressed エッジを出せない。false を返す (連続入力は isPressed を使う)。
			return false;
		case Bind::Key:
			return false;
		default:
			return false;
		}
	}

	std::unordered_map<std::string, std::vector<Bind>> m_map;
};

}  // namespace mitiru::input
