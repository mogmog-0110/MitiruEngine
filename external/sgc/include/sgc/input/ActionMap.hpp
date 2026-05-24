#pragma once

/// @file ActionMap.hpp
/// @brief フレームワーク非依存の入力アクション管理
///
/// キーコードをアクション名（ハッシュID）にバインドし、
/// Pressed / Held / Released の状態を追跡する。
///
/// @code
/// using namespace sgc::literals;
/// sgc::ActionMap actions;
/// actions.bind("jump"_hash, 32); // スペースキー
/// actions.bind("left"_hash, 'A');
///
/// // 毎フレーム
/// actions.update(currentPressedKeys);
/// if (actions.isPressed("jump"_hash)) { jump(); }
/// if (actions.isHeld("left"_hash)) { moveLeft(); }
/// @endcode

#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sgc
{

/// @brief アクションID型（Hash.hppの_hashリテラルで生成）
using ActionId = std::uint64_t;

/// @brief アクション状態
enum class ActionState
{
	None,      ///< 非アクティブ
	Pressed,   ///< このフレームで押された
	Held,      ///< 押し続けている
	Released   ///< このフレームで離された
};

/// @brief 入力バインディング
struct InputBinding
{
	int keyCode{0};  ///< キーコード
};

/// @brief フレームワーク非依存の入力アクションマップ
///
/// キーコードをアクションIDにバインドし、フレームごとの状態を管理する。
class ActionMap
{
public:
	/// @brief アクションにキーをバインドする
	/// @param actionId アクションID
	/// @param binding バインディング
	void bind(ActionId actionId, InputBinding binding)
	{
		m_bindings[actionId].push_back(binding);
	}

	/// @brief アクションにキーコードを直接バインドする
	/// @param actionId アクションID
	/// @param keyCode キーコード
	void bind(ActionId actionId, int keyCode)
	{
		m_bindings[actionId].push_back(InputBinding{keyCode});
	}

	/// @brief アクションのバインディングを解除する
	/// @param actionId アクションID
	void unbind(ActionId actionId)
	{
		m_bindings.erase(actionId);
		m_states.erase(actionId);
	}

	/// @brief フレーム更新 — 現在押されているキーから状態を更新する
	/// @param pressedKeys 現在押されているキーコードの配列
	void update(std::span<const int> pressedKeys)
	{
		std::unordered_set<int> pressedSet(pressedKeys.begin(), pressedKeys.end());

		for (auto& [actionId, bindings] : m_bindings)
		{
			bool anyPressed = false;
			for (const auto& binding : bindings)
			{
				if (pressedSet.contains(binding.keyCode))
				{
					anyPressed = true;
					break;
				}
			}

			auto& state = m_states[actionId];
			if (anyPressed)
			{
				if (state == ActionState::None || state == ActionState::Released)
				{
					state = ActionState::Pressed;
				}
				else
				{
					state = ActionState::Held;
				}
			}
			else
			{
				if (state == ActionState::Pressed || state == ActionState::Held)
				{
					state = ActionState::Released;
				}
				else
				{
					state = ActionState::None;
				}
			}
		}
	}

	/// @brief アクションがこのフレームで押されたか
	/// @param actionId アクションID
	/// @return 押された瞬間ならtrue
	[[nodiscard]] bool isPressed(ActionId actionId) const
	{
		const auto it = m_states.find(actionId);
		return it != m_states.end() && it->second == ActionState::Pressed;
	}

	/// @brief アクションが押し続けられているか
	/// @param actionId アクションID
	/// @return 押し続けならtrue
	[[nodiscard]] bool isHeld(ActionId actionId) const
	{
		const auto it = m_states.find(actionId);
		return it != m_states.end()
			&& (it->second == ActionState::Pressed || it->second == ActionState::Held);
	}

	/// @brief アクションがこのフレームで離されたか
	/// @param actionId アクションID
	/// @return 離された瞬間ならtrue
	[[nodiscard]] bool isReleased(ActionId actionId) const
	{
		const auto it = m_states.find(actionId);
		return it != m_states.end() && it->second == ActionState::Released;
	}

	/// @brief アクションの現在の状態を取得する
	/// @param actionId アクションID
	/// @return アクション状態
	[[nodiscard]] ActionState state(ActionId actionId) const
	{
		const auto it = m_states.find(actionId);
		return (it != m_states.end()) ? it->second : ActionState::None;
	}

private:
	std::unordered_map<ActionId, std::vector<InputBinding>> m_bindings;  ///< バインディング
	std::unordered_map<ActionId, ActionState> m_states;                  ///< 状態
};

} // namespace sgc
