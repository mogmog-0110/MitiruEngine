#pragma once

/// @file ActionExecutor.hpp
/// @brief コマンド→アクション変換エグゼキューター
/// @details AIエージェントが送信したコマンドを、登録されたアクションハンドラに
///          ディスパッチする。コマンド名とハンドラ関数のマッピングを管理する。

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "mitiru/control/CommandQueue.hpp"

namespace mitiru::control
{

/// @brief アクションハンドラ型
/// @details コマンドのペイロードを受け取り処理する関数。
using ActionHandler = std::function<void(std::string_view payload)>;

/// @brief アクションエグゼキューター
/// @details コマンド名にハンドラを紐付け、コマンド実行時に適切な
///          ハンドラを呼び出す。未登録のコマンドは無視する。
class ActionExecutor
{
public:
	/// @brief アクションハンドラを登録する
	/// @param name アクション名（Command.typeに対応）
	/// @param handler 実行するハンドラ関数
	void registerAction(const std::string& name, ActionHandler handler)
	{
		m_handlers[name] = std::move(handler);
	}

	/// @brief コマンドを実行する
	/// @param command 実行するコマンド
	/// @return ハンドラが見つかり実行できた場合 true
	bool execute(const Command& command)
	{
		const auto it = m_handlers.find(command.type);
		if (it == m_handlers.end())
		{
			return false;
		}
		it->second(command.payload);
		return true;
	}

	/// @brief 複数コマンドを一括実行する
	/// @param commands 実行するコマンド一覧
	/// @return 実行に成功したコマンド数
	std::size_t executeAll(const std::vector<Command>& commands)
	{
		std::size_t executed = 0;
		for (const auto& cmd : commands)
		{
			if (execute(cmd))
			{
				++executed;
			}
		}
		return executed;
	}

	/// @brief 登録されているアクション名一覧を返す
	/// @return アクション名のリスト
	[[nodiscard]] std::vector<std::string> listActions() const
	{
		std::vector<std::string> names;
		names.reserve(m_handlers.size());
		for (const auto& [name, handler] : m_handlers)
		{
			names.push_back(name);
		}
		return names;
	}

	/// @brief 指定アクションが登録されているか判定する
	/// @param name アクション名
	/// @return 登録済みなら true
	[[nodiscard]] bool hasAction(const std::string& name) const
	{
		return m_handlers.find(name) != m_handlers.end();
	}

	/// @brief アクション登録を解除する
	/// @param name 解除するアクション名
	/// @return 解除できた場合 true
	bool removeAction(const std::string& name)
	{
		return m_handlers.erase(name) > 0;
	}

	/// @brief 全アクション登録をクリアする
	void clear()
	{
		m_handlers.clear();
	}

	/// @brief 登録アクション数を取得する
	/// @return 登録されているアクション数
	[[nodiscard]] std::size_t actionCount() const noexcept
	{
		return m_handlers.size();
	}

private:
	std::map<std::string, ActionHandler> m_handlers;  ///< アクション名→ハンドラマップ
};

} // namespace mitiru::control
