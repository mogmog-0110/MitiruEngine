#pragma once

/// @file Command.hpp
/// @brief コマンドパターンとUndo/Redoスタック
///
/// ユーザー操作の記録・取り消し・やり直しを実現する。
/// レベルエディタ、ペイントツール等で使用。

#include <functional>
#include <memory>
#include <vector>

namespace sgc
{

/// @brief コマンドインターフェース
///
/// execute()とundo()を実装して具体的な操作を定義する。
class ICommand
{
public:
	virtual ~ICommand() = default;

	/// @brief コマンドを実行する
	virtual void execute() = 0;

	/// @brief コマンドの実行を取り消す
	virtual void undo() = 0;
};

/// @brief ラムダベースの簡易コマンド
///
/// ICommandを継承せず、ラムダで直接コマンドを定義できる。
///
/// @code
/// auto cmd = sgc::makeLambdaCommand(
///     [&]{ player.move(10); },  // execute
///     [&]{ player.move(-10); }  // undo
/// );
/// @endcode
class LambdaCommand : public ICommand
{
public:
	/// @brief 実行関数とundo関数を指定して構築する
	LambdaCommand(std::function<void()> doFunc, std::function<void()> undoFunc)
		: m_doFunc(std::move(doFunc))
		, m_undoFunc(std::move(undoFunc))
	{
	}

	void execute() override { m_doFunc(); }
	void undo() override { m_undoFunc(); }

private:
	std::function<void()> m_doFunc;
	std::function<void()> m_undoFunc;
};

/// @brief LambdaCommandのファクトリ関数
/// @param doFunc 実行関数
/// @param undoFunc 取り消し関数
/// @return コマンドのunique_ptr
[[nodiscard]] inline std::unique_ptr<ICommand> makeLambdaCommand(
	std::function<void()> doFunc,
	std::function<void()> undoFunc)
{
	return std::make_unique<LambdaCommand>(std::move(doFunc), std::move(undoFunc));
}

/// @brief Undo/Redoスタック
///
/// コマンドの実行履歴を管理し、取り消しとやり直しを提供する。
///
/// @code
/// sgc::CommandHistory history;
///
/// history.execute(sgc::makeLambdaCommand(
///     [&]{ value += 10; },
///     [&]{ value -= 10; }
/// ));
///
/// history.undo();  // value -= 10
/// history.redo();  // value += 10
/// @endcode
class CommandHistory
{
public:
	/// @brief コマンドを実行し、履歴に追加する
	/// @param command 実行するコマンド
	void execute(std::unique_ptr<ICommand> command)
	{
		command->execute();
		m_undoStack.push_back(std::move(command));
		m_redoStack.clear();
	}

	/// @brief 直前のコマンドを取り消す
	/// @return 取り消せた場合 true
	bool undo()
	{
		if (m_undoStack.empty()) return false;

		auto command = std::move(m_undoStack.back());
		m_undoStack.pop_back();
		command->undo();
		m_redoStack.push_back(std::move(command));
		return true;
	}

	/// @brief 取り消したコマンドをやり直す
	/// @return やり直せた場合 true
	bool redo()
	{
		if (m_redoStack.empty()) return false;

		auto command = std::move(m_redoStack.back());
		m_redoStack.pop_back();
		command->execute();
		m_undoStack.push_back(std::move(command));
		return true;
	}

	/// @brief Undo可能か判定する
	[[nodiscard]] bool canUndo() const noexcept { return !m_undoStack.empty(); }

	/// @brief Redo可能か判定する
	[[nodiscard]] bool canRedo() const noexcept { return !m_redoStack.empty(); }

	/// @brief 履歴をクリアする
	void clear()
	{
		m_undoStack.clear();
		m_redoStack.clear();
	}

	/// @brief Undo履歴のサイズを返す
	[[nodiscard]] std::size_t undoSize() const noexcept { return m_undoStack.size(); }

	/// @brief Redo履歴のサイズを返す
	[[nodiscard]] std::size_t redoSize() const noexcept { return m_redoStack.size(); }

private:
	std::vector<std::unique_ptr<ICommand>> m_undoStack;
	std::vector<std::unique_ptr<ICommand>> m_redoStack;
};

} // namespace sgc
