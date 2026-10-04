// --capture <png>: 窓を作らずに windowless の Dx12Device へ描いて 1 枚撮る。ツール窓の見た目を
// 人の画面に何も出さずに確かめるための口。HTTP を叩くページは応答を待つので実時間で回す。

#include "CaptureInput.hpp"
#include "ToolMain.hpp"

#include <mitiru/gfx/dx12/Dx12Device.hpp>

#include <stb_image_write.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace mitiru::tool
{

int runCapture(const CliArgs& args, const WindowSpec& spec)
{
	const float dp = args.captureDp > 0.0f ? args.captureDp : 1.0f;
	const int w = args.captureWidth > 0 ? args.captureWidth : static_cast<int>(std::lround(static_cast<float>(spec.width) * dp));
	const int h = args.captureHeight > 0 ? args.captureHeight : static_cast<int>(std::lround(static_cast<float>(spec.height) * dp));
	std::unique_ptr<gfx::Dx12Device> device;
	try { device = std::make_unique<gfx::Dx12Device>(w, h); }
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "mitiru_tool: D3D12 のデバイスの作成に失敗しました (%s)。\n", e.what());
		return 1;
	}
	const auto bg = pageBackground(args.options.page);
	device->setClearColor(bg[0], bg[1], bg[2], bg[3]);
	ToolSession session;
	std::string error;
	if (!session.start(device->nativeDevice(), device->commandQueue(), args.options, w, h, dp, error))
	{
		std::fprintf(stderr, "mitiru_tool: 画面の準備に失敗しました (%s)。\n", error.c_str());
		return 1;
	}
	CaptureInput script(args.captureInput);
	std::vector<platform::Win32KeyMessage> keys;
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < args.captureFrames; ++i)
	{
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		const ToolPointer pointer = script.pointerAt(i, keys);
		session.frame(now, pointer, keys);
		device->beginFrame();
		session.render(device->currentBackBuffer()->nativeResource(), w, h);
		device->endFrame();
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	const std::vector<std::uint8_t> pixels = device->readPixels(w, h);
	const std::string out = args.capture->string();
	if (pixels.size() < static_cast<std::size_t>(w) * h * 4 || stbi_write_png(out.c_str(), w, h, 4, pixels.data(), w * 4) == 0)
	{
		std::fprintf(stderr, "mitiru_tool: %s に PNG を書き込めません。\n", out.c_str());
		return 1;
	}
	std::fprintf(stderr, "mitiru_tool: %s (%dx%d)\n", out.c_str(), w, h);
	return 0;
}

} // namespace mitiru::tool
