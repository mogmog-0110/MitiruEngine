#pragma once

/// @file InputInjector.hpp
/// @brief AI入力インジェクター
/// @details 外部から入力コマンドを注入するための仕組み。
///          AIエージェントや自動テストが仮想入力を送信する際に使用する。

#include <cstdint>
#include <mutex>
#include <vector>

namespace mitiru
{

/// @brief 入力コマンドの種別
enum class InputCommandType : std::uint8_t
{
	KeyDown,       ///< キー押下
	KeyUp,         ///< キー解放
	MouseMove,     ///< マウス移動
	MouseDown,     ///< マウスボタン押下
	MouseUp        ///< マウスボタン解放
};

/// @brief 入力コマンド
/// @details 1つの入力操作を表す値型。
struct InputCommand
{
	InputCommandType type = InputCommandType::KeyDown;  ///< コマンド種別
	int keyCode = 0;        ///< キーコード（KeyDown/KeyUp時）
	int mouseButton = 0;    ///< マウスボタン番号（MouseDown/MouseUp時）
	float mouseX = 0.0f;    ///< マウスX座標（MouseMove時）
	float mouseY = 0.0f;    ///< マウスY座標（MouseMove時）
};

/// @brief AI入力インジェクター
/// @details スレッドセーフにコマンドを蓄積し、エンジンが毎フレーム消費する。
class InputInjector
{
public:
	/// @brief 入力コマンドを注入する
	/// @param command 注入するコマンド
	void inject(const InputCommand& command)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_pendingCommands.push_back(command);
	}

	/// @brief 蓄積されたコマンドを全て取得しクリアする
	/// @return 蓄積されていたコマンドのリスト
	[[nodiscard]] std::vector<InputCommand> consumePending()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<InputCommand> commands;
		commands.swap(m_pendingCommands);
		return commands;
	}

	/// @brief 蓄積されたコマンド数を取得する
	[[nodiscard]] std::size_t pendingCount() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_pendingCommands.size();
	}

	/// @brief 蓄積されたコマンドをクリアする
	void clear()
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_pendingCommands.clear();
	}

private:
	mutable std::mutex m_mutex;                 ///< 排他制御用ミューテックス
	std::vector<InputCommand> m_pendingCommands; ///< 未処理コマンドキュー
};

} // namespace mitiru
