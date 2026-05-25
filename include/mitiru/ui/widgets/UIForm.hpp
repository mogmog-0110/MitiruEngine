#pragma once

/// @file UIForm.hpp
/// @brief ラベル付き入力、validation、submit ロジックを備えた form 構成 helper。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <variant>
#include <vector>

namespace mitiru::ui {

/// @brief 対応する form field の種別。
enum class UIFormFieldType : std::uint8_t
{
	Text,
	Number,
	Password,
	Toggle,
	Slider,
	Dropdown,
	RadioGroup
};

/// @brief field ラベルの揃え方。
enum class LabelAlign : std::uint8_t
{
	Left,
	Right
};

/// @brief 単一 field の validation 結果。
struct UIFormError
{
	std::string key;
	std::string message;
};

/// @brief validation 関数型: 値を受け取り、有効なら空文字列を返す。
using FormValidationFn = std::function<std::string(const std::string&)>;

/// @brief 単一 form field の定義。
struct UIFormField
{
	std::string label;
	UIFormFieldType fieldType = UIFormFieldType::Text;
	std::string key;
	bool required = false;
	std::string placeholder;
	/// @brief validation: regex pattern 文字列 or callback。空 = validation なし。
	std::string validationRegex;
	FormValidationFn validationCallback;
	std::string errorMessage;
	std::string defaultValue;
	/// @brief Dropdown / RadioGroup field の選択肢。
	std::vector<std::string> options;
	/// @brief Slider field の min / max。
	float sliderMin = 0.0f;
	float sliderMax = 1.0f;
};

/// @brief UIForm 生成用の設定。
struct UIFormConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIFormField> fields;
	float labelWidth = 120.0f;
	float fieldWidth = 200.0f;
	float rowSpacing = 8.0f;
	LabelAlign labelAlign = LabelAlign::Left;
	std::string submitLabel = "Submit";
	std::string cancelLabel = "Cancel";
	std::string backgroundImageKey;
	sgc::Colorf errorColor{1.0f, 0.3f, 0.3f, 1.0f};
	std::string requiredMarker = "*";
};

/// @brief ラベル付き入力と validation を備えた form 構成 widget。
///
/// 名前付き field の集合を、リアルタイム validation、tab 移動、
/// submit / cancel 操作とともに管理する。
///
/// @code
///   UIFormConfig cfg;
///   cfg.id = 400;
///   cfg.fields = {
///       { "Name", UIFormFieldType::Text, "name", true, "Enter name" },
///       { "Email", UIFormFieldType::Text, "email", true, "", "^.+@.+$", {}, "Invalid email" },
///       { "Age", UIFormFieldType::Number, "age" },
///       { "Agree", UIFormFieldType::Toggle, "agree", true }
///   };
///   UIForm form(cfg);
///
///   form.setOnSubmit([](const auto& values) { /* process */ });
///   form.setValue("name", "Alice");
///   auto errors = form.validate();
/// @endcode
class UIForm
{
	std::shared_ptr<UINode> m_node;
	UIFormConfig m_config;

	/// @brief field key で引く現在値。
	std::map<std::string, std::string> m_values;

	/// @brief field key で引く現在の validation error。
	std::map<std::string, std::string> m_errors;

	/// @brief 現在 focus 中の field の index、無ければ -1。
	std::int32_t m_focusedFieldIndex = -1;

	std::function<void(const std::map<std::string, std::string>&)> m_onSubmit;
	std::function<void()> m_onCancel;
	std::function<void(const std::string&, const std::string&)> m_onValueChanged;

public:
	/// @brief 設定から form を構築する。
	/// @param config form の設定。
	explicit UIForm(const UIFormConfig& config)
		: m_config(config)
	{
		// default 値で初期化する
		for (const auto& field : config.fields)
		{
			m_values[field.key] = field.defaultValue;
		}

		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.bounds = sgc::Rectf(0.0f, 0.0f, totalWidth(), totalHeight());
		data.properties["widget_type"] = "form";
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["label_width"] = std::to_string(config.labelWidth);
		data.properties["field_width"] = std::to_string(config.fieldWidth);
		data.properties["row_spacing"] = std::to_string(config.rowSpacing);
		data.properties["label_align"] = (config.labelAlign == LabelAlign::Left) ? "left" : "right";
		data.properties["submit_label"] = config.submitLabel;
		data.properties["cancel_label"] = config.cancelLabel;
		data.properties["required_marker"] = config.requiredMarker;

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief 基底の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief form の総幅を取得する。
	[[nodiscard]] float totalWidth() const noexcept
	{
		return m_config.labelWidth + m_config.fieldWidth + 16.0f; // 余白
	}

	/// @brief form の総高さを取得する。
	[[nodiscard]] float totalHeight() const noexcept
	{
		const float fieldHeight = 28.0f; // 標準の行高さ
		const float rows = static_cast<float>(m_config.fields.size());
		const float buttonsRow = fieldHeight + m_config.rowSpacing;
		return rows * (fieldHeight + m_config.rowSpacing) + buttonsRow;
	}

	/// @brief field 定義を取得する。
	[[nodiscard]] const std::vector<UIFormField>& fields() const noexcept
	{
		return m_config.fields;
	}

	/// @brief 現在 focus 中の field の index を取得する、無ければ -1。
	[[nodiscard]] std::int32_t focusedFieldIndex() const noexcept { return m_focusedFieldIndex; }

	/// @brief field の現在の error メッセージを取得する、error が無ければ空。
	[[nodiscard]] std::string fieldError(const std::string& key) const
	{
		const auto it = m_errors.find(key);
		return (it != m_errors.end()) ? it->second : std::string{};
	}

	/// @brief 現在の全値を取得する。
	[[nodiscard]] const std::map<std::string, std::string>& values() const noexcept
	{
		return m_values;
	}

	// ── 値の管理 ─────────────────────────────────────

	/// @brief key で field の値を設定する。
	/// @param key field key。
	/// @param value 新しい値。
	void setValue(const std::string& key, const std::string& value)
	{
		m_values[key] = value;
		validateField(key);
		syncFieldProperty(key);
		if (m_onValueChanged) { m_onValueChanged(key, value); }
	}

	/// @brief key で field の値を取得する。
	/// @param key field key。
	/// @return 現在値、key が見つからなければ空文字列。
	[[nodiscard]] std::string getValue(const std::string& key) const
	{
		const auto it = m_values.find(key);
		return (it != m_values.end()) ? it->second : std::string{};
	}

	/// @brief 全 field を validation し error を返す。
	/// @return validation error の vector (全て有効なら空)。
	[[nodiscard]] std::vector<UIFormError> validate()
	{
		m_errors.clear();
		std::vector<UIFormError> errors;

		for (const auto& field : m_config.fields)
		{
			const auto& value = m_values[field.key];
			const auto msg = validateSingleField(field, value);
			if (!msg.empty())
			{
				m_errors[field.key] = msg;
				errors.push_back({field.key, msg});
			}
		}

		syncNodeState();
		return errors;
	}

	/// @brief form が現在有効か確認する (全 field が validation を通過)。
	[[nodiscard]] bool isValid()
	{
		const auto errors = validate();
		return errors.empty();
	}

	/// @brief 全 field を default 値にリセットし error を消す。
	void reset()
	{
		m_errors.clear();
		for (const auto& field : m_config.fields)
		{
			m_values[field.key] = field.defaultValue;
		}
		m_focusedFieldIndex = -1;
		syncNodeState();
	}

	// ── 操作 ──────────────────────────────────────────────

	/// @brief form の submit を試みる。まず validation する。
	/// @return submit された (全 field 有効) なら true、それ以外は false。
	bool submit()
	{
		if (!isValid()) { return false; }
		if (m_onSubmit) { m_onSubmit(m_values); }
		return true;
	}

	/// @brief form を cancel する。
	void cancel()
	{
		if (m_onCancel) { m_onCancel(); }
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief submit 成功時に呼ばれる callback を設定する。
	void setOnSubmit(std::function<void(const std::map<std::string, std::string>&)> callback)
	{
		m_onSubmit = std::move(callback);
	}

	/// @brief cancel 時に呼ばれる callback を設定する。
	void setOnCancel(std::function<void()> callback) { m_onCancel = std::move(callback); }

	/// @brief いずれかの field 値が変わったとき呼ばれる callback を設定する。
	void setOnValueChanged(std::function<void(const std::string&, const std::string&)> callback)
	{
		m_onValueChanged = std::move(callback);
	}

	// ── 移動 (event system から呼ばれる) ──────────────────

	/// @brief index で特定の field を focus する。
	/// @param index field index。
	void focusField(std::int32_t index)
	{
		if (index < 0 || index >= static_cast<std::int32_t>(m_config.fields.size()))
		{
			m_focusedFieldIndex = -1;
		}
		else
		{
			m_focusedFieldIndex = index;
		}
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
	}

	/// @brief 次の field へ focus を移す (Tab キー)。
	void focusNext()
	{
		const auto count = static_cast<std::int32_t>(m_config.fields.size());
		if (count == 0) { return; }
		m_focusedFieldIndex = (m_focusedFieldIndex + 1) % count;
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
	}

	/// @brief 前の field へ focus を移す (Shift+Tab)。
	void focusPrevious()
	{
		const auto count = static_cast<std::int32_t>(m_config.fields.size());
		if (count == 0) { return; }
		m_focusedFieldIndex = (m_focusedFieldIndex - 1 + count) % count;
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
	}

	/// @brief Enter キー押下を処理する (有効なら submit)。
	void onEnterPressed()
	{
		submit();
	}

	/// @brief Tab キー押下を処理する。
	/// @param shiftHeld Shift が押されていれば true。
	void onTabPressed(bool shiftHeld)
	{
		if (shiftHeld) { focusPrevious(); } else { focusNext(); }
	}

private:
	/// @brief key で単一 field を validation する。
	void validateField(const std::string& key)
	{
		const auto* field = findField(key);
		if (field == nullptr) { return; }

		const auto& value = m_values[key];
		const auto msg = validateSingleField(*field, value);

		if (msg.empty())
		{
			m_errors.erase(key);
		}
		else
		{
			m_errors[key] = msg;
		}
	}

	/// @brief 1 つの field を validation し error メッセージを返す (空 = 有効)。
	[[nodiscard]] std::string validateSingleField(const UIFormField& field, const std::string& value) const
	{
		// 必須チェック
		if (field.required && value.empty())
		{
			return field.errorMessage.empty()
				? (field.label + " is required")
				: field.errorMessage;
		}

		// 空の任意 field はそれ以上の validation を飛ばす
		if (value.empty()) { return {}; }

		// 数値型チェック
		if (field.fieldType == UIFormFieldType::Number)
		{
			try
			{
				[[maybe_unused]] auto unused = std::stof(value);
			}
			catch (...)
			{
				return field.errorMessage.empty()
					? (field.label + " must be a number")
					: field.errorMessage;
			}
		}

		// regex validation
		if (!field.validationRegex.empty())
		{
			try
			{
				const std::regex pattern(field.validationRegex);
				if (!std::regex_match(value, pattern))
				{
					return field.errorMessage.empty()
						? (field.label + " is invalid")
						: field.errorMessage;
				}
			}
			catch (const std::regex_error&)
			{
				return "Invalid validation pattern for " + field.label;
			}
		}

		// callback validation
		if (field.validationCallback)
		{
			return field.validationCallback(value);
		}

		return {};
	}

	/// @brief key で field 定義を探す。
	[[nodiscard]] const UIFormField* findField(const std::string& key) const
	{
		for (const auto& f : m_config.fields)
		{
			if (f.key == key) { return &f; }
		}
		return nullptr;
	}

	/// @brief 単一 field の状態を node properties に同期する。
	void syncFieldProperty(const std::string& key)
	{
		const auto prefix = "field_" + key + "_";
		m_node->setProperty(prefix + "value", m_values[key]);
		const auto errIt = m_errors.find(key);
		m_node->setProperty(prefix + "error", (errIt != m_errors.end()) ? errIt->second : "");
	}

	/// @brief 全体の状態を UINode に同期する。
	void syncNodeState()
	{
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
		m_node->setProperty("is_valid", m_errors.empty() ? "true" : "false");

		for (const auto& field : m_config.fields)
		{
			syncFieldProperty(field.key);

			const auto prefix = "field_" + field.key + "_";
			m_node->setProperty(prefix + "label", field.label);
			m_node->setProperty(prefix + "type", fieldTypeToString(field.fieldType));
			m_node->setProperty(prefix + "required", field.required ? "true" : "false");
			m_node->setProperty(prefix + "placeholder", field.placeholder);

			// dropdown / radio 用に options を serialize する
			if (!field.options.empty())
			{
				std::string joined;
				for (std::size_t i = 0; i < field.options.size(); ++i)
				{
					if (i > 0) { joined += '|'; }
					joined += field.options[i];
				}
				m_node->setProperty(prefix + "options", joined);
			}
		}
	}

	/// @brief field type を文字列に変換する。
	[[nodiscard]] static const char* fieldTypeToString(UIFormFieldType t) noexcept
	{
		switch (t)
		{
		case UIFormFieldType::Number:     return "number";
		case UIFormFieldType::Password:   return "password";
		case UIFormFieldType::Toggle:     return "toggle";
		case UIFormFieldType::Slider:     return "slider";
		case UIFormFieldType::Dropdown:   return "dropdown";
		case UIFormFieldType::RadioGroup: return "radio_group";
		default:                          return "text";
		}
	}
};

} // namespace mitiru::ui
