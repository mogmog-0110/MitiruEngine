// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
// 注意: このファイルは _WIN32 と MITIRU_HAS_CEF の両方が定義されているときのみインクルードされる。
#pragma once

#include <cstdio>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/cef/MitiruCefSchemeHandler.hpp>

// ── CEF integration のクラス外定義 ─────────────────────────────

MITIRU_INLINE void mitiru::Engine::initializeCef(const EngineConfig& config)
{
#if defined(_WIN32) && defined(MITIRU_HAS_CEF)
	auto* dx12Device = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	if (!dx12Device || !m_window)
	{
		return;
	}

	// 実行ファイルのディレクトリを取得する (CEF helper exe の検索に使う)
	char exePath[MAX_PATH] = {};
	::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	std::string exeDir = exePath;
	const auto lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos)
	{
		exeDir = exeDir.substr(0, lastSlash);
	}

	const std::string logPath = config.cefLogPath.empty()
		? exeDir + "/cef_debug.log"
		: config.cefLogPath;

	const std::string startUrl = config.cefStartUrl.empty()
		? "about:blank"
		: config.cefStartUrl;

	// `app://` ディスクフォールバック追加ルート (consumer ゲームが宣言)。
	// CefInitialize 前に登録する必要があるため、`MitiruCefContext::initialize`
	// より前にここで投入しておく。
	mitiru::cef::MitiruCefSchemeHandlerFactory::clearExtraAssetRoots();
	for (const auto& dir : config.cefAdditionalAssetDirs)
	{
		mitiru::cef::MitiruCefSchemeHandlerFactory::addAssetRoot(dir);
	}

	// CEF は常にゲーム論理解像度で動作させる。
	// フルスクリーン時に物理解像度（例 2560x1440）で初期化すると
	// HTML の 1920×1080 固定レイアウトとの乖離が起き黒画面・マウスズレが発生する。
	const int w = config.windowWidth  > 0 ? config.windowWidth  : m_window->width();
	const int h = config.windowHeight > 0 ? config.windowHeight : m_window->height();

	// C-5: リモート URL (http/https) は既定 deny。config で明示 opt-in のみ許可。
	m_cefContext.setAllowRemoteUrls(config.cefAllowRemoteUrls);

	if (!m_cefContext.initialize(*dx12Device, exeDir, logPath, w, h,
	                             startUrl, config.cefRemoteDebuggingPort))
	{
		// H-20: 無言で UI 無し画面を出さない。stderr 1 行 + 失敗フラグ。
		// 詳細原因は MitiruCefContext::initialize が段階別に stderr へ出す。
		// 以降の CEF 呼び出しは isInitialized()==false で全て no-op (安全)。
		std::fprintf(stderr,
			"[mitiru] CEF init failed — HTML UI は無効で続行 (log: %s)\n",
			logPath.c_str());
		OutputDebugStringA("[CEF] initialize() returned false\n");
		m_cefInitFailed = true;
	}
	else if (config.cefRemoteDebuggingPort > 0)
	{
		char msg[128];
		std::snprintf(msg, sizeof(msg),
			"[CEF] Remote debugging listening on http://localhost:%d\n",
			config.cefRemoteDebuggingPort);
		OutputDebugStringA(msg);
	}
#else
	(void)config;
#endif
}
