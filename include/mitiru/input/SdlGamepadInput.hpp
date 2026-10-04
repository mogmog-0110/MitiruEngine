#pragma once

/// @file SdlGamepadInput.hpp
/// @brief パッドの唯一の経路。SDL3 の SDL_Gamepad で Xbox / DualShock 4 / DualSense / Switch Pro などを同じ形で読む (ADR 0048 段階 2)。
/// @details パッドは GamepadSlotTable で枠に固定し、枠ごとにボタンと軸を読む。just-pressed は endTick() で
///          fixed-update の刻みに揃える。InputSnapshot に載らない機能 (機種、電池、ジャイロ、タッチパッド、
///          ライトバー、アダプティブトリガー) は host 側の API として持つ (GamepadFeatures.hpp)。
///          SDL3 が無いビルド (MITIRU_HAS_SDL3 未定義) では何もしない stub になり、窓を出す起動で 1 度だけ警告する。

#include <cstdint>
#include <filesystem>

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/input/GamepadFeatures.hpp>
#include <mitiru/input/GamepadSlots.hpp>
#include <mitiru/module/ModuleApi.hpp>

#ifdef MITIRU_HAS_SDL3
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_sensor.h>

#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <mitiru/input/GamepadAxisMath.hpp>
#include <mitiru/input/GamepadSlotTable.hpp>
#endif

namespace mitiru::input
{

#ifdef MITIRU_HAS_SDL3

struct SdlButtonBit { SDL_GamepadButton button; std::uint32_t bit; };

/// 面ボタンは位置で対応させる (下が A)。Switch Pro の「B」刻印のボタンも A ビットになる
inline constexpr SdlButtonBit kSdlButtonMap[] = {
	{SDL_GAMEPAD_BUTTON_SOUTH, module::gamepad::A},          {SDL_GAMEPAD_BUTTON_EAST, module::gamepad::B},
	{SDL_GAMEPAD_BUTTON_WEST, module::gamepad::X},           {SDL_GAMEPAD_BUTTON_NORTH, module::gamepad::Y},
	{SDL_GAMEPAD_BUTTON_BACK, module::gamepad::Back},        {SDL_GAMEPAD_BUTTON_START, module::gamepad::Start},
	{SDL_GAMEPAD_BUTTON_LEFT_STICK, module::gamepad::LS},    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, module::gamepad::RS},
	{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, module::gamepad::LB}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, module::gamepad::RB},
	{SDL_GAMEPAD_BUTTON_DPAD_UP, module::gamepad::DPadUp},   {SDL_GAMEPAD_BUTTON_DPAD_DOWN, module::gamepad::DPadDown},
	{SDL_GAMEPAD_BUTTON_DPAD_LEFT, module::gamepad::DPadLeft}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, module::gamepad::DPadRight},
};

/// 添字は module::gamepad::Axis の順
inline constexpr SDL_GamepadAxis kSdlAxisMap[module::gamepad::AxisCount] = {
	SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
	SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

[[nodiscard]] inline PadKind padKindFromSdl(SDL_GamepadType t) noexcept
{
	switch (t)
	{
	case SDL_GAMEPAD_TYPE_STANDARD:                     return PadKind::Standard;
	case SDL_GAMEPAD_TYPE_XBOX360:                      return PadKind::Xbox360;
	case SDL_GAMEPAD_TYPE_XBOXONE:                      return PadKind::XboxOne;
	case SDL_GAMEPAD_TYPE_PS3:                          return PadKind::PS3;
	case SDL_GAMEPAD_TYPE_PS4:                          return PadKind::PS4;
	case SDL_GAMEPAD_TYPE_PS5:                          return PadKind::PS5;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:          return PadKind::SwitchPro;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:  return PadKind::JoyConLeft;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return PadKind::JoyConRight;
	case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:  return PadKind::JoyConPair;
	case SDL_GAMEPAD_TYPE_GAMECUBE:                     return PadKind::GameCube;
	default:                                            return PadKind::Unknown;
	}
}

[[nodiscard]] inline PadPower padPowerFromSdl(SDL_PowerState s) noexcept
{
	switch (s)
	{
	case SDL_POWERSTATE_ON_BATTERY: return PadPower::OnBattery;
	case SDL_POWERSTATE_NO_BATTERY: return PadPower::Wired;
	case SDL_POWERSTATE_CHARGING:   return PadPower::Charging;
	case SDL_POWERSTATE_CHARGED:    return PadPower::Charged;
	default:                        return PadPower::Unknown;
	}
}

#endif  // MITIRU_HAS_SDL3

class SdlGamepadInput
{
public:
	static constexpr int kMaxPads = kGamepadSlots;

	SdlGamepadInput() = default;
	SdlGamepadInput(const SdlGamepadInput&) = delete;
	SdlGamepadInput& operator=(const SdlGamepadInput&) = delete;

#ifdef MITIRU_HAS_SDL3
	~SdlGamepadInput() { shutdown(); }

	/// @brief SDL の gamepad を起こし、つながっているパッドを枠に入れる。失敗したら 1 度だけ警告し、以後は試さない
	bool init()
	{
		if (m_initialized) { return true; }
		if (m_initFailed) { return false; }
		if (!runtimeAvailable())
		{
			m_initFailed = true;
			mitiru::debug::warnOnce("sdl3.gamepad.dll",
				"exe の隣に SDL3.dll が見つからないので、パッドを読みません。"
				"ビルドし直すか、配る物に SDL3.dll を入れてください。");
			return false;
		}
		if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		{
			m_initFailed = true;
			mitiru::debug::warnOnce("sdl3.gamepad.init",
				std::string("パッドを読む準備に失敗しました (") + SDL_GetError() + ")。"
				"mitiru_host の隣にある SDL3.dll が 3.4 系かを確かめてください。");
			return false;
		}
		m_initialized = true;
		// 対応表はパッドを開く前に足す。開いた後に足した割り当ては、そのパッドを開き直すまで効かない
		addMappingsFromFile(baseDirectory() / kMappingFileName);
		addConnected();
		return true;
	}

	/// @brief SDL 同梱の対応表に無いパッド (新しい機種、互換品) の割り当てを足す
	/// @return 足した件数。ファイルが無ければ 0、読めなければ -1
	/// @details 書式は SDL_GameControllerDB の gamecontrollerdb.txt。SDL は自分の platform: の行しか読まないので、0 件は警告する
	static int addMappingsFromFile(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) { return 0; }
		const auto u8 = path.u8string();
		const std::string utf8(u8.begin(), u8.end());
		const int added = SDL_AddGamepadMappingsFromFile(utf8.c_str());
		if (added < 0)
		{
			mitiru::debug::warnOnce("sdl.gamecontrollerdb.read",
				"パッドの割り当て表 " + utf8 + " を読めません (" + SDL_GetError() + ")。"
				"読み取り権限があるか、文字コードが UTF-8 かを確かめてください。");
			return -1;
		}
		if (added == 0)
		{
			mitiru::debug::warnOnce("sdl.gamecontrollerdb.empty",
				"パッドの割り当て表 " + utf8 + " に、この OS 向け (platform:" + SDL_GetPlatform()
					+ ") の行がありません。SDL_GameControllerDB の最新の gamecontrollerdb.txt に置き換えてください。");
		}
		return added;
	}

	void shutdown()
	{
		if (!m_initialized) { return; }
		for (int s = 0; s < kMaxPads; ++s) { closeSlot(s); }
		m_slots.clear();
		SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		m_initialized = false;
	}

	/// @brief 毎 render フレーム呼ぶ。抜き差しを反映し、今の状態を読む。prev の前進は endTick() が行う
	void update()
	{
		if (!init()) { return; }
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_EVENT_GAMEPAD_ADDED)        { onAdded(ev.gdevice.which); }
			else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) { onRemoved(ev.gdevice.which); }
		}
		for (int s = 0; s < kMaxPads; ++s) { readSlot(s); }
	}

	void endTick() noexcept
	{
		for (int s = 0; s < kMaxPads; ++s) { m_prevDown[s] = m_currDown[s]; }
	}

	[[nodiscard]] int count() const noexcept { return m_slots.count(); }
	[[nodiscard]] bool connected(int slot) const noexcept { return pad(slot) != nullptr; }

	[[nodiscard]] std::uint32_t buttonsDown(int slot) const noexcept { return valid(slot) ? m_currDown[slot] : 0u; }
	[[nodiscard]] std::uint32_t buttonsJustPressed(int slot) const noexcept
	{
		return valid(slot) ? (m_currDown[slot] & ~m_prevDown[slot]) : 0u;
	}
	[[nodiscard]] std::uint32_t buttonsJustReleased(int slot) const noexcept
	{
		return valid(slot) ? (~m_currDown[slot] & m_prevDown[slot]) : 0u;
	}
	/// @brief 軸値。stick は [-1,1] (上が正、デッドゾーン適用済)、trigger は [0,1]。axisIndex は module::gamepad::Axis
	[[nodiscard]] float axis(int slot, int axisIndex) const noexcept
	{
		if (!valid(slot) || axisIndex < 0 || axisIndex >= module::gamepad::AxisCount) { return 0.0f; }
		return m_axes[slot][axisIndex];
	}

	// ── InputSnapshot に載らない機能 (host 側だけ) ──

	[[nodiscard]] PadInfo info(int slot) const noexcept
	{
		PadInfo out;
		SDL_Gamepad* g = pad(slot);
		if (g == nullptr) { return out; }
		out.kind = padKindFromSdl(SDL_GetGamepadType(g));
		out.vendorId = SDL_GetGamepadVendor(g);
		out.productId = SDL_GetGamepadProduct(g);
		int percent = -1;
		out.power = padPowerFromSdl(SDL_GetGamepadPowerInfo(g, &percent));
		out.batteryPercent = static_cast<std::int8_t>(percent);
		out.touchpadCount = static_cast<std::uint8_t>(SDL_GetNumGamepadTouchpads(g));
		out.hasGyro = SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO);
		out.hasAccel = SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL);
		const SDL_PropertiesID props = SDL_GetGamepadProperties(g);
		out.hasRgbLed = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);
		out.hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
		out.hasTriggerRumble = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);
		// 対応表の type: 上書きで絵柄を変えても、効果のレポートの形は実機で決まる
		out.hasAdaptiveTriggers = SDL_GetRealGamepadType(g) == SDL_GAMEPAD_TYPE_PS5;
		return out;
	}

	/// @brief ジャイロと加速度を読めるようにする。電池を食うので使う間だけ有効にする
	bool setMotionEnabled(int slot, bool enabled) noexcept
	{
		SDL_Gamepad* g = pad(slot);
		if (g == nullptr) { return false; }
		bool ok = true;
		for (const SDL_SensorType t : {SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL})
		{
			if (SDL_GamepadHasSensor(g, t)) { ok = SDL_SetGamepadSensorEnabled(g, t, enabled) && ok; }
		}
		return ok;
	}

	[[nodiscard]] PadMotion motion(int slot) const noexcept
	{
		PadMotion out;
		SDL_Gamepad* g = pad(slot);
		if (g == nullptr) { return out; }
		out.hasGyro = SDL_GetGamepadSensorData(g, SDL_SENSOR_GYRO, out.gyro.data(), 3);
		out.hasAccel = SDL_GetGamepadSensorData(g, SDL_SENSOR_ACCEL, out.accel.data(), 3);
		return out;
	}

	[[nodiscard]] PadTouchFinger touchFinger(int slot, int touchpad, int finger) const noexcept
	{
		PadTouchFinger out;
		SDL_Gamepad* g = pad(slot);
		if (g == nullptr) { return out; }
		if (!SDL_GetGamepadTouchpadFinger(g, touchpad, finger, &out.down, &out.x, &out.y, &out.pressure)) { return {}; }
		return out;
	}

	/// タッチパッドの押し込みは InputSnapshot のボタンに枠が無いので別に読む
	[[nodiscard]] bool touchpadPressed(int slot) const noexcept
	{
		SDL_Gamepad* g = pad(slot);
		return g != nullptr && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_TOUCHPAD);
	}

	bool setLightbar(int slot, std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
	{
		SDL_Gamepad* p = pad(slot);
		return p != nullptr && SDL_SetGamepadLED(p, r, g, b);
	}

	/// @brief DualSense のアダプティブトリガー。DualSense 以外では false を返し、何も送らない
	bool setTriggerEffect(int slot, TriggerSide side, const TriggerEffect& effect) noexcept
	{
		SDL_Gamepad* g = pad(slot);
		if (g == nullptr || SDL_GetRealGamepadType(g) != SDL_GAMEPAD_TYPE_PS5) { return false; }
		const auto packet = dualSenseTriggerPacket(side, effect);
		return SDL_SendGamepadEffect(g, packet.data(), static_cast<int>(packet.size()));
	}

	/// @param low 左の重いモーター 0..1、high 右の軽いモーター 0..1。durationMs 経つと止まる (host が止まっても回り続けない)
	bool rumble(int slot, float low, float high, std::uint32_t durationMs) noexcept
	{
		SDL_Gamepad* g = pad(slot);
		return g != nullptr && SDL_RumbleGamepad(g, rumbleMagnitude(low), rumbleMagnitude(high), durationMs);
	}

	/// @brief トリガーの振動 (Xbox One 以降の Impulse Trigger)。持たないパッドは false
	bool rumbleTriggers(int slot, float left, float right, std::uint32_t durationMs) noexcept
	{
		SDL_Gamepad* g = pad(slot);
		return g != nullptr && SDL_RumbleGamepadTriggers(g, rumbleMagnitude(left), rumbleMagnitude(right), durationMs);
	}

	/// Hud::rumble にはパッドの指定が無く、InputSnapshot の合成 (gamepad*) も全台をまとめるので、全台を揺らす
	void rumbleAll(float low, float high, std::uint32_t durationMs) noexcept
	{
		for (int s = 0; s < kMaxPads; ++s) { rumble(s, low, high, durationMs); }
	}

private:
	static constexpr const char* kMappingFileName = "gamecontrollerdb.txt";

	/// exe は SDL3.dll を遅延読み込みにしている (cmake/MitiruSdl3.cmake)。無いまま SDL を呼ぶと例外で落ちるので先に確かめる。
	/// 遅延読み込みと同じ探し方 (exe のディレクトリが先) で読み、プロセスの終わりまで解放しない
	static bool runtimeAvailable() noexcept
	{
#ifdef _WIN32
		return LoadLibraryExW(L"SDL3.dll", nullptr, 0) != nullptr;
#else
		return true;
#endif
	}

	[[nodiscard]] bool valid(int slot) const noexcept { return slot >= 0 && slot < kMaxPads; }
	[[nodiscard]] SDL_Gamepad* pad(int slot) const noexcept { return valid(slot) ? m_pads[slot] : nullptr; }

	/// 作業ディレクトリでなく exe の隣を見る。ダブルクリック起動とショートカット起動で読む物が変わらないように
	static std::filesystem::path baseDirectory()
	{
		const char* raw = SDL_GetBasePath();
		if (raw == nullptr) { return {}; }
		return std::filesystem::path(reinterpret_cast<const char8_t*>(raw));
	}

	void onAdded(SDL_JoystickID id)
	{
		const int s = m_slots.add(id);
		if (s >= 0) { openSlot(s); }
	}

	/// 空いた枠には、満員の間につながって表に載らなかったパッドを入れる
	void onRemoved(SDL_JoystickID id)
	{
		const int s = m_slots.slotOf(id);
		if (s < 0) { return; }
		closeSlot(s);
		m_slots.remove(id);
		addConnected();
	}

	void addConnected()
	{
		int n = 0;
		SDL_JoystickID* ids = SDL_GetGamepads(&n);
		if (ids == nullptr) { return; }
		for (int i = 0; i < n; ++i) { onAdded(ids[i]); }
		SDL_free(ids);
	}

	void openSlot(int s)
	{
		const SDL_JoystickID id = m_slots.idAt(s);
		if (id == GamepadSlotTable::kEmpty || m_pads[s] != nullptr) { return; }
		m_pads[s] = SDL_OpenGamepad(id);
		if (m_pads[s] != nullptr) { return; }
		mitiru::debug::warnOnce("sdl3.gamepad.open",
			std::string("つないだパッドを開けません (") + SDL_GetError() + ")。"
			"DS4Windows などの別のアプリがパッドを使っていないか確かめてください。");
		m_slots.remove(id);
	}

	void closeSlot(int s)
	{
		if (m_pads[s] != nullptr) { SDL_CloseGamepad(m_pads[s]); }
		m_pads[s] = nullptr;
		m_currDown[s] = 0;
		m_prevDown[s] = 0;
		for (float& a : m_axes[s]) { a = 0.0f; }
	}

	void readSlot(int s) noexcept
	{
		SDL_Gamepad* g = m_pads[s];
		if (g == nullptr) { return; }
		std::uint32_t bits = 0;
		for (const SdlButtonBit& m : kSdlButtonMap)
		{
			if (SDL_GetGamepadButton(g, m.button)) { bits |= m.bit; }
		}
		m_currDown[s] = bits;
		for (int a = 0; a < module::gamepad::AxisCount; ++a)
		{
			const auto raw = static_cast<std::int16_t>(SDL_GetGamepadAxis(g, kSdlAxisMap[a]));
			const bool isTrigger = a == module::gamepad::LeftTrigger || a == module::gamepad::RightTrigger;
			const bool isY = a == module::gamepad::LeftStickY || a == module::gamepad::RightStickY;
			m_axes[s][a] = isTrigger ? triggerAxis(raw) : stickAxis(raw, isY);
		}
	}

	bool             m_initialized = false;
	bool             m_initFailed = false;
	GamepadSlotTable m_slots;
	SDL_Gamepad*     m_pads[kMaxPads] = {};
	std::uint32_t    m_prevDown[kMaxPads] = {};
	std::uint32_t    m_currDown[kMaxPads] = {};
	float            m_axes[kMaxPads][module::gamepad::AxisCount] = {};

#else  // !MITIRU_HAS_SDL3

	bool init() noexcept { return false; }
	static int addMappingsFromFile(const std::filesystem::path&) noexcept { return 0; }
	void shutdown() noexcept {}
	/// 窓を出す起動 (headless でない) でだけ呼ばれる。パッドが黙って効かない状態を作らないため、ここで知らせる
	void update()
	{
		mitiru::debug::warnOnce("sdl3.gamepad.missing",
			"このビルドには SDL3 が入っていないので、パッドを読みません。"
			"python tools/fetch_sdl3.py を実行してから configure し直してください。");
	}
	void endTick() noexcept {}
	[[nodiscard]] int count() const noexcept { return 0; }
	[[nodiscard]] bool connected(int) const noexcept { return false; }
	[[nodiscard]] std::uint32_t buttonsDown(int) const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustPressed(int) const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustReleased(int) const noexcept { return 0; }
	[[nodiscard]] float axis(int, int) const noexcept { return 0.0f; }
	[[nodiscard]] PadInfo info(int) const noexcept { return {}; }
	bool setMotionEnabled(int, bool) noexcept { return false; }
	[[nodiscard]] PadMotion motion(int) const noexcept { return {}; }
	[[nodiscard]] PadTouchFinger touchFinger(int, int, int) const noexcept { return {}; }
	[[nodiscard]] bool touchpadPressed(int) const noexcept { return false; }
	bool setLightbar(int, std::uint8_t, std::uint8_t, std::uint8_t) noexcept { return false; }
	bool setTriggerEffect(int, TriggerSide, const TriggerEffect&) noexcept { return false; }
	bool rumble(int, float, float, std::uint32_t) noexcept { return false; }
	bool rumbleTriggers(int, float, float, std::uint32_t) noexcept { return false; }
	void rumbleAll(float, float, std::uint32_t) noexcept {}
#endif
};

}  // namespace mitiru::input
