#pragma once

/// @file SceneStack.hpp
/// @brief 「Title → Playing → Paused → Playing」のような行ったり来たりを管理する薄いスタック。
/// @details engine は SceneId (game-side enum/int) のスタックを保持するだけ。`enter / exit / update /
///          draw` の実体は game が `dispatch(id, ...)` で組む。engine が scene 種類を knowing しないので
///          ADR 0005 整合 (game 状態は game のもの)。

#include <vector>

namespace mitiru::scene
{

/// @brief scene id は game-side enum/int。engine は不透明扱い。
using SceneId = int;

/// @brief 小さな scene stack。空スタックは current() が -1 を返す。
class SceneStack
{
public:
	/// @brief 新しい scene を上に積む。
	void push(SceneId id) { m_stack.push_back(id); }

	/// @brief 一番上を捨てる (空なら no-op)。捨てた id を返す。空なら -1。
	SceneId pop()
	{
		if (m_stack.empty()) { return -1; }
		const SceneId top = m_stack.back();
		m_stack.pop_back();
		return top;
	}

	/// @brief 一番上を置き換える (= pop + push)。空でも push と等価。
	void replace(SceneId id)
	{
		if (!m_stack.empty()) { m_stack.pop_back(); }
		m_stack.push_back(id);
	}

	/// @brief 全部捨てて id を 1 つだけ積む (シーン切替の "reset")。
	void resetTo(SceneId id) { m_stack.clear(); m_stack.push_back(id); }

	void clear() { m_stack.clear(); }

	[[nodiscard]] bool    empty() const noexcept { return m_stack.empty(); }
	[[nodiscard]] int     size()  const noexcept { return static_cast<int>(m_stack.size()); }
	[[nodiscard]] SceneId current() const noexcept
	{
		return m_stack.empty() ? -1 : m_stack.back();
	}
	[[nodiscard]] SceneId previous() const noexcept
	{
		return (m_stack.size() < 2) ? -1 : m_stack[m_stack.size() - 2];
	}
	[[nodiscard]] const std::vector<SceneId>& stack() const noexcept { return m_stack; }

private:
	std::vector<SceneId> m_stack;
};

}  // namespace mitiru::scene
