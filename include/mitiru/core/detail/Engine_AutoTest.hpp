// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── auto-test hooks のクラス外定義 ─────────────────────────────

MITIRU_INLINE void mitiru::Engine::saveAutoTestScreenshot(const std::string& outputDir)
{
	const auto pixels = capture();
	if (pixels.empty())
	{
		return;
	}

	// 画像サイズは capture() で読み取ったバックバッファ実サイズに合わせる。
	const int w = m_window ? m_window->width()  : (m_screen ? m_screen->width()  : 0);
	const int h = m_window ? m_window->height() : (m_screen ? m_screen->height() : 0);
	if (w <= 0 || h <= 0)
	{
		return;
	}

	const std::string path = outputDir + "/auto_test.png";
	savePng(path, pixels, w, h);
}

MITIRU_INLINE void mitiru::Engine::saveAutoTestReport(
	const std::string& outputDir, const EngineConfig& config)
{
	const float fps = (m_clock && m_clock->elapsed() > 0.0f)
		? static_cast<float>(m_clock->frameNumber()) / m_clock->elapsed()
		: 0.0f;
	const int drawCalls = m_screen ? m_screen->drawCallCount() : 0;
	const int backend = static_cast<int>(config.gfxBackend);

	std::string json = "{\n";
	json += "  \"success\": true,\n";
	json += "  \"frames_rendered\": " + std::to_string(m_autoTestFrameCount) + ",\n";
	json += "  \"fps\": " + std::to_string(fps) + ",\n";
	json += "  \"draw_calls\": " + std::to_string(drawCalls) + ",\n";
	json += "  \"errors\": [],\n";
	json += "  \"warnings\": [],\n";
	json += "  \"window_size\": [" + std::to_string(config.windowWidth)
		+ ", " + std::to_string(config.windowHeight) + "],\n";
	json += "  \"renderer\": " + std::to_string(backend) + ",\n";
	json += "  \"screenshot\": \"auto_test.png\"\n";
	json += "}\n";

	const std::string path = outputDir + "/auto_test_report.json";
	std::ofstream ofs(path);
	if (ofs)
	{
		ofs << json;
	}
}
