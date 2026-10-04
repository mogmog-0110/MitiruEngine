// mitiru::Engine の detail ヘッダー。直接 include せず、core/Engine.hpp 経由で include する
#pragma once

/// @file Engine_Module_Fault.hpp
/// @brief game DLL の callback が落ちたときの後始末を行う
/// @details 報告ファイルを書いたうえで、reload 直後の猶予中なら差し替え前の DLL と GameMemory へ戻す。
/// それ以外は host を停止中にし、GameMemory の復元と停止通知を行う。新しい DLL を読み込むと、最後に記録した
/// フレームから再開する。停止を続けるかは host が moduleFaultCount() と moduleFaulted() で決める。

#include <cstring>
#include <string>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/module/ModuleHost.hpp>

namespace mitiru::module::detail
{

// hot reload 中も元ファイルを書き換えられるよう、host は DLL のコピーを読み込む。
// 報告にはコピー名ではなく利用者が知っている元の DLL 名を使う。
inline void nameFaultAfterSource(ModuleFault& fault, const ModuleHost* host)
{
	if (host != nullptr && host->isLoaded()
	    && debug::pathToUtf8(host->runtimePath().filename()) == fault.moduleName)
	{
		debug::setCrashContextText(fault.moduleName, debug::pathToUtf8(host->sourcePath().filename()));
	}
}

}  // namespace mitiru::module::detail

MITIRU_INLINE void mitiru::Engine::handleModuleFault()
{
	++m_moduleFaultCount;

	module::detail::nameFaultAfterSource(m_moduleFault, m_moduleHost.get());

	auto& ctx = debug::crashContext();
	const auto frame = ctx.frame.load(std::memory_order_relaxed);
	const bool rollBack = reloadRollbackArmed();
	m_moduleCrashReport = debug::writeCrashReport(m_moduleFault, ctx,
		rollBack ? "rolled back to the DLL and GameMemory from before the reload"
		         : "stopped; waiting for a new DLL");
	const std::string summary = debug::summarizeFault(m_moduleFault);
	const std::string report  = debug::pathToUtf8(m_moduleCrashReport);
	if (report.empty())
	{
		console::noticef("ゲームが %llu フレーム目で止まりました (%s)。",
		                 static_cast<unsigned long long>(frame), summary.c_str());
	}
	else
	{
		console::noticef("ゲームが %llu フレーム目で止まりました (%s)。詳しい報告は %s にあります。",
		                 static_cast<unsigned long long>(frame), summary.c_str(), report.c_str());
	}

	if (rollBack && rollbackModuleReload()) { return; }
	m_moduleFaulted = true;

	// callback が書きかけた GameMemory を、このフレームの on_update より前に記録した bytes へ戻す。
	bool restored = false;
	if (!modulePartialState() && m_sideState.empty())
	{
		if (const std::uint8_t* last = m_moduleMemoryRing.at(0))
		{
			restored = rewindModuleMemory(last, m_moduleMemorySize);
		}
	}
	else if (!m_sideState.empty())
	{
		// 落ちた DLL の窓口は呼ばない (壊れた heap を触らせない)。GameMemory だけ戻し、窓口は次の DLL が
		// 読まれたときに、同じフレームの記録から戻す。
		std::size_t sideLen = 0;
		const std::uint8_t* last = m_moduleMemoryRing.at(0);
		if (last != nullptr && m_moduleMemory != nullptr && m_sideStateRing.at(0, sideLen) != nullptr)
		{
			std::memcpy(m_moduleMemory, last, m_moduleMemorySize);
			m_sideRestorePending = true;
			restored = true;
		}
	}

	m_moduleFaultLines = {
		summary,
		"frame " + std::to_string(frame) + (restored ? " (GameMemory はその前のフレームへ戻しました)" : ""),
		"報告: " + (report.empty() ? std::string("(書けませんでした)") : report),
	};
}

MITIRU_INLINE void mitiru::Engine::clearModuleFault() noexcept
{
	m_moduleFaulted = false;
	m_moduleFaultLines.clear();
}

// ghost は録画を並べて見せるだけの別 DLL なので、落ちても live の game は止めない。
// 落ちた DLL の解放処理は呼ばず (壊れた heap を触らない)、ghost の表示だけをやめる。
MITIRU_INLINE void mitiru::Engine::abandonGhostModule(const module::ModuleFault& crashed) noexcept
{
	reportModuleCodeFault(crashed, m_ghostHost.get(), "ghost replay dropped; the live game keeps running");
	dropGhostModule();
}

MITIRU_INLINE void mitiru::Engine::dropGhostModule() noexcept
{
	m_ghostMemory     = nullptr;
	m_ghostMemorySize = 0;
	m_ghostApi        = module::ModuleApi{};
	if (m_ghostHost) { m_ghostHost->unload(); }
}

// live の callback 以外 (読み込み・解放の入口、ghost、reload の戻り先) のフォールトは game を止めず、
// 報告だけを残す。何を諦めたかは呼ぶ側が action に書く。
MITIRU_INLINE void mitiru::Engine::reportModuleCodeFault(const module::ModuleFault& crashed,
	const module::ModuleHost* host, const char* action) noexcept
{
	try
	{
		module::ModuleFault fault = crashed;
		module::detail::nameFaultAfterSource(fault, host);
		const auto report = debug::writeCrashReport(fault, debug::crashContext(), action);
		const std::string reportText = debug::pathToUtf8(report);
		if (reportText.empty())
		{
			console::noticef("game の DLL が落ちました (%s)。", debug::summarizeFault(fault).c_str());
		}
		else
		{
			console::noticef("game の DLL が落ちました (%s)。詳しい報告は %s にあります。",
			                 debug::summarizeFault(fault).c_str(), reportText.c_str());
		}
		++m_moduleFaultCount;
		m_moduleFault       = fault;
		m_moduleCrashReport = report;
	}
	catch (...) {}
}
