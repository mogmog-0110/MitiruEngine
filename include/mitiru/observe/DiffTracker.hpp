#pragma once

/// @file DiffTracker.hpp
/// @brief フレーム間状態差分トラッカー
/// @details フレーム間の状態変化を追跡し、差分情報をAIエージェントに提供する。
///          前フレームとの比較で変更があったkey-valueのみを抽出する。

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mitiru::observe
{

/// @brief 差分エントリ
/// @details 1つの状態変化を表す。
struct DiffEntry
{
	std::string key;         ///< 状態キー
	std::string oldValue;    ///< 変更前の値（新規追加時は空文字列）
	std::string newValue;    ///< 変更後の値（削除時は空文字列）
	std::uint64_t frame = 0; ///< 変化が発生したフレーム番号

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"key\":\"" + key + "\",";
		json += "\"oldValue\":\"" + oldValue + "\",";
		json += "\"newValue\":\"" + newValue + "\",";
		json += "\"frame\":" + std::to_string(frame);
		json += "}";
		return json;
	}
};

/// @brief フレーム間状態差分トラッカー
/// @details 毎フレームの状態を記録し、前フレームからの差分を計算する。
class DiffTracker
{
public:
	/// @brief 現在のフレーム番号を設定する
	/// @param frame フレーム番号
	void beginFrame(std::uint64_t frame) noexcept
	{
		m_currentFrame = frame;
		m_currentStates.clear();
	}

	/// @brief 現在のフレームの状態を記録する
	/// @param key 状態キー
	/// @param value 状態値
	void recordState(const std::string& key, const std::string& value)
	{
		m_currentStates[key] = value;
	}

	/// @brief フレーム終了時に差分を計算し返す
	/// @return 前フレームからの差分エントリ一覧
	/// @note 内部で前フレーム状態を更新する
	[[nodiscard]] std::vector<DiffEntry> endFrame()
	{
		std::vector<DiffEntry> diffs;

		/// 新規追加・変更の検出
		for (const auto& [key, value] : m_currentStates)
		{
			const auto it = m_previousStates.find(key);
			if (it == m_previousStates.end())
			{
				/// 新規追加
				diffs.push_back(DiffEntry{
					.key = key,
					.oldValue = "",
					.newValue = value,
					.frame = m_currentFrame
				});
			}
			else if (it->second != value)
			{
				/// 値が変化
				diffs.push_back(DiffEntry{
					.key = key,
					.oldValue = it->second,
					.newValue = value,
					.frame = m_currentFrame
				});
			}
		}

		/// 削除の検出
		for (const auto& [key, value] : m_previousStates)
		{
			if (m_currentStates.find(key) == m_currentStates.end())
			{
				diffs.push_back(DiffEntry{
					.key = key,
					.oldValue = value,
					.newValue = "",
					.frame = m_currentFrame
				});
			}
		}

		m_previousStates = m_currentStates;
		return diffs;
	}

	/// @brief 差分エントリ一覧をJSON文字列に変換する
	/// @param diffs 差分エントリの配列
	/// @return JSON配列形式の文字列
	[[nodiscard]] static std::string toJson(const std::vector<DiffEntry>& diffs)
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < diffs.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += diffs[i].toJson();
		}
		json += "]";
		return json;
	}

	/// @brief トラッカーをリセットする
	void reset()
	{
		m_previousStates.clear();
		m_currentStates.clear();
		m_currentFrame = 0;
	}

private:
	std::map<std::string, std::string> m_previousStates;  ///< 前フレームの状態
	std::map<std::string, std::string> m_currentStates;    ///< 現フレームの状態
	std::uint64_t m_currentFrame = 0;                      ///< 現在のフレーム番号
};

} // namespace mitiru::observe
