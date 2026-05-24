#pragma once

/// @file CharacterManager.hpp
/// @brief キャラクターのステージ管理
///
/// 登録されたキャラクターの表示・非表示・表情変更等を管理する。
/// VNシーンのキャラクター演出を統括する。
///
/// @code
/// using namespace sgc::vn;
/// CharacterManager mgr;
/// CharacterDef def;
/// def.id = "sakura";
/// def.displayName = "Sakura";
/// mgr.registerCharacter(std::move(def));
/// mgr.show("sakura", CharacterPosition::Left, "happy");
/// auto state = mgr.getState("sakura");
/// @endcode

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/vn/Character.hpp"

namespace sgc::vn
{

/// @brief キャラクターのステージ管理クラス
///
/// 複数キャラクターの登録・表示・非表示・表情変更を管理する。
/// VNSceneからの指示に従ってキャラクターの状態を更新する。
class CharacterManager
{
public:
	/// @brief キャラクターを登録する
	/// @param def キャラクター定義
	void registerCharacter(CharacterDef def)
	{
		const auto id = def.id;
		m_definitions.emplace(id, std::move(def));
	}

	/// @brief キャラクターを表示する
	/// @param charId キャラクターID
	/// @param pos 立ち位置
	/// @param expression 表情名（デフォルト: "default"）
	void show(const std::string& charId, CharacterPosition pos,
		const std::string& expression = "default")
	{
		if (m_definitions.find(charId) == m_definitions.end())
		{
			return;
		}

		auto it = m_states.find(charId);
		if (it != m_states.end())
		{
			it->second.position = pos;
			it->second.expression = expression;
			it->second.visible = true;
			it->second.alpha = 1.0f;
		}
		else
		{
			CharacterState state;
			state.characterId = charId;
			state.expression = expression;
			state.position = pos;
			state.alpha = 1.0f;
			state.visible = true;
			m_states.emplace(charId, std::move(state));
		}
	}

	/// @brief キャラクターを非表示にする
	/// @param charId キャラクターID
	void hide(const std::string& charId)
	{
		auto it = m_states.find(charId);
		if (it != m_states.end())
		{
			it->second.visible = false;
		}
	}

	/// @brief すべてのキャラクターを非表示にする
	void hideAll()
	{
		for (auto& [id, state] : m_states)
		{
			state.visible = false;
		}
	}

	/// @brief キャラクターの表情を変更する
	/// @param charId キャラクターID
	/// @param expression 表情名
	void setExpression(const std::string& charId, const std::string& expression)
	{
		auto it = m_states.find(charId);
		if (it != m_states.end())
		{
			it->second.expression = expression;
		}
	}

	/// @brief キャラクターの位置を変更する
	/// @param charId キャラクターID
	/// @param pos 立ち位置
	void setPosition(const std::string& charId, CharacterPosition pos)
	{
		auto it = m_states.find(charId);
		if (it != m_states.end())
		{
			it->second.position = pos;
		}
	}

	/// @brief キャラクターの透明度を変更する
	/// @param charId キャラクターID
	/// @param alpha 透明度 [0, 1]
	void setAlpha(const std::string& charId, float alpha)
	{
		auto it = m_states.find(charId);
		if (it != m_states.end())
		{
			it->second.alpha = alpha;
		}
	}

	/// @brief キャラクターの状態を取得する
	/// @param charId キャラクターID
	/// @return キャラクターの状態。未登録ならstd::nullopt
	[[nodiscard]] std::optional<CharacterState> getState(const std::string& charId) const
	{
		auto it = m_states.find(charId);
		if (it == m_states.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief 表示中のキャラクター一覧を取得する
	/// @return 表示中キャラクターの状態配列
	[[nodiscard]] std::vector<CharacterState> visibleCharacters() const
	{
		std::vector<CharacterState> result;
		for (const auto& [id, state] : m_states)
		{
			if (state.visible)
			{
				result.push_back(state);
			}
		}
		return result;
	}

	/// @brief 登録済みキャラクター数を取得する
	/// @return 登録数
	[[nodiscard]] std::size_t registeredCount() const noexcept
	{
		return m_definitions.size();
	}

	/// @brief 表示中キャラクター数を取得する
	/// @return 表示中の数
	[[nodiscard]] std::size_t visibleCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [id, state] : m_states)
		{
			if (state.visible)
			{
				++count;
			}
		}
		return count;
	}

private:
	std::unordered_map<std::string, CharacterDef> m_definitions;  ///< キャラクター定義マップ
	std::unordered_map<std::string, CharacterState> m_states;     ///< キャラクター状態マップ
};

} // namespace sgc::vn
