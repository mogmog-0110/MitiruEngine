#pragma once

/// @file ReplaySystem.hpp
/// @brief 高レベルリプレイオーケストレーター
/// @details InputRecorderとInputReplayerを内部で統合し、
///          記録・再生の高レベルAPIを提供する。
///          ゲームループから直接使用することを想定した統合インターフェース。

#include <cstdint>
#include <vector>

#include "mitiru/input/InputInjector.hpp"
#include "mitiru/input/InputRecorder.hpp"
#include "mitiru/input/InputReplayer.hpp"
#include "mitiru/input/InputState.hpp"

namespace mitiru::control
{

/// @brief リプレイシステムの状態
enum class ReplayState : std::uint8_t
{
	Idle,       ///< 待機中
	Recording,  ///< 記録中
	Playing     ///< 再生中
};

/// @brief 高レベルリプレイオーケストレーター
/// @details 記録・再生を統合管理する。
///          記録時はInputRecorderにフレームデータを渡し、
///          再生時はInputReplayerからコマンドを取得する。
class ReplaySystem
{
public:
	/// @brief 記録を開始する
	/// @param seed 乱数シード（決定論的リプレイに必要）
	/// @param tps Ticks Per Second
	void startRecording(std::uint64_t seed = 0, int tps = 60)
	{
		stopAll();
		m_recorder.beginRecording(seed, tps);
		m_state = ReplayState::Recording;
	}

	/// @brief 1フレーム分の入力を記録する
	/// @param frameNumber フレーム番号
	/// @param state そのフレームの入力状態
	/// @note InputStateから有効なコマンドを抽出して記録する
	void recordFrame(std::uint64_t frameNumber, const InputState& state)
	{
		if (m_state != ReplayState::Recording) return;

		/// 入力状態からコマンドリストを生成
		std::vector<InputCommand> commands;

		/// キー状態をスキャン
		for (int i = 0; i < InputState::MAX_KEYS; ++i)
		{
			if (state.isKeyDown(i))
			{
				commands.push_back(InputCommand{
					.type = InputCommandType::KeyDown,
					.keyCode = i,
					.mouseButton = 0,
					.mouseX = 0.0f,
					.mouseY = 0.0f
				});
			}
		}

		/// マウス位置を記録
		const auto [mx, my] = state.mousePosition();
		commands.push_back(InputCommand{
			.type = InputCommandType::MouseMove,
			.keyCode = 0,
			.mouseButton = 0,
			.mouseX = mx,
			.mouseY = my
		});

		/// マウスボタン状態をスキャン
		for (int i = 0; i < InputState::MAX_MOUSE_BUTTONS; ++i)
		{
			const auto btn = static_cast<MouseButton>(i);
			if (state.isMouseButtonDown(btn))
			{
				commands.push_back(InputCommand{
					.type = InputCommandType::MouseDown,
					.keyCode = 0,
					.mouseButton = i,
					.mouseX = 0.0f,
					.mouseY = 0.0f
				});
			}
		}

		m_recorder.recordFrame(frameNumber, commands);
	}

	/// @brief コマンドリストを直接記録する
	/// @param frameNumber フレーム番号
	/// @param commands 入力コマンド一覧
	void recordFrameCommands(std::uint64_t frameNumber,
		const std::vector<InputCommand>& commands)
	{
		if (m_state != ReplayState::Recording) return;
		m_recorder.recordFrame(frameNumber, commands);
	}

	/// @brief 記録を停止しReplayDataを返す
	/// @return 記録されたリプレイデータ
	[[nodiscard]] ReplayData stopRecording()
	{
		if (m_state != ReplayState::Recording)
		{
			return ReplayData{};
		}
		m_state = ReplayState::Idle;
		return m_recorder.endRecording();
	}

	/// @brief 再生を開始する
	/// @param data リプレイデータ
	void startPlayback(const ReplayData& data)
	{
		stopAll();
		m_replayer.load(data);
		m_state = ReplayState::Playing;
	}

	/// @brief 指定フレームの再生コマンドを取得する
	/// @param frameNumber フレーム番号
	/// @return そのフレームの入力コマンド一覧
	[[nodiscard]] std::vector<InputCommand> getPlaybackCommands(
		std::uint64_t frameNumber)
	{
		if (m_state != ReplayState::Playing)
		{
			return {};
		}

		auto commands = m_replayer.getCommandsForFrame(frameNumber);

		if (m_replayer.isFinished())
		{
			m_state = ReplayState::Idle;
		}

		return commands;
	}

	/// @brief 再生中か判定する
	/// @return 再生中なら true
	[[nodiscard]] bool isPlaying() const noexcept
	{
		return m_state == ReplayState::Playing;
	}

	/// @brief 記録中か判定する
	/// @return 記録中なら true
	[[nodiscard]] bool isRecording() const noexcept
	{
		return m_state == ReplayState::Recording;
	}

	/// @brief 現在の状態を取得する
	/// @return リプレイシステムの状態
	[[nodiscard]] ReplayState state() const noexcept
	{
		return m_state;
	}

	/// @brief システムをリセットする
	void reset()
	{
		stopAll();
		m_replayer.reset();
	}

private:
	/// @brief 記録・再生を全て停止する
	void stopAll()
	{
		if (m_state == ReplayState::Recording)
		{
			m_recorder.endRecording();
		}
		m_state = ReplayState::Idle;
	}

	InputRecorder m_recorder;     ///< 入力記録器
	InputReplayer m_replayer;     ///< 入力再生器
	ReplayState m_state = ReplayState::Idle;  ///< 現在の状態
};

} // namespace mitiru::control
