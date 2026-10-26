#pragma once

/// @file HostUiContrast.hpp
/// @brief --ui-contrast <file>: 指定したフレームの UI の文字ごとのコントラスト比を JSON に書く。
/// @details 画素は読まず、RmlUi の計算後のスタイル (文字色と下地) から測る。撮影 (tools/grab_scene.py --contrast) が呼ぶ。

#include <filesystem>
#include <fstream>
#include <string>

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/ui_rml/ContrastReport.hpp>

namespace mitiru::host
{

class HostUiContrast
{
public:
	void configure(std::string outPath, int atFrame)
	{
		m_out = std::move(outPath);
		m_atFrame = atFrame;
	}

	[[nodiscard]] bool enabled() const noexcept { return !m_out.empty(); }

	/// @brief onFrameStart から毎フレーム呼ぶ。決めたフレームで 1 回だけ書く
	void onFrame(Engine& engine)
	{
		if (m_out.empty() || m_done || m_frame++ < m_atFrame) { return; }
		m_done = true;
		const bool active = engine.uiHost().active();
		const std::string json = ui_rml::contrastReportJson(engine.uiHost().checkContrast(), active);
		const std::u8string path(m_out.begin(), m_out.end());
		std::ofstream file(std::filesystem::path(path), std::ios::binary);
		if (!file || !(file << json))
		{
			console::noticef("--ui-contrast の書き出し先 %s に書けませんでした", m_out.c_str());
		}
	}

private:
	std::string m_out;
	int m_atFrame = 0;
	int m_frame = 0;
	bool m_done = false;
};

}  // namespace mitiru::host
