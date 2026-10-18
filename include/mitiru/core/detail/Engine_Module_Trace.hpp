// mitiru::Engine の detail 宣言。直接 include せず、core/Engine.hpp から include する
#pragma once

/// @file Engine_Module_Trace.hpp
/// @brief 記録した入力を再実行し、指定した欄の値を毎フレーム集める宣言 (POST /api/ai/trace)。
/// @details motion_tune が、同じ入力で値を変えたときの動きの違いを線で示すために使う。
/// runDeterminismResim と同じ手順で、k フレーム前の GameMemory と窓口を戻し、InputRing の入力を on_update に通す。
/// 復元直後に欄を上書きでき、毎フレームの値を返す。
/// 終了後は live の GameMemory と窓口を元に戻し、プレイ中の状態に変更を残さない。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/observe/Reflect.hpp>

namespace mitiru::detail
{

inline constexpr int kMaxTraceFrames = 600;     ///< 1 回に流す上限 (10 秒 @60fps)
inline constexpr std::size_t kMaxTraceProbes = 32;

struct TraceProbe
{
	std::string name;
	std::uint32_t offset = 0;
	std::uint32_t size = 0;
	std::string tag;
};

[[nodiscard]] inline std::string traceError(const std::string& why)
{
	return nlohmann::json{ { "error", why } }.dump();
}

/// @brief probes の名前を GameMemory の位置に対応付ける。対応付けられない名前があれば why に理由を書いて false を返す
[[nodiscard]] inline bool locateTraceProbes(const nlohmann::json& names, const module::FieldDescriptor* fields,
                                            std::int32_t fieldCount, const module::ReflectSchema* schemas,
                                            std::int32_t schemaCount, std::uint32_t memorySize,
                                            std::vector<TraceProbe>& out, std::string& why)
{
	if (!names.is_array() || names.empty() || names.size() > kMaxTraceProbes)
	{
		why = "probe は 1 から " + std::to_string(kMaxTraceProbes) + " 個の欄の名前の配列で渡してください";
		return false;
	}
	for (const nlohmann::json& n : names)
	{
		TraceProbe p;
		p.name = n.is_string() ? n.get<std::string>() : std::string();
		const char* tag = nullptr;
		if (!observe::reflectFieldLocate(fields, fieldCount, schemas, schemaCount, memorySize, p.name,
		                                 p.offset, p.size, tag) || tag == nullptr)
		{
			why = "probe の欄 '" + p.name + "' が GameMemory の反射に見つかりません";
			return false;
		}
		p.tag = tag;
		out.push_back(std::move(p));
	}
	return true;
}

[[nodiscard]] inline nlohmann::json readTraceRow(const std::uint8_t* memory, const std::vector<TraceProbe>& probes)
{
	nlohmann::json row = nlohmann::json::array();
	for (const TraceProbe& p : probes) { row.push_back(observe::detail::readScalar(memory + p.offset, p.tag.c_str())); }
	return row;
}

} // namespace mitiru::detail

MITIRU_INLINE std::string mitiru::Engine::traceFromRing(const std::string& bodyJson)
{
	using detail::traceError;
	if (m_moduleMemory == nullptr || m_moduleMemorySize == 0 || m_moduleApi.on_update == nullptr
	    || m_moduleReflection.fieldCount() <= 0)
	{
		return traceError("reflection not wired - game に MITIRU_REFLECT がありますか?");
	}
	if (modulePartialState())
	{
		return traceError("このゲームは GameMemory に進行データだけを置いているので、流し直しは使えません");
	}
	nlohmann::json req = nlohmann::json::parse(bodyJson, nullptr, false);
	if (!req.is_object()) { return traceError("body は JSON のオブジェクトにしてください"); }

	const int framesAgo = req.value("framesAgo", 0);
	const std::size_t memFrames = m_sideState.empty()
		? m_moduleMemoryRing.size() : (std::min)(m_moduleMemoryRing.size(), m_sideStateRing.size());
	const std::size_t inFrames = m_moduleInputRing.size();
	if (framesAgo < 1 || static_cast<std::size_t>(framesAgo) >= memFrames || static_cast<std::size_t>(framesAgo) > inFrames)
	{
		return traceError("framesAgo は 1 から " + std::to_string(memFrames > 0 ? memFrames - 1 : 0)
		                  + " の間にしてください (記録が貯まっていないか、巻き戻しの記録が切ってあります)");
	}
	const int frames = (std::min)({ req.value("frames", framesAgo), framesAgo, detail::kMaxTraceFrames });
	if (frames < 1) { return traceError("frames は 1 以上にしてください"); }

	std::vector<detail::TraceProbe> probes;
	std::string why;
	if (!detail::locateTraceProbes(req.value("probe", nlohmann::json::array()), m_moduleReflection.fieldsData(),
	                               m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(),
	                               m_moduleReflection.schemaCount(), m_moduleMemorySize, probes, why))
	{
		return traceError(why);
	}

	constexpr std::uint32_t kSnapSize = sizeof(module::InputSnapshot);
	std::vector<module::InputSnapshot> inputs(static_cast<std::size_t>(frames));
	for (int i = 0; i < frames; ++i)
	{
		const std::uint8_t* snap = m_moduleInputRing.at(static_cast<std::size_t>(framesAgo - 1 - i));
		if (snap == nullptr) { return traceError("入力の記録が途切れています"); }
		std::memcpy(&inputs[static_cast<std::size_t>(i)], snap, kSnapSize);
	}
	const std::uint8_t* past = m_moduleMemoryRing.at(static_cast<std::size_t>(framesAgo));
	std::size_t pastSideLen = 0;
	const std::uint8_t* pastSide = m_sideState.empty() ? nullptr : m_sideStateRing.at(static_cast<std::size_t>(framesAgo), pastSideLen);
	if (past == nullptr || (!m_sideState.empty() && pastSide == nullptr)) { return traceError("過去の状態を取り出せません"); }

	std::vector<std::uint8_t> savedLive(m_moduleMemory == nullptr ? 0 : m_moduleMemorySize);
	std::memcpy(savedLive.data(), m_moduleMemory, m_moduleMemorySize);
	if (!captureModuleSideState(m_sideLiveScratch, true) || !liveSideStateRoundTripSafe("軌跡の流し直し", true))
	{
		return traceError("窓口の状態を写せないので、流し直しは使えません");
	}
	const auto restoreLive = [&] {
		std::memcpy(m_moduleMemory, savedLive.data(), m_moduleMemorySize);
		(void)restoreModuleSideImage(m_sideLiveScratch.data(), m_sideLiveScratch.size(), "軌跡の流し直しの後始末");
	};
	if (!m_sideState.empty() && !restoreModuleSideImage(pastSide, pastSideLen, "軌跡の流し直し"))
	{
		restoreLive();
		return traceError("過去の窓口の状態を戻せません");
	}
	std::memcpy(m_moduleMemory, past, m_moduleMemorySize);

	nlohmann::json applied = nlohmann::json::array();
	nlohmann::json skipped = nlohmann::json::array();
	if (const nlohmann::json over = req.value("overrides", nlohmann::json::object()); over.is_object())
	{
		auto* bytes = static_cast<std::uint8_t*>(m_moduleMemory);
		for (auto it = over.begin(); it != over.end(); ++it)
		{
			std::string err;
			if (observe::reflectWriteField(bytes, m_moduleMemorySize, m_moduleReflection.fieldsData(),
			        m_moduleReflection.fieldCount(), m_moduleReflection.schemasData(), m_moduleReflection.schemaCount(),
			        it.key(), it.value(), err))
			{
				applied.push_back(it.key());
			}
			else { skipped.push_back({ { "field", it.key() }, { "why", err } }); }
		}
	}

	nlohmann::json rows = nlohmann::json::array();
	rows.push_back(detail::readTraceRow(static_cast<const std::uint8_t*>(m_moduleMemory), probes));
	auto intents = std::make_unique<module::FrameIntents>();
	for (int i = 0; i < frames; ++i)
	{
		std::memset(intents.get(), 0, sizeof(module::FrameIntents));
		const bool ok = guardModuleCode("on_update (trace)", m_moduleHost.get(),
			"trace discarded; the live game keeps running", [&] {
			m_moduleApi.on_update(m_moduleMemory, inputs[static_cast<std::size_t>(i)].effectiveDt,
			                      &inputs[static_cast<std::size_t>(i)], intents.get());
		});
		if (!ok)
		{
			restoreLive();
			return traceError("流し直しの途中でゲームが落ちたので捨てました");
		}
		rows.push_back(detail::readTraceRow(static_cast<const std::uint8_t*>(m_moduleMemory), probes));
	}
	restoreLive();

	nlohmann::json names = nlohmann::json::array();
	for (const detail::TraceProbe& p : probes) { names.push_back(p.name); }
	nlohmann::json out;
	out["framesAgo"] = framesAgo;
	out["frames"] = frames;
	out["dt"] = Engine::kFixedDt;
	out["probe"] = std::move(names);
	out["rows"] = std::move(rows);
	out["applied"] = std::move(applied);
	out["skipped"] = std::move(skipped);
	return out.dump();
}
