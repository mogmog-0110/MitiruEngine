#pragma once

/// @file UIProgressBar.hpp
/// @brief progress bar widget。アニメーション fill、ラベル整形、不定 (indeterminate) mode 対応。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief progress bar の表示テキストのラベル書式。
enum class ProgressLabelFormat : std::uint8_t
{
	None,           ///< ラベル無し。
	ValueSlashMax,  ///< "75/100"
	Percent         ///< "75%"
};

/// @brief UIProgressBar 生成用の設定。
struct UIProgressBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float min = 0.0f;
	float max = 100.0f;
	float value = 0.0f;
	bool showLabel = true;
	ProgressLabelFormat labelFormat = ProgressLabelFormat::Percent;
	bool animated = true;         ///< 値の遷移を滑らかにする。
	bool indeterminate = false;   ///< 往復する bar mode (value を無視)。
	float width = 200.0f;
	float height = 20.0f;
};

/// @brief UINode をラップし、アニメーション fill とラベルロジックを持つ progress bar widget。
///
/// 値の表示、update() による滑らかなアニメーション、不定 (往復) mode を管理する。
/// 描画に必要な state はすべて UINode の value/maxValue と properties に
/// エンコードされる。
///
/// @code
///   UIProgressBarConfig cfg;
///   cfg.id = 80;
///   cfg.max = 100.0f;
///   cfg.value = 30.0f;
///   cfg.labelFormat = ProgressLabelFormat::Percent;
///   UIProgressBar bar(cfg);
///
///   bar.setValue(75.0f);
///   bar.update(0.016f);  // smooth animation step
/// @endcode
class UIProgressBar
{
	std::shared_ptr<UINode> m_node;
	float m_min;
	float m_max;
	float m_targetValue;
	float m_displayValue;
	bool m_showLabel;
	ProgressLabelFormat m_labelFormat;
	bool m_animated;
	bool m_indeterminate;
	float m_indeterminatePhase = 0.0f;
	float m_animationSpeed = 5.0f;  ///< 滑らかな遷移の秒あたり単位数。

public:
	/// @brief 設定から progress bar を構築する。
	/// @param config progress bar の設定。
	explicit UIProgressBar(const UIProgressBarConfig& config)
		: m_min(config.min)
		, m_max(config.max)
		, m_targetValue(std::clamp(config.value, config.min, config.max))
		, m_displayValue(m_targetValue)
		, m_showLabel(config.showLabel)
		, m_labelFormat(config.labelFormat)
		, m_animated(config.animated)
		, m_indeterminate(config.indeterminate)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::ProgressBar;
		data.value = m_displayValue;
		data.maxValue = m_max;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "progress_bar";
		data.properties["min"] = std::to_string(m_min);
		data.properties["indeterminate"] = config.indeterminate ? "true" : "false";
		data.properties["animated"] = config.animated ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 基盤となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief target 値 (アニメーションの到達先) を取得する。
	[[nodiscard]] float value() const noexcept { return m_targetValue; }

	/// @brief 現在の表示値を取得する (アニメーション中は target より遅れる場合あり)。
	[[nodiscard]] float displayValue() const noexcept { return m_displayValue; }

	/// @brief 正規化された進捗 (0..1) を取得する。
	[[nodiscard]] float normalizedValue() const noexcept
	{
		if (m_max <= m_min) { return 0.0f; }
		return (m_displayValue - m_min) / (m_max - m_min);
	}

	/// @brief 不定 (indeterminate) mode かどうかを確認する。
	[[nodiscard]] bool isIndeterminate() const noexcept { return m_indeterminate; }

	/// @brief 整形済みのラベル文字列を取得する。
	[[nodiscard]] std::string labelText() const
	{
		if (!m_showLabel || m_indeterminate) { return {}; }

		switch (m_labelFormat)
		{
		case ProgressLabelFormat::ValueSlashMax:
		{
			const int v = static_cast<int>(std::round(m_displayValue));
			const int mx = static_cast<int>(std::round(m_max));
			return std::to_string(v) + "/" + std::to_string(mx);
		}
		case ProgressLabelFormat::Percent:
		{
			const int pct = static_cast<int>(std::round(normalizedValue() * 100.0f));
			return std::to_string(pct) + "%";
		}
		case ProgressLabelFormat::None:
		default:
			return {};
		}
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief target 値を設定する。
	/// @param val 新しい target 値 ([min, max] に clamp される)。
	void setValue(float val)
	{
		m_targetValue = std::clamp(val, m_min, m_max);
		if (!m_animated)
		{
			m_displayValue = m_targetValue;
			syncNodeState();
		}
	}

	/// @brief 値の範囲を設定する。
	/// @param minVal 最小値。
	/// @param maxVal 最大値。
	void setRange(float minVal, float maxVal)
	{
		m_min = minVal;
		m_max = maxVal;
		m_node->setProperty("min", std::to_string(m_min));
		m_targetValue = std::clamp(m_targetValue, m_min, m_max);
		m_displayValue = std::clamp(m_displayValue, m_min, m_max);
		syncNodeState();
	}

	/// @brief アニメーション速度を設定する。
	/// @param unitsPerSecond 表示値が target に追いつく速さ。
	void setAnimationSpeed(float unitsPerSecond) { m_animationSpeed = unitsPerSecond; }

	/// @brief 不定 (indeterminate) mode を有効/無効にする。
	/// @param indeterminate 往復する bar mode にする場合 true。
	void setIndeterminate(bool indeterminate)
	{
		m_indeterminate = indeterminate;
		m_node->setProperty("indeterminate", m_indeterminate ? "true" : "false");
		syncNodeState();
	}

	/// @brief ラベル書式を設定する。
	/// @param format ラベルの表示書式。
	void setLabelFormat(ProgressLabelFormat format)
	{
		m_labelFormat = format;
		syncNodeState();
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief アニメーションを 1 frame 進める。
	/// @param deltaTime 前 frame からの経過秒数。
	void update(float deltaTime)
	{
		if (m_indeterminate)
		{
			m_indeterminatePhase += deltaTime * 2.0f;
			if (m_indeterminatePhase > 2.0f) { m_indeterminatePhase -= 2.0f; }
			m_node->setProperty("indeterminate_phase", std::to_string(m_indeterminatePhase));
			return;
		}

		if (m_animated && m_displayValue != m_targetValue)
		{
			const float diff = m_targetValue - m_displayValue;
			const float step = m_animationSpeed * deltaTime;

			if (std::abs(diff) <= step)
			{
				m_displayValue = m_targetValue;
			}
			else
			{
				m_displayValue += (diff > 0.0f ? step : -step);
			}
			syncNodeState();
		}
	}

private:
	/// @brief state を UINode に同期する。
	void syncNodeState()
	{
		m_node->setValue(m_displayValue);
		m_node->setProperty("normalized", std::to_string(normalizedValue()));
		m_node->setText(labelText());
	}
};

} // namespace mitiru::ui
