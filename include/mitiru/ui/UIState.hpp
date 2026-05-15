#pragma once

/// @file UIState.hpp
/// @brief UIビジュアルステートとインタラクション状態の管理
/// @details 各UIノードの視覚的状態（ホバー・プレス・フォーカス等）を追跡し、
///          入力イベントからの状態遷移を処理する。

#include <string>
#include <unordered_map>

#include <mitiru/input/InputState.hpp>
#include <mitiru/ui/UIEvent.hpp>
#include <mitiru/ui/UIFocus.hpp>
#include <mitiru/ui/UIHitTest.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief UIノードの視覚的状態
enum class UIVisualState : std::uint8_t
{
	Normal,   ///< 通常状態
	Hovered,  ///< マウスホバー中
	Pressed,  ///< 押下中
	Focused,  ///< フォーカス中
	Disabled, ///< 無効状態
	Active    ///< アクティブ状態（トグルON等）
};

/// @brief ドラッグ状態
struct UIDragState
{
	bool dragging = false;  ///< ドラッグ中フラグ
	float startX = 0.0f;   ///< ドラッグ開始X座標
	float startY = 0.0f;   ///< ドラッグ開始Y座標
	float currentX = 0.0f; ///< 現在のX座標
	float currentY = 0.0f; ///< 現在のY座標
};

/// @brief UIノードごとのインタラクション状態
struct UIInteractiveState
{
	UIVisualState visualState = UIVisualState::Normal; ///< 現在の視覚的状態
	bool hovered = false;   ///< ホバー中
	bool pressed = false;   ///< プレス中
	bool focused = false;   ///< フォーカス中
	bool disabled = false;  ///< 無効化
	UIDragState dragState;  ///< ドラッグ状態
	float value = 0.0f;     ///< 値（スライダー等）
	bool checked = false;   ///< チェック状態（トグル等）
	std::string text;       ///< テキスト（テキスト入力等）
};

/// @brief UIステートマネージャ
/// @details 入力状態からUIノードのインタラクション状態を更新し、
///          適切なイベントを発行する。ホバー・クリック・フォーカス・ドラッグの
///          完全な入力処理パイプラインを提供する。
class UIStateManager
{
	std::unordered_map<UINodeId, UIInteractiveState> m_states;
	UINodeId m_hoveredNodeId = INVALID_UI_NODE;
	UINodeId m_pressedNodeId = INVALID_UI_NODE;
	UIFocusManager m_focusManager;

	/// @brief ドラッグ判定の移動閾値（ピクセル）
	static constexpr float DRAG_THRESHOLD = 5.0f;

	float m_pressStartX = 0.0f;
	float m_pressStartY = 0.0f;

public:
	/// @brief 入力状態を処理してUI状態を更新する
	/// @details ヒットテスト→ホバー更新→クリック/プレス/リリース→フォーカス変更→
	///          ドラッグ処理→ビジュアルステート更新の順でパイプラインを実行する。
	/// @param root UIツリーのルートノード
	/// @param input 入力状態スナップショット
	/// @param dispatcher イベントディスパッチャ
	void processInput(UINode& root, const InputState& input, UIEventDispatcher& dispatcher)
	{
		const auto [mouseX, mouseY] = input.mousePosition();

		// 1. ヒットテスト: マウス位置のノードを検出
		UINode* hitNode = hitTest(root, mouseX, mouseY);
		const UINodeId hitNodeId = hitNode ? hitNode->id() : INVALID_UI_NODE;

		// 2. ホバー状態の更新
		processHover(root, hitNodeId, mouseX, mouseY, dispatcher);

		// 3. マウスボタン処理（プレス・リリース・クリック）
		processMouseButton(root, input, hitNodeId, mouseX, mouseY, dispatcher);

		// 4. ドラッグ処理
		processDrag(root, input, mouseX, mouseY, dispatcher);

		// 5. ビジュアルステートの更新
		updateVisualStates();
	}

	/// @brief 指定ノードのインタラクション状態を取得する
	/// @param nodeId ノードID
	/// @return インタラクション状態（未登録ノードはデフォルト状態を返す）
	[[nodiscard]] UIInteractiveState getState(UINodeId nodeId) const
	{
		const auto it = m_states.find(nodeId);
		if (it != m_states.end())
		{
			return it->second;
		}
		return {};
	}

	/// @brief 指定ノードのインタラクション状態を設定する
	/// @param nodeId ノードID
	/// @param state 設定する状態
	void setState(UINodeId nodeId, const UIInteractiveState& state)
	{
		m_states.insert_or_assign(nodeId, state);
	}

	/// @brief 指定ノードの無効化状態を設定する
	/// @param nodeId ノードID
	/// @param disabled 無効化するならtrue
	void setDisabled(UINodeId nodeId, bool disabled)
	{
		m_states[nodeId].disabled = disabled;
	}

	/// @brief フォーカスマネージャを取得する
	/// @return フォーカスマネージャへの参照
	[[nodiscard]] UIFocusManager& focusManager() noexcept
	{
		return m_focusManager;
	}

	/// @brief フォーカスマネージャを取得する（const版）
	[[nodiscard]] const UIFocusManager& focusManager() const noexcept
	{
		return m_focusManager;
	}

private:
	/// @brief ホバー状態を処理する
	void processHover(UINode& root, UINodeId hitNodeId, float mouseX, float mouseY,
		UIEventDispatcher& dispatcher)
	{
		if (hitNodeId != m_hoveredNodeId)
		{
			// 前のホバーノードにLeaveイベント
			if (m_hoveredNodeId != INVALID_UI_NODE)
			{
				auto& prevState = m_states[m_hoveredNodeId];
				prevState.hovered = false;

				UIEvent leaveEvent;
				leaveEvent.type = UIEventType::HoverLeave;
				leaveEvent.targetId = m_hoveredNodeId;
				leaveEvent.mouseX = mouseX;
				leaveEvent.mouseY = mouseY;
				dispatcher.dispatch(root, leaveEvent);
			}

			// 新しいホバーノードにEnterイベント
			if (hitNodeId != INVALID_UI_NODE)
			{
				auto& newState = m_states[hitNodeId];
				if (!newState.disabled)
				{
					newState.hovered = true;

					UIEvent enterEvent;
					enterEvent.type = UIEventType::HoverEnter;
					enterEvent.targetId = hitNodeId;
					enterEvent.mouseX = mouseX;
					enterEvent.mouseY = mouseY;
					dispatcher.dispatch(root, enterEvent);
				}
			}

			m_hoveredNodeId = hitNodeId;
		}

		// ホバー中イベント（毎フレーム）
		if (m_hoveredNodeId != INVALID_UI_NODE && !m_states[m_hoveredNodeId].disabled)
		{
			UIEvent hoverEvent;
			hoverEvent.type = UIEventType::Hover;
			hoverEvent.targetId = m_hoveredNodeId;
			hoverEvent.mouseX = mouseX;
			hoverEvent.mouseY = mouseY;
			dispatcher.dispatch(root, hoverEvent);
		}
	}

	/// @brief マウスボタンの押下・解放・クリックを処理する
	void processMouseButton(UINode& root, const InputState& input, UINodeId hitNodeId,
		float mouseX, float mouseY, UIEventDispatcher& dispatcher)
	{
		// 左ボタンの押下
		if (input.isMouseButtonJustPressed(MouseButton::Left))
		{
			m_pressStartX = mouseX;
			m_pressStartY = mouseY;

			if (hitNodeId != INVALID_UI_NODE && !m_states[hitNodeId].disabled)
			{
				m_pressedNodeId = hitNodeId;
				m_states[hitNodeId].pressed = true;

				UIEvent pressEvent;
				pressEvent.type = UIEventType::Press;
				pressEvent.targetId = hitNodeId;
				pressEvent.mouseX = mouseX;
				pressEvent.mouseY = mouseY;
				pressEvent.button = 0;
				dispatcher.dispatch(root, pressEvent);

				// フォーカス変更
				processFocusChange(root, hitNodeId, dispatcher);
			}
			else
			{
				// 空白クリックでフォーカスクリア
				auto* prevFocused = m_focusManager.getFocusedNode();
				if (prevFocused != nullptr)
				{
					m_states[prevFocused->id()].focused = false;

					UIEvent blurEvent;
					blurEvent.type = UIEventType::Blur;
					blurEvent.targetId = prevFocused->id();
					dispatcher.dispatch(root, blurEvent);

					m_focusManager.clearFocus();
				}
			}
		}

		// 左ボタンの解放
		if (input.isMouseButtonJustReleased(MouseButton::Left))
		{
			if (m_pressedNodeId != INVALID_UI_NODE)
			{
				m_states[m_pressedNodeId].pressed = false;

				UIEvent releaseEvent;
				releaseEvent.type = UIEventType::Release;
				releaseEvent.targetId = m_pressedNodeId;
				releaseEvent.mouseX = mouseX;
				releaseEvent.mouseY = mouseY;
				releaseEvent.button = 0;
				dispatcher.dispatch(root, releaseEvent);

				// 同一ノード上で解放 → クリック
				if (hitNodeId == m_pressedNodeId)
				{
					UIEvent clickEvent;
					clickEvent.type = UIEventType::Click;
					clickEvent.targetId = m_pressedNodeId;
					clickEvent.mouseX = mouseX;
					clickEvent.mouseY = mouseY;
					clickEvent.button = 0;
					dispatcher.dispatch(root, clickEvent);
				}

				m_pressedNodeId = INVALID_UI_NODE;
			}
		}
	}

	/// @brief フォーカス変更を処理する
	void processFocusChange(UINode& root, UINodeId newFocusId, UIEventDispatcher& dispatcher)
	{
		auto* prevFocused = m_focusManager.getFocusedNode();
		const UINodeId prevFocusId = prevFocused ? prevFocused->id() : INVALID_UI_NODE;

		if (newFocusId == prevFocusId)
		{
			return;
		}

		// 前のフォーカスノードにBlurイベント
		if (prevFocused != nullptr)
		{
			m_states[prevFocusId].focused = false;

			UIEvent blurEvent;
			blurEvent.type = UIEventType::Blur;
			blurEvent.targetId = prevFocusId;
			dispatcher.dispatch(root, blurEvent);
		}

		// 新しいノードを検索してフォーカス設定
		UINode* newNode = root.findById(newFocusId);
		if (newNode != nullptr && newNode->focusable())
		{
			m_focusManager.setFocusedNode(newNode);
			m_states[newFocusId].focused = true;

			UIEvent focusEvent;
			focusEvent.type = UIEventType::Focus;
			focusEvent.targetId = newFocusId;
			dispatcher.dispatch(root, focusEvent);
		}
		else
		{
			m_focusManager.clearFocus();
		}
	}

	/// @brief ドラッグ処理
	void processDrag(UINode& root, const InputState& input, float mouseX, float mouseY,
		UIEventDispatcher& dispatcher)
	{
		if (!input.isMouseButtonDown(MouseButton::Left) || m_pressedNodeId == INVALID_UI_NODE)
		{
			// ドラッグ終了チェック
			for (auto& [nodeId, state] : m_states)
			{
				if (state.dragState.dragging)
				{
					state.dragState.dragging = false;

					UIEvent dragEndEvent;
					dragEndEvent.type = UIEventType::DragEnd;
					dragEndEvent.targetId = nodeId;
					dragEndEvent.mouseX = mouseX;
					dragEndEvent.mouseY = mouseY;
					dispatcher.dispatch(root, dragEndEvent);
				}
			}
			return;
		}

		auto& state = m_states[m_pressedNodeId];

		if (!state.dragState.dragging)
		{
			// ドラッグ開始判定（閾値超過）
			const float dx = mouseX - m_pressStartX;
			const float dy = mouseY - m_pressStartY;
			if (dx * dx + dy * dy >= DRAG_THRESHOLD * DRAG_THRESHOLD)
			{
				state.dragState.dragging = true;
				state.dragState.startX = m_pressStartX;
				state.dragState.startY = m_pressStartY;
				state.dragState.currentX = mouseX;
				state.dragState.currentY = mouseY;

				UIEvent dragStartEvent;
				dragStartEvent.type = UIEventType::DragStart;
				dragStartEvent.targetId = m_pressedNodeId;
				dragStartEvent.mouseX = mouseX;
				dragStartEvent.mouseY = mouseY;
				dispatcher.dispatch(root, dragStartEvent);
			}
		}
		else
		{
			// ドラッグ中
			const float prevX = state.dragState.currentX;
			const float prevY = state.dragState.currentY;
			state.dragState.currentX = mouseX;
			state.dragState.currentY = mouseY;

			UIEvent dragEvent;
			dragEvent.type = UIEventType::Drag;
			dragEvent.targetId = m_pressedNodeId;
			dragEvent.mouseX = mouseX;
			dragEvent.mouseY = mouseY;
			dragEvent.deltaX = mouseX - prevX;
			dragEvent.deltaY = mouseY - prevY;
			dispatcher.dispatch(root, dragEvent);
		}
	}

	/// @brief 全ノードのビジュアルステートを更新する
	void updateVisualStates()
	{
		for (auto& [nodeId, state] : m_states)
		{
			if (state.disabled)
			{
				state.visualState = UIVisualState::Disabled;
			}
			else if (state.pressed)
			{
				state.visualState = UIVisualState::Pressed;
			}
			else if (state.hovered)
			{
				state.visualState = UIVisualState::Hovered;
			}
			else if (state.focused)
			{
				state.visualState = UIVisualState::Focused;
			}
			else
			{
				state.visualState = UIVisualState::Normal;
			}
		}
	}
};

} // namespace mitiru::ui
