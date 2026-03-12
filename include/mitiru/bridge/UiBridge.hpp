#pragma once

/// @file UiBridge.hpp
/// @brief sgc UI統合ブリッジ（スタブ）
/// @details sgcのUIウィジェットシステムをMitiruエンジンに統合する。
///          現段階ではスタブ実装。

#include <string>
#include <vector>

#include <mitiru/core/Screen.hpp>

namespace mitiru::bridge
{

/// @brief UIウィジェット情報（スタブ）
struct UiWidgetInfo
{
	std::string name;    ///< ウィジェット名
	std::string type;    ///< ウィジェット種別
	bool visible = true; ///< 表示状態
};

/// @brief UIブリッジ（スタブ）
/// @details sgcのUIシステムとMitiruの描画パイプラインを統合する。
///          将来的にsgcのButton, Slider等のウィジェットを描画する。
///
/// @code
/// mitiru::bridge::UiBridge ui;
/// ui.addWidget({"start_button", "Button", true});
/// ui.render(screen);
/// @endcode
class UiBridge
{
public:
	/// @brief ウィジェットを追加する
	/// @param widget ウィジェット情報
	void addWidget(UiWidgetInfo widget)
	{
		m_widgets.push_back(std::move(widget));
	}

	/// @brief ウィジェットを削除する
	/// @param name ウィジェット名
	void removeWidget(const std::string& name)
	{
		m_widgets.erase(
			std::remove_if(m_widgets.begin(), m_widgets.end(),
				[&name](const UiWidgetInfo& w) { return w.name == name; }),
			m_widgets.end());
	}

	/// @brief 全ウィジェットを描画する（スタブ）
	/// @param screen 描画サーフェス
	void render(Screen& screen) const
	{
		/// スタブ実装：描画コールのみカウント
		for (const auto& widget : m_widgets)
		{
			if (widget.visible)
			{
				static_cast<void>(screen);
				/// 将来: screen.drawRect(...) 等を呼び出す
			}
		}
	}

	/// @brief UIツリー情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"widgetCount\":" + std::to_string(m_widgets.size()) + ",";
		json += "\"widgets\":[";
		for (std::size_t i = 0; i < m_widgets.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			const auto& w = m_widgets[i];
			json += "{";
			json += "\"name\":\"" + w.name + "\",";
			json += "\"type\":\"" + w.type + "\",";
			json += "\"visible\":" + std::string(w.visible ? "true" : "false");
			json += "}";
		}
		json += "]";
		json += "}";
		return json;
	}

	/// @brief ウィジェット数を取得する
	/// @return ウィジェット数
	[[nodiscard]] std::size_t widgetCount() const noexcept
	{
		return m_widgets.size();
	}

	/// @brief 全ウィジェットをクリアする
	void clear() noexcept
	{
		m_widgets.clear();
	}

private:
	std::vector<UiWidgetInfo> m_widgets;  ///< 登録済みウィジェット
};

} // namespace mitiru::bridge
