#pragma once

/// @file CrashReporter.hpp
/// @brief host 自身のクラッシュを sentry-native で報告する opt-in の宣言。
/// @details `-DMITIRU_WITH_CRASH_REPORT=ON` のときだけ働く。game の callback 内のフォールトは ModuleFaultGuard が先に拾う。既定では minidump と報告を crashDirectory() に残し、MITIRU_CRASH_DSN が設定されたときだけ送る。

#include <filesystem>
#include <string>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/module/ModuleFaultGuard.hpp>

#if defined(MITIRU_HAS_SENTRY)
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sentry.h>
#endif

namespace mitiru::debug
{

class CrashReporter
{
public:
	CrashReporter() = default;
	~CrashReporter() { stop(); }
	CrashReporter(const CrashReporter&) = delete;
	CrashReporter& operator=(const CrashReporter&) = delete;

	/// @brief クラッシュの捕捉を始める。opt-in が無効なら false を返す。
	bool start();

	void stop() noexcept;

	[[nodiscard]] bool active() const noexcept { return m_active; }

	/// @brief DSN が設定され、報告を送る状態か。
	[[nodiscard]] bool uploads() const noexcept { return m_uploads; }

	/// @brief ローカルに書き出した game のフォールトを、送信が有効なときだけ送る。
	void captureGameFault(const module::ModuleFault& fault, const std::filesystem::path& report);

private:
	bool m_active  = false;
	bool m_uploads = false;
};

#if defined(MITIRU_HAS_SENTRY)

namespace detail
{

/// host が落ちたとき、breakpad の処理スレッドから呼ばれる。minidump と報告を残し、送信が無効ならイベントを破棄する。
inline sentry_value_t onHostCrash(const sentry_ucontext_t* uctx, sentry_value_t event,
                                  sentry_hint_t* hint, void* uploads)
{
	module::ModuleFault fault{};
#if MITIRU_HAS_SEH_GUARD
	if (uctx != nullptr)
	{
		EXCEPTION_POINTERS ep = uctx->exception_ptrs;
		module::detail::captureFault(&ep, fault);
	}
#else
	(void)uctx;
#endif
	const auto report = writeCrashReport(fault, crashContext());
	if (uploads == nullptr)
	{
		sentry_value_decref(event);
		return sentry_value_new_null();
	}
	if (!report.empty()) { sentry_hint_add_attachment(hint, sentry_attachment_from_filew(report.c_str())); }
	return event;
}

}  // namespace detail

inline bool CrashReporter::start()
{
	if (m_active) { return true; }
	const std::filesystem::path dir = crashDirectory();
	const std::string dsn = env::value("MITIRU_CRASH_DSN");
	m_uploads = !dsn.empty();

	sentry_options_t* options = sentry_options_new();
	sentry_options_set_database_pathw(options, (dir / "sentry-db").c_str());
	sentry_options_set_release(options, "mitiru-engine@" MITIRU_ENGINE_VERSION);
	sentry_options_set_auto_session_tracking(options, 0);  // 起動のたびの通信をしない
	// 詳細ログで host の出力を埋めないよう、SENTRY_DEBUG=1 のときだけ有効にする。
	sentry_options_set_debug(options, env::value("SENTRY_DEBUG").starts_with('1'));
	if (m_uploads) { sentry_options_set_dsn(options, dsn.c_str()); }
	else           { sentry_options_set_transport(options, nullptr); }
	sentry_options_set_on_crash(options, &detail::onHostCrash, m_uploads ? this : nullptr);

	module::setFaultDumpDirectory(dir);
	m_active = sentry_init(options) == 0;
	if (!m_active) { console::notice("クラッシュの報告 (sentry) を始めるのに失敗しました。"); }
	return m_active;
}

inline void CrashReporter::stop() noexcept
{
	if (!m_active) { return; }
	sentry_close();
	m_active = false;
}

inline void CrashReporter::captureGameFault(const module::ModuleFault& fault, const std::filesystem::path& report)
{
	if (!m_active || !m_uploads || fault.dumpPath[0] == '\0') { return; }
	std::ifstream in(report, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	sentry_set_extra("mitiru_report", sentry_value_new_string(text.str().c_str()));
	sentry_capture_minidumpw(pathFromUtf8(fault.dumpPath).c_str());
}

#else

inline bool CrashReporter::start() { return false; }
inline void CrashReporter::stop() noexcept {}
inline void CrashReporter::captureGameFault(const module::ModuleFault&, const std::filesystem::path&) {}

#endif

}  // namespace mitiru::debug
