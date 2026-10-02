#pragma once
// mitiru_tool の exe の部品 (引数と撮影)。ページの中身と窓のループは ui/ にある。

#include "ui/ToolWindow.hpp"

#include <climits>
#include <filesystem>
#include <optional>
#include <string>

namespace mitiru::tool
{

struct CliArgs
{
	ToolOptions options;
	int windowX = INT_MIN;   ///< --window-pos (INT_MIN は OS 任せ)
	int windowY = INT_MIN;
	std::optional<std::filesystem::path> capture;   ///< --capture <png>: 窓を出さずに撮って終わる
	int captureFrames = 60;
	int captureWidth = 0;    ///< 0 はページの既定の大きさ
	int captureHeight = 0;
	float captureDp = 1.0f;
	std::string captureInput;   ///< --capture-input "10:click:200,40;12:type:hp;14:key:13" (撮る前に UI へ渡す操作)
	bool ok = true;
};

[[nodiscard]] CliArgs parseArgs(int argc, char* argv[]);

int runCapture(const CliArgs& args, const WindowSpec& spec);

} // namespace mitiru::tool
