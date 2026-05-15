#pragma once

/// @file UIChatWindow.hpp
/// @brief Scrollable chat window widget with channels, input field, and message history.

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

/// @brief A single chat message with sender, channel, and style metadata.
struct UIChatMessage
{
	std::string sender;                            ///< Sender display name.
	std::string text;                              ///< Message body.
	double timestamp         = 0.0;                ///< Unix timestamp or game time.
	std::string channel;                           ///< Channel name this message belongs to.
	float color[4]           = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Message text RGBA color.
	float senderColor[4]     = {0.8f, 0.8f, 0.2f, 1.0f}; ///< Sender name RGBA color.
	std::string iconImageKey;                      ///< Optional sender icon image key.
};

/// @brief Definition of a chat channel with filtering.
struct UIChatChannel
{
	std::string name;                              ///< Channel identifier/name.
	float color[4]           = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Channel accent RGBA color.
	std::string iconImageKey;                      ///< Optional channel icon image key.
	bool enabled             = true;               ///< Whether this channel is visible.
};

/// @brief Scroll bar style hints for the renderer.
struct UIChatScrollBarStyle
{
	float width              = 8.0f;               ///< Scroll bar width in pixels.
	float minThumbHeight     = 20.0f;              ///< Minimum thumb height.
	std::string trackImageKey;                     ///< Optional track background image key.
	std::string thumbImageKey;                     ///< Optional thumb image key.
};

/// @brief Configuration for the chat window.
struct UIChatWindowConfig
{
	float width              = 400.0f;             ///< Window width.
	float height             = 300.0f;             ///< Window height.
	std::size_t maxMessages  = 200;                ///< Maximum stored messages (oldest auto-removed).
	float inputHeight        = 28.0f;              ///< Input field height.
	std::string backgroundImageKey;                ///< Window background image key.
	std::string inputBackgroundImageKey;           ///< Input field background image key.
	float channelTabHeight   = 24.0f;              ///< Channel tab strip height.
	std::string channelTabImageKey;                ///< Channel tab background image key.
	UIChatScrollBarStyle scrollBarStyle;            ///< Scroll bar appearance.
	float messagePadding     = 4.0f;               ///< Vertical padding between messages.
	float messageFontSize    = 14.0f;              ///< Message body font size.
	float senderFontSize     = 14.0f;              ///< Sender name font size.
	float timestampFontSize  = 11.0f;              ///< Timestamp font size.
	float timestampColor[4]  = {0.5f, 0.5f, 0.5f, 0.8f}; ///< Timestamp RGBA color.
	float separatorColor[4]  = {0.3f, 0.3f, 0.3f, 0.4f}; ///< Message separator RGBA color.
	bool showTimestamps      = true;               ///< Show timestamps on messages.
	bool showSenderIcon      = true;               ///< Show sender icon beside messages.
	std::size_t inputHistorySize = 32;             ///< Max stored input history entries.
};

/// @brief Chat window widget with channels, scrolling, and input history.
///
/// Provides a complete chat log with channel-based filtering, auto-scrolling,
/// input field with history navigation, and configurable appearance via
/// image keys and style parameters.
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

	// Input history (newest at back).
	std::vector<std::string> m_inputHistory;
	int m_historyIndex         = -1;

	// Callbacks.
	std::function<void(const std::string&, const std::string&)> m_onMessageSent;
	std::function<void(const std::string&)> m_onChannelChanged;

public:
	/// @brief Construct a chat window from configuration.
	/// @param config Chat window configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current configuration (read-only).
	[[nodiscard]] const UIChatWindowConfig& config() const noexcept { return m_config; }

	// ── Messages ─────────────────────────────────────────────

	/// @brief Add a message to the chat log.
	/// @param msg Message to add.
	void addMessage(UIChatMessage msg)
	{
		// Enforce max messages by removing oldest.
		while (m_messages.size() >= m_config.maxMessages)
		{
			m_messages.pop_front();
		}

		m_messages.push_back(std::move(msg));

		// Auto-scroll to bottom if user hasn't scrolled up.
		if (m_autoScroll)
		{
			scrollToBottom();
		}

		syncNodeState();
	}

	/// @brief Get all messages.
	[[nodiscard]] const std::deque<UIChatMessage>& messages() const noexcept
	{
		return m_messages;
	}

	/// @brief Get messages filtered by active channel.
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

	/// @brief Get the total message count.
	[[nodiscard]] std::size_t messageCount() const noexcept { return m_messages.size(); }

	/// @brief Clear all messages.
	void clearMessages()
	{
		m_messages.clear();
		m_scrollOffset = 0.0f;
		m_autoScroll = true;
		syncNodeState();
	}

	// ── Input ────────────────────────────────────────────────

	/// @brief Set the input field text.
	/// @param text New input text.
	void setInput(const std::string& text)
	{
		m_inputText = text;
		m_node->setProperty("input_text", m_inputText);
	}

	/// @brief Get the current input text.
	[[nodiscard]] const std::string& getInput() const noexcept { return m_inputText; }

	/// @brief Clear the input field.
	void clearInput()
	{
		m_inputText.clear();
		m_historyIndex = -1;
		m_node->setProperty("input_text", "");
	}

	/// @brief Submit the current input (sends message).
	void submitInput()
	{
		if (m_inputText.empty()) { return; }

		// Add to input history.
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

	/// @brief Navigate input history upward (older entries).
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

	/// @brief Navigate input history downward (newer entries).
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

	// ── Channels ─────────────────────────────────────────────

	/// @brief Add a channel to the chat window.
	/// @param channel Channel definition.
	void addChannel(UIChatChannel channel)
	{
		if (m_activeChannel.empty())
		{
			m_activeChannel = channel.name;
		}
		m_channels.push_back(std::move(channel));
		syncNodeState();
	}

	/// @brief Set the active channel by name.
	/// @param name Channel name to activate.
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

	/// @brief Get the active channel name.
	[[nodiscard]] const std::string& activeChannel() const noexcept { return m_activeChannel; }

	/// @brief Get all channels.
	[[nodiscard]] const std::vector<UIChatChannel>& channels() const noexcept { return m_channels; }

	/// @brief Enable or disable a channel by name.
	/// @param name Channel name.
	/// @param enabled Whether the channel should be visible.
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

	// ── Scrolling ────────────────────────────────────────────

	/// @brief Scroll by a delta amount.
	/// @param delta Positive = scroll down, negative = scroll up.
	void scroll(float delta)
	{
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0.0f, m_maxScrollOffset);

		// If user scrolls up, disable auto-scroll.
		if (delta < 0.0f && m_scrollOffset < m_maxScrollOffset)
		{
			m_autoScroll = false;
		}

		// Re-enable auto-scroll when at bottom.
		if (m_scrollOffset >= m_maxScrollOffset)
		{
			m_autoScroll = true;
		}

		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief Scroll to the bottom of the message log.
	void scrollToBottom()
	{
		m_scrollOffset = m_maxScrollOffset;
		m_autoScroll = true;
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief Check if auto-scroll is active.
	[[nodiscard]] bool isAutoScrolling() const noexcept { return m_autoScroll; }

	/// @brief Get the current scroll offset.
	[[nodiscard]] float scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief Set the maximum scroll offset (call when content height changes).
	/// @param maxOffset Maximum scroll offset in pixels.
	void setMaxScrollOffset(float maxOffset)
	{
		m_maxScrollOffset = std::max(0.0f, maxOffset);
		m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, m_maxScrollOffset);
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback when user sends a message.
	/// @param callback Receives (messageText, channelName).
	void setOnMessageSent(std::function<void(const std::string&, const std::string&)> callback)
	{
		m_onMessageSent = std::move(callback);
	}

	/// @brief Set callback when active channel changes.
	/// @param callback Receives the new channel name.
	void setOnChannelChanged(std::function<void(const std::string&)> callback)
	{
		m_onChannelChanged = std::move(callback);
	}

private:
	/// @brief Check if a channel is currently visible.
	[[nodiscard]] bool isChannelVisible(const std::string& channelName) const
	{
		// Show all if no channels configured.
		if (m_channels.empty()) { return true; }

		// If activeChannel is set and the message matches, show it.
		// Also show if channel is enabled.
		for (const auto& ch : m_channels)
		{
			if (ch.name == channelName)
			{
				return ch.enabled;
			}
		}

		// Unknown channel: show by default.
		return true;
	}

	/// @brief Synchronize state to the UINode properties.
	void syncNodeState()
	{
		m_node->setProperty("message_count", std::to_string(m_messages.size()));
		m_node->setProperty("active_channel", m_activeChannel);
		m_node->setProperty("auto_scroll", m_autoScroll ? "true" : "false");
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("input_text", m_inputText);

		// Encode channel list.
		std::string channelList;
		for (const auto& ch : m_channels)
		{
			if (!channelList.empty()) { channelList += ","; }
			channelList += ch.name;
		}
		m_node->setProperty("channels", channelList);

		// Encode channel count.
		m_node->setProperty("channel_count", std::to_string(m_channels.size()));
	}
};

} // namespace mitiru::ui
