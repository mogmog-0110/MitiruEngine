#pragma once

/// @file UISlider.hpp
/// @brief 数値を選択するための drag 可能な slider widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief slider や bar widget の向き。
enum class Orientation : std::uint8_t
{
	Horizontal,
	Vertical
};

/// @brief UISlider 生成用の設定。
struct UISliderConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float min = 0.0f;
	float max = 1.0f;
	float value = 0.0f;
	float step = 0.0f;             ///< 0 = 連続値 (snap なし)。
	Orientation orientation = Orientation::Horizontal;
	float trackLength = 200.0f;
	float trackThickness = 8.0f;
	float handleSize = 16.0f;
};

/// @brief drag で値を変える logic を備え UINode をラップする slider widget。
///
/// pointer drag、keyboard での増減、step snap を扱う。
/// 値は renderer がアクセスできるよう UINode の value/maxValue field に格納される。
///
/// @code
///   UISliderConfig cfg;
///   cfg.id = 20;
///   cfg.min = 0.0f;
///   cfg.max = 100.0f;
///   cfg.step = 5.0f;
///   UISlider slider(cfg);
///
///   slider.setOnValueChanged([](float v) { /* use v */ });
///   slider.onDragUpdate(0.45f);  // normalized 0..1 along track
/// @endcode
class UISlider
{
	std::shared_ptr<UINode> m_node;
	float m_min;
	float m_max;
	float m_value;
	float m_step;
	Orientation m_orientation;
	float m_handleSize;
	bool m_dragging = false;
	bool m_enabled = true;
	std::function<void(float)> m_onValueChanged;

public:
	/// @brief 設定から slider を構築する。
	/// @param config slider の設定。
	explicit UISlider(const UISliderConfig& config)
		: m_min(config.min)
		, m_max(config.max)
		, m_value(std::clamp(config.value, config.min, config.max))
		, m_step(config.step)
		, m_orientation(config.orientation)
		, m_handleSize(config.handleSize)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Slider;
		data.value = m_value;
		data.maxValue = m_max;
		data.properties["widget_type"] = "slider";
		data.properties["min"] = std::to_string(m_min);
		data.properties["orientation"] = (m_orientation == Orientation::Horizontal) ? "horizontal" : "vertical";
		data.properties["handle_size"] = std::to_string(m_handleSize);
		data.properties["step"] = std::to_string(m_step);

		if (m_orientation == Orientation::Horizontal)
		{
			data.bounds = sgc::Rectf(0.0f, 0.0f, config.trackLength, config.trackThickness);
		}
		else
		{
			data.bounds = sgc::Rectf(0.0f, 0.0f, config.trackThickness, config.trackLength);
		}

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の値を取得する。
	[[nodiscard]] float value() const noexcept { return m_value; }

	/// @brief 最小値を取得する。
	[[nodiscard]] float min() const noexcept { return m_min; }

	/// @brief 最大値を取得する。
	[[nodiscard]] float max() const noexcept { return m_max; }

	/// @brief 正規化された値 (0..1) を取得する。
	[[nodiscard]] float normalizedValue() const noexcept
	{
		if (m_max <= m_min) { return 0.0f; }
		return (m_value - m_min) / (m_max - m_min);
	}

	/// @brief 現在 drag 中か判定する。
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	// ── 設定 ────────────────────────────────────────

	/// @brief 値変更時の callback を設定する。
	/// @param callback 値が変わったときに呼ばれる関数。
	void setOnValueChanged(std::function<void(float)> callback) { m_onValueChanged = std::move(callback); }

	/// @brief 値をプログラム的に設定する。
	/// @param val 新しい値 ([min, max] に clamp され step に snap される)。
	void setValue(float val)
	{
		const float snapped = snapToStep(std::clamp(val, m_min, m_max));
		if (snapped != m_value)
		{
			m_value = snapped;
			syncNodeState();
			if (m_onValueChanged) { m_onValueChanged(m_value); }
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
		setValue(m_value);  // 再 clamp
	}

	/// @brief 有効状態を設定する。
	/// @param enabled 操作を有効にするなら true。
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}

	// ── 操作 ──────────────────────────────────────────

	/// @brief slider 上で drag が始まったときに呼ばれる。
	void onDragBegin()
	{
		if (!m_enabled) { return; }
		m_dragging = true;
		m_node->setProperty("dragging", "true");
	}

	/// @brief drag 中に track 上の正規化位置 (0..1) で呼ばれる。
	/// @param normalizedPos track 上の位置。0 = min 端、1 = max 端。
	void onDragUpdate(float normalizedPos)
	{
		if (!m_enabled || !m_dragging) { return; }
		const float clamped = std::clamp(normalizedPos, 0.0f, 1.0f);
		setValue(m_min + clamped * (m_max - m_min));
	}

	/// @brief drag が終わったときに呼ばれる。
	void onDragEnd()
	{
		m_dragging = false;
		m_node->setProperty("dragging", "false");
	}

	/// @brief 値を 1 step 増やす (step 未設定なら range の 1%)。
	void increment()
	{
		if (!m_enabled) { return; }
		const float delta = (m_step > 0.0f) ? m_step : (m_max - m_min) * 0.01f;
		setValue(m_value + delta);
	}

	/// @brief 値を 1 step 減らす (step 未設定なら range の 1%)。
	void decrement()
	{
		if (!m_enabled) { return; }
		const float delta = (m_step > 0.0f) ? m_step : (m_max - m_min) * 0.01f;
		setValue(m_value - delta);
	}

private:
	/// @brief step > 0 のとき、値を最も近い step に snap する。
	[[nodiscard]] float snapToStep(float val) const noexcept
	{
		if (m_step <= 0.0f) { return val; }
		return m_min + std::round((val - m_min) / m_step) * m_step;
	}

	/// @brief state を UINode に同期する。
	void syncNodeState()
	{
		m_node->setValue(m_value);
		m_node->setProperty("normalized", std::to_string(normalizedValue()));
	}
};

} // namespace mitiru::ui
