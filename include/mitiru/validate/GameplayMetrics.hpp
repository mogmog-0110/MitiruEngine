#pragma once

/// @file GameplayMetrics.hpp
/// @brief ゲームプレイ統計異常検出
/// @details ゲームプレイメトリクスを収集し、統計的な異常（入力なし、移動なし、
///          不正値、停滞等）を検出する。
///
/// @code
/// mitiru::validate::GameplayMetrics metrics;
/// metrics.addRule({"no_input", GameplayAnomaly::NoInput, 300.0f, 300});
/// metrics.recordInput(frameNumber);
/// metrics.recordMetric("player_speed", speed, frameNumber);
/// auto anomalies = metrics.check(frameNumber);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/observe/JsonEscape.hpp>

namespace mitiru::validate
{

/// @brief ゲームプレイ異常の情報
struct GameplayAnomaly
{
	/// @brief 異常の種類
	enum Type
	{
		NoInput,       ///< 入力がない
		NoMovement,    ///< 移動がない
		NegativeValue, ///< 負の値（不正）
		Stagnation,    ///< メトリクスが停滞
		InvalidState   ///< 不正な状態
	};

	Type type;                      ///< 異常タイプ
	std::uint64_t frame = 0;        ///< 検出フレーム
	std::string metricName;         ///< メトリクス名
	float value = 0.0f;             ///< 検出時の値
	float threshold = 0.0f;         ///< 閾値
	std::string description;        ///< 説明

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"type\":" + std::to_string(static_cast<int>(type)) + ",";
		json += "\"frame\":" + std::to_string(frame) + ",";
		json += "\"metricName\":\"" + observe::jsonEscape(metricName) + "\",";
		json += "\"value\":" + std::to_string(value) + ",";
		json += "\"threshold\":" + std::to_string(threshold) + ",";
		json += "\"description\":\"" + observe::jsonEscape(description) + "\"";
		json += "}";
		return json;
	}
};

/// @brief メトリクスルール定義
struct MetricRule
{
	std::string name;              ///< ルール名
	GameplayAnomaly::Type type;    ///< 異常タイプ
	float threshold = 0.0f;        ///< 閾値
	int windowFrames = 60;         ///< 観測窓フレーム数
};

/// @brief ゲームプレイメトリクス収集・異常検出
/// @details メトリクスを時系列で記録し、ルールに基づいて異常を検出する。
class GameplayMetrics
{
public:
	/// @brief ルールを追加する
	/// @param rule 追加するルール
	void addRule(MetricRule rule)
	{
		m_rules.push_back(std::move(rule));
	}

	/// @brief メトリクス値を記録する
	/// @param name メトリクス名
	/// @param value 値
	/// @param frame 現在のフレーム番号
	void recordMetric(const std::string& name, float value, std::uint64_t frame)
	{
		auto& history = m_metrics[name];
		history.samples.emplace_back(frame, value);
		if (static_cast<int>(history.samples.size()) > history.maxSamples)
		{
			history.samples.erase(history.samples.begin());
		}
	}

	/// @brief 入力があったことを記録する
	/// @param frame 現在のフレーム番号
	void recordInput(std::uint64_t frame)
	{
		m_lastInputFrame = frame;
		m_hasInput = true;
	}

	/// @brief 全ルールをチェックする
	/// @param currentFrame 現在のフレーム番号
	/// @return 検出された異常のリスト
	[[nodiscard]] std::vector<GameplayAnomaly> check(std::uint64_t currentFrame) const
	{
		std::vector<GameplayAnomaly> anomalies;

		for (const auto& rule : m_rules)
		{
			switch (rule.type)
			{
			case GameplayAnomaly::NoInput:
			{
				if (m_hasInput)
				{
					const auto framesSinceInput = currentFrame - m_lastInputFrame;
					if (framesSinceInput > static_cast<std::uint64_t>(rule.threshold))
					{
						GameplayAnomaly a;
						a.type = GameplayAnomaly::NoInput;
						a.frame = currentFrame;
						a.metricName = rule.name;
						a.value = static_cast<float>(framesSinceInput);
						a.threshold = rule.threshold;
						a.description = "No input for " + std::to_string(framesSinceInput)
							+ " frames (threshold: "
							+ std::to_string(static_cast<int>(rule.threshold)) + ")";
						anomalies.push_back(std::move(a));
					}
				}
				break;
			}
			case GameplayAnomaly::NoMovement:
			{
				auto it = m_metrics.find(rule.name);
				if (it != m_metrics.end())
				{
					const float variance = computeVariance(
						it->second.samples, currentFrame, rule.windowFrames);
					constexpr float epsilon = 1e-6f;
					if (variance < epsilon)
					{
						GameplayAnomaly a;
						a.type = GameplayAnomaly::NoMovement;
						a.frame = currentFrame;
						a.metricName = rule.name;
						a.value = variance;
						a.threshold = epsilon;
						a.description = rule.name + ": no movement detected over "
							+ std::to_string(rule.windowFrames) + " frames";
						anomalies.push_back(std::move(a));
					}
				}
				break;
			}
			case GameplayAnomaly::NegativeValue:
			{
				auto it = m_metrics.find(rule.name);
				if (it != m_metrics.end() && !it->second.samples.empty())
				{
					const float current = it->second.samples.back().second;
					if (current < 0.0f)
					{
						GameplayAnomaly a;
						a.type = GameplayAnomaly::NegativeValue;
						a.frame = currentFrame;
						a.metricName = rule.name;
						a.value = current;
						a.threshold = 0.0f;
						a.description = rule.name + ": negative value "
							+ std::to_string(current);
						anomalies.push_back(std::move(a));
					}
				}
				break;
			}
			case GameplayAnomaly::Stagnation:
			{
				auto it = m_metrics.find(rule.name);
				if (it != m_metrics.end() && it->second.samples.size() >= 2)
				{
					const auto staleFrames = computeStaleFrames(
						it->second.samples, currentFrame);
					if (staleFrames > static_cast<std::uint64_t>(rule.windowFrames))
					{
						GameplayAnomaly a;
						a.type = GameplayAnomaly::Stagnation;
						a.frame = currentFrame;
						a.metricName = rule.name;
						a.value = static_cast<float>(staleFrames);
						a.threshold = static_cast<float>(rule.windowFrames);
						a.description = rule.name + ": stagnant for "
							+ std::to_string(staleFrames) + " frames (threshold: "
							+ std::to_string(rule.windowFrames) + ")";
						anomalies.push_back(std::move(a));
					}
				}
				break;
			}
			case GameplayAnomaly::InvalidState:
			{
				// InvalidStateはアプリケーション固有の検証で使用する
				break;
			}
			}
		}

		return anomalies;
	}

	/// @brief 登録されているルール数を返す
	/// @return ルール数
	[[nodiscard]] std::size_t ruleCount() const noexcept
	{
		return m_rules.size();
	}

	/// @brief 全データをクリアする
	void clear()
	{
		m_rules.clear();
		m_metrics.clear();
		m_lastInputFrame = 0;
		m_hasInput = false;
	}

	/// @brief 異常リストをJSON配列に変換する
	/// @param anomalies 異常リスト
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson(const std::vector<GameplayAnomaly>& anomalies) const
	{
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < anomalies.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += anomalies[i].toJson();
		}
		json += "]";
		return json;
	}

private:
	/// @brief メトリクス履歴の内部構造
	struct MetricHistory
	{
		std::vector<std::pair<std::uint64_t, float>> samples;  ///< (フレーム, 値)
		int maxSamples = 300;                                   ///< 最大サンプル数
	};

	/// @brief 指定窓内のサンプルの分散を計算する
	/// @param samples サンプルリスト
	/// @param currentFrame 現在のフレーム
	/// @param windowFrames 窓のフレーム数
	/// @return 分散値
	[[nodiscard]] static float computeVariance(
		const std::vector<std::pair<std::uint64_t, float>>& samples,
		std::uint64_t currentFrame,
		int windowFrames)
	{
		const std::uint64_t startFrame = (currentFrame > static_cast<std::uint64_t>(windowFrames))
			? currentFrame - static_cast<std::uint64_t>(windowFrames)
			: 0;

		float sum = 0.0f;
		float sumSq = 0.0f;
		int count = 0;

		for (const auto& [frame, value] : samples)
		{
			if (frame >= startFrame)
			{
				sum += value;
				sumSq += value * value;
				++count;
			}
		}

		if (count < 2)
		{
			return 0.0f;
		}

		const float mean = sum / static_cast<float>(count);
		return (sumSq / static_cast<float>(count)) - (mean * mean);
	}

	/// @brief 値が変化していないフレーム数を計算する
	/// @param samples サンプルリスト
	/// @param currentFrame 現在のフレーム
	/// @return 停滞フレーム数
	[[nodiscard]] static std::uint64_t computeStaleFrames(
		const std::vector<std::pair<std::uint64_t, float>>& samples,
		std::uint64_t currentFrame)
	{
		if (samples.empty())
		{
			return 0;
		}

		const float currentValue = samples.back().second;
		std::uint64_t lastChangeFrame = samples.back().first;

		for (int i = static_cast<int>(samples.size()) - 2; i >= 0; --i)
		{
			if (samples[i].second != currentValue)
			{
				lastChangeFrame = samples[i + 1].first;
				break;
			}
			if (i == 0)
			{
				lastChangeFrame = samples[0].first;
			}
		}

		return currentFrame - lastChangeFrame;
	}

	std::vector<MetricRule> m_rules;              ///< 登録済みルール
	std::map<std::string, MetricHistory> m_metrics;  ///< メトリクス履歴
	std::uint64_t m_lastInputFrame = 0;           ///< 最後に入力があったフレーム
	bool m_hasInput = false;                      ///< 入力が記録されたか
};

} // namespace mitiru::validate
