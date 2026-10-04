// mitiru::Engine の detail ヘッダー。直接 include せず、core/Engine.hpp 経由で include する
#pragma once

/// @file Engine_Module_SideState.hpp
/// @brief GameMemory の外に持つ状態 (窓口、ADR 0054) を host が記録し、GameMemory と一緒に戻す部分と、
///        画面なしで 1 フレーム進める stepModuleFrame。
/// @details 戻す順は GameMemory → on_rebuild → 窓口。窓口の restore は GameMemory を読んで作り直す物が
/// あってよく、on_rebuild が組み立てた場面へ上書きする形になる。窓口を戻せない操作は、GameMemory だけ
/// 戻して半分だけ過去にすることはせず、断って理由を知らせる。

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/ModuleHost.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/module/SideStateHost.hpp>
#include <mitiru/observe/BugRing.hpp>

MITIRU_INLINE void mitiru::Engine::bindModuleSideState()
{
	std::array<module::SideStateChannel, module::kMaxSideStateChannels> table{};
	std::int32_t declared = 0;
	if (m_moduleHost && m_moduleHost->isLoaded())
	{
		if (auto fn = m_moduleHost->sideStatesFn())
		{
			(void)guardModuleCallback("mitiru_module_side_states", [&] {
				declared = fn(table.data(), static_cast<std::int32_t>(table.size()));
			});
		}
	}
	else
	{
		// 静的リンク (web / 1 exe) では game と engine が同じ binary なので、表を直接読む。
		declared = module::detail::copySideStates(table.data(), static_cast<std::int32_t>(table.size()));
	}
	const std::int32_t n = (declared < module::kMaxSideStateChannels) ? declared : module::kMaxSideStateChannels;
	const std::string dropped = m_sideState.bind(table.data(), n);
	if (!dropped.empty())
	{
		debug::warnOnce("sidestate.bind." + dropped,
			"MITIRU_SIDE_STATE の " + dropped + " を使いません。名前が空か重複しているか、saveState / restoreState "
			"がありません。名前を重ならないようにし、saveState と restoreState を持たせてください。");
	}
	m_sideRestorePending = false;
}

MITIRU_INLINE bool mitiru::Engine::captureModuleSideState(std::vector<std::uint8_t>& out, bool withBytes)
{
	out.clear();
	if (m_sideState.empty() || m_moduleMemory == nullptr) { return true; }
	std::string error;
	bool ok = false;
	(void)guardModuleCallback("side state save", [&] { ok = m_sideState.capture(m_moduleMemory, withBytes, out, &error); });
	if (!ok)
	{
		debug::warnOnce("sidestate.capture",
			"MITIRU_SIDE_STATE で預けた状態を保存できないので、巻き戻しとセーブにこの状態が入りません ("
				+ error + ")。");
		out.clear();
	}
	return ok;
}

MITIRU_INLINE bool mitiru::Engine::restoreModuleSideImage(const std::uint8_t* image, std::size_t n,
	const char* operation) noexcept
{
	if (m_sideState.empty()) { return true; }
	std::string error;
	bool ok = false;
	try
	{
		(void)guardModuleCallback("side state restore", [&] { ok = m_sideState.restore(m_moduleMemory, image, n, &error); });
		if (!ok)
		{
			debug::warnOnce(std::string("sidestate.restore.") + operation,
				std::string(operation) + "で MITIRU_SIDE_STATE の状態を戻せません (" + error + ")。記録と今の DLL で"
				"名前か version が違うか、restoreState が失敗を返しました。形を変えたときは version を上げてください。");
		}
	}
	catch (...) { ok = false; }
	return ok;
}

MITIRU_INLINE bool mitiru::Engine::rewindModuleFramesAgo(std::size_t k) noexcept
{
	if (modulePartialState())
	{
		debug::warnOnce("rewind.partial-state",
			"このゲームは MITIRU_GAME_OBJECTS を使い、GameMemory に進行データだけを置いているので、"
			"巻き戻しは使えません。");
		return false;
	}
	const std::uint8_t* past = m_moduleMemoryRing.at(k);
	if (past == nullptr || m_moduleMemory == nullptr) { return false; }
	if (m_sideState.empty()) { return rewindModuleMemory(past, m_moduleMemorySize); }

	std::size_t sideLen = 0;
	const std::uint8_t* side = m_sideStateRing.at(k, sideLen);
	if (side == nullptr)
	{
		debug::warnOnce("rewind.side-missing",
			"そのフレームの MITIRU_SIDE_STATE の状態が記録に残っていないので、巻き戻せません。"
			"MITIRU_REWIND_BUDGET か --rewind-mb で記録の量を増やすか、戻るフレーム数を減らしてください。");
		return false;
	}
	std::memcpy(m_moduleMemory, past, m_moduleMemorySize);
	if (m_moduleApi.on_rebuild != nullptr)
	{
		(void)guardModuleCallback("on_rebuild", [&] { m_moduleApi.on_rebuild(m_moduleMemory, module::kModuleRebuildRestore); });
	}
	observe::restartBugRing(this);
	return restoreModuleSideImage(side, sideLen, "巻き戻し");
}

MITIRU_INLINE bool mitiru::Engine::carryModuleSideStateAcrossReload(const std::vector<std::uint8_t>& image)
{
	if (m_sideState.empty() && image.empty()) { return true; }
	std::string why;
	if (image.empty()) { why = "差し替える前の DLL に MITIRU_SIDE_STATE がない"; }
	else if (m_sideState.empty()) { why = "差し替えた DLL に MITIRU_SIDE_STATE がない"; }
	else
	{
		observe::SideImageView view;
		why = observe::parseSideImage(image.data(), image.size(), view) ? m_sideState.mismatch(view)
		                                                                  : std::string("引き継ぐ記録の形が壊れている");
	}
	if (why.empty() && restoreModuleSideImage(image.data(), image.size(), "ホットリロード")) { return true; }

	// GameMemory だけ温存して窓口が初期状態だと、2 つが食い違ったまま進む。両方を初期状態からやり直す。
	console::noticef("ホットリロードで MITIRU_SIDE_STATE の状態を引き継げないので、ゲームを初めからやり直します (%s)。",
		why.empty() ? "restoreState が失敗を返した" : why.c_str());
	if (m_moduleMemory != nullptr && m_moduleMemorySize > 0 && m_moduleApi.on_init != nullptr)
	{
		std::memset(m_moduleMemory, 0, m_moduleMemorySize);
		(void)guardModuleCallback("on_init", [&] { m_moduleApi.on_init(m_moduleMemory); });
	}
	m_moduleMemoryRing.clear();
	m_sideStateRing.clear();
	observe::restartBugRing(this);
	m_resimQueue.clear(); m_resimCursor = 0; m_resimSnapSize = 0;
	return false;
}

MITIRU_INLINE bool mitiru::Engine::runModuleFrameBody()
{
	zeroModuleFrameIntents();
	const auto* snap = m_moduleInputSnapshot.get();
	const ModuleFrameDrive drive = (m_config.moduleFrameDriver && snap != nullptr)
		? m_config.moduleFrameDriver(*snap, *m_moduleFrameIntents) : ModuleFrameDrive::Local;
	if (drive == ModuleFrameDrive::Faulted) { return false; }
	if (drive == ModuleFrameDrive::Idle) { return true; }
	if (drive == ModuleFrameDrive::Local && m_moduleApi.on_update != nullptr && snap != nullptr)
	{
		if (!callModuleUpdate(snap, m_moduleFrameIntents.get())) { return false; }
	}
	// restart (§8-4) は ring 記録より前に適用する。ring のフレーム N = 次フレームの memory_in が成立する。
	applyModuleRestartIntent();
	recordModuleMemoryFrame();
	recordModuleInputFrame();
	drainModuleFrameIntents();
	recordModuleBugRingFrame();
	return true;
}

MITIRU_INLINE void mitiru::Engine::recordModuleBugRingFrame()
{
	if (m_config.bugRingSeconds <= 0.0f || !m_moduleInputSnapshot) { return; }
	const auto frames = static_cast<std::uint32_t>(m_config.bugRingSeconds / kFixedDt) + 1;
	observe::pushBugRingFrame(this, m_moduleMemory, m_moduleMemorySize, m_moduleInputSnapshot.get(),
		static_cast<std::uint32_t>(sizeof(module::InputSnapshot)), frames,
		[this](std::vector<std::uint8_t>& state) {
			// drain がロードで状態を差し替えることがあるので、毎フレームの記録の image は使わずに取り直す。
			if (m_sideState.empty()) { return true; }
			if (!captureModuleSideState(m_bugRingSideScratch, true)) { return false; }
			state.insert(state.end(), m_bugRingSideScratch.begin(), m_bugRingSideScratch.end());
			return true;
		});
}

MITIRU_INLINE bool mitiru::Engine::stepModuleFrame(const module::InputSnapshot& input)
{
	if (m_moduleMemory == nullptr || m_moduleApi.on_update == nullptr) { return false; }
	ensureModuleBindings();
	if (m_moduleFaulted) { return false; }
	if (applyScrubHold())
	{
		publishToolSnapshot();
		return false;
	}
	if (!m_moduleInputSnapshot) { m_moduleInputSnapshot = std::make_unique<module::InputSnapshot>(); }
	if (!m_moduleFrameIntents)  { m_moduleFrameIntents  = std::make_unique<module::FrameIntents>(); }
	std::memcpy(m_moduleInputSnapshot.get(), &input, sizeof(input));
	applyResimInputOverride();
	return runModuleFrameBody();
}
