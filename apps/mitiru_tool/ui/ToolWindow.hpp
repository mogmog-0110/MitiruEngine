#pragma once
// ツール窓を本物の窓に出す。窓は Win32Window、描画はエンジンの Dx12Device の swap chain。
// ページごとの窓の形 (rewind は横長の低いシークバー、scene_view / why_view / frame_view は吸着しない対話窓)
// もここで決める。

#include "ToolSession.hpp"

#include <array>
#include <climits>
#include <string>

namespace mitiru::tool
{

struct WindowSpec
{
	int width = 400;
	int height = 620;
	int minWidth = 300;
	int minHeight = 360;
	int dockMode = 2;       ///< 0 = 吸着しない、1 = ゲーム窓の下辺 (シークバー)、2 = 右脇
	std::string title;
};

[[nodiscard]] WindowSpec windowSpecFor(const std::string& page);

/// ページの地色 (撮影と窓の clear に使う)。RCSS の body と同じ色。
[[nodiscard]] std::array<float, 4> pageBackground(const std::string& page);

struct WindowRun
{
	int frames = 0;
	bool documentLoaded = false;
	std::string error;
};

/// 窓を開いて、閉じられるか監視中のゲーム (host) が終わるまで回す。ゲームが閉じたらこの窓も閉じる (逆はしない)。
/// @param posX INT_MIN は OS 任せ
/// @param maxFrames 0 は無制限 (テストが数フレームで止めるため)
WindowRun runToolWindow(const ToolOptions& options, const WindowSpec& spec, int posX = INT_MIN, int posY = INT_MIN,
                        int maxFrames = 0);

} // namespace mitiru::tool
