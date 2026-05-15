#pragma once

/// @file UIEventSystem.hpp
/// @brief DOM風UIイベントシステム
/// @details Capture -> Target -> Bubble の3フェーズでイベントを伝搬する。
///          マウス・キーボード・フォーカスイベントを統一的に処理する。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/ui/UIEvent.hpp>
#include <mitiru/ui/UIFocus.hpp>
#include <mitiru/ui/UIHitTest.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief イベント伝搬フェーズ
enum class UIEventPhase : std::uint8_t
{
	None, Capture, Target, Bubble
};

/// @brief マウスイベントデータ
struct UIMouseEvent
{
	float x = 0.0f;
	float y = 0.0f;
	int button = 0;
	int clickCount = 1;
	UINodeId targetId = INVALID_UI_NODE;
	UIEventPhase phase = UIEventPhase::None;
	bool propagationStopped = false;
	bool defaultPrevented = false;
	double timestamp = 0.0;

	void stopPropagation() noexcept { propagationStopped = true; }
	void preventDefault() noexcept { defaultPrevented = true; }
};

/// @brief キーイベントデータ
struct UIKeyEvent
{
	int key = 0;
	int modifiers = 0; ///< ビットフラグ: 1=Shift, 2=Ctrl, 4=Alt
	UINodeId targetId = INVALID_UI_NODE;
	UIEventPhase phase = UIEventPhase::None;
	bool propagationStopped = false;
	bool defaultPrevented = false;
	double timestamp = 0.0;

	void stopPropagation() noexcept { propagationStopped = true; }
	void preventDefault() noexcept { defaultPrevented = true; }
};

/// @brief フォーカスイベントデータ
struct UIFocusEvent
{
	UINodeId targetId = INVALID_UI_NODE;
	UINodeId relatedTargetId = INVALID_UI_NODE; ///< 前/次のフォーカスノード
	UIEventPhase phase = UIEventPhase::None;
	bool propagationStopped = false;
	double timestamp = 0.0;

	void stopPropagation() noexcept { propagationStopped = true; }
};

using MouseEventHandler = std::function<void(UIMouseEvent&)>;
using KeyEventHandler = std::function<void(UIKeyEvent&)>;
using FocusEventHandler = std::function<void(UIFocusEvent&)>;
using ListenerId = std::uint64_t;

/// @brief マウスイベント種類
enum class MouseEventType : std::uint8_t { Press, Release, Move, Scroll };

/// @brief キーイベント種類
enum class KeyEventType : std::uint8_t { KeyDown, KeyUp };

/// @brief DOM風イベントプロセッサ
/// @details Capture -> Target -> Bubble の3フェーズでUIツリーにイベントを伝搬する。
class UIEventProcessor
{
	struct MouseEntry { ListenerId id{}; UINodeId nodeId{}; MouseEventType type{}; MouseEventHandler handler; bool capture = false; };
	struct KeyEntry   { ListenerId id{}; UINodeId nodeId{}; KeyEventType type{}; KeyEventHandler handler; bool capture = false; };
	struct FocusEntry { ListenerId id{}; UINodeId nodeId{}; bool isFocusIn = true; FocusEventHandler handler; };

	std::vector<MouseEntry> m_mouse;
	std::vector<KeyEntry> m_key;
	std::vector<FocusEntry> m_focus;
	ListenerId m_nextId = 1;

public:
	/// @brief マウスイベントリスナーを登録する
	[[nodiscard]] ListenerId addMouseListener(UINodeId nodeId, MouseEventType type,
		MouseEventHandler handler, bool useCapture = false)
	{
		auto id = m_nextId++;
		m_mouse.push_back({id, nodeId, type, std::move(handler), useCapture});
		return id;
	}

	/// @brief キーイベントリスナーを登録する
	[[nodiscard]] ListenerId addKeyListener(UINodeId nodeId, KeyEventType type,
		KeyEventHandler handler, bool useCapture = false)
	{
		auto id = m_nextId++;
		m_key.push_back({id, nodeId, type, std::move(handler), useCapture});
		return id;
	}

	/// @brief フォーカスイベントリスナーを登録する
	[[nodiscard]] ListenerId addFocusListener(UINodeId nodeId, bool isFocusIn,
		FocusEventHandler handler)
	{
		auto id = m_nextId++;
		m_focus.push_back({id, nodeId, isFocusIn, std::move(handler)});
		return id;
	}

	/// @brief リスナーを削除する
	void removeEventListener(ListenerId lid)
	{
		eraseById(m_mouse, lid);
		eraseById(m_key, lid);
		eraseById(m_focus, lid);
	}

	/// @brief マウスイベントを処理する（ヒットテスト + 3フェーズ伝搬）
	bool processMouseEvent(UINode& root, float x, float y,
		MouseEventType type, int button = 0, double timestamp = 0.0)
	{
		UINode* target = hitTest(root, x, y);
		if (!target) { return false; }

		std::vector<UINode*> path;
		if (!buildPath(root, target->id(), path)) { return false; }

		UIMouseEvent event;
		event.x = x; event.y = y; event.button = button;
		event.targetId = target->id(); event.timestamp = timestamp;

		// Capture: root -> target
		event.phase = UIEventPhase::Capture;
		for (auto* node : path)
		{
			if (event.propagationStopped) { break; }
			fireMouseHandlers(node->id(), type, event, true);
		}

		// Target
		if (!event.propagationStopped)
		{
			event.phase = UIEventPhase::Target;
			fireMouseHandlers(target->id(), type, event, false);
		}

		// Bubble: target -> root
		if (!event.propagationStopped)
		{
			event.phase = UIEventPhase::Bubble;
			for (auto it = path.rbegin(); it != path.rend(); ++it)
			{
				if (event.propagationStopped) { break; }
				if (*it == target) { continue; }
				fireMouseHandlers((*it)->id(), type, event, false);
			}
		}

		return event.propagationStopped || event.defaultPrevented;
	}

	/// @brief キーイベントを処理する（フォーカスチェーン上で3フェーズ伝搬）
	bool processKeyEvent(UINode& focusedNode, int key,
		KeyEventType type, int modifiers = 0, double timestamp = 0.0)
	{
		std::vector<UINode*> chain;
		for (UINode* n = &focusedNode; n; n = n->parent()) { chain.push_back(n); }

		UIKeyEvent event;
		event.key = key; event.modifiers = modifiers;
		event.targetId = focusedNode.id(); event.timestamp = timestamp;

		// Capture: root -> target
		event.phase = UIEventPhase::Capture;
		for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		{
			if (event.propagationStopped) { break; }
			fireKeyHandlers((*it)->id(), type, event, true);
		}

		// Target
		if (!event.propagationStopped)
		{
			event.phase = UIEventPhase::Target;
			fireKeyHandlers(focusedNode.id(), type, event, false);
		}

		// Bubble: target -> root
		if (!event.propagationStopped)
		{
			event.phase = UIEventPhase::Bubble;
			for (auto* node : chain)
			{
				if (event.propagationStopped) { break; }
				if (node == &focusedNode) { continue; }
				fireKeyHandlers(node->id(), type, event, false);
			}
		}

		return event.propagationStopped || event.defaultPrevented;
	}

	/// @brief フォーカス変更を処理する（blur + focus イベント発火）
	void processFocusChange(UINode* oldNode, UINode* newNode, double timestamp = 0.0)
	{
		if (oldNode)
		{
			UIFocusEvent blur;
			blur.targetId = oldNode->id();
			blur.relatedTargetId = newNode ? newNode->id() : INVALID_UI_NODE;
			blur.phase = UIEventPhase::Target;
			blur.timestamp = timestamp;
			for (const auto& e : m_focus)
			{
				if (e.nodeId == oldNode->id() && !e.isFocusIn)
				{
					e.handler(blur);
					if (blur.propagationStopped) { break; }
				}
			}
		}
		if (newNode)
		{
			UIFocusEvent focus;
			focus.targetId = newNode->id();
			focus.relatedTargetId = oldNode ? oldNode->id() : INVALID_UI_NODE;
			focus.phase = UIEventPhase::Target;
			focus.timestamp = timestamp;
			for (const auto& e : m_focus)
			{
				if (e.nodeId == newNode->id() && e.isFocusIn)
				{
					e.handler(focus);
					if (focus.propagationStopped) { break; }
				}
			}
		}
	}

	/// @brief 登録済みリスナー総数
	[[nodiscard]] std::size_t listenerCount() const noexcept
	{
		return m_mouse.size() + m_key.size() + m_focus.size();
	}

private:
	/// @brief root -> target のパスを構築する
	static bool buildPath(UINode& cur, UINodeId targetId, std::vector<UINode*>& path)
	{
		path.push_back(&cur);
		if (cur.id() == targetId) { return true; }
		for (auto& child : cur.children())
		{
			if (buildPath(*child, targetId, path)) { return true; }
		}
		path.pop_back();
		return false;
	}

	void fireMouseHandlers(UINodeId nid, MouseEventType t, UIMouseEvent& e, bool cap) const
	{
		for (const auto& entry : m_mouse)
		{
			if (entry.nodeId == nid && entry.type == t && entry.capture == cap)
			{
				entry.handler(e);
				if (e.propagationStopped) { return; }
			}
		}
	}

	void fireKeyHandlers(UINodeId nid, KeyEventType t, UIKeyEvent& e, bool cap) const
	{
		for (const auto& entry : m_key)
		{
			if (entry.nodeId == nid && entry.type == t && entry.capture == cap)
			{
				entry.handler(e);
				if (e.propagationStopped) { return; }
			}
		}
	}

	template <typename Vec>
	static void eraseById(Vec& v, ListenerId lid)
	{
		v.erase(std::remove_if(v.begin(), v.end(),
			[lid](const auto& e) { return e.id == lid; }), v.end());
	}
};

} // namespace mitiru::ui
