#pragma once

/// @file HostCrashInbox.hpp
/// @brief 出荷したゲームの起動時に、前の実行が残したクラッシュ報告を利用者へ知らせ、許された時だけ送る。
/// @details 人が遊んでいる起動 (headless・台本・リプレイ・キャプチャでない) でだけ働く。確認画面は engine の
///          assets/ui/crash_report.rml を RmlUi で重ね、開いている間はゲームを止める。答えは settings.json の
///          privacy.crashReports に残すので、聞くのは 1 回だけ。送り先は ship.json の crashReportUrl で、
///          無ければ置き場を知らせるだけで何も送らない。

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/CrashInbox.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/CrashUpload.hpp>
#include <mitiru/settings/UserSettings.hpp>

#ifdef _WIN32
#include <shellapi.h>
#endif

namespace mitiru::host
{

class HostCrashInbox
{
public:
	static constexpr std::string_view kPrompt = "mitiru:crash_report.rml";

	HostCrashInbox() = default;
	HostCrashInbox(const HostCrashInbox&) = delete;
	HostCrashInbox& operator=(const HostCrashInbox&) = delete;
	~HostCrashInbox()
	{
		if (m_upload.joinable()) { m_upload.join(); }
	}

	/// @brief Engine を作る前に呼ぶ。ゲームが UI を持たなければ、確認画面をその UI の文書にする。
	void configure(std::string url, settings::CrashReportConsent consent, bool interactive, EngineConfig& cfg)
	{
		m_url = std::move(url);
		if (!interactive) { return; }
		m_dir = debug::crashDirectory();
		m_pending = debug::pendingCrashes(m_dir);
		m_step = debug::decideCrashStep(m_pending.size(), !m_url.empty(), consent);
		if (m_step == debug::CrashInboxStep::Upload) { startUpload(); }
		if (needsPrompt() && cfg.uiDocument.empty()) { cfg.uiDocument = std::string(kPrompt); }
	}

	/// @brief 毎フレーム呼ぶ。最初のフレームで確認画面を開く。
	void onFrame(Engine& engine)
	{
		if (m_shown || !needsPrompt()) { return; }
		m_shown = true;
		auto& ui = engine.uiHost();
		if (!ui.active())
		{
			std::fprintf(stderr, "[mitiru_host] 前の実行のクラッシュ報告は %s にある (UI が無いので画面には出さない)\n",
			             debug::pathToUtf8(m_dir).c_str());
			markAll("kept");
			return;
		}
		ui.setInt("view.crash_count", static_cast<int>(m_pending.size()));
		ui.setText("view.crash_dir", debug::pathToUtf8(m_dir.lexically_normal().make_preferred()));
		ui.setBool("view.crash_can_send", m_step == debug::CrashInboxStep::AskConsent);
		ui.openOverlay(kPrompt);
		if (!engine.isPaused())
		{
			engine.setPauseKind(EngineConfig::kPauseKindIngame);
			engine.setPaused(true);
			m_pausedByPrompt = true;
		}
	}

	/// @brief 確認画面の操作 ("crash.*") を受ける。利用者が答えたら consent を書き換える。
	bool onUiAction(std::string_view name, Engine* engine, settings::CrashReportConsent& consent)
	{
		if (name.rfind("crash.", 0) != 0) { return false; }
		if (name == "crash.open_folder") { openFolder(); return true; }
		if (name == "crash.send")
		{
			consent = settings::CrashReportConsent::Send;
			startUpload();
		}
		else if (name == "crash.keep")
		{
			consent = settings::CrashReportConsent::Never;
			markAll("kept");
		}
		else if (name == "crash.close") { markAll("kept"); }
		else { return false; }
		close(engine);
		return true;
	}

private:
	[[nodiscard]] bool needsPrompt() const noexcept
	{
		return m_step == debug::CrashInboxStep::AskConsent || m_step == debug::CrashInboxStep::ShowKept;
	}

	void markAll(std::string_view outcome)
	{
		for (const auto& c : m_pending) { (void)debug::markCrashSeen(c, outcome); }
	}

	// 送れなかった報告は印を付けずに残し、次の起動でまた送る。
	void startUpload()
	{
		if (m_upload.joinable() || m_url.empty()) { return; }
		m_upload = std::thread([url = m_url, pending = m_pending] {
			for (const auto& c : pending)
			{
				std::string error;
				if (debug::uploadCrash(url, c, error)) { (void)debug::markCrashSeen(c, "sent"); }
				else { std::fprintf(stderr, "[mitiru_host] クラッシュ報告を送れなかった (次の起動で送り直す): %s\n", error.c_str()); }
			}
		});
	}

	void close(Engine* engine)
	{
		m_step = debug::CrashInboxStep::Nothing;
		if (engine == nullptr) { return; }
		engine->uiHost().closeOverlay(kPrompt);
		// 開いている間に別の理由 (F8 など) で止め直されていれば、その停止を残す
		if (m_pausedByPrompt && engine->isPaused() && engine->config().pauseKind == EngineConfig::kPauseKindIngame)
		{
			engine->setPaused(false);
		}
		m_pausedByPrompt = false;
	}

	void openFolder() const
	{
#ifdef _WIN32
		::ShellExecuteW(nullptr, L"open", m_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
	}

	std::string m_url;
	std::filesystem::path m_dir;
	std::vector<debug::PendingCrash> m_pending;
	debug::CrashInboxStep m_step = debug::CrashInboxStep::Nothing;
	bool m_shown = false;
	bool m_pausedByPrompt = false;
	std::thread m_upload;
};

}  // namespace mitiru::host
