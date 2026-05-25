#pragma once

/// @file UINumberSpinner.hpp
/// @brief 増減ボタンと auto-repeat を備えた数値 input widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace mitiru::ui {

/// @brief UINumberSpinner 生成用の設定。
struct UINumberSpinnerConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float value = 0.0f;                    ///< 初期値。
	float min = 0.0f;                      ///< 最小値。
	float max = 100.0f;                    ///< 最大値。
	float step = 1.0f;                     ///< 増減の step。
	int decimalPlaces = 0;                 ///< 表示の小数桁数。
	float width = 160.0f;                  ///< widget 全体の幅。
	float height = 32.0f;                  ///< widget 全体の高さ。
	float buttonWidth = 32.0f;             ///< +/- ボタンの幅。
	float inputWidth = 96.0f;              ///< 数値表示領域の幅。
	std::string incrementImageKey;         ///< increment ボタンの画像。
	std::string decrementImageKey;         ///< decrement ボタンの画像。
	std::string inputBackgroundImageKey;   ///< 数値 input 背景の画像。
	float fontSize = 14.0f;                ///< 数値表示のフォントサイズ。
	float holdRepeatDelay = 0.5f;          ///< auto-repeat 開始までの秒数。
	float holdRepeatRate = 0.1f;           ///< auto-repeat の tick 間隔 (秒)。
};

/// @brief +/- ボタンと長押しリピートを備えた数値 spinner widget。
///
/// 増減ボタン・直接のテキスト入力・ボタン長押し時の auto-repeat を提供する。
/// 値は [min, max] に clamp され、設定された step に snap される。
///
/// @code
///   UINumberSpinnerConfig cfg;
///   cfg.id = 110;
///   cfg.value = 50.0f;
///   cfg.min = 0.0f;
///   cfg.max = 100.0f;
///   cfg.step = 5.0f;
///   UINumberSpinner spinner(cfg);
///
///   spinner.setOnValueChanged([](float v) { /* use v */ });
///   spinner.increment();
///   spinner.update(0.016f);
/// @endcode
class UINumberSpinner
{
	std::shared_ptr<UINode> m_node;
	float m_value;
	float m_min;
	float m_max;
	float m_step;
	int m_decimalPlaces;
	float m_holdRepeatDelay;
	float m_holdRepeatRate;
	bool m_editing = false;
	std::string m_editBuffer;

	// 長押しリピートの state。
	enum class HoldDirection : std::uint8_t { None, Increment, Decrement };
	HoldDirection m_holdDirection = HoldDirection::None;
	float m_holdTimer = 0.0f;
	bool m_holdRepeating = false;

	std::function<void(float)> m_onValueChanged;

public:
	/// @brief 設定から number spinner を構築する。
	/// @param config spinner の設定。
	explicit UINumberSpinner(const UINumberSpinnerConfig& config)
		: m_value(std::clamp(config.value, config.min, config.max))
		, m_min(config.min)
		, m_max(config.max)
		, m_step(config.step)
		, m_decimalPlaces(config.decimalPlaces)
		, m_holdRepeatDelay(config.holdRepeatDelay)
		, m_holdRepeatRate(config.holdRepeatRate)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.value = m_value;
		data.maxValue = m_max;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "number_spinner";
		data.properties["min"] = std::to_string(m_min);
		data.properties["max"] = std::to_string(m_max);
		data.properties["step"] = std::to_string(m_step);
		data.properties["decimal_places"] = std::to_string(m_decimalPlaces);
		data.properties["button_width"] = std::to_string(config.buttonWidth);
		data.properties["input_width"] = std::to_string(config.inputWidth);
		data.properties["increment_image"] = config.incrementImageKey;
		data.properties["decrement_image"] = config.decrementImageKey;
		data.properties["input_bg_image"] = config.inputBackgroundImageKey;
		data.properties["font_size"] = std::to_string(config.fontSize);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の値を取得する。
	[[nodiscard]] float getValue() const noexcept { return m_value; }

	/// @brief 最小値を取得する。
	[[nodiscard]] float getMin() const noexcept { return m_min; }

	/// @brief 最大値を取得する。
	[[nodiscard]] float getMax() const noexcept { return m_max; }

	/// @brief 現在テキスト編集 mode かどうか判定する。
	[[nodiscard]] bool isEditing() const noexcept { return m_editing; }

	// ── Configuration ────────────────────────────────────────

	/// @brief 値変更時の callback を設定する。
	/// @param callback 値が変わったときに呼ばれる関数。
	void setOnValueChanged(std::function<void(float)> callback) { m_onValueChanged = std::move(callback); }

	/// @brief プログラムから値を設定する。
	/// @param val 新しい値 (clamp と snap が行われる)。
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
		m_node->setProperty("max", std::to_string(m_max));
		setValue(m_value); // 再 clamp
	}

	/// @brief step サイズを設定する。
	/// @param step 新しい step 値。
	void setStep(float step)
	{
		m_step = step;
		m_node->setProperty("step", std::to_string(m_step));
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief 値を 1 step 増やす。
	void increment()
	{
		setValue(m_value + m_step);
	}

	/// @brief 値を 1 step 減らす。
	void decrement()
	{
		setValue(m_value - m_step);
	}

	/// @brief increment ボタンの長押しを開始する。
	void beginHoldIncrement()
	{
		m_holdDirection = HoldDirection::Increment;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
		increment();
	}

	/// @brief decrement ボタンの長押しを開始する。
	void beginHoldDecrement()
	{
		m_holdDirection = HoldDirection::Decrement;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
		decrement();
	}

	/// @brief 長押し中のボタンを離す。
	void endHold()
	{
		m_holdDirection = HoldDirection::None;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
	}

	/// @brief auto-repeat ロジックを更新する。毎フレーム呼ぶ。
	/// @param dt 経過時間 (秒)。
	void update(float dt)
	{
		if (m_holdDirection == HoldDirection::None) { return; }

		m_holdTimer += dt;

		if (!m_holdRepeating)
		{
			if (m_holdTimer >= m_holdRepeatDelay)
			{
				m_holdRepeating = true;
				m_holdTimer -= m_holdRepeatDelay;
			}
		}

		if (m_holdRepeating)
		{
			while (m_holdTimer >= m_holdRepeatRate)
			{
				m_holdTimer -= m_holdRepeatRate;
				if (m_holdDirection == HoldDirection::Increment)
				{
					increment();
				}
				else
				{
					decrement();
				}
			}
		}
	}

	// ── Text Editing ─────────────────────────────────────────

	/// @brief 数値フィールドの直接テキスト編集を開始する。
	void beginEditing()
	{
		m_editing = true;
		m_editBuffer = formatValue(m_value);
		m_node->setProperty("editing", "true");
		m_node->setProperty("edit_buffer", m_editBuffer);
	}

	/// @brief edit buffer に文字を追加する。
	/// @param ch 追加する文字 (数字、'.'、'-')。
	void editAppendChar(char ch)
	{
		if (!m_editing) { return; }

		// 数字・小数点・マイナス記号を許可する。
		const bool isDigit = (ch >= '0' && ch <= '9');
		const bool isDot = (ch == '.' && m_decimalPlaces > 0 && m_editBuffer.find('.') == std::string::npos);
		const bool isMinus = (ch == '-' && m_editBuffer.empty() && m_min < 0.0f);

		if (isDigit || isDot || isMinus)
		{
			m_editBuffer += ch;
			m_node->setProperty("edit_buffer", m_editBuffer);
		}
	}

	/// @brief edit buffer の末尾の文字を削除する。
	void editBackspace()
	{
		if (!m_editing || m_editBuffer.empty()) { return; }
		m_editBuffer.pop_back();
		m_node->setProperty("edit_buffer", m_editBuffer);
	}

	/// @brief テキスト編集を確定し値を適用する。
	void confirmEditing()
	{
		if (!m_editing) { return; }
		m_editing = false;
		m_node->setProperty("editing", "false");

		if (!m_editBuffer.empty())
		{
			try
			{
				const float parsed = std::stof(m_editBuffer);
				setValue(parsed);
			}
			catch (...)
			{
				// 不正な入力。現在値を保持する。
			}
		}
		syncNodeState();
	}

	/// @brief 適用せずにテキスト編集をキャンセルする。
	void cancelEditing()
	{
		m_editing = false;
		m_node->setProperty("editing", "false");
		syncNodeState();
	}

private:
	/// @brief 値を最も近い step に snap する。
	[[nodiscard]] float snapToStep(float val) const noexcept
	{
		if (m_step <= 0.0f) { return val; }
		return m_min + std::round((val - m_min) / m_step) * m_step;
	}

	/// @brief 設定された小数桁数で値を整形する。
	[[nodiscard]] std::string formatValue(float val) const
	{
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(m_decimalPlaces) << val;
		return oss.str();
	}

	/// @brief state を UINode へ同期する。
	void syncNodeState()
	{
		m_node->setValue(m_value);
		m_node->setProperty("display_value", formatValue(m_value));
		m_node->setText(formatValue(m_value));
	}
};

} // namespace mitiru::ui
