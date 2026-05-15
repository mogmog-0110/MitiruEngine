#pragma once

/// @file UITextInput.hpp
/// @brief Single/multi-line text input widget with cursor, selection, and editing.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Configuration for creating a UITextInput.
struct UITextInputConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string placeholder;
	std::string initialText;
	std::size_t maxLength = 256;
	bool password = false;
	bool multiline = false;
	float width = 200.0f;
	float height = 28.0f;
};

/// @brief Text input widget that wraps a UINode with cursor and editing logic.
///
/// Manages cursor position, selection range, text insertion/deletion,
/// and password masking. Does not handle actual keyboard input dispatch --
/// the event system calls the editing methods.
///
/// @code
///   UITextInputConfig cfg;
///   cfg.id = 40;
///   cfg.placeholder = "Enter name...";
///   cfg.maxLength = 32;
///   UITextInput input(cfg);
///
///   input.setOnTextChanged([](const std::string& t) { /* use text */ });
///   input.insertText("Hello");
///   input.moveCursor(-2);
///   input.deleteForward();
/// @endcode
class UITextInput
{
	std::shared_ptr<UINode> m_node;
	std::string m_text;
	std::string m_placeholder;
	std::size_t m_maxLength;
	bool m_password;
	bool m_multiline;
	std::size_t m_cursorPos = 0;
	std::size_t m_selectionStart = 0;
	std::size_t m_selectionEnd = 0;
	bool m_focused = false;
	std::function<void(const std::string&)> m_onTextChanged;
	std::function<void(const std::string&)> m_onSubmit;

public:
	/// @brief Construct a text input from configuration.
	/// @param config Text input configuration.
	explicit UITextInput(const UITextInputConfig& config)
		: m_text(config.initialText)
		, m_placeholder(config.placeholder)
		, m_maxLength(config.maxLength)
		, m_password(config.password)
		, m_multiline(config.multiline)
		, m_cursorPos(config.initialText.size())
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::TextInput;
		data.text = config.initialText;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "text_input";
		data.properties["placeholder"] = config.placeholder;
		data.properties["password"] = config.password ? "true" : "false";
		data.properties["multiline"] = config.multiline ? "true" : "false";
		data.properties["max_length"] = std::to_string(config.maxLength);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current text.
	[[nodiscard]] const std::string& text() const noexcept { return m_text; }

	/// @brief Get the display text (masked if password mode).
	[[nodiscard]] std::string displayText() const
	{
		if (m_text.empty()) { return m_placeholder; }
		if (m_password) { return std::string(m_text.size(), '*'); }
		return m_text;
	}

	/// @brief Get the cursor position.
	[[nodiscard]] std::size_t cursorPos() const noexcept { return m_cursorPos; }

	/// @brief Get the selection start index.
	[[nodiscard]] std::size_t selectionStart() const noexcept { return m_selectionStart; }

	/// @brief Get the selection end index.
	[[nodiscard]] std::size_t selectionEnd() const noexcept { return m_selectionEnd; }

	/// @brief Check if text is currently selected.
	[[nodiscard]] bool hasSelection() const noexcept { return m_selectionStart != m_selectionEnd; }

	/// @brief Check if the input is focused.
	[[nodiscard]] bool isFocused() const noexcept { return m_focused; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the text-changed callback.
	void setOnTextChanged(std::function<void(const std::string&)> callback) { m_onTextChanged = std::move(callback); }

	/// @brief Set the submit callback (Enter key).
	void setOnSubmit(std::function<void(const std::string&)> callback) { m_onSubmit = std::move(callback); }

	/// @brief Set the text programmatically.
	/// @param text New text content.
	void setText(const std::string& text)
	{
		m_text = text.substr(0, m_maxLength);
		m_cursorPos = m_text.size();
		clearSelection();
		syncNodeState();
		if (m_onTextChanged) { m_onTextChanged(m_text); }
	}

	// ── Focus ────────────────────────────────────────────────

	/// @brief Called when the input gains focus.
	void onFocus()
	{
		m_focused = true;
		m_node->setProperty("focused", "true");
	}

	/// @brief Called when the input loses focus.
	void onBlur()
	{
		m_focused = false;
		clearSelection();
		m_node->setProperty("focused", "false");
	}

	// ── Text Editing ─────────────────────────────────────────

	/// @brief Insert text at the current cursor position.
	/// @param str Text to insert.
	void insertText(const std::string& str)
	{
		if (!m_focused) { return; }

		deleteSelection();

		const std::size_t available = m_maxLength - m_text.size();
		const std::size_t insertLen = std::min(str.size(), available);
		if (insertLen == 0) { return; }

		m_text.insert(m_cursorPos, str, 0, insertLen);
		m_cursorPos += insertLen;
		syncNodeState();
		if (m_onTextChanged) { m_onTextChanged(m_text); }
	}

	/// @brief Delete the character before the cursor (Backspace).
	void deleteBackward()
	{
		if (!m_focused) { return; }

		if (hasSelection())
		{
			deleteSelection();
			syncNodeState();
			if (m_onTextChanged) { m_onTextChanged(m_text); }
			return;
		}

		if (m_cursorPos > 0)
		{
			m_text.erase(m_cursorPos - 1, 1);
			--m_cursorPos;
			syncNodeState();
			if (m_onTextChanged) { m_onTextChanged(m_text); }
		}
	}

	/// @brief Delete the character after the cursor (Delete key).
	void deleteForward()
	{
		if (!m_focused) { return; }

		if (hasSelection())
		{
			deleteSelection();
			syncNodeState();
			if (m_onTextChanged) { m_onTextChanged(m_text); }
			return;
		}

		if (m_cursorPos < m_text.size())
		{
			m_text.erase(m_cursorPos, 1);
			syncNodeState();
			if (m_onTextChanged) { m_onTextChanged(m_text); }
		}
	}

	/// @brief Move the cursor by an offset.
	/// @param offset Positive = right, negative = left.
	/// @param extendSelection If true, extends the selection range.
	void moveCursor(int offset, bool extendSelection = false)
	{
		const auto newPos = static_cast<std::size_t>(
			std::clamp(
				static_cast<int>(m_cursorPos) + offset,
				0,
				static_cast<int>(m_text.size())));

		if (extendSelection)
		{
			if (!hasSelection())
			{
				m_selectionStart = m_cursorPos;
			}
			m_selectionEnd = newPos;
		}
		else
		{
			clearSelection();
		}

		m_cursorPos = newPos;
		syncNodeState();
	}

	/// @brief Move cursor to the beginning of the text (Home key).
	/// @param extendSelection If true, extends the selection.
	void moveToStart(bool extendSelection = false)
	{
		if (extendSelection && !hasSelection())
		{
			m_selectionStart = m_cursorPos;
		}
		m_cursorPos = 0;
		if (extendSelection) { m_selectionEnd = 0; }
		else { clearSelection(); }
		syncNodeState();
	}

	/// @brief Move cursor to the end of the text (End key).
	/// @param extendSelection If true, extends the selection.
	void moveToEnd(bool extendSelection = false)
	{
		if (extendSelection && !hasSelection())
		{
			m_selectionStart = m_cursorPos;
		}
		m_cursorPos = m_text.size();
		if (extendSelection) { m_selectionEnd = m_text.size(); }
		else { clearSelection(); }
		syncNodeState();
	}

	/// @brief Select all text (Ctrl+A).
	void selectAll()
	{
		m_selectionStart = 0;
		m_selectionEnd = m_text.size();
		m_cursorPos = m_text.size();
		syncNodeState();
	}

	/// @brief Handle the Enter/Return key.
	void onSubmitKey()
	{
		if (!m_focused) { return; }

		if (m_multiline)
		{
			insertText("\n");
		}
		else
		{
			if (m_onSubmit) { m_onSubmit(m_text); }
		}
	}

	/// @brief Get the currently selected text.
	[[nodiscard]] std::string selectedText() const
	{
		if (!hasSelection()) { return {}; }
		const auto start = std::min(m_selectionStart, m_selectionEnd);
		const auto end = std::max(m_selectionStart, m_selectionEnd);
		return m_text.substr(start, end - start);
	}

private:
	/// @brief Clear the current selection.
	void clearSelection()
	{
		m_selectionStart = 0;
		m_selectionEnd = 0;
	}

	/// @brief Delete the currently selected text and move cursor.
	void deleteSelection()
	{
		if (!hasSelection()) { return; }
		const auto start = std::min(m_selectionStart, m_selectionEnd);
		const auto end = std::max(m_selectionStart, m_selectionEnd);
		m_text.erase(start, end - start);
		m_cursorPos = start;
		clearSelection();
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setText(m_password ? std::string(m_text.size(), '*') : m_text);
		m_node->setProperty("cursor_pos", std::to_string(m_cursorPos));
		m_node->setProperty("sel_start", std::to_string(std::min(m_selectionStart, m_selectionEnd)));
		m_node->setProperty("sel_end", std::to_string(std::max(m_selectionStart, m_selectionEnd)));
		m_node->setProperty("has_text", m_text.empty() ? "false" : "true");
	}
};

} // namespace mitiru::ui
