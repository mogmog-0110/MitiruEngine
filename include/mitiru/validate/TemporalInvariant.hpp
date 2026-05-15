#pragma once

/// @file TemporalInvariant.hpp
/// @brief 時系列不変条件チェッカー
/// @details 値を時間経過で追跡し、異常な変化を検出する。
///          MaxDelta、RateOfChange、Monotonic、RangeBound、Timeout等の
///          ルールタイプをサポートする。
///
/// @code
/// mitiru::validate::TemporalInvariantChecker checker;
/// checker.addRule({"player_hp_range", TemporalRuleType::RangeBound,
///                  0.0f, 0.0f, 100.0f});
/// checker.recordValue("player_hp", 85.0f, frameNumber);
/// auto violations = checker.check(frameNumber);
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief 時系列ルールの種類
enum class TemporalRuleType : uint8_t
{
	MaxDelta,       ///< 1フレームでの変化がthresholdを超えた
	MinDelta,       ///< 変化量がthreshold未満（十分に変化していない）
	RateOfChange,   ///< Nフレーム間の変化速度がthresholdを超えた
	Monotonic,      ///< 値が単調増加（または単調減少）でなければならない
	RangeBound,     ///< 値が [min, max] の範囲内になければならない
	Timeout         ///< Nフレーム間値が変化していない（停滞検出）
};

/// @brief 時系列ルール違反の情報
struct TemporalViolation
{
	std::string ruleName;             ///< ルール名
	TemporalRuleType type;            ///< ルールタイプ
	std::uint64_t frame = 0;          ///< 検出フレーム
	float previousValue = 0.0f;       ///< 前回値
	float currentValue = 0.0f;        ///< 現在値
	float threshold = 0.0f;           ///< 閾値
	std::string description;          ///< 違反の説明

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"ruleName\":\"" + observe::jsonEscape(ruleName) + "\",";
		json += "\"type\":" + std::to_string(static_cast<int>(type)) + ",";
		json += "\"frame\":" + std::to_string(frame) + ",";
		json += "\"previousValue\":" + std::to_string(previousValue) + ",";
		json += "\"currentValue\":" + std::to_string(currentValue) + ",";
		json += "\"threshold\":" + std::to_string(threshold) + ",";
		json += "\"description\":\"" + observe::jsonEscape(description) + "\"";
		json += "}";
		return json;
	}
};

/// @brief 時系列ルール定義
struct TemporalRule
{
	std::string name;              ///< ルール名
	TemporalRuleType type;         ///< ルールタイプ
	float threshold = 0.0f;        ///< 閾値（MaxDelta, MinDelta, RateOfChange用）
	float minBound = 0.0f;         ///< 下限値（RangeBound用）
	float maxBound = 0.0f;         ///< 上限値（RangeBound用）
	int windowFrames = 1;          ///< 観測窓フレーム数（RateOfChange, Timeout用）
	bool increasing = true;        ///< 単調増加か単調減少か（Monotonic用）
};

/// @brief 時系列不変条件チェッカー
/// @details 値を時間経過で追跡し、登録されたルールに基づいて異常を検出する。
class TemporalInvariantChecker
{
public:
	/// @brief ルールを追加する
	/// @param rule 追加するルール
	void addRule(TemporalRule rule)
	{
		m_rules.push_back(std::move(rule));
	}

	/// @brief ルールを名前で削除する
	/// @param name 削除するルール名
	void removeRule(std::string_view name)
	{
		m_rules.erase(
			std::remove_if(m_rules.begin(), m_rules.end(),
				[name](const TemporalRule& r) { return r.name == name; }),
			m_rules.end());
	}

	/// @brief 値を記録する
	/// @param valueName 値の名前
	/// @param value 現在の値
	/// @param frame 現在のフレーム番号
	void recordValue(const std::string& valueName, float value, std::uint64_t frame)
	{
		auto& tracked = m_values[valueName];
		if (tracked.name.empty())
		{
			tracked.name = valueName;
		}
		tracked.history.emplace_back(frame, value);
		if (static_cast<int>(tracked.history.size()) > tracked.maxHistory)
		{
			tracked.history.erase(tracked.history.begin());
		}
	}

	/// @brief 全ルールを現在の追跡値に対してチェックする
	/// @param currentFrame 現在のフレーム番号
	/// @return 検出された違反のリスト
	[[nodiscard]] std::vector<TemporalViolation> check(std::uint64_t currentFrame) const
	{
		std::vector<TemporalViolation> violations;
		for (const auto& [name, tracked] : m_values)
		{
			auto v = checkValue(name, currentFrame);
			violations.insert(violations.end(), v.begin(), v.end());
		}
		return violations;
	}

	/// @brief 特定の値に対して全ルールをチェックする
	/// @param valueName 値の名前
	/// @param currentFrame 現在のフレーム番号
	/// @return 検出された違反のリスト
	[[nodiscard]] std::vector<TemporalViolation> checkValue(
		std::string_view valueName,
		std::uint64_t currentFrame) const
	{
		std::vector<TemporalViolation> violations;

		const std::string valueNameStr(valueName);
		auto it = m_values.find(valueNameStr);
		if (it == m_values.end() || it->second.history.empty())
		{
			return violations;
		}

		const auto& history = it->second.history;
		const float current = history.back().second;
		const float previous = (history.size() >= 2)
			? history[history.size() - 2].second
			: current;

		for (const auto& rule : m_rules)
		{
			switch (rule.type)
			{
			case TemporalRuleType::MaxDelta:
			{
				const float delta = std::abs(current - previous);
				if (delta > rule.threshold)
				{
					TemporalViolation v;
					v.ruleName = rule.name;
					v.type = rule.type;
					v.frame = currentFrame;
					v.previousValue = previous;
					v.currentValue = current;
					v.threshold = rule.threshold;
					v.description = valueNameStr + ": delta " + std::to_string(delta)
						+ " exceeds max " + std::to_string(rule.threshold);
					violations.push_back(std::move(v));
				}
				break;
			}
			case TemporalRuleType::MinDelta:
			{
				const float delta = std::abs(current - previous);
				if (delta < rule.threshold)
				{
					TemporalViolation v;
					v.ruleName = rule.name;
					v.type = rule.type;
					v.frame = currentFrame;
					v.previousValue = previous;
					v.currentValue = current;
					v.threshold = rule.threshold;
					v.description = valueNameStr + ": delta " + std::to_string(delta)
						+ " below min " + std::to_string(rule.threshold);
					violations.push_back(std::move(v));
				}
				break;
			}
			case TemporalRuleType::RateOfChange:
			{
				if (history.size() >= 2)
				{
					const int window = std::min(rule.windowFrames,
						static_cast<int>(history.size()) - 1);
					if (window > 0)
					{
						const auto& oldest = history[history.size() - 1 - window];
						const float rate = std::abs(current - oldest.second)
							/ static_cast<float>(window);
						if (rate > rule.threshold)
						{
							TemporalViolation v;
							v.ruleName = rule.name;
							v.type = rule.type;
							v.frame = currentFrame;
							v.previousValue = oldest.second;
							v.currentValue = current;
							v.threshold = rule.threshold;
							v.description = valueNameStr + ": rate " + std::to_string(rate)
								+ " exceeds " + std::to_string(rule.threshold)
								+ " over " + std::to_string(window) + " frames";
							violations.push_back(std::move(v));
						}
					}
				}
				break;
			}
			case TemporalRuleType::Monotonic:
			{
				if (history.size() >= 2)
				{
					const bool violated = rule.increasing
						? (current < previous)
						: (current > previous);
					if (violated)
					{
						TemporalViolation v;
						v.ruleName = rule.name;
						v.type = rule.type;
						v.frame = currentFrame;
						v.previousValue = previous;
						v.currentValue = current;
						v.threshold = 0.0f;
						v.description = valueNameStr + ": monotonic "
							+ std::string(rule.increasing ? "increase" : "decrease")
							+ " violated (" + std::to_string(previous)
							+ " -> " + std::to_string(current) + ")";
						violations.push_back(std::move(v));
					}
				}
				break;
			}
			case TemporalRuleType::RangeBound:
			{
				if (current < rule.minBound || current > rule.maxBound)
				{
					TemporalViolation v;
					v.ruleName = rule.name;
					v.type = rule.type;
					v.frame = currentFrame;
					v.previousValue = previous;
					v.currentValue = current;
					v.threshold = 0.0f;
					v.description = valueNameStr + ": value " + std::to_string(current)
						+ " out of range [" + std::to_string(rule.minBound)
						+ ", " + std::to_string(rule.maxBound) + "]";
					violations.push_back(std::move(v));
				}
				break;
			}
			case TemporalRuleType::Timeout:
			{
				std::uint64_t lastChangeFrame = 0;
				if (history.size() >= 2)
				{
					for (int i = static_cast<int>(history.size()) - 2; i >= 0; --i)
					{
						if (history[i].second != current)
						{
							lastChangeFrame = history[i + 1].first;
							break;
						}
					}
					if (lastChangeFrame == 0)
					{
						lastChangeFrame = history.front().first;
					}
				}
				else
				{
					lastChangeFrame = history.front().first;
				}

				const auto staleFrames = currentFrame - lastChangeFrame;
				if (staleFrames > static_cast<std::uint64_t>(rule.windowFrames))
				{
					TemporalViolation v;
					v.ruleName = rule.name;
					v.type = rule.type;
					v.frame = currentFrame;
					v.previousValue = current;
					v.currentValue = current;
					v.threshold = static_cast<float>(rule.windowFrames);
					v.description = valueNameStr + ": stale for "
						+ std::to_string(staleFrames) + " frames (threshold: "
						+ std::to_string(rule.windowFrames) + ")";
					violations.push_back(std::move(v));
				}
				break;
			}
			}
		}

		return violations;
	}

	/// @brief 全ての追跡データとルールをクリアする
	void clear()
	{
		m_rules.clear();
		m_values.clear();
	}

	/// @brief 登録されているルール数を返す
	/// @return ルール数
	[[nodiscard]] std::size_t ruleCount() const noexcept
	{
		return m_rules.size();
	}

	/// @brief 違反リストをJSON配列に変換する
	/// @param violations 違反リスト
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson(
		const std::vector<TemporalViolation>& violations) const
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < violations.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += violations[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief 追跡値の内部構造
	struct TrackedValue
	{
		std::string name;                                  ///< 値の名前
		std::vector<std::pair<std::uint64_t, float>> history;  ///< (フレーム, 値)の履歴
		int maxHistory = 120;                              ///< 保持する最大履歴数
	};

	std::vector<TemporalRule> m_rules;              ///< 登録済みルール
	std::map<std::string, TrackedValue> m_values;   ///< 追跡中の値
};

} // namespace mitiru::validate
