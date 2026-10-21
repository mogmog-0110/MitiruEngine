#pragma once

/// @file HostDebugMenu.hpp
/// @brief 汎用デバッグメニュー（ADR 0085）を Engine と RmlUi につなぐ宣言。
/// @details debug::DebugMenuController がメニューの判断を持ち、ここは Engine の関数と画面
///          （assets/ui/debug_menu.rml、debug_info.rml）の呼び出しを受け持つ。
/// メニューは F1 またはスティックの 2 つ押しで開く。
/// 開いている間はゲームへ入力を渡さず、進行を止める。
/// ゲームが UI を持たない起動では、debug_info.rml をゲーム UI の文書として RmlUi を動かす。

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/DebugMenuController.hpp>
#include <mitiru/debug/HostHotkeys.hpp>
#include <mitiru/debug/InspectorLauncher.hpp>
#include <mitiru/debug/ToolSpawnLedger.hpp>

#ifdef _WIN32
#include <shellapi.h>
#endif

namespace mitiru::host
{

class HostDebugMenu final : public debug::IDebugMenuHost
{
public:
	static constexpr std::string_view kMenuDocument = "mitiru:debug_menu.rml";
	static constexpr std::string_view kInfoDocument = "mitiru:debug_info.rml";

	HostDebugMenu() : m_clock0(std::chrono::steady_clock::now()) {}
	HostDebugMenu(const HostDebugMenu&) = delete;
	HostDebugMenu& operator=(const HostDebugMenu&) = delete;

	/// @brief Engine を作る前に呼ぶ。起動の引数と ship.json は devKeys() に入れておく。切ってあるか inert（録画の再生など）なら何も追加しない。
	void configure(bool inert, EngineConfig& cfg)
	{
		m_controller.setEnabled(m_devKeys.menuEnabled());
		m_controller.setInert(inert);
		if (!m_devKeys.menuEnabled() || inert) { return; }
		if (cfg.uiDocument.empty())
		{
			cfg.uiDocument = std::string(kInfoDocument);
			m_infoIsMainDocument = true;
		}
		const auto previousGate = cfg.moduleInputGate;
		cfg.moduleInputGate = [this, previousGate](module::InputSnapshot& snap) {
			if (previousGate) { previousGate(snap); }
			m_controller.gate(snap, nowSec());
		};
		const auto previousFilter = cfg.uiActionFilter;
		cfg.uiActionFilter = [this, previousFilter](std::string_view name, std::string_view payload) {
			return m_controller.onUiAction(name, payload) || (previousFilter && previousFilter(name, payload));
		};
	}

	void attach(Engine& engine) noexcept { m_engine = &engine; }

	/// @brief 画面を保存する関数を host の main.cpp から渡す。F12 と同じ処理を使う。
	void setScreenshot(std::function<void(Engine&)> fn) { m_screenshot = std::move(fn); }

	/// @brief onFrameStart から毎フレーム呼ぶ。
	void onFrame(Engine& engine)
	{
		m_engine = &engine;
		const auto pull = [&engine](std::vector<module::DebugMenuItem>& items, std::uint32_t& flags) {
			return engine.pullModuleDebugMenu(items, flags);
		};
		m_controller.onFrame(*this, pull, engine.frameNumber(), nowSec());
		m_devKeys.setGameFlags(m_controller.gameFlags());
		m_controller.setEnabled(m_devKeys.menuEnabled());
		noteDevKeysOff();
		m_tools.endFrame();
	}

	[[nodiscard]] debug::DebugMenuController& controller() noexcept { return m_controller; }

	/// @brief 開発用のキー (F7〜F12) とメニューを切る方針。起動の引数と ship.json はここへ入れ、ゲームの申告は onFrame が足す
	[[nodiscard]] debug::DevKeyPolicy& devKeys() noexcept { return m_devKeys; }

	bool paused() const override { return m_engine != nullptr && m_engine->isPaused(); }

	void setPausedByMenu(bool pause) override
	{
		if (m_engine == nullptr) { return; }
		if (pause)
		{
			m_engine->setPauseKind(EngineConfig::kPauseKindDebug);
			m_engine->setPaused(true);
		}
		else if (m_engine->isPaused() && m_engine->config().pauseKind == EngineConfig::kPauseKindDebug)
		{
			m_engine->setPaused(false);
		}
	}

	void stepOneFrame() override { if (m_engine != nullptr) { m_engine->stepOneFrame(); } }
	float timeScale() const override { return m_engine != nullptr ? m_engine->timeScale() : 1.0f; }
	void setTimeScale(float scale) override { if (m_engine != nullptr) { m_engine->setTimeScale(scale); } }
	bool lofi() const override { return m_engine != nullptr && m_engine->isLofiEnabled(); }
	void toggleLofi() override { if (m_engine != nullptr) { m_engine->toggleLofi(); } }

	void saveScreenshot() override
	{
		if (m_engine != nullptr && m_screenshot) { m_screenshot(*m_engine); }
	}

	void saveBugRecord() override
	{
		if (m_engine != nullptr) { m_engine->mutableConfig().bugRingSaveRequested = true; }
	}

	void openLogFolder() override { openFolder(debug::crashDirectory().parent_path()); }

	void openSaveFolder() override
	{
		if (m_engine == nullptr || m_engine->config().saveDir.empty())
		{
			console::notice("セーブの置き場が決まっていないので、フォルダを開けません");
			return;
		}
		openFolder(std::filesystem::path(m_engine->config().saveDir));
	}

	bool toolAvailable() const override
	{
		if (m_engine == nullptr || m_engine->window() == nullptr) { return false; }
		// コントローラーが毎フレーム問うので、exe の探索 (ファイルの存在確認と文字列) は 1 秒に 1 回にする
		const auto now = std::chrono::steady_clock::now();
		if (!m_toolProbed || now - m_toolProbedAt >= std::chrono::seconds(1))
		{
			m_toolExeFound = !debug::findToolExe("tool").empty();
			m_toolProbedAt = now;
			m_toolProbed = true;
		}
		return m_toolExeFound;
	}

	void openTool(int builtinToolId) override
	{
		const char* page = toolPage(builtinToolId);
		if (page == nullptr) { return; }
		const std::string args = std::string("--page ") + page;
		if (!m_tools.shouldSpawn(args)) { return; }
		if (auto process = debug::spawnToolProcess("tool", 0, args)) { m_tools.record(args, std::move(*process)); }
		else { console::notice("ツール窓 mitiru_tool.exe を開けませんでした"); }
	}

	bool pushGameAction(const std::string& name, const std::string& payloadJson) override
	{
		return m_engine != nullptr && m_engine->pushModuleActionEvent(name, payloadJson);
	}

	void showView(const debug::MenuView& view) override
	{
		if (m_engine == nullptr) { return; }
		auto& ui = m_engine->uiHost();
		if (!ui.active()) { return; }
		ui.setBool("view.mdbg_open", view.open);
		ui.setText("view.mdbg_rows", view.rowsJson);
		ui.setText("view.mdbg_filter", view.filter);
		ui.setBool("view.mdbg_nomatch", view.noMatch);
		ui.setBool("view.mdbg_up", view.canScrollUp);
		ui.setBool("view.mdbg_down", view.canScrollDown);
		ui.setText("view.mdbg_info", view.info);
		syncDocuments(ui, view);
	}

private:
	// 切ってあることを 1 度だけ知らせる (出荷版でキーが効かない理由を探す人のため)
	void noteDevKeysOff()
	{
		if (m_noted || !m_devKeys.off()) { return; }
		m_noted = true;
		console::verbosef("開発用のキー (F1、F7〜F12) は %s の指定で切ってあります。", m_devKeys.reason().c_str());
	}

	[[nodiscard]] double nowSec() const
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_clock0).count();
	}

	// 開いている間だけ操作を受け取る文書を重ね、FPS の 1 行は操作を受け取らない文書に表示する。
	void syncDocuments(ui_rml::RmlUiHost& ui, const debug::MenuView& view)
	{
		if (view.open != m_menuShown)
		{
			m_menuShown = view.open;
			if (view.open) { ui.openOverlay(kMenuDocument); } else { ui.closeOverlay(kMenuDocument); }
		}
		if (m_infoIsMainDocument) { return; }
		const bool wantInfo = !view.info.empty();
		if (wantInfo != m_infoShown)
		{
			m_infoShown = wantInfo;
			if (wantInfo) { ui.openOverlay(kInfoDocument, false); } else { ui.closeOverlay(kInfoDocument); }
		}
	}

	static const char* toolPage(int id) noexcept
	{
		switch (id)
		{
		case debug::kBuiltinToolInspector: return "inspect";
		case debug::kBuiltinToolPerformance: return "perf";
		case debug::kBuiltinToolRewind: return "rewind";
		case debug::kBuiltinToolInput: return "input";
		case debug::kBuiltinToolMixer: return "mixer";
		case debug::kBuiltinToolSceneTree: return "scene";
		default: return nullptr;
		}
	}

	static void openFolder(const std::filesystem::path& dir)
	{
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
#ifdef _WIN32
		ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
		(void)dir;
#endif
	}

	debug::DebugMenuController m_controller;
	debug::DevKeyPolicy m_devKeys;
	debug::ToolSpawnLedger<debug::ToolProcess> m_tools;
	std::function<void(Engine&)> m_screenshot;
	std::chrono::steady_clock::time_point m_clock0;
	Engine* m_engine = nullptr;
	mutable bool m_toolProbed = false;
	mutable bool m_toolExeFound = false;
	mutable std::chrono::steady_clock::time_point m_toolProbedAt{};
	bool m_infoIsMainDocument = false;
	bool m_menuShown = false;
	bool m_infoShown = false;
	bool m_noted = false;
};

}  // namespace mitiru::host
