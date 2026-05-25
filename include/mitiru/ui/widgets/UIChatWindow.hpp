#pragma once

/// @file UIChatWindow.hpp
/// @brief channel、入力欄、message 履歴を備えた scroll 可能な chat window widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief sender、channel、style metadata を持つ単一 chat message。
struct UIChatMessage
{
	std::string sender;                            ///< sender の表示名。
	std::string text;                              ///< message 本文。
	double timestamp         = 0.0;                ///< Unix timestamp または game time。
	std::string channel;                           ///< この message が属する channel 名。
	float color[4]           = {1.0f, 1.0f, 1.0f, 1.0f}; ///< message テキストの RGBA 色。
	float senderColor[4]     = {0.8f, 0.8f, 0.2f, 1.0f}; ///< sender 名の RGBA 色。
	std::string iconImageKey;                      ///< sender icon の image key (任意)。
};

/// @brief フィルタリング付き chat channel の定義。
struct UIChatChannel
{
	std::string name;                              ///< channel の識別子 / 名前。
	float color[4]           = {1.0f, 1.0f, 1.0f, 1.0f}; ///< channel アクセントの RGBA 色。
	std::string iconImageKey;                      ///< channel icon の image key (任意)。
	bool enabled             = true;               ///< この channel が表示されるか。
};

/// @brief renderer 向けの scroll bar スタイルヒント。
struct UIChatScrollBarStyle
{
	float width              = 8.0f;               ///< scroll bar の幅 (pixel)。
	float minThumbHeight     = 20.0f;              ///< thumb の最小高さ。
	std::string trackImageKey;                     ///< track 背景の image key (任意)。
	std::string thumbImageKey;                     ///< thumb の image key (任意)。
};

/// @brief chat window の設定。
struct UIChatWindowConfig
{
	float width              = 400.0f;             ///< window 幅。
	float height             = 300.0f;             ///< window 高さ。
	std::size_t maxMessages  = 200;                ///< 保持する最大 message 数 (古いものを自動削除)。
	float inputHeight        = 28.0f;              ///< 入力欄の高さ。
	std::string backgroundImageKey;                ///< window 背景の image key。
	std::string inputBackgroundImageKey;           ///< 入力欄背景の image key。
	float channelTabHeight   = 24.0f;              ///< channel tab 列の高さ。
	std::string channelTabImageKey;                ///< channel tab 背景の image key。
	UIChatScrollBarStyle scrollBarStyle;            ///< scroll bar の見た目。
	float messagePadding     = 4.0f;               ///< message 間の縦 padding。
	float messageFontSize    = 14.0f;              ///< message 本文の font size。
	float senderFontSize     = 14.0f;              ///< sender 名の font size。
	float timestampFontSize  = 11.0f;              ///< timestamp の font size。
	float timestampColor[4]  = {0.5f, 0.5f, 0.5f, 0.8f}; ///< timestamp の RGBA 色。
	float separatorColor[4]  = {0.3f, 0.3f, 0.3f, 0.4f}; ///< message 区切りの RGBA 色。
	bool showTimestamps      = true;               ///< message に timestamp を表示するか。
	bool showSenderIcon      = true;               ///< message の横に sender icon を表示するか。
	std::size_t inputHistorySize = 32;             ///< 保持する入力履歴の最大件数。
};

/// @brief channel、scroll、入力履歴を備えた chat window widget。
///
/// channel ベースのフィルタリング、auto-scroll、履歴移動付き入力欄、image key
/// や style パラメータによる外観カスタマイズを備えた完全な chat ログを提供する。
///
/// @code
///   UIChatWindowConfig cfg;
///   cfg.width = 500.0f;
///   cfg.height = 350.0f;
///   cfg.maxMessages = 500;
///   UIChatWindow chat(cfg);
///
///   chat.addChannel(UIChatChannel{"General", {1,1,1,1}, "", true});
///   chat.addChannel(UIChatChannel{"Team", {0.2f,0.8f,1,1}, "", true});
///   chat.setActiveChannel("General");
///
///   chat.setOnMessageSent([](const std::string& text, const std::string& ch) {
///       // Send to network
///   });
///
///   UIChatMessage msg;
///   msg.sender = "Player1";
///   msg.text = "Hello!";
///   msg.channel = "General";
///   msg.timestamp = 1234567.0;
///   chat.addMessage(msg);
/// @endcode
class UIChatWindow
{
	std::shared_ptr<UINode> m_node;
	UIChatWindowConfig m_config;
	std::deque<UIChatMessage> m_messages;
	std::vector<UIChatChannel> m_channels;
	std::string m_activeChannel;
	std::string m_inputText;
	bool m_autoScroll          = true;
	float m_scrollOffset       = 0.0f;
	float m_maxScrollOffset    = 0.0f;

	// 入力履歴 (新しいものが末尾)。
	std::vector<std::string> m_inputHistory;
	int m_historyIndex         = -1;

	// Callbacks。
	std::function<void(const std::string&, const std::string&)> m_onMessageSent;
	std::function<void(const std::string&)> m_onChannelChanged;

public:
	/// @brief 設定から chat window を構築する。
	/// @param config chat window の設定。
	explicit UIChatWindow(const UIChatWindowConfig& config)
		: m_config(config)
	{
		UINodeData data;
		data.id   = INVALID_UI_NODE;
		data.name = "chat_window";
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "chat_window";
		data.properties["max_messages"] = std::to_string(config.maxMessages);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 基底の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の設定を取得する (読み取り専用)。
	[[nodiscard]] const UIChatWindowConfig& config() const noexcept { return m_config; }

	// ── message ─────────────────────────────────────────────

	/// @brief chat ログに message を追加する。
	/// @param msg 追加する message。
	void addMessage(UIChatMessage msg)
	{
		// 古いものを消して最大 message 数を守る。
		while (m_messages.size() >= m_config.maxMessages)
		{
			m_messages.pop_front();
		}

		m_messages.push_back(std::move(msg));

		// ユーザーが上へ scroll していなければ最下部へ auto-scroll する。
		if (m_autoScroll)
		{
			scrollToBottom();
		}

		syncNodeState();
	}

	/// @brief 全 message を取得する。
	[[nodiscard]] const std::deque<UIChatMessage>& messages() const noexcept
	{
		return m_messages;
	}

	/// @brief active channel でフィルタした message を取得する。
	[[nodiscard]] std::vector<const UIChatMessage*> filteredMessages() const
	{
		std::vector<const UIChatMessage*> result;
		result.reserve(m_messages.size());

		for (const auto& msg : m_messages)
		{
			if (isChannelVisible(msg.channel))
			{
				result.push_back(&msg);
			}
		}

		return result;
	}

	/// @brief message の総数を取得する。
	[[nodiscard]] std::size_t messageCount() const noexcept { return m_messages.size(); }

	/// @brief 全 message を消去する。
	void clearMessages()
	{
		m_messages.clear();
		m_scrollOffset = 0.0f;
		m_autoScroll = true;
		syncNodeState();
	}

	// ── 入力 ────────────────────────────────────────────────

	/// @brief 入力欄のテキストを設定する。
	/// @param text 新しい入力テキスト。
	void setInput(const std::string& text)
	{
		m_inputText = text;
		m_node->setProperty("input_text", m_inputText);
	}

	/// @brief 現在の入力テキストを取得する。
	[[nodiscard]] const std::string& getInput() const noexcept { return m_inputText; }

	/// @brief 入力欄を消去する。
	void clearInput()
	{
		m_inputText.clear();
		m_historyIndex = -1;
		m_node->setProperty("input_text", "");
	}

	/// @brief 現在の入力を submit する (message を送信)。
	void submitInput()
	{
		if (m_inputText.empty()) { return; }

		// 入力履歴に追加する。
		if (m_inputHistory.size() >= m_config.inputHistorySize)
		{
			m_inputHistory.erase(m_inputHistory.begin());
		}
		m_inputHistory.push_back(m_inputText);
		m_historyIndex = -1;

		const std::string submittedText = m_inputText;
		clearInput();

		if (m_onMessageSent)
		{
			m_onMessageSent(submittedText, m_activeChannel);
		}
	}

	/// @brief 入力履歴を上へ辿る (古いエントリ)。
	void inputHistoryUp()
	{
		if (m_inputHistory.empty()) { return; }

		if (m_historyIndex < 0)
		{
			m_historyIndex = static_cast<int>(m_inputHistory.size()) - 1;
		}
		else if (m_historyIndex > 0)
		{
			--m_historyIndex;
		}

		m_inputText = m_inputHistory[static_cast<std::size_t>(m_historyIndex)];
		m_node->setProperty("input_text", m_inputText);
	}

	/// @brief 入力履歴を下へ辿る (新しいエントリ)。
	void inputHistoryDown()
	{
		if (m_historyIndex < 0) { return; }

		++m_historyIndex;
		if (m_historyIndex >= static_cast<int>(m_inputHistory.size()))
		{
			m_historyIndex = -1;
			clearInput();
		}
		else
		{
			m_inputText = m_inputHistory[static_cast<std::size_t>(m_historyIndex)];
			m_node->setProperty("input_text", m_inputText);
		}
	}

	// ── channel ─────────────────────────────────────────────

	/// @brief chat window に channel を追加する。
	/// @param channel channel の定義。
	void addChannel(UIChatChannel channel)
	{
		if (m_activeChannel.empty())
		{
			m_activeChannel = channel.name;
		}
		m_channels.push_back(std::move(channel));
		syncNodeState();
	}

	/// @brief 名前で active channel を設定する。
	/// @param name 有効化する channel 名。
	void setActiveChannel(const std::string& name)
	{
		if (m_activeChannel == name) { return; }

		m_activeChannel = name;
		m_autoScroll = true;
		scrollToBottom();
		syncNodeState();

		if (m_onChannelChanged)
		{
			m_onChannelChanged(m_activeChannel);
		}
	}

	/// @brief active channel 名を取得する。
	[[nodiscard]] const std::string& activeChannel() const noexcept { return m_activeChannel; }

	/// @brief 全 channel を取得する。
	[[nodiscard]] const std::vector<UIChatChannel>& channels() const noexcept { return m_channels; }

	/// @brief 名前で channel を有効 / 無効にする。
	/// @param name channel 名。
	/// @param enabled channel を表示するか。
	void setChannelEnabled(const std::string& name, bool enabled)
	{
		for (auto& ch : m_channels)
		{
			if (ch.name == name)
			{
				ch.enabled = enabled;
				break;
			}
		}
		syncNodeState();
	}

	// ── scroll ────────────────────────────────────────────

	/// @brief delta 量だけ scroll する。
	/// @param delta 正 = 下へ scroll、負 = 上へ scroll。
	void scroll(float delta)
	{
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0.0f, m_maxScrollOffset);

		// ユーザーが上へ scroll したら auto-scroll を無効化する。
		if (delta < 0.0f && m_scrollOffset < m_maxScrollOffset)
		{
			m_autoScroll = false;
		}

		// 最下部に来たら auto-scroll を再有効化する。
		if (m_scrollOffset >= m_maxScrollOffset)
		{
			m_autoScroll = true;
		}

		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief message ログの最下部へ scroll する。
	void scrollToBottom()
	{
		m_scrollOffset = m_maxScrollOffset;
		m_autoScroll = true;
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief auto-scroll が有効か確認する。
	[[nodiscard]] bool isAutoScrolling() const noexcept { return m_autoScroll; }

	/// @brief 現在の scroll offset を取得する。
	[[nodiscard]] float scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief 最大 scroll offset を設定する (コンテンツ高さが変わったら呼ぶ)。
	/// @param maxOffset 最大 scroll offset (pixel)。
	void setMaxScrollOffset(float maxOffset)
	{
		m_maxScrollOffset = std::max(0.0f, maxOffset);
		m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, m_maxScrollOffset);
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief ユーザーが message を送信したときの callback を設定する。
	/// @param callback (messageText, channelName) を受け取る。
	void setOnMessageSent(std::function<void(const std::string&, const std::string&)> callback)
	{
		m_onMessageSent = std::move(callback);
	}

	/// @brief active channel が変わったときの callback を設定する。
	/// @param callback 新しい channel 名を受け取る。
	void setOnChannelChanged(std::function<void(const std::string&)> callback)
	{
		m_onChannelChanged = std::move(callback);
	}

private:
	/// @brief channel が現在表示されているか確認する。
	[[nodiscard]] bool isChannelVisible(const std::string& channelName) const
	{
		// channel 未設定なら全て表示する。
		if (m_channels.empty()) { return true; }

		// activeChannel が設定され message が一致すれば表示する。
		// channel が enabled でも表示する。
		for (const auto& ch : m_channels)
		{
			if (ch.name == channelName)
			{
				return ch.enabled;
			}
		}

		// 未知の channel: default で表示する。
		return true;
	}

	/// @brief 状態を UINode の properties に同期する。
	void syncNodeState()
	{
		m_node->setProperty("message_count", std::to_string(m_messages.size()));
		m_node->setProperty("active_channel", m_activeChannel);
		m_node->setProperty("auto_scroll", m_autoScroll ? "true" : "false");
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("input_text", m_inputText);

		// channel リストを encode する。
		std::string channelList;
		for (const auto& ch : m_channels)
		{
			if (!channelList.empty()) { channelList += ","; }
			channelList += ch.name;
		}
		m_node->setProperty("channels", channelList);

		// channel 数を encode する。
		m_node->setProperty("channel_count", std::to_string(m_channels.size()));
	}
};

} // namespace mitiru::ui
