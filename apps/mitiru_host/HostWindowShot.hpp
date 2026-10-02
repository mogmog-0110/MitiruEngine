#pragma once

/// @file HostWindowShot.hpp
/// @brief --window-shot: 本物の窓を人の見ない場所 (仮想ディスプレイか画面外) に出し、DWM が合成した絵を
///        PNG に撮って終わる。窓は前に出さず、フォーカスも取らない
/// @details 撮るのは --max-frames の最後のフレーム (onFrameStart の時点で 1 つ手前まで提示済み)。
///          走っている間、毎フレーム前面の窓を見て、自分の窓が前面になったフレームを数える。1 回でも
///          あれば、撮れていても失敗 (kExitWindowShotFailed) にする。人の操作を奪わないことが撮影より先。

#include <cstdio>
#include <optional>
#include <string>

#include <mitiru/core/Engine.hpp>
#include <mitiru/platform/win32/AiDisplay.hpp>
#include <mitiru/platform/win32/WindowShot.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

namespace mitiru::host
{

inline constexpr int kExitWindowShotFailed = 8;

class HostWindowShot
{
public:
	HostWindowShot(std::string path, int shotFrame) : m_path(std::move(path)), m_shotFrame(shotFrame) {}

	/// @brief client 寸法の窓を置く場所。仮想ディスプレイに置けなければ nullopt (= 画面外)
	/// @details 外形は client に枠とタイトルの分 (高 DPI でも足りる量) を足して見積もる。
	[[nodiscard]] static std::optional<platform::WindowOrigin> placement(int clientW, int clientH)
	{
#ifdef _WIN32
		return platform::chooseAiWindowOrigin(platform::enumerateDisplays(), clientW + 64, clientH + 96);
#else
		(void)clientW; (void)clientH;
		return std::nullopt;
#endif
	}

	/// @brief onFrameStart から毎フレーム呼ぶ。totalFrame は 1 始まりの host frame 番号
	void onFrame(Engine& engine, int totalFrame)
	{
#ifdef _WIN32
		IWindow* win = engine.window();
		if (win == nullptr) { return; }
		const HWND hwnd = reinterpret_cast<HWND>(win->nativeHandle());
		if (GetForegroundWindow() == hwnd) { ++m_foregroundFrames; }
		if (m_taken || totalFrame < m_shotFrame) { return; }
		m_taken = true;
		const platform::WindowShot shot = platform::captureWindowClient(hwnd);
		m_saved = !shot.empty() && render::savePixelsToPng(shot.rgba.data(), shot.width, shot.height, m_path);
		if (m_saved) { m_width = shot.width; m_height = shot.height; }
#else
		(void)engine; (void)totalFrame;
#endif
	}

	/// @brief 結果を 1 行出し、終了コードを返す (0 = 撮れて前面も取らなかった)
	[[nodiscard]] int finish() const
	{
		if (m_foregroundFrames > 0)
		{
			std::fprintf(stderr, "mitiru_host: --window-shot: 窓が %d フレーム前面に出た (人の操作を奪った)\n",
			             m_foregroundFrames);
			return kExitWindowShotFailed;
		}
		if (!m_saved)
		{
			std::fprintf(stderr, "mitiru_host: --window-shot: %s を撮れなかった (%s)\n", m_path.c_str(),
			             m_taken ? "PrintWindow か PNG の保存に失敗" : "撮るフレームまで走らなかった");
			return kExitWindowShotFailed;
		}
		std::fprintf(stderr, "[mitiru_host] window-shot: %dx%d -> %s (前面に出たフレーム 0)\n",
		             m_width, m_height, m_path.c_str());
		return 0;
	}

private:
	std::string m_path;
	int         m_shotFrame;
	int         m_foregroundFrames = 0;
	bool        m_taken = false;
	bool        m_saved = false;
	int         m_width = 0;
	int         m_height = 0;
};

}  // namespace mitiru::host
