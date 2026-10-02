#pragma once

/// @file SdlGamepadInput.hpp
/// @brief SDL_GameController バックエンド (DS4/DS5 等 XInput 非対応パッドの実機サポート、#32)。
/// @details Windows の XInput は Microsoft 系コントローラしか拾わないため、DS4/DS5/Switch Pro 等は
///          別途 DS4Windows / Steam Input が無いと反応しない。SDL_GameController は controller DB
///          経由で広範な機種を統一 API で扱える。本クラスは XInput と並走し、開いた順に 1 台ずつ読める
///          (buildModuleInputSnapshot が XInput の空き枠へ詰める。input/GamepadSlots.hpp)。
///
/// MITIRU_HAS_SDL2 未定義 (SDL2 が CMake で見つからない) 環境では all-no-op stub を提供するので、
/// engine コードは無条件に SdlGamepadInput を持って良い。

#include <cstdint>

#include <mitiru/module/ModuleApi.hpp>

#include <filesystem>

#ifdef MITIRU_HAS_SDL2
#include <SDL.h>
#include <string>
#include <system_error>
#include <vector>

#include <mitiru/debug/WarnOnce.hpp>
#endif

namespace mitiru::input
{

class SdlGamepadInput
{
public:
	/// @brief 1 台ずつ読める上限 (InputSnapshot::gamepads と同じ)。これを超えた分は読まない。
	static constexpr int kMaxPads = 4;

#ifdef MITIRU_HAS_SDL2
	~SdlGamepadInput() { shutdown(); }

	/// @brief SDL_GameController サブシステムを初期化し、現在接続中の全 controller を open する。
	/// @return 成功で true。SDL_Init が既に呼ばれてれば InitSubSystem は安全 (refcount)。
	bool init()
	{
		if (m_initialized) { return true; }
		if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) { return false; }
		m_initialized = true;
		// 対応表は controller を open する前に足す。open 済みの controller は古い割り当てのまま動き続けるため
		addMappingsFromFile(baseDirectory() / kMappingFileName);
		rescan();
		return true;
	}

	/// @brief SDL 同梱の対応表に無いパッド (新しい機種、互換品) の割り当てを足す
	/// @return 足した件数。ファイルが無ければ 0、読めなければ -1
	/// @details 書式は SDL_GameControllerDB (https://github.com/mdqinc/SDL_GameControllerDB) の
	///          gamecontrollerdb.txt。SDL は自分の platform: の行しか読まないので、0 件は警告する。
	static int addMappingsFromFile(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) { return 0; }
		const auto u8 = path.u8string();
		const std::string utf8(u8.begin(), u8.end());
		const int added = SDL_GameControllerAddMappingsFromFile(utf8.c_str());
		if (added < 0)
		{
			mitiru::debug::warnOnceFix("sdl.gamecontrollerdb.read",
				"gamepad: " + utf8 + " を読めなかった", SDL_GetError(),
				"ファイルの読み取り権限と文字コード (UTF-8) を確かめる");
			return -1;
		}
		if (added == 0)
		{
			mitiru::debug::warnOnceFix("sdl.gamecontrollerdb.empty",
				"gamepad: " + utf8 + " にこの OS 向けの割り当てが 1 行も無かった",
				std::string("platform:") + SDL_GetPlatform() + " の行だけが読まれる",
				"SDL_GameControllerDB の最新の gamecontrollerdb.txt に置き換える");
		}
		return added;
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
		for (int i = 0; i < kMaxPads; ++i)
		{
			m_currDown[i] = (i < count()) ? computeDown(m_open[static_cast<std::size_t>(i)]) : 0u;
		}
	}

	/// @brief edge 検出用に prev=curr を 1 段進める (1 fixed-update tick の末で呼ぶ)。
	void endTick() noexcept
	{
		for (int i = 0; i < kMaxPads; ++i) { m_prevDown[i] = m_currDown[i]; }
	}

	/// @brief 開いている controller の数 (kMaxPads まで)。
	[[nodiscard]] int count() const noexcept
	{
		const int n = static_cast<int>(m_open.size());
		return (n < kMaxPads) ? n : kMaxPads;
	}

	/// @brief i 台目が XInput でも見えている Xbox 系パッドか。Windows では XInput 側で読むので二重に数えない。
	[[nodiscard]] bool isXInputDevice(int i) const noexcept
	{
		if (!valid(i)) { return false; }
		const SDL_GameControllerType t = SDL_GameControllerGetType(m_open[static_cast<std::size_t>(i)]);
		return t == SDL_CONTROLLER_TYPE_XBOX360 || t == SDL_CONTROLLER_TYPE_XBOXONE;
	}

	[[nodiscard]] std::uint32_t buttonsDown(int i) const noexcept { return valid(i) ? m_currDown[i] : 0u; }
	[[nodiscard]] std::uint32_t buttonsJustPressed(int i) const noexcept
	{
		return valid(i) ? (m_currDown[i] & ~m_prevDown[i]) : 0u;
	}
	[[nodiscard]] std::uint32_t buttonsJustReleased(int i) const noexcept
	{
		return valid(i) ? (~m_currDown[i] & m_prevDown[i]) : 0u;
	}

	/// @brief i 台目の軸値 [-1,1] (trigger は [0,1])。axisIndex は mitiru::module::gamepad::LeftStickX 等。
	[[nodiscard]] float axis(int i, int axisIndex) const noexcept
	{
		if (!valid(i)) { return 0.0f; }
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
		const int v = SDL_GameControllerGetAxis(m_open[static_cast<std::size_t>(i)], a);
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
	static constexpr const char* kMappingFileName = "gamecontrollerdb.txt";

	[[nodiscard]] bool valid(int i) const noexcept { return i >= 0 && i < count(); }

	/// 作業ディレクトリでなく exe の隣を見る。ダブルクリック起動とショートカット起動で読む物が変わらないように
	static std::filesystem::path baseDirectory()
	{
		char* raw = SDL_GetBasePath();
		if (raw == nullptr) { return {}; }
		const std::filesystem::path dir(reinterpret_cast<const char8_t*>(raw));
		SDL_free(raw);
		return dir;
	}

	static std::uint32_t computeDown(SDL_GameController* c) noexcept
	{
		if (c == nullptr) { return 0u; }
		using namespace mitiru::module;
		struct Map { SDL_GameControllerButton sdl; std::uint32_t bit; };
		static constexpr Map kMap[] = {
			{SDL_CONTROLLER_BUTTON_A, gamepad::A}, {SDL_CONTROLLER_BUTTON_B, gamepad::B},
			{SDL_CONTROLLER_BUTTON_X, gamepad::X}, {SDL_CONTROLLER_BUTTON_Y, gamepad::Y},
			{SDL_CONTROLLER_BUTTON_BACK, gamepad::Back}, {SDL_CONTROLLER_BUTTON_START, gamepad::Start},
			{SDL_CONTROLLER_BUTTON_LEFTSTICK, gamepad::LS}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, gamepad::RS},
			{SDL_CONTROLLER_BUTTON_LEFTSHOULDER, gamepad::LB}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, gamepad::RB},
			{SDL_CONTROLLER_BUTTON_DPAD_UP, gamepad::DPadUp}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, gamepad::DPadDown},
			{SDL_CONTROLLER_BUTTON_DPAD_LEFT, gamepad::DPadLeft}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, gamepad::DPadRight},
		};
		std::uint32_t bits = 0;
		for (const Map& m : kMap)
		{
			if (SDL_GameControllerGetButton(c, m.sdl)) { bits |= m.bit; }
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
	std::uint32_t                     m_prevDown[kMaxPads] = {};
	std::uint32_t                     m_currDown[kMaxPads] = {};

#else  // !MITIRU_HAS_SDL2 → all-no-op stub

public:
	bool init() noexcept { return false; }
	static int addMappingsFromFile(const std::filesystem::path&) noexcept { return 0; }
	void shutdown() noexcept {}
	void update() noexcept {}
	void endTick() noexcept {}
	[[nodiscard]] int count() const noexcept { return 0; }
	[[nodiscard]] bool isXInputDevice(int) const noexcept { return false; }
	[[nodiscard]] std::uint32_t buttonsDown(int)         const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustPressed(int)  const noexcept { return 0; }
	[[nodiscard]] std::uint32_t buttonsJustReleased(int) const noexcept { return 0; }
	[[nodiscard]] float axis(int, int) const noexcept { return 0.0f; }
#endif
};

}  // namespace mitiru::input
