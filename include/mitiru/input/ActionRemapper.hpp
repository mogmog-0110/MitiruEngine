#pragma once

/// @file ActionRemapper.hpp
/// @brief 利用者のキー割り当てで InputSnapshot を組み替える (host が snapshot を game へ渡す前に 1 回掛ける)。
/// @details 操作ごとに、割り当てた入力のどれかが押されていれば、その操作の slot (ゲームが読むキー) を押下にする。
///          reads に載ったキーとボタンは先に空にするので、割り当てから外したキーでは操作が起きない。
///          押した瞬間と離した瞬間は、組み替えた後の押下から作り直す。録画と replay は組み替えた後の snapshot を
///          使うので、割り当てを変えても再生の結果は変わらない。
///          状態 (前フレームの押下、切り替えの状態) は host が持つ。GameMemory には入らない。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mitiru/input/ActionMap.hpp>
#include <mitiru/input/GamepadSlots.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

struct RemapOptions
{
	float mouseSensitivity = 1.0f;     ///< mouseDeltaX / Y に掛ける
	bool  invertMouseY = false;
	std::vector<std::string> toggleActions;  ///< 「押すたびに切り替え」にする操作 (toggleable のものだけ効く)
};

/// @brief 割り当ての取り込み (設定画面で「次に押した入力を割り当てる」) の結果。
struct CapturedBinding
{
	std::string actionId;
	int index = 0;           ///< 何番目の割り当てを置き換えるか (beginCapture に渡した値)
	InputSource source;
	bool cancelled = false;  ///< Escape で取り消した
};

class ActionRemapper
{
public:
	static constexpr std::size_t kMaxBinds = kMaxBindingsPerAction;
	static constexpr float kAxisPress = 0.5f;    ///< 軸をボタンとして読む時の押下のしきい値
	static constexpr float kAxisCapture = 0.6f;  ///< 取り込みで軸を拾うしきい値

	/// @brief 表と割り当てを組み込む (割り当てが変わった時だけ呼ぶ。毎フレームは apply だけ)。
	/// @details 同じ id の操作は、切り替えの状態と前フレームの押下を引き継ぐ。押したまま設定を変えても、
	///          押した瞬間がもう一度起きたり、切り替えが外れたりしない。
	/// @return 上限 (kMaxActions / kMaxBindingsPerAction) を超えて組み込めなかった操作と割り当ての数
	std::size_t configure(const ActionManifest& manifest, const BindingOverrides& overrides, const RemapOptions& options)
	{
		const auto oldIds = std::exchange(m_ids, {});
		const auto oldLatched = m_latched;
		const auto oldPhysical = m_prevPhysical;
		m_count = 0;
		m_ownedKeys.fill(0);
		m_ownedMouse.fill(0);
		m_ownedPadBits = 0;
		m_ownedAxes = 0;
		m_options = options;
		m_latched.fill(false);
		m_prevPhysical.fill(false);
		std::size_t dropped = 0;
		for (const ActionDef& def : manifest.actions)
		{
			const auto& binds = effectiveBindings(def, overrides);
			if (m_count >= kMaxActions) { dropped += 1; continue; }
			dropped += binds.size() > kMaxBinds ? binds.size() - kMaxBinds : 0;
			compileAction(def, binds, m_actions[m_count]);
			const auto old = std::find(oldIds.begin(), oldIds.end(), def.id);
			if (old != oldIds.end())
			{
				const auto k = static_cast<std::size_t>(old - oldIds.begin());
				m_latched[m_count] = oldLatched[k];
				m_prevPhysical[m_count] = oldPhysical[k];
			}
			m_ids.push_back(def.id);
			++m_count;
		}
		return dropped;
	}

	/// @brief snapshot を組み替える。毎フレーム 1 回、replay の上書きより前に呼ぶ。
	void apply(module::InputSnapshot& snap) noexcept
	{
		captureRaw(snap);
		if (m_captureAction >= 0)
		{
			detectCapture(snap);
			blankButtons(snap);
			return;
		}
		updateSuppression();
		computeActions();
		writeKeysAndMouse(snap);
		writePads(snap);
		applyMouseOptions(snap);
	}

	/// @brief 次に押した入力を actionId の index 番目の割り当てにする。押すまでの間、ゲームには何も渡さない。
	bool beginCapture(std::string_view actionId, int index)
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_ids[i] != actionId) { continue; }
			m_captureAction = static_cast<int>(i);
			m_captureIndex = index;
			m_captured.reset();
			return true;
		}
		return false;
	}

	void cancelCapture() noexcept { m_captureAction = -1; }
	[[nodiscard]] bool capturing() const noexcept { return m_captureAction >= 0; }

	/// @brief 取り込みが終わっていれば結果を返す (1 回だけ)。
	[[nodiscard]] std::optional<CapturedBinding> takeCaptured()
	{
		return std::exchange(m_captured, std::nullopt);
	}

	/// @brief 今のフレームで操作が押されているか (組み替えの結果。テストと設定画面の確認用)。
	[[nodiscard]] bool actionDown(std::string_view actionId) const noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_ids[i] == actionId) { return m_down[i]; }
		}
		return false;
	}

	/// @brief bit i = 表の i 番目の操作が押されている (InputSnapshot::actionsDown、ABI v48)。取り込み中は 0
	[[nodiscard]] std::uint64_t downMask() const noexcept
	{
		if (capturing()) { return 0; }
		std::uint64_t mask = 0;
		for (std::size_t i = 0; i < m_count && i < 64; ++i) { mask |= m_down[i] ? (std::uint64_t{1} << i) : 0; }
		return mask;
	}

private:
	struct CompiledAction
	{
		ActionKind kind = ActionKind::Button;
		InputSource slot;
		std::array<InputSource, kMaxBinds> binds{};
		std::uint8_t bindCount = 0;
		bool toggle = false;
	};

	void compileAction(const ActionDef& def, const std::vector<InputSource>& binds, CompiledAction& out)
	{
		out.kind = def.kind;
		out.slot = def.slot;
		out.bindCount = 0;
		for (const InputSource& b : binds)
		{
			if (out.bindCount < kMaxBinds) { out.binds[out.bindCount++] = b; }
		}
		const auto& toggles = m_options.toggleActions;
		out.toggle = def.toggleable && std::find(toggles.begin(), toggles.end(), def.id) != toggles.end();
		for (const InputSource& r : def.reads)
		{
			// ボタンの操作が軸を倒した向きで読んでいても、軸そのものはアナログの移動にも使われるので空にしない
			if (r.kind == SourceKind::PadAxis && def.kind != ActionKind::Axis) { continue; }
			markOwned(r);
		}
	}

	void markOwned(const InputSource& s) noexcept
	{
		switch (s.kind)
		{
		case SourceKind::Key:       m_ownedKeys[s.code & 0xFFu] = 1; break;
		case SourceKind::Mouse:     if (s.code < 5) { m_ownedMouse[s.code] = 1; } break;
		case SourceKind::PadButton: m_ownedPadBits |= s.code; break;
		case SourceKind::PadAxis:   m_ownedAxes |= (1u << s.code); break;
		case SourceKind::None:      break;
		}
	}

	void captureRaw(const module::InputSnapshot& snap) noexcept
	{
		std::memcpy(m_rawKeys.data(), snap.keysDown, sizeof(snap.keysDown));
		for (int i = 0; i < 3; ++i) { m_rawMouse[i] = snap.mouseButtonsDown[i]; }
		for (int i = 0; i < 2; ++i) { m_rawMouse[3 + i] = snap.mouseXButtonsDown[i]; }
		for (int p = 0; p < kGamepadSlots; ++p) { m_rawPads[p] = snap.gamepads[p]; }
	}

	[[nodiscard]] static bool padSourceDown(const InputSource& s, const module::GamepadState& pad) noexcept
	{
		if (!pad.connected) { return false; }
		if (s.kind == SourceKind::PadButton) { return (pad.buttonsDown & s.code) != 0; }
		const float v = pad.axes[s.code % module::gamepad::AxisCount];
		return s.sign == 0 ? std::fabs(v) >= kAxisPress : v * static_cast<float>(s.sign) >= kAxisPress;
	}

	/// @param pad -1 なら全部のパッド
	[[nodiscard]] bool sourceDown(const InputSource& s, int pad) const noexcept
	{
		if (m_suppress && *m_suppress == s) { return false; }
		switch (s.kind)
		{
		case SourceKind::Key:   return pad < 0 && m_rawKeys[s.code & 0xFFu] != 0;
		case SourceKind::Mouse: return pad < 0 && s.code < 5 && m_rawMouse[s.code] != 0;
		case SourceKind::PadButton:
		case SourceKind::PadAxis:
			if (pad >= 0) { return padSourceDown(s, m_rawPads[static_cast<std::size_t>(pad)]); }
			return std::any_of(m_rawPads.begin(), m_rawPads.end(), [&](const auto& p) { return padSourceDown(s, p); });
		case SourceKind::None:  break;
		}
		return false;
	}

	[[nodiscard]] bool anyBindDown(const CompiledAction& a, int pad) const noexcept
	{
		for (std::uint8_t b = 0; b < a.bindCount; ++b)
		{
			if (sourceDown(a.binds[b], pad)) { return true; }
		}
		return false;
	}

	void computeActions() noexcept
	{
		for (std::size_t i = 0; i < m_count; ++i)
		{
			const CompiledAction& a = m_actions[i];
			if (a.kind != ActionKind::Button) { m_down[i] = false; continue; }
			const bool physical = anyBindDown(a, -1);
			if (a.toggle && physical && !m_prevPhysical[i]) { m_latched[i] = !m_latched[i]; }
			m_prevPhysical[i] = physical;
			m_down[i] = a.toggle ? m_latched[i] : physical;
		}
	}

	static void setMouseButton(module::InputSnapshot& snap, int b, std::uint8_t down, std::uint8_t pressed,
	                           std::uint8_t released) noexcept
	{
		if (b < 3)
		{
			snap.mouseButtonsDown[b] = down;
			snap.mouseButtonsJustPressed[b] = pressed;
			snap.mouseButtonsJustReleased[b] = released;
			return;
		}
		snap.mouseXButtonsDown[b - 3] = down;
		snap.mouseXButtonsJustPressed[b - 3] = pressed;
		snap.mouseXButtonsJustReleased[b - 3] = released;
	}

	void writeKeysAndMouse(module::InputSnapshot& snap) noexcept
	{
		std::array<std::uint8_t, 256> keys{};
		std::array<std::uint8_t, 5> mouse{};
		for (std::size_t i = 0; i < m_count; ++i)
		{
			const InputSource& s = m_actions[i].slot;
			if (!m_down[i]) { continue; }
			if (s.kind == SourceKind::Key) { keys[s.code & 0xFFu] = 1; }
			else if (s.kind == SourceKind::Mouse && s.code < 5) { mouse[s.code] = 1; }
		}
		for (int vk = 0; vk < 256; ++vk)
		{
			if (!m_ownedKeys[vk]) { continue; }
			snap.keysDown[vk] = keys[vk];
			snap.keysJustPressed[vk] = (keys[vk] && !m_prevKeys[vk]) ? 1 : 0;
			snap.keysJustReleased[vk] = (!keys[vk] && m_prevKeys[vk]) ? 1 : 0;
			m_prevKeys[vk] = keys[vk];
		}
		for (int b = 0; b < 5; ++b)
		{
			if (!m_ownedMouse[b]) { continue; }
			const std::uint8_t now = mouse[b];
			setMouseButton(snap, b, now, (now && !m_prevMouse[b]) ? 1 : 0, (!now && m_prevMouse[b]) ? 1 : 0);
			m_prevMouse[b] = now;
		}
	}

	[[nodiscard]] static float axisValue(const CompiledAction& a, const module::GamepadState& pad) noexcept
	{
		for (std::uint8_t b = 0; b < a.bindCount; ++b)
		{
			const InputSource& s = a.binds[b];
			if (s.kind != SourceKind::PadAxis) { continue; }
			const float v = pad.axes[s.code % module::gamepad::AxisCount];
			return s.sign < 0 ? -v : v;
		}
		return 0.0f;
	}

	/// 1 台分。ボタンは割り当てたパッドの入力だけで決め、切り替えの操作はまとめた状態を使う。
	void writeOnePad(module::GamepadState& pad, int index) noexcept
	{
		const module::GamepadState raw = m_rawPads[static_cast<std::size_t>(index)];
		std::uint32_t down = raw.buttonsDown & ~m_ownedPadBits;
		for (int ax = 0; ax < module::gamepad::AxisCount; ++ax)
		{
			if (m_ownedAxes & (1u << ax)) { pad.axes[ax] = 0.0f; }
		}
		for (std::size_t i = 0; i < m_count; ++i)
		{
			const CompiledAction& a = m_actions[i];
			if (a.kind == ActionKind::Axis) { pad.axes[a.slot.code % module::gamepad::AxisCount] = axisValue(a, raw); continue; }
			if (a.slot.kind != SourceKind::PadButton) { continue; }
			if (a.toggle ? m_down[i] : anyBindDown(a, index)) { down |= a.slot.code; }
		}
		const std::uint32_t prev = m_prevPads[static_cast<std::size_t>(index)];
		pad.buttonsDown = down;
		pad.buttonsJustPressed = (raw.buttonsJustPressed & ~m_ownedPadBits) | (down & ~prev & m_ownedPadBits);
		pad.buttonsJustReleased = (raw.buttonsJustReleased & ~m_ownedPadBits) | (~down & prev & m_ownedPadBits);
		m_prevPads[static_cast<std::size_t>(index)] = down & m_ownedPadBits;
	}

	void writePads(module::InputSnapshot& snap) noexcept
	{
		for (int p = 0; p < kGamepadSlots; ++p)
		{
			if (snap.gamepads[p].connected) { writeOnePad(snap.gamepads[p], p); }
			else { m_prevPads[static_cast<std::size_t>(p)] = 0; }
		}
		mergeGamepads(snap.gamepads, snap);
		// キーボードに割り当てたパッドの操作は、1 人用の合成にだけ足す (どのパッドの入力でもないため)
		std::uint32_t extra = 0;
		for (std::size_t i = 0; i < m_count; ++i)
		{
			if (m_actions[i].slot.kind == SourceKind::PadButton && m_down[i]) { extra |= m_actions[i].slot.code; }
		}
		const std::uint32_t down = snap.gamepadButtonsDown | extra;
		const std::uint32_t owned = m_ownedPadBits;
		snap.gamepadButtonsDown = down;
		snap.gamepadButtonsJustPressed = (snap.gamepadButtonsJustPressed & ~owned) | (down & ~m_prevMerged & owned);
		snap.gamepadButtonsJustReleased = (snap.gamepadButtonsJustReleased & ~owned) | (~down & m_prevMerged & owned);
		m_prevMerged = down & owned;
	}

	void applyMouseOptions(module::InputSnapshot& snap) const noexcept
	{
		snap.mouseDeltaX *= m_options.mouseSensitivity;
		snap.mouseDeltaY *= m_options.invertMouseY ? -m_options.mouseSensitivity : m_options.mouseSensitivity;
	}

	[[nodiscard]] std::optional<InputSource> firstPadPressed(const module::InputSnapshot& snap) const noexcept
	{
		const bool analog = m_actions[static_cast<std::size_t>(m_captureAction)].kind == ActionKind::Axis;
		for (const auto& pad : snap.gamepads)
		{
			if (!pad.connected) { continue; }
			for (const NamedCode& n : kPadButtonNames)
			{
				if (!analog && (pad.buttonsJustPressed & n.code)) { return InputSource{ SourceKind::PadButton, static_cast<std::uint16_t>(n.code), 0 }; }
			}
			for (int ax = 0; ax < module::gamepad::AxisCount; ++ax)
			{
				if (std::fabs(pad.axes[ax]) < kAxisCapture) { continue; }
				const std::int8_t sign = analog ? 0 : (pad.axes[ax] > 0.0f ? 1 : -1);
				return InputSource{ SourceKind::PadAxis, static_cast<std::uint16_t>(ax), sign };
			}
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<InputSource> firstPressed(const module::InputSnapshot& snap) const noexcept
	{
		for (int vk = 1; vk < 256; ++vk)
		{
			if (snap.keysJustPressed[vk]) { return InputSource{ SourceKind::Key, static_cast<std::uint16_t>(vk), 0 }; }
		}
		for (int b = 0; b < 5; ++b)
		{
			const bool pressed = b < 3 ? snap.mouseButtonsJustPressed[b] != 0 : snap.mouseXButtonsJustPressed[b - 3] != 0;
			if (pressed) { return InputSource{ SourceKind::Mouse, static_cast<std::uint16_t>(b), 0 }; }
		}
		return firstPadPressed(snap);
	}

	void detectCapture(const module::InputSnapshot& snap)
	{
		const auto src = firstPressed(snap);
		if (!src) { return; }
		CapturedBinding c;
		c.actionId = m_ids[static_cast<std::size_t>(m_captureAction)];
		c.index = m_captureIndex;
		c.source = *src;
		c.cancelled = src->kind == SourceKind::Key && src->code == 0x1B;
		m_captured = c;
		m_captureAction = -1;
		// 押したままの入力が、割り当てた直後の操作として届かないよう、離すまで読まない
		m_suppress = *src;
	}

	void updateSuppression() noexcept
	{
		if (!m_suppress) { return; }
		const InputSource s = *m_suppress;
		m_suppress.reset();
		if (sourceDown(s, -1)) { m_suppress = s; }
	}

	static void blankButtons(module::InputSnapshot& snap) noexcept
	{
		std::memset(snap.keysDown, 0, sizeof(snap.keysDown));
		std::memset(snap.keysJustPressed, 0, sizeof(snap.keysJustPressed));
		std::memset(snap.keysJustReleased, 0, sizeof(snap.keysJustReleased));
		for (int b = 0; b < 5; ++b) { setMouseButton(snap, b, 0, 0, 0); }
		for (auto& p : snap.gamepads)
		{
			p.buttonsDown = 0;
			p.buttonsJustPressed = 0;
			p.buttonsJustReleased = 0;
		}
		snap.gamepadButtonsDown = 0;
		snap.gamepadButtonsJustPressed = 0;
		snap.gamepadButtonsJustReleased = 0;
	}

	std::array<CompiledAction, kMaxActions> m_actions{};
	std::vector<std::string> m_ids;
	std::size_t m_count = 0;
	RemapOptions m_options;

	std::array<std::uint8_t, 256> m_ownedKeys{};
	std::array<std::uint8_t, 5> m_ownedMouse{};
	std::uint32_t m_ownedPadBits = 0;
	std::uint32_t m_ownedAxes = 0;

	std::array<std::uint8_t, 256> m_rawKeys{};
	std::array<std::uint8_t, 5> m_rawMouse{};
	std::array<module::GamepadState, kGamepadSlots> m_rawPads{};

	std::array<bool, kMaxActions> m_down{};
	std::array<bool, kMaxActions> m_latched{};
	std::array<bool, kMaxActions> m_prevPhysical{};
	std::array<std::uint8_t, 256> m_prevKeys{};
	std::array<std::uint8_t, 5> m_prevMouse{};
	std::array<std::uint32_t, kGamepadSlots> m_prevPads{};
	std::uint32_t m_prevMerged = 0;

	int m_captureAction = -1;
	int m_captureIndex = 0;
	std::optional<CapturedBinding> m_captured;
	std::optional<InputSource> m_suppress;
};

}  // namespace mitiru::input
