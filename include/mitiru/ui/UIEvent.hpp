#pragma once

/// @file UIEvent.hpp
/// @brief UIイベントシステム
/// @details UIノードに対するインタラクションイベントの定義とディスパッチ機構を提供する。
///          キャプチャフェーズ（root→target）とバブルフェーズ（target→root）の2段階伝搬を行う。

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief UIイベントの種類
enum class UIEventType : std::uint8_t
{
	Click,        ///< クリック（press + release 同一ノード上）
	DoubleClick,  ///< ダブルクリック
	Press,        ///< マウスボタン押下
	Release,      ///< マウスボタン解放
	Hover,        ///< マウスホバー中（毎フレーム）
	HoverEnter,   ///< マウスがノード領域に入った
	HoverLeave,   ///< マウスがノード領域から出た
	Focus,        ///< フォーカス獲得
	Blur,         ///< フォーカス喪失
	ValueChanged, ///< 値が変更された（スライダー等）
	TextChanged,  ///< テキストが変更された（テキスト入力等）
	DragStart,    ///< ドラッグ開始
	Drag,         ///< ドラッグ中
	DragEnd,      ///< ドラッグ終了
	Scroll,       ///< スクロール
	KeyDown,      ///< キー押下
	KeyUp,        ///< キー解放
	TextInput     ///< テキスト入力（文字）
};

/// @brief UIイベントデータ
/// @details イベントの種類・対象・座標・入力情報を保持する。
///          consumedフラグをtrueにすると以降の伝搬が停止される。
struct UIEvent
{
	UIEventType type{};          ///< イベント種類
	UINodeId targetId = INVALID_UI_NODE; ///< 対象ノードID
	float mouseX = 0.0f;        ///< マウスX座標
	float mouseY = 0.0f;        ///< マウスY座標
	int button = 0;              ///< マウスボタン番号（0=左, 1=右, 2=中）
	int keyCode = 0;             ///< キーコード
	std::string text;            ///< テキスト入力内容
	float deltaX = 0.0f;        ///< X方向差分（スクロール・ドラッグ用）
	float deltaY = 0.0f;        ///< Y方向差分（スクロール・ドラッグ用）
	double timestamp = 0.0;     ///< イベント発生時刻
	bool consumed = false;       ///< 消費フラグ（trueで伝搬停止）
};

/// @brief イベントハンドラ型
using UIEventHandler = std::function<void(UIEvent&)>;

/// @brief イベントハンドラの識別子
using UIEventHandlerId = std::uint64_t;

/// @brief UIイベントディスパッチャ
/// @details ノードIDとイベント種類の組み合わせにハンドラを登録し、
///          キャプチャフェーズ（root→target）とバブルフェーズ（target→root）で
///          イベントを伝搬する。
class UIEventDispatcher
{
	struct HandlerEntry
	{
		UIEventHandlerId handlerId{};
		UINodeId nodeId = INVALID_UI_NODE;
		UIEventType eventType{};
		UIEventHandler handler;
		bool capture = false; ///< キャプチャフェーズで呼ばれるか
	};

	std::vector<HandlerEntry> m_handlers;
	UIEventHandlerId m_nextHandlerId = 1;

public:
	/// @brief イベントリスナーを登録する（バブルフェーズ）
	/// @param nodeId 対象ノードID
	/// @param eventType イベント種類
	/// @param handler ハンドラ関数
	/// @return ハンドラID（removeに使用）
	[[nodiscard]] UIEventHandlerId addEventListener(
		UINodeId nodeId,
		UIEventType eventType,
		UIEventHandler handler) noexcept
	{
		return addEventListenerImpl(nodeId, eventType, std::move(handler), false);
	}

	/// @brief イベントリスナーを登録する（キャプチャフェーズ）
	/// @param nodeId 対象ノードID
	/// @param eventType イベント種類
	/// @param handler ハンドラ関数
	/// @return ハンドラID
	[[nodiscard]] UIEventHandlerId addCaptureListener(
		UINodeId nodeId,
		UIEventType eventType,
		UIEventHandler handler) noexcept
	{
		return addEventListenerImpl(nodeId, eventType, std::move(handler), true);
	}

	/// @brief イベントリスナーを削除する
	/// @param handlerId 削除するハンドラのID
	void removeEventListener(UIEventHandlerId handlerId) noexcept
	{
		m_handlers.erase(
			std::remove_if(m_handlers.begin(), m_handlers.end(),
				[handlerId](const HandlerEntry& entry)
				{
					return entry.handlerId == handlerId;
				}),
			m_handlers.end());
	}

	/// @brief イベントをディスパッチする
	/// @details targetIdのノードからルートまでの経路を構築し、
	///          キャプチャフェーズ（root→target）→バブルフェーズ（target→root）の
	///          順でハンドラを呼び出す。consumedフラグで伝搬停止。
	/// @param root UIツリーのルートノード
	/// @param event ディスパッチするイベント
	void dispatch(UINode& root, UIEvent& event) const
	{
		// target→rootの経路を構築
		std::vector<UINode*> path;
		buildPathToTarget(root, event.targetId, path);

		if (path.empty())
		{
			return;
		}

		// キャプチャフェーズ: root→target
		for (auto it = path.rbegin(); it != path.rend(); ++it)
		{
			if (event.consumed)
			{
				return;
			}
			invokeHandlers((*it)->id(), event.type, event, true);
		}

		// バブルフェーズ: target→root
		for (auto* node : path)
		{
			if (event.consumed)
			{
				return;
			}
			invokeHandlers(node->id(), event.type, event, false);
		}
	}

	/// @brief 登録済みハンドラ数を取得する
	[[nodiscard]] std::size_t handlerCount() const noexcept
	{
		return m_handlers.size();
	}

private:
	[[nodiscard]] UIEventHandlerId addEventListenerImpl(
		UINodeId nodeId,
		UIEventType eventType,
		UIEventHandler handler,
		bool capture) noexcept
	{
		const auto id = m_nextHandlerId++;
		m_handlers.push_back(HandlerEntry{id, nodeId, eventType, std::move(handler), capture});
		return id;
	}

	/// @brief ターゲットノードまでの経路を構築する（target先頭、root末尾）
	static bool buildPathToTarget(UINode& current, UINodeId targetId, std::vector<UINode*>& path)
	{
		if (current.id() == targetId)
		{
			path.push_back(&current);
			return true;
		}
		for (auto& child : current.children())
		{
			if (buildPathToTarget(*child, targetId, path))
			{
				path.push_back(&current);
				return true;
			}
		}
		return false;
	}

	/// @brief 指定ノード・イベント種類・フェーズに該当するハンドラを呼び出す
	void invokeHandlers(UINodeId nodeId, UIEventType eventType, UIEvent& event, bool capturePhase) const
	{
		for (const auto& entry : m_handlers)
		{
			if (entry.nodeId == nodeId && entry.eventType == eventType && entry.capture == capturePhase)
			{
				entry.handler(event);
				if (event.consumed)
				{
					return;
				}
			}
		}
	}
};

} // namespace mitiru::ui
