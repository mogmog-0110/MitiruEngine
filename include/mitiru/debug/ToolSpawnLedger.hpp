#pragma once

/// @file ToolSpawnLedger.hpp
/// @brief 開いたツール窓を覚え、閉じられたあとに選び直されたら開き直せるようにする帳面。
/// @details 窓の持ち主の process が動いている間は同じ要求を無視する (毎フレーム hud.open を呼んでも 1 枚)。
///          閉じられていても、前のフレームも同じ要求が続いているなら開き直さない。毎フレーム頼むゲームでは、
///          人が × で閉じた窓がすぐ戻ってしまうため。メニューなどで 1 度だけ頼めば、閉じたあとでも開き直せる。
///          Process は running() を持つこと (本物は debug::ToolProcess、試験では偽物)。

#include <map>
#include <set>
#include <string>
#include <utility>

namespace mitiru::debug
{

template <class Process>
class ToolSpawnLedger
{
public:
	/// @return key の窓を今開いてよいか。true なら、開けたあとで record、開けなかったら何もしない
	[[nodiscard]] bool shouldSpawn(const std::string& key)
	{
		m_now.insert(key);
		const auto it = m_open.find(key);
		if (it == m_open.end()) { return true; }
		if (it->second.running()) { return false; }
		if (m_before.count(key) != 0) { return false; }
		m_open.erase(it);
		return true;
	}

	void record(const std::string& key, Process process) { m_open.insert_or_assign(key, std::move(process)); }

	/// @return key が pred を満たす窓が、まだ 1 つでも動いているか
	template <class KeyPred>
	[[nodiscard]] bool anyRunning(KeyPred pred) const
	{
		for (const auto& [key, process] : m_open)
		{
			if (pred(key) && process.running()) { return true; }
		}
		return false;
	}

	/// 毎フレームの終わりに 1 度。このフレームの要求が、次のフレームの「前のフレームの要求」になる
	void endFrame()
	{
		m_before = std::move(m_now);
		m_now.clear();
	}

private:
	std::map<std::string, Process> m_open;
	std::set<std::string> m_now;
	std::set<std::string> m_before;
};

} // namespace mitiru::debug
