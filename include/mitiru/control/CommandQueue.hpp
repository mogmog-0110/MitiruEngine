#pragma once

/// @file CommandQueue.hpp
/// @brief スレッドセーフコマンドキュー
/// @details AIエージェントからのコマンドをスレッドセーフに蓄積し、
///          エンジンが毎フレーム消費するためのキュー。

#include <mutex>
#include <string>
#include <vector>

namespace mitiru::control
{

/// @brief コマンド
/// @details AIエージェントまたはスクリプトから発行される1つの命令。
struct Command
{
	std::string type;      ///< コマンド種別（例: "key_down", "snapshot"）
	std::string payload;   ///< コマンドペイロード（JSON文字列等）
};

/// @brief スレッドセーフコマンドキュー
/// @details 複数スレッドから安全にコマンドを投入でき、
///          メインスレッドが一括消費するパターンを実現する。
class CommandQueue
{
public:
	/// @brief コマンドを投入する
	/// @param command 投入するコマンド
	void submit(const Command& command)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_commands.push_back(command);
	}

	/// @brief コマンドをムーブで投入する
	/// @param command 投入するコマンド
	void submit(Command&& command)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_commands.push_back(std::move(command));
	}

	/// @brief 蓄積された全コマンドを取得しクリアする
	/// @return 蓄積されていたコマンドの一覧
	[[nodiscard]] std::vector<Command> consumeAll()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<Command> commands;
		commands.swap(m_commands);
		return commands;
	}

	/// @brief 蓄積コマンド数を取得する
	/// @return 未消費のコマンド数
	[[nodiscard]] std::size_t pendingCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_commands.size();
	}

	/// @brief キューをクリアする
	void clear()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_commands.clear();
	}

	/// @brief キューが空か判定する
	/// @return 空なら true
	[[nodiscard]] bool empty() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_commands.empty();
	}

private:
	mutable std::mutex m_mutex;          ///< 排他制御用ミューテックス
	std::vector<Command> m_commands;     ///< コマンドバッファ
};

} // namespace mitiru::control
