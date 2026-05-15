#pragma once

/// @file UIForm.hpp
/// @brief Form composition helper with labeled inputs, validation, and submit logic.

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

/// @brief Supported form field types.
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

/// @brief Alignment for field labels.
enum class LabelAlign : std::uint8_t
{
	Left,
	Right
};

/// @brief Validation result for a single field.
struct UIFormError
{
	std::string key;
	std::string message;
};

/// @brief Validation function type: receives value, returns empty string if valid.
using FormValidationFn = std::function<std::string(const std::string&)>;

/// @brief Definition of a single form field.
struct UIFormField
{
	std::string label;
	UIFormFieldType fieldType = UIFormFieldType::Text;
	std::string key;
	bool required = false;
	std::string placeholder;
	/// @brief Validation: regex pattern string OR callback. Empty = no validation.
	std::string validationRegex;
	FormValidationFn validationCallback;
	std::string errorMessage;
	std::string defaultValue;
	/// @brief Options for Dropdown / RadioGroup fields.
	std::vector<std::string> options;
	/// @brief Min/max for Slider fields.
	float sliderMin = 0.0f;
	float sliderMax = 1.0f;
};

/// @brief Configuration for creating a UIForm.
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

/// @brief Form composition widget with labeled inputs and validation.
///
/// Manages a collection of named fields with real-time validation,
/// tab navigation, and submit/cancel actions.
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

	/// @brief Current values keyed by field key.
	std::map<std::string, std::string> m_values;

	/// @brief Current validation errors keyed by field key.
	std::map<std::string, std::string> m_errors;

	/// @brief Index of the currently focused field, or -1 if none.
	std::int32_t m_focusedFieldIndex = -1;

	std::function<void(const std::map<std::string, std::string>&)> m_onSubmit;
	std::function<void()> m_onCancel;
	std::function<void(const std::string&, const std::string&)> m_onValueChanged;

public:
	/// @brief Construct a form from configuration.
	/// @param config Form configuration.
	explicit UIForm(const UIFormConfig& config)
		: m_config(config)
	{
		// Initialize default values
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get total form width.
	[[nodiscard]] float totalWidth() const noexcept
	{
		return m_config.labelWidth + m_config.fieldWidth + 16.0f; // margin
	}

	/// @brief Get total form height.
	[[nodiscard]] float totalHeight() const noexcept
	{
		const float fieldHeight = 28.0f; // standard row height
		const float rows = static_cast<float>(m_config.fields.size());
		const float buttonsRow = fieldHeight + m_config.rowSpacing;
		return rows * (fieldHeight + m_config.rowSpacing) + buttonsRow;
	}

	/// @brief Get the field definitions.
	[[nodiscard]] const std::vector<UIFormField>& fields() const noexcept
	{
		return m_config.fields;
	}

	/// @brief Get the index of the currently focused field, or -1.
	[[nodiscard]] std::int32_t focusedFieldIndex() const noexcept { return m_focusedFieldIndex; }

	/// @brief Get current error message for a field, empty if no error.
	[[nodiscard]] std::string fieldError(const std::string& key) const
	{
		const auto it = m_errors.find(key);
		return (it != m_errors.end()) ? it->second : std::string{};
	}

	/// @brief Get all current values.
	[[nodiscard]] const std::map<std::string, std::string>& values() const noexcept
	{
		return m_values;
	}

	// ── Value management ─────────────────────────────────────

	/// @brief Set the value of a field by key.
	/// @param key Field key.
	/// @param value New value.
	void setValue(const std::string& key, const std::string& value)
	{
		m_values[key] = value;
		validateField(key);
		syncFieldProperty(key);
		if (m_onValueChanged) { m_onValueChanged(key, value); }
	}

	/// @brief Get the value of a field by key.
	/// @param key Field key.
	/// @return Current value, or empty string if key not found.
	[[nodiscard]] std::string getValue(const std::string& key) const
	{
		const auto it = m_values.find(key);
		return (it != m_values.end()) ? it->second : std::string{};
	}

	/// @brief Validate all fields and return errors.
	/// @return Vector of validation errors (empty if all valid).
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

	/// @brief Check if the form is currently valid (all fields pass validation).
	[[nodiscard]] bool isValid()
	{
		const auto errors = validate();
		return errors.empty();
	}

	/// @brief Reset all fields to their default values and clear errors.
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

	// ── Actions ──────────────────────────────────────────────

	/// @brief Attempt to submit the form. Validates first.
	/// @return True if submitted (all fields valid), false otherwise.
	bool submit()
	{
		if (!isValid()) { return false; }
		if (m_onSubmit) { m_onSubmit(m_values); }
		return true;
	}

	/// @brief Cancel the form.
	void cancel()
	{
		if (m_onCancel) { m_onCancel(); }
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback invoked on successful submit.
	void setOnSubmit(std::function<void(const std::map<std::string, std::string>&)> callback)
	{
		m_onSubmit = std::move(callback);
	}

	/// @brief Set callback invoked on cancel.
	void setOnCancel(std::function<void()> callback) { m_onCancel = std::move(callback); }

	/// @brief Set callback invoked when any field value changes.
	void setOnValueChanged(std::function<void(const std::string&, const std::string&)> callback)
	{
		m_onValueChanged = std::move(callback);
	}

	// ── Navigation (called by event system) ──────────────────

	/// @brief Focus a specific field by index.
	/// @param index Field index.
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

	/// @brief Move focus to the next field (Tab key).
	void focusNext()
	{
		const auto count = static_cast<std::int32_t>(m_config.fields.size());
		if (count == 0) { return; }
		m_focusedFieldIndex = (m_focusedFieldIndex + 1) % count;
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
	}

	/// @brief Move focus to the previous field (Shift+Tab).
	void focusPrevious()
	{
		const auto count = static_cast<std::int32_t>(m_config.fields.size());
		if (count == 0) { return; }
		m_focusedFieldIndex = (m_focusedFieldIndex - 1 + count) % count;
		m_node->setProperty("focused_field", std::to_string(m_focusedFieldIndex));
	}

	/// @brief Handle Enter key press (submit if valid).
	void onEnterPressed()
	{
		submit();
	}

	/// @brief Handle Tab key press.
	/// @param shiftHeld True if Shift is held.
	void onTabPressed(bool shiftHeld)
	{
		if (shiftHeld) { focusPrevious(); } else { focusNext(); }
	}

private:
	/// @brief Validate a single field by key.
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

	/// @brief Run validation for one field and return error message (empty = valid).
	[[nodiscard]] std::string validateSingleField(const UIFormField& field, const std::string& value) const
	{
		// Required check
		if (field.required && value.empty())
		{
			return field.errorMessage.empty()
				? (field.label + " is required")
				: field.errorMessage;
		}

		// Skip further validation on empty optional fields
		if (value.empty()) { return {}; }

		// Number type check
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

		// Regex validation
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

		// Callback validation
		if (field.validationCallback)
		{
			return field.validationCallback(value);
		}

		return {};
	}

	/// @brief Find a field definition by key.
	[[nodiscard]] const UIFormField* findField(const std::string& key) const
	{
		for (const auto& f : m_config.fields)
		{
			if (f.key == key) { return &f; }
		}
		return nullptr;
	}

	/// @brief Sync a single field's state to node properties.
	void syncFieldProperty(const std::string& key)
	{
		const auto prefix = "field_" + key + "_";
		m_node->setProperty(prefix + "value", m_values[key]);
		const auto errIt = m_errors.find(key);
		m_node->setProperty(prefix + "error", (errIt != m_errors.end()) ? errIt->second : "");
	}

	/// @brief Synchronize overall state to the UINode.
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

			// Serialize options for dropdown/radio
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

	/// @brief Convert field type to string.
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
