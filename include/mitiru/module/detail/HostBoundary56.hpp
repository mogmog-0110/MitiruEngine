#pragma once

/// @file HostBoundary56.hpp
/// @brief ABI v56 (ADR 0081) の host 側の小さな関数。Engine を持ち込まずにテストする。使うのは core/detail/Engine_Module_*.hpp

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module::detail
{

struct WindowRequest
{
	bool present;
	bool fullscreen;
	int keepAspectMilli;
};

[[nodiscard]] inline WindowRequest decodeWindowRequest(const FrameIntents& intents) noexcept
{
	switch (intents.windowModeRequest)
	{
	case 1:
		return WindowRequest{true, false, 0};
	case 2:
		return WindowRequest{
			true,
			true,
			static_cast<int>(intents.windowKeepAspectMilli)};
	default:
		return WindowRequest{false, false, 0};
	}
}

}  // namespace mitiru::module::detail
