#pragma once

// Detail header for mitiru::vn::ScenarioScript — included via vn/ScenarioScript.hpp
// ScenarioExecutor::applySetToFlagManager の out-of-line 定義。
//
// 循環参照回避: FlagManager.hpp を遅延 include して定義するため、
// このファイルは umbrella の **最後** に include する必要がある。

#include <cstdlib>
#include <string>

#include "ScenarioScript_Executor.hpp"

// ════════════════════════════════════════════════════════════════════
//  ScenarioExecutor::applySetToFlagManager (out-of-line defn)
//  FlagManager の完全型が必要なので、include を遅延させてここで定義する。
// ════════════════════════════════════════════════════════════════════

#include <mitiru/vn/FlagManager.hpp>

namespace mitiru::vn
{

inline void ScenarioExecutor::applySetToFlagManager(const std::string& variable,
	const std::string& value)
{
	if (!m_flagManager) return;

	// 型推論: true/false → bool, 整数 → int, 小数 → float, それ以外 → string
	if (value == "true")
	{
		m_flagManager->set(variable, true);
		return;
	}
	if (value == "false")
	{
		m_flagManager->set(variable, false);
		return;
	}

	// 整数試行
	if (!value.empty())
	{
		char* endp = nullptr;
		const long long asInt = std::strtoll(value.c_str(), &endp, 10);
		if (endp != value.c_str() && *endp == '\0')
		{
			m_flagManager->set(variable, static_cast<int>(asInt));
			return;
		}

		// 小数試行
		endp = nullptr;
		const double asDouble = std::strtod(value.c_str(), &endp);
		if (endp != value.c_str() && *endp == '\0')
		{
			m_flagManager->set(variable, static_cast<float>(asDouble));
			return;
		}
	}

	// それ以外は文字列
	m_flagManager->set(variable, value);
}

} // namespace mitiru::vn
