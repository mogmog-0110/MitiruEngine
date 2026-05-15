#pragma once

/// @file UITransition.hpp
/// @brief UI状態間のアニメーション遷移システム
/// @details 名前付きUIステートを定義し、ステート間の遷移時にノードプロパティを
///          アニメーション補間する。スタガー遷移（ノード順次アニメーション）対応。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>
#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::ui
{

// ── ノードプロパティスナップショット ─────────────────────────────

/// @brief ノードの遷移対象プロパティ値
struct UINodeProperties
{
	float posX    = 0.0f;  ///< X座標
	float posY    = 0.0f;  ///< Y座標
	float sizeW   = 0.0f;  ///< 幅
	float sizeH   = 0.0f;  ///< 高さ
	float opacity = 1.0f;  ///< 不透明度
	float scaleX  = 1.0f;  ///< X方向スケール
	float scaleY  = 1.0f;  ///< Y方向スケール
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f}; ///< 表示色
	bool visible  = true;  ///< 可視フラグ

	/// @brief 2つのプロパティセット間をlerp補間する
	/// @param a 開始値
	/// @param b 終了値
	/// @param t 補間係数（0.0〜1.0）
	/// @return 補間結果
	[[nodiscard]] static UINodeProperties lerp(
		const UINodeProperties& a, const UINodeProperties& b, float t) noexcept
	{
		UINodeProperties result;
		result.posX    = a.posX + (b.posX - a.posX) * t;
		result.posY    = a.posY + (b.posY - a.posY) * t;
		result.sizeW   = a.sizeW + (b.sizeW - a.sizeW) * t;
		result.sizeH   = a.sizeH + (b.sizeH - a.sizeH) * t;
		result.opacity = a.opacity + (b.opacity - a.opacity) * t;
		result.scaleX  = a.scaleX + (b.scaleX - a.scaleX) * t;
		result.scaleY  = a.scaleY + (b.scaleY - a.scaleY) * t;
		result.color.r = a.color.r + (b.color.r - a.color.r) * t;
		result.color.g = a.color.g + (b.color.g - a.color.g) * t;
		result.color.b = a.color.b + (b.color.b - a.color.b) * t;
		result.color.a = a.color.a + (b.color.a - a.color.a) * t;
		result.visible = (t < 0.5f) ? a.visible : b.visible;
		return result;
	}
};

// ── UIステート定義 ──────────────────────────────────────────────

/// @brief 名前付きUIステート（ノードIDごとのプロパティ集合）
struct UIState
{
	std::string name;  ///< ステート名
	std::unordered_map<UINodeId, UINodeProperties> nodeProperties; ///< ノードごとのプロパティ
};

// ── 遷移定義 ────────────────────────────────────────────────────

/// @brief UIステート遷移の定義
struct UITransitionDef
{
	std::string fromState;                                   ///< 遷移元ステート名
	std::string toState;                                     ///< 遷移先ステート名
	float duration = 0.3f;                                   ///< 遷移時間（秒）
	vn::EasingType easing = vn::EasingType::EaseOutCubic;   ///< イージング関数
	float perNodeDelay = 0.0f;                               ///< ノード間のスタガー遅延（秒）
};

// ── アクティブ遷移の内部状態 ────────────────────────────────────

/// @brief ノード単位の遷移進行状態
struct UINodeTransitionState
{
	UINodeId nodeId = 0;          ///< 対象ノードID
	UINodeProperties from;        ///< 開始プロパティ
	UINodeProperties to;          ///< 終了プロパティ
	float delay = 0.0f;           ///< このノードの開始遅延
	float elapsed = 0.0f;         ///< 経過時間
	bool finished = false;        ///< 完了フラグ
};

/// @brief アクティブな遷移の実行状態
struct UIActiveTransition
{
	UITransitionDef def;                        ///< 遷移定義
	std::vector<UINodeTransitionState> nodes;   ///< ノードごとの遷移状態
	float totalElapsed = 0.0f;                  ///< 総経過時間
	bool finished = false;                      ///< 全ノード完了フラグ
};

// ── UIStateManager ──────────────────────────────────────────────

/// @brief UIステート遷移管理クラス
/// @details ステートを定義し、ステート間のアニメーション遷移を管理する。
///          update()でフレーム更新、transitionTo()で遷移開始。
///
/// @code
/// mitiru::ui::UIStateManager stateMgr;
///
/// // ステート定義
/// mitiru::ui::UINodeProperties menuPos;
/// menuPos.posX = 100.0f; menuPos.posY = 100.0f;
/// menuPos.opacity = 1.0f;
/// stateMgr.defineState("menu", {{nodeId, menuPos}});
///
/// mitiru::ui::UINodeProperties gamePos;
/// gamePos.posX = -200.0f; gamePos.posY = 100.0f;
/// gamePos.opacity = 0.0f;
/// stateMgr.defineState("game", {{nodeId, gamePos}});
///
/// // 遷移開始
/// stateMgr.transitionTo("game", 0.5f, mitiru::vn::EasingType::EaseOutCubic);
///
/// // ゲームループ内
/// stateMgr.update(dt);
/// @endcode
class UIStateManager
{
	std::unordered_map<std::string, UIState> m_states;
	std::string m_currentState;
	std::vector<UIActiveTransition> m_activeTransitions;
	std::unordered_map<UINodeId, UINode*> m_nodeRegistry;

public:
	/// @brief ステートを定義する
	/// @param name ステート名
	/// @param nodeProperties ノードIDごとのプロパティマップ
	void defineState(const std::string& name,
	                 std::unordered_map<UINodeId, UINodeProperties> nodeProperties)
	{
		UIState state;
		state.name = name;
		state.nodeProperties = std::move(nodeProperties);
		m_states.insert_or_assign(name, std::move(state));

		// 最初のステートをカレントとする
		if (m_currentState.empty())
		{
			m_currentState = name;
		}
	}

	/// @brief ノードを登録する（プロパティ適用先）
	/// @param node 登録するノード
	void registerNode(UINode& node)
	{
		m_nodeRegistry.insert_or_assign(node.id(), &node);
	}

	/// @brief 複数ノードを一括登録する
	/// @param nodes ノードリスト
	void registerNodes(const std::vector<UINode*>& nodes)
	{
		for (auto* node : nodes)
		{
			if (node)
			{
				m_nodeRegistry.insert_or_assign(node->id(), node);
			}
		}
	}

	/// @brief ステート遷移を開始する
	/// @param stateName 遷移先ステート名
	/// @param duration 遷移時間（秒）
	/// @param easing イージング関数
	/// @param perNodeDelay ノード間スタガー遅延（秒）
	void transitionTo(const std::string& stateName, float duration = 0.3f,
	                  vn::EasingType easing = vn::EasingType::EaseOutCubic,
	                  float perNodeDelay = 0.0f)
	{
		const auto toIt = m_states.find(stateName);
		if (toIt == m_states.end()) return;

		UITransitionDef def;
		def.fromState = m_currentState;
		def.toState = stateName;
		def.duration = duration;
		def.easing = easing;
		def.perNodeDelay = perNodeDelay;

		UIActiveTransition active;
		active.def = def;

		// 各ノードの遷移状態を構築
		std::size_t nodeIndex = 0;
		for (const auto& [nodeId, targetProps] : toIt->second.nodeProperties)
		{
			UINodeTransitionState nodeState;
			nodeState.nodeId = nodeId;
			nodeState.to = targetProps;
			nodeState.delay = perNodeDelay * static_cast<float>(nodeIndex);

			// 現在の状態を取得（from）
			nodeState.from = getCurrentProperties(nodeId);

			active.nodes.push_back(std::move(nodeState));
			++nodeIndex;
		}

		m_activeTransitions.push_back(std::move(active));
		m_currentState = stateName;
	}

	/// @brief 全アクティブ遷移を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& transition : m_activeTransitions)
		{
			if (transition.finished) continue;

			transition.totalElapsed += dt;
			bool allDone = true;

			for (auto& nodeState : transition.nodes)
			{
				if (nodeState.finished) continue;

				nodeState.elapsed += dt;

				// 遅延待ち
				if (nodeState.elapsed < nodeState.delay)
				{
					allDone = false;
					continue;
				}

				const float activeTime = nodeState.elapsed - nodeState.delay;
				const float dur = std::max(0.001f, transition.def.duration);
				const float rawT = std::clamp(activeTime / dur, 0.0f, 1.0f);
				const float t = vn::Easing::apply(transition.def.easing, rawT);

				// プロパティ補間
				const auto interpolated = UINodeProperties::lerp(
					nodeState.from, nodeState.to, t);

				// ノードに適用
				applyProperties(nodeState.nodeId, interpolated);

				if (rawT >= 1.0f)
				{
					nodeState.finished = true;
				}
				else
				{
					allDone = false;
				}
			}

			transition.finished = allDone;
		}

		// 完了した遷移をクリーンアップ
		m_activeTransitions.erase(
			std::remove_if(m_activeTransitions.begin(), m_activeTransitions.end(),
				[](const UIActiveTransition& t) { return t.finished; }),
			m_activeTransitions.end());
	}

	/// @brief 現在のステート名を取得する
	[[nodiscard]] const std::string& getCurrentState() const noexcept
	{
		return m_currentState;
	}

	/// @brief 遷移中かどうかを判定する
	[[nodiscard]] bool isTransitioning() const noexcept
	{
		return !m_activeTransitions.empty();
	}

	/// @brief アクティブ遷移数を取得する
	[[nodiscard]] std::size_t activeTransitionCount() const noexcept
	{
		return m_activeTransitions.size();
	}

	/// @brief 定義済みステート数を取得する
	[[nodiscard]] std::size_t stateCount() const noexcept
	{
		return m_states.size();
	}

	/// @brief 即座にステートを適用する（アニメーションなし）
	/// @param stateName 適用するステート名
	void applyStateImmediate(const std::string& stateName)
	{
		const auto it = m_states.find(stateName);
		if (it == m_states.end()) return;

		for (const auto& [nodeId, props] : it->second.nodeProperties)
		{
			applyProperties(nodeId, props);
		}
		m_currentState = stateName;
	}

private:
	/// @brief ノードの現在のプロパティを取得する
	[[nodiscard]] UINodeProperties getCurrentProperties(UINodeId nodeId) const
	{
		UINodeProperties props;

		// 登録済みノードから読み取り
		const auto nodeIt = m_nodeRegistry.find(nodeId);
		if (nodeIt != m_nodeRegistry.end() && nodeIt->second)
		{
			const auto& b = nodeIt->second->bounds();
			props.posX = b.x();
			props.posY = b.y();
			props.sizeW = b.width();
			props.sizeH = b.height();
			props.visible = nodeIt->second->visible();
			props.color = nodeIt->second->color();
		}

		// 現在のステートから読み取り（登録ノードがない場合のフォールバック）
		if (!m_currentState.empty())
		{
			const auto stateIt = m_states.find(m_currentState);
			if (stateIt != m_states.end())
			{
				const auto propIt = stateIt->second.nodeProperties.find(nodeId);
				if (propIt != stateIt->second.nodeProperties.end())
				{
					return propIt->second;
				}
			}
		}

		return props;
	}

	/// @brief プロパティをノードに適用する
	void applyProperties(UINodeId nodeId, const UINodeProperties& props)
	{
		const auto it = m_nodeRegistry.find(nodeId);
		if (it == m_nodeRegistry.end() || !it->second) return;

		UINode& node = *it->second;
		node.setBounds(sgc::Rectf{props.posX, props.posY, props.sizeW, props.sizeH});
		node.setVisible(props.visible);

		// スケール・オパシティはカスタムプロパティとして保存
		node.setProperty("scaleX", std::to_string(props.scaleX));
		node.setProperty("scaleY", std::to_string(props.scaleY));
		node.setProperty("opacity", std::to_string(props.opacity));
	}
};

} // namespace mitiru::ui
