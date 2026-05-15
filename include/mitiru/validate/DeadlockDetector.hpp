#pragma once

/// @file DeadlockDetector.hpp
/// @brief ゲーム状態のデッドロック（停滞）検出
/// @details ゲーム状態のキー・バリューペアを毎フレーム記録し、
///          一定フレーム以上変化がない場合にデッドロックとして報告する。
///
/// @code
/// mitiru::validate::DeadlockDetector detector;
/// detector.setThreshold(300);
/// detector.recordState("game_phase", "battle", frameNumber);
/// detector.recordState("enemy_count", "5", frameNumber);
/// if (auto info = detector.check(frameNumber)) {
///     log("Deadlock detected: " + info->description);
/// }
/// @endcode

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief デッドロック検出情報
struct DeadlockInfo
{
	std::uint64_t detectedAtFrame = 0;  ///< 検出フレーム
	std::uint64_t staleForFrames = 0;   ///< 停滞フレーム数
	std::string lastState;              ///< 最後の状態概要
	std::string description;            ///< 説明

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"detectedAtFrame\":" + std::to_string(detectedAtFrame) + ",";
		json += "\"staleForFrames\":" + std::to_string(staleForFrames) + ",";
		json += "\"lastState\":\"" + observe::jsonEscape(lastState) + "\",";
		json += "\"description\":\"" + observe::jsonEscape(description) + "\"";
		json += "}";
		return json;
	}
};

/// @brief ゲーム状態デッドロック検出器
/// @details 状態のキー・バリューペアを追跡し、変化が停止した場合を検出する。
class DeadlockDetector
{
public:
	/// @brief デッドロック判定の閾値フレーム数を設定する
	/// @param frames 閾値フレーム数
	void setThreshold(std::uint64_t frames)
	{
		m_staleThreshold = frames;
	}

	/// @brief ゲーム状態を記録する（毎フレーム呼び出す）
	/// @param key 状態キー
	/// @param value 状態値
	/// @param frame 現在のフレーム番号
	void recordState(const std::string& key, const std::string& value, std::uint64_t frame)
	{
		auto it = m_currentState.find(key);
		if (it == m_currentState.end() || it->second != value)
		{
			m_currentState[key] = value;
			m_lastChangeFrame = frame;

			// 変化フレーム履歴に記録
			m_stateChangeFrames.push_back(frame);
			if (static_cast<int>(m_stateChangeFrames.size()) > m_maxHistory)
			{
				m_stateChangeFrames.erase(m_stateChangeFrames.begin());
			}
		}
		m_lastChangedState[key] = value;
	}

	/// @brief ゲームがデッドロック状態かチェックする
	/// @param currentFrame 現在のフレーム番号
	/// @return デッドロック検出時はDeadlockInfo、正常時はnullopt
	[[nodiscard]] std::optional<DeadlockInfo> check(std::uint64_t currentFrame) const
	{
		if (m_currentState.empty())
		{
			return std::nullopt;
		}

		const auto staleFrames = framesSinceLastChange(currentFrame);
		if (staleFrames >= m_staleThreshold)
		{
			DeadlockInfo info;
			info.detectedAtFrame = currentFrame;
			info.staleForFrames = staleFrames;

			// 最後の状態をサマリとして構築
			std::string stateSummary;
			for (const auto& [key, val] : m_currentState)
			{
				if (!stateSummary.empty())
				{
					stateSummary += ", ";
				}
				stateSummary += key + "=" + val;
			}
			info.lastState = stateSummary;
			info.description = "Game state unchanged for " + std::to_string(staleFrames)
				+ " frames (threshold: " + std::to_string(m_staleThreshold)
				+ "). State: " + stateSummary;

			return info;
		}

		return std::nullopt;
	}

	/// @brief 最後の状態変化からのフレーム数を返す
	/// @param currentFrame 現在のフレーム番号
	/// @return 経過フレーム数
	[[nodiscard]] std::uint64_t framesSinceLastChange(std::uint64_t currentFrame) const
	{
		if (m_lastChangeFrame == 0)
		{
			return 0;
		}
		return currentFrame - m_lastChangeFrame;
	}

	/// @brief 状態変化頻度を返す（60フレームあたりの変化回数）
	/// @param currentFrame 現在のフレーム番号
	/// @return 60フレームあたりの変化回数
	[[nodiscard]] float changeFrequency(std::uint64_t currentFrame) const
	{
		if (m_stateChangeFrames.empty())
		{
			return 0.0f;
		}

		constexpr std::uint64_t windowSize = 60;
		const std::uint64_t windowStart = (currentFrame > windowSize)
			? currentFrame - windowSize
			: 0;

		int changesInWindow = 0;
		for (const auto& frame : m_stateChangeFrames)
		{
			if (frame >= windowStart && frame <= currentFrame)
			{
				++changesInWindow;
			}
		}

		return static_cast<float>(changesInWindow);
	}

	/// @brief 追跡中の状態キー数を返す
	/// @return 追跡中のキー数
	[[nodiscard]] std::size_t trackedKeyCount() const noexcept
	{
		return m_currentState.size();
	}

	/// @brief 全データをリセットする
	void reset()
	{
		m_currentState.clear();
		m_lastChangedState.clear();
		m_lastChangeFrame = 0;
		m_stateChangeFrames.clear();
	}

	/// @brief 現在の検出器状態をJSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"staleThreshold\":" + std::to_string(m_staleThreshold) + ",";
		json += "\"lastChangeFrame\":" + std::to_string(m_lastChangeFrame) + ",";
		json += "\"trackedKeys\":" + std::to_string(m_currentState.size()) + ",";
		json += "\"changeHistorySize\":" + std::to_string(m_stateChangeFrames.size()) + ",";
		json += "\"currentState\":{";
		bool first = true;
		for (const auto& [key, val] : m_currentState)
		{
			if (!first)
			{
				json += ",";
			}
			json += "\"" + observe::jsonEscape(key) + "\":\"" + observe::jsonEscape(val) + "\"";
			first = false;
		}
		json += "}";
		json += "}";
		return json;
	}

private:
	std::map<std::string, std::string> m_currentState;       ///< 現在の状態
	std::map<std::string, std::string> m_lastChangedState;   ///< 最後に変化した状態
	std::uint64_t m_lastChangeFrame = 0;                     ///< 最後の変化フレーム
	std::uint64_t m_staleThreshold = 300;                    ///< デッドロック判定閾値
	std::vector<std::uint64_t> m_stateChangeFrames;          ///< 変化フレーム履歴
	int m_maxHistory = 600;                                  ///< 最大履歴数
};

} // namespace mitiru::validate
