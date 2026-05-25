#pragma once

/// @file UITextInput.hpp
/// @brief cursor / 選択 / 編集を備えた単一行・複数行の text input widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief UITextInput 生成用の設定。
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

/// @brief UINode を cursor と編集ロジックで包む text input widget。
///
/// cursor 位置・選択範囲・テキストの挿入/削除・password マスクを管理する。
/// 実際のキーボード入力 dispatch は扱わない — event system が編集メソッドを
/// 呼ぶ。
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
	/// @brief 設定から text input を構築する。
	/// @param config text input の設定。
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

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在のテキストを取得する。
	[[nodiscard]] const std::string& text() const noexcept { return m_text; }

	/// @brief 表示用テキストを取得する (password mode ではマスクされる)。
	[[nodiscard]] std::string displayText() const
	{
		if (m_text.empty()) { return m_placeholder; }
		if (m_password) { return std::string(m_text.size(), '*'); }
		return m_text;
	}

	/// @brief cursor 位置を取得する。
	[[nodiscard]] std::size_t cursorPos() const noexcept { return m_cursorPos; }

	/// @brief 選択開始 index を取得する。
	[[nodiscard]] std::size_t selectionStart() const noexcept { return m_selectionStart; }

	/// @brief 選択終了 index を取得する。
	[[nodiscard]] std::size_t selectionEnd() const noexcept { return m_selectionEnd; }

	/// @brief 現在テキストが選択されているか判定する。
	[[nodiscard]] bool hasSelection() const noexcept { return m_selectionStart != m_selectionEnd; }

	/// @brief input が focus されているか判定する。
	[[nodiscard]] bool isFocused() const noexcept { return m_focused; }

	// ── Configuration ────────────────────────────────────────

	/// @brief text 変更時の callback を設定する。
	void setOnTextChanged(std::function<void(const std::string&)> callback) { m_onTextChanged = std::move(callback); }

	/// @brief submit 時 (Enter キー) の callback を設定する。
	void setOnSubmit(std::function<void(const std::string&)> callback) { m_onSubmit = std::move(callback); }

	/// @brief プログラムからテキストを設定する。
	/// @param text 新しいテキスト内容。
	void setText(const std::string& text)
	{
		m_text = text.substr(0, m_maxLength);
		m_cursorPos = m_text.size();
		clearSelection();
		syncNodeState();
		if (m_onTextChanged) { m_onTextChanged(m_text); }
	}

	// ── Focus ────────────────────────────────────────────────

	/// @brief input が focus を得たときに呼ばれる。
	void onFocus()
	{
		m_focused = true;
		m_node->setProperty("focused", "true");
	}

	/// @brief input が focus を失ったときに呼ばれる。
	void onBlur()
	{
		m_focused = false;
		clearSelection();
		m_node->setProperty("focused", "false");
	}

	// ── Text Editing ─────────────────────────────────────────

	/// @brief 現在の cursor 位置にテキストを挿入する。
	/// @param str 挿入するテキスト。
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

	/// @brief cursor の前の文字を削除する (Backspace)。
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

	/// @brief cursor の後の文字を削除する (Delete キー)。
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

	/// @brief cursor を offset 分だけ動かす。
	/// @param offset 正 = 右、負 = 左。
	/// @param extendSelection true なら選択範囲を拡張する。
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

	/// @brief cursor をテキスト先頭へ動かす (Home キー)。
	/// @param extendSelection true なら選択を拡張する。
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

	/// @brief cursor をテキスト末尾へ動かす (End キー)。
	/// @param extendSelection true なら選択を拡張する。
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

	/// @brief 全テキストを選択する (Ctrl+A)。
	void selectAll()
	{
		m_selectionStart = 0;
		m_selectionEnd = m_text.size();
		m_cursorPos = m_text.size();
		syncNodeState();
	}

	/// @brief Enter/Return キーを処理する。
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

	/// @brief 現在選択されているテキストを取得する。
	[[nodiscard]] std::string selectedText() const
	{
		if (!hasSelection()) { return {}; }
		const auto start = std::min(m_selectionStart, m_selectionEnd);
		const auto end = std::max(m_selectionStart, m_selectionEnd);
		return m_text.substr(start, end - start);
	}

private:
	/// @brief 現在の選択を解除する。
	void clearSelection()
	{
		m_selectionStart = 0;
		m_selectionEnd = 0;
	}

	/// @brief 選択中テキストを削除し cursor を移動する。
	void deleteSelection()
	{
		if (!hasSelection()) { return; }
		const auto start = std::min(m_selectionStart, m_selectionEnd);
		const auto end = std::max(m_selectionStart, m_selectionEnd);
		m_text.erase(start, end - start);
		m_cursorPos = start;
		clearSelection();
	}

	/// @brief state を UINode へ同期する。
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
