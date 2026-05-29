#pragma once

/// @file EventQueue.hpp
/// @brief 小さなカットシーン event 列再生 state machine。
/// @details `Event{kind, dur, args}` を push して `update(dt, onEvent)` を毎フレーム呼ぶだけ。
///          各 event の中身 (「キャラを A→B 移動」「弾ませる」「向きを変える」等) は game の
///          `onEvent` callback が `kind` で分岐して実行する (engine は entity を持たない)。
///          engine は順序 / dur 経過 / 次へ進む を担う。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::script
{

/// @brief 汎用 event。`kind` は game 側の enum 値 (engine は中身を解釈しない)。
struct Event
{
	int    kind = 0;
	float  dur  = 0.0f;     ///< 秒。0 = 1 フレームで完了 (パルス的イベント)。
	float  a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;  ///< 汎用 float 引数
	int    i = 0;           ///< 汎用 int 引数 (entity id 等)
	std::string s;          ///< 汎用 string 引数 (asset id 等、空可)
};

class EventQueue
{
public:
	void push(Event ev)       { m_events.push_back(std::move(ev)); }
	void clear() noexcept     { m_events.clear(); m_idx = -1; m_elapsed = 0.0f; }
	[[nodiscard]] bool active() const noexcept       { return m_idx >= 0 && m_idx < static_cast<int>(m_events.size()); }
	[[nodiscard]] int  size()   const noexcept       { return static_cast<int>(m_events.size()); }
	[[nodiscard]] int  currentIndex() const noexcept { return m_idx; }
	[[nodiscard]] float elapsedInCurrent() const noexcept { return m_elapsed; }
	[[nodiscard]] const Event* current() const noexcept
	{
		return active() ? &m_events[static_cast<std::size_t>(m_idx)] : nullptr;
	}

	/// @brief 1 フレーム進める。
	/// @details 未開始なら先頭を current にする。current の dur が elapsed を超えるまで
	///          `onEvent(ev, elapsed)` を毎フレーム呼ぶ。dur 経過で current を進める。
	///          dur=0 の event は 1 回 onEvent を呼んで即次へ。fn の signature: `void(const Event&, float elapsed)`。
	template <class Fn>
	void update(float dt, Fn&& onEvent)
	{
		if (m_events.empty()) { return; }
		if (m_idx < 0) { m_idx = 0; m_elapsed = 0.0f; }

		while (m_idx < static_cast<int>(m_events.size()))
		{
			const auto& ev = m_events[static_cast<std::size_t>(m_idx)];
			onEvent(ev, m_elapsed);
			if (ev.dur <= 0.0f)
			{
				++m_idx; m_elapsed = 0.0f;
				continue;
			}
			m_elapsed += dt;
			if (m_elapsed >= ev.dur)
			{
				++m_idx; m_elapsed = 0.0f;
				continue;
			}
			break;  // 現 event がまだ続く
		}
	}

private:
	std::vector<Event> m_events;
	int                m_idx = -1;        ///< -1 = 未開始 / >=size = 全完了
	float              m_elapsed = 0.0f;  ///< current event 内 elapsed
};

}  // namespace mitiru::script
