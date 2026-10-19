#pragma once

/// @file HostHotkeys.hpp
/// @brief host の開発用のキー (F7〜F12) とデバッグメニュー (F1) をまとめて切る方針と、キーを命令にする判断 (ADR 0084)。
/// @details 切る指示は起動の引数、DLL の隣の ship.json、ゲームの申告 (kDebugMenuDevToolsOff) の 3 つで、どれか 1 つでも切ると言えば切れる。
///          F12 (画面の保存) だけは、どれかが残すと言えば切っていても残る。Win32 のキー読みはここに入れず、host が押した瞬間を渡す。

#include <cstdint>
#include <string>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::debug
{

class DevKeyPolicy
{
public:
	void setCommandLine(bool off, bool keepScreenshot) noexcept
	{
		commandLineOff_ = off;
		commandLineKeepScreenshot_ = keepScreenshot;
	}

	void setShipManifest(bool off, bool keepScreenshot) noexcept
	{
		shipManifestOff_ = off;
		shipManifestKeepScreenshot_ = keepScreenshot;
	}

	void setGameFlags(std::uint32_t flags) noexcept
	{
		gameOff_ = (flags & module::kDebugMenuDevToolsOff) != 0;
		gameKeepScreenshot_ = (flags & module::kDebugMenuKeepScreenshotKey) != 0;
	}

	[[nodiscard]] bool off() const noexcept
	{
		return commandLineOff_ || shipManifestOff_ || gameOff_;
	}

	[[nodiscard]] bool hotkeysEnabled() const noexcept
	{
		return !off();
	}

	[[nodiscard]] bool screenshotKeyEnabled() const noexcept
	{
		return !off() || commandLineKeepScreenshot_ || shipManifestKeepScreenshot_ ||
		       gameKeepScreenshot_;
	}

	[[nodiscard]] bool menuEnabled() const noexcept
	{
		return !off();
	}

	[[nodiscard]] std::string reason() const
	{
		if (commandLineOff_)
		{
			return "--no-dev-keys";
		}
		if (shipManifestOff_)
		{
			return "ship.json";
		}
		if (gameOff_)
		{
			return "the game";
		}
		return {};
	}

private:
	bool commandLineOff_ = false;
	bool commandLineKeepScreenshot_ = false;
	bool shipManifestOff_ = false;
	bool shipManifestKeepScreenshot_ = false;
	bool gameOff_ = false;
	bool gameKeepScreenshot_ = false;
};

struct HotkeyPress
{
	bool f7 = false;
	bool f8 = false;
	bool f9 = false;
	bool f10 = false;
	bool f11 = false;
	bool f12 = false;
};

struct HotkeyActions
{
	bool screenshot = false;
	bool step = false;
	bool togglePause = false;
	bool cycleSpeed = false;
	bool toggleLofi = false;
	bool saveBugRecord = false;
	bool cinematicNotice = false;
};

[[nodiscard]] inline HotkeyActions decideHotkeys(
	const DevKeyPolicy& policy,
	const HotkeyPress& press,
	bool cinematic,
	bool paused) noexcept
{
	HotkeyActions actions;
	actions.screenshot = policy.screenshotKeyEnabled() && press.f12;
	if (!policy.hotkeysEnabled())
	{
		return actions;
	}

	actions.toggleLofi = press.f10;
	actions.saveBugRecord = press.f11;
	if (cinematic)
	{
		actions.cinematicNotice = press.f7 || press.f8 || press.f9;
		return actions;
	}

	actions.step = press.f7 && paused;
	actions.togglePause = press.f8;
	actions.cycleSpeed = press.f9;
	return actions;
}

}  // namespace mitiru::debug
