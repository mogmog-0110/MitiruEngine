#pragma once

/// @file SdlGamepadInput.hpp
/// @brief SDL_GameController バックエンド (DS4/DS5 等 XInput 非対応パッドの実機サポート、#32)。
/// @details Windows の XInput は Microsoft 系コントローラしか拾わないため、DS4/DS5/Switch Pro 等は
///          別途 DS4Windows / Steam Input が無いと反応しない。SDL_GameController は controller DB
///          経由で広範な機種を統一 API で扱える。本クラスは XInput と並走し、buildModuleInputSnapshot で
///          ボタンを OR、axes は XInput 未接続時のみ SDL を採用する。
///
/// MITIRU_HAS_SDL2 未定義 (SDL2 が CMake で見つからない) 環境では all-no-op stub を提供するので、
/// engine コードは無条件に SdlGamepadInput を持って良い。

#include <cstdint>

#include <mitiru/module/ModuleApi.hpp>

#ifdef MITIRU_HAS_SDL2
#include <SDL.h>
#include <vector>
#endif

namespace mitiru::input
{

class SdlGamepadInput
{
public:
#ifdef MITIRU_HAS_SDL2
	~SdlGamepadInput() { shutdown(); }

	/// @brief SDL_GameController サブシステムを初期化し、現在接続中の全 controller を open する。
	/// @return 成功で true。SDL_Init が既に呼ばれてれば InitSubSystem は安全 (refcount)。
	bool init()
	{
		if (m_initialized) { return true; }
		if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) { return false; }
		m_initialized = true;
		rescan();
		return true;
	}

	void shutdown()
	{
		if (!m_initialized) { return; }
		for (auto* c : m_open) { if (c) { SDL_GameControllerClose(c); } }
		m_open.clear();
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
		m_initialized = false;
	}

	/// @brief 毎 render フレーム呼ぶ。SDL state を pump し、抜き差しを再 scan、現在状態を poll。
	/// @details edge (prev/curr) の前進はここでは行わない。just-pressed を fixed-update
	///          cadence に揃えるため、prev の前進は endTick() が担う。
	void update()
	{
		if (!m_initialized) { if (!init()) { return; } }  // lazy init
		SDL_GameControllerUpdate();
		// event を消化して device added/removed に追従。
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_CONTROLLERDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEREMOVED)
			{
				rescan();
			}
		}

		m_currDown = computeDown();
	}

	/// @brief edge 検出用に prev=curr を 1 段進める (1 fixed-update tick の末で呼ぶ)。
	void endTick() noexcept { m_prevDown = m_currDown; }

	[[nodiscard]] bool connected() const noexcept { return !m_open.empty(); }

	[[nodiscard]] std::uint32_t buttonsDown()         const noexcept { return m_currDown; }
	[[nodiscard]] std::uint32_t buttonsJustPressed()  const noexcept { return m_currDown & ~m_prevDown; }
	[[nodiscard]] std::uint32_t buttonsJustReleased() const noexcept { return ~m_currDown & m_prevDown; }

	/// @brief 軸値 [-1,1] (trigger は [0,1])。axisIndex は mitiru::module::gamepad::LeftStickX 等。
	[[nodiscard]] float axis(int axisIndex) const noexcept
	{
		if (m_open.empty()) { return 0.0f; }
		SDL_GameControllerAxis a = SDL_CONTROLLER_AXIS_INVALID;
		bool isTrigger = false;
		switch (axisIndex)
		{
		case mitiru::module::gamepad::LeftStickX:  a = SDL_CONTROLLER_AXIS_LEFTX;  break;
		case mitiru::module::gamepad::LeftStickY:  a = SDL_CONTROLLER_AXIS_LEFTY;  break;
		case mitiru::module::gamepad::RightStickX: a = SDL_CONTROLLER_AXIS_RIGHTX; break;
		case mitiru::module::gamepad::RightStickY: a = SDL_CONTROLLER_AXIS_RIGHTY; break;
		case mitiru::module::gamepad::LeftTrigger:  a = SDL_CONTROLLER_AXIS_TRIGGERLEFT;  isTrigger = true; break;
		case mitiru::module::gamepad::RightTrigger: a = SDL_CONTROLLER_AXIS_TRIGGERRIGHT; isTrigger = true; break;
		default: return 0.0f;
		}
		const int v = SDL_GameControllerGetAxis(m_open.front(), a);
		// SDL stick = -32768..32767。Y は SDL では下が正 → engine 慣習 (上が正) に反転する。
		if (isTrigger)
		{
			return static_cast<float>(v) / 32767.0f;  // 0..32767 を 0..1
		}
		float f = static_cast<float>(v) / 32767.0f;
		if (axisIndex == mitiru::module::gamepad::LeftStickY
		    || axisIndex == mitiru::module::gamepad::RightStickY)
		{
			f = -f;
		}
		// 簡素 deadzone。
		constexpr float kDead = 0.24f;
		if (f > -kDead && f < kDead) { return 0.0f; }
		return f;
	}

private:
	std::uint32_t computeDown() const noexcept
	{
		std::uint32_t bits = 0;
		for (auto* c : m_open)
		{
			if (!c) { continue; }
			using namespace mitiru::module;
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A))             { bits |= gamepad::A; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B))             { bits |= gamepad::B; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X))             { bits |= gamepad::X; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y))             { bits |= gamepad::Y; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK))          { bits |= gamepad::Back; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))         { bits |= gamepad::Start; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSTICK))     { bits |= gamepad::LS; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSTICK))    { bits |= gamepad::RS; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  { bits |= gamepad::LB; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) { bits |= gamepad::RB; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))       { bits |= gamepad::DPadUp; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))     { bits |= gamepad::DPadDown; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))     { bits |= gamepad::DPadLeft; }
			if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    { bits |= gamepad::DPadRight; }
		}
		return bits;
	}

	void rescan()
	{
		// 切断済みを破棄。
		std::vector<SDL_GameController*> kept;
		kept.reserve(m_open.size());
		for (auto* c : m_open)
		{
			if (c && SDL_GameControllerGetAttached(c)) { kept.push_back(c); }
			else if (c) { SDL_GameControllerClose(c); }
		}
		m_open.swap(kept);

		// 新規 device を open。
		const int n = SDL_NumJoysticks();
		for (int i = 0; i < n; ++i)
		{
			if (!SDL_IsGameController(i)) { continue; }
			auto* nc = SDL_GameControllerOpen(i);
			if (!nc) { continue; }
			// 既に持ってる instance なら閉じてスキップ。
			const auto newInst = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(nc));
			bool dup = false;
			for (auto* c : m_open)
			{
				if (c && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == newInst)
				{
					dup = true; break;
				}
			}
			if (dup) { SDL_GameControllerClose(nc); }
			else     { m_open.push_back(nc); }
		}
	}

	bool                              m_initialized = false;
	std::vector<SDL_GameController*>  m_open;
	std::uint32_t                     m_prevDown = 0;
	std::uint32_t                     m_currDown = 0;

#else  // !MITIRU_HAS_SDL2 → all-no-op stub

public:
	bool init() noexcept { return false; }
	void shutdown() noexcept {}
	void update() noexcept {}
	void endTick() noexcept {}
	[[nodiscard]] bool connected() const noexcept { return false; }
	[[nodiscard]] std::uint32_t buttonsDown()         const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustPressed()  const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustReleased() const noexcept { return 0; }
	[[nodiscard]] float axis(int) const noexcept { return 0.0f; }
#endif
};

}  // namespace mitiru::input
