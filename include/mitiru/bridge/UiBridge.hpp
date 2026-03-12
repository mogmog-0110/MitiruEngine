#pragma once

/// @file UiBridge.hpp
/// @brief sgc UI統合ブリッジ
/// @details sgcのUIウィジェットシステム（Button、Slider、Checkbox）を
///          Mitiruの描画パイプラインと入力システムに統合する。

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>
#include <sgc/ui/Button.hpp>
#include <sgc/ui/Checkbox.hpp>
#include <sgc/ui/Slider.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/input/InputState.hpp>

namespace mitiru::bridge
{

/// @brief UIウィジェット情報（後方互換）
/// @details 旧APIからの移行用。addWidget(UiWidgetInfo)で使用する。
struct UiWidgetInfo
{
	std::string name;    ///< ウィジェット名
	std::string type;    ///< ウィジェット種別
	bool visible = true; ///< 表示状態
};

/// @brief ウィジェット種別
enum class WidgetType
{
	Button,    ///< ボタン
	Slider,    ///< スライダー
	Checkbox   ///< チェックボックス
};

/// @brief UIウィジェット状態
/// @details 各ウィジェットの位置・状態・種別固有パラメータを保持する。
struct UiWidgetState
{
	std::string id;                                    ///< ウィジェットID
	WidgetType type = WidgetType::Button;              ///< ウィジェット種別
	sgc::Rectf bounds;                                 ///< ウィジェット矩形
	bool visible = true;                               ///< 表示状態
	bool enabled = true;                               ///< 有効状態
	sgc::ui::WidgetState visualState = sgc::ui::WidgetState::Normal;  ///< 視覚状態

	/// ボタン固有
	std::string label;                                 ///< ボタンラベル
	bool clicked = false;                              ///< このフレームでクリックされたか

	/// スライダー固有
	float sliderValue = 0.0f;                          ///< スライダー現在値
	float sliderMin = 0.0f;                            ///< スライダー最小値
	float sliderMax = 1.0f;                            ///< スライダー最大値
	bool sliderDragging = false;                       ///< ドラッグ中か
	bool sliderChanged = false;                        ///< 値が変化したか

	/// チェックボックス固有
	bool checked = false;                              ///< チェック状態
	bool toggled = false;                              ///< このフレームでトグルされたか

	/// 後方互換用
	std::string legacyType;                            ///< 旧API互換の型文字列
};

/// @brief sgc UI統合ブリッジ
/// @details ウィジェットの登録・入力処理・描画を一元管理する。
///          processInput()でsgcのevaluate関数を呼び出し、
///          render()でScreenに矩形・テキストを描画する。
///
/// @code
/// mitiru::bridge::UiBridge ui;
/// ui.addButton("start", {{10, 10}, {120, 40}}, "Start");
/// ui.addSlider("volume", {{10, 60}, {200, 20}}, 0.0f, 1.0f, 0.5f);
/// ui.addCheckbox("mute", {{10, 90}, {24, 24}}, false);
///
/// // 毎フレーム
/// ui.processInput(inputState);
/// ui.render(screen);
///
/// if (auto* btn = ui.getWidget("start"); btn && btn->clicked)
/// {
///     startGame();
/// }
/// @endcode
class UiBridge
{
public:
	// ── 後方互換API ────────────────────────────────────────

	/// @brief ウィジェットを追加する（後方互換）
	/// @param widget ウィジェット情報
	void addWidget(UiWidgetInfo widget)
	{
		UiWidgetState state;
		state.id = widget.name;
		state.label = widget.name;
		state.visible = widget.visible;

		/// 型文字列からWidgetTypeを推定する
		if (widget.type == "Slider")
		{
			state.type = WidgetType::Slider;
		}
		else if (widget.type == "Checkbox")
		{
			state.type = WidgetType::Checkbox;
		}
		else
		{
			state.type = WidgetType::Button;
		}

		/// 型文字列を保存する（toJsonで使用）
		state.legacyType = widget.type;

		m_widgets[widget.name] = std::move(state);
		m_order.push_back(widget.name);
	}

	// ── ウィジェット追加 ────────────────────────────────────

	/// @brief ボタンを追加する
	/// @param id ウィジェットID
	/// @param bounds 矩形領域
	/// @param label ボタンラベル
	void addButton(const std::string& id, const sgc::Rectf& bounds, const std::string& label)
	{
		UiWidgetState widget;
		widget.id = id;
		widget.type = WidgetType::Button;
		widget.bounds = bounds;
		widget.label = label;
		m_widgets[id] = std::move(widget);
		m_order.push_back(id);
	}

	/// @brief スライダーを追加する
	/// @param id ウィジェットID
	/// @param bounds 矩形領域
	/// @param minVal 最小値
	/// @param maxVal 最大値
	/// @param value 初期値
	void addSlider(const std::string& id, const sgc::Rectf& bounds,
		float minVal, float maxVal, float value)
	{
		UiWidgetState widget;
		widget.id = id;
		widget.type = WidgetType::Slider;
		widget.bounds = bounds;
		widget.sliderMin = minVal;
		widget.sliderMax = maxVal;
		widget.sliderValue = value;
		m_widgets[id] = std::move(widget);
		m_order.push_back(id);
	}

	/// @brief チェックボックスを追加する
	/// @param id ウィジェットID
	/// @param bounds 矩形領域
	/// @param checked 初期チェック状態
	void addCheckbox(const std::string& id, const sgc::Rectf& bounds, bool checked)
	{
		UiWidgetState widget;
		widget.id = id;
		widget.type = WidgetType::Checkbox;
		widget.bounds = bounds;
		widget.checked = checked;
		m_widgets[id] = std::move(widget);
		m_order.push_back(id);
	}

	// ── ウィジェット管理 ────────────────────────────────────

	/// @brief ウィジェットを削除する
	/// @param id ウィジェットID
	void removeWidget(const std::string& id)
	{
		m_widgets.erase(id);
		m_order.erase(
			std::remove(m_order.begin(), m_order.end(), id),
			m_order.end());
	}

	/// @brief ウィジェット状態を取得する
	/// @param id ウィジェットID
	/// @return ウィジェット状態へのポインタ（未登録時はnullptr）
	[[nodiscard]] const UiWidgetState* getWidget(const std::string& id) const
	{
		const auto it = m_widgets.find(id);
		if (it == m_widgets.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief ウィジェット状態を取得する（非const版）
	/// @param id ウィジェットID
	/// @return ウィジェット状態へのポインタ（未登録時はnullptr）
	[[nodiscard]] UiWidgetState* getWidget(const std::string& id)
	{
		const auto it = m_widgets.find(id);
		if (it == m_widgets.end())
		{
			return nullptr;
		}
		return &it->second;
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
		m_order.clear();
	}

	// ── 入力処理 ────────────────────────────────────────────

	/// @brief 入力状態からウィジェットを評価する
	/// @param input 現在フレームの入力状態
	/// @details sgcのevaluateButton/evaluateSlider/evaluateCheckboxを使用し、
	///          各ウィジェットの視覚状態・クリック・値変更を更新する。
	void processInput(const InputState& input)
	{
		const auto [mx, my] = input.mousePosition();
		const sgc::Vec2f mousePos{mx, my};
		const bool mouseDown = input.isMouseButtonDown(MouseButton::Left);
		const bool mousePressed = input.isMouseButtonJustPressed(MouseButton::Left);

		for (auto& [id, widget] : m_widgets)
		{
			if (!widget.visible || !widget.enabled)
			{
				widget.visualState = sgc::ui::WidgetState::Disabled;
				widget.clicked = false;
				widget.toggled = false;
				widget.sliderChanged = false;
				continue;
			}

			switch (widget.type)
			{
			case WidgetType::Button:
			{
				const auto result = sgc::ui::evaluateButton(
					widget.bounds, mousePos, mouseDown, mousePressed, widget.enabled);
				widget.visualState = result.state;
				widget.clicked = result.clicked;
				break;
			}
			case WidgetType::Slider:
			{
				const auto result = sgc::ui::evaluateSlider(
					widget.bounds, mousePos, mouseDown, mousePressed,
					widget.sliderValue, widget.sliderMin, widget.sliderMax,
					widget.sliderDragging, widget.enabled);
				widget.visualState = result.state;
				widget.sliderValue = result.value;
				widget.sliderChanged = result.changed;
				widget.sliderDragging = result.dragging;
				break;
			}
			case WidgetType::Checkbox:
			{
				const auto result = sgc::ui::evaluateCheckbox(
					widget.bounds, mousePos, mouseDown, mousePressed,
					widget.checked, widget.enabled);
				widget.visualState = result.state;
				widget.checked = result.checked;
				widget.toggled = result.toggled;
				break;
			}
			}
		}
	}

	// ── 描画 ────────────────────────────────────────────────

	/// @brief 全ウィジェットを描画する
	/// @param screen 描画サーフェス
	/// @details 登録順にウィジェットを描画する。
	///          ボタン: 矩形背景 + ラベルテキスト
	///          スライダー: トラック矩形 + ノブ矩形
	///          チェックボックス: 外枠矩形 + チェックマーク矩形
	void render(Screen& screen) const
	{
		for (const auto& id : m_order)
		{
			const auto it = m_widgets.find(id);
			if (it == m_widgets.end() || !it->second.visible)
			{
				continue;
			}

			const auto& widget = it->second;

			switch (widget.type)
			{
			case WidgetType::Button:
				renderButton(screen, widget);
				break;
			case WidgetType::Slider:
				renderSlider(screen, widget);
				break;
			case WidgetType::Checkbox:
				renderCheckbox(screen, widget);
				break;
			}
		}
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief UIツリー情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"widgetCount\":" + std::to_string(m_widgets.size()) + ",";
		json += "\"widgets\":[";

		bool first = true;
		for (const auto& id : m_order)
		{
			const auto it = m_widgets.find(id);
			if (it == m_widgets.end()) continue;

			if (!first) json += ",";

			const auto& w = it->second;
			/// 型文字列: legacyTypeがあればそちらを優先する（後方互換）
			const auto& typeStr = w.legacyType.empty() ? widgetTypeToString(w.type) : w.legacyType;

			json += "{";
			json += "\"name\":\"" + w.id + "\",";
			json += "\"type\":\"" + typeStr + "\",";
			json += "\"visible\":" + std::string(w.visible ? "true" : "false");
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief ウィジェット種別を文字列に変換する
	[[nodiscard]] static std::string widgetTypeToString(WidgetType type)
	{
		switch (type)
		{
		case WidgetType::Button:   return "Button";
		case WidgetType::Slider:   return "Slider";
		case WidgetType::Checkbox: return "Checkbox";
		}
		return "Unknown";
	}

	/// @brief 視覚状態に応じた背景色を取得する
	[[nodiscard]] static sgc::Colorf stateColor(sgc::ui::WidgetState state) noexcept
	{
		switch (state)
		{
		case sgc::ui::WidgetState::Normal:   return {0.3f, 0.3f, 0.3f, 1.0f};
		case sgc::ui::WidgetState::Hovered:  return {0.4f, 0.4f, 0.5f, 1.0f};
		case sgc::ui::WidgetState::Pressed:  return {0.2f, 0.2f, 0.3f, 1.0f};
		case sgc::ui::WidgetState::Focused:  return {0.35f, 0.35f, 0.45f, 1.0f};
		case sgc::ui::WidgetState::Disabled: return {0.2f, 0.2f, 0.2f, 0.5f};
		}
		return {0.3f, 0.3f, 0.3f, 1.0f};
	}

	/// @brief ボタンを描画する
	/// @param screen 描画サーフェス
	/// @param widget ウィジェット状態
	void renderButton(Screen& screen, const UiWidgetState& widget) const
	{
		const auto bgColor = stateColor(widget.visualState);
		screen.drawRect(widget.bounds, bgColor);

		/// ラベルをバウンディングボックスの中央付近に描画する
		if (!widget.label.empty())
		{
			constexpr float fontSize = 16.0f;
			const sgc::Vec2f textPos{
				widget.bounds.x() + 4.0f,
				widget.bounds.y() + (widget.bounds.height() - fontSize) * 0.5f
			};
			const sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
			screen.drawText(textPos, widget.label, textColor, fontSize);
		}
	}

	/// @brief スライダーを描画する
	/// @param screen 描画サーフェス
	/// @param widget ウィジェット状態
	void renderSlider(Screen& screen, const UiWidgetState& widget) const
	{
		/// トラック背景
		const sgc::Colorf trackColor{0.25f, 0.25f, 0.25f, 1.0f};
		screen.drawRect(widget.bounds, trackColor);

		/// ノブ位置を計算する
		const float range = widget.sliderMax - widget.sliderMin;
		const float ratio = (range > 0.0f)
			? (widget.sliderValue - widget.sliderMin) / range
			: 0.0f;

		constexpr float knobWidth = 12.0f;
		const float knobX = widget.bounds.x() + ratio * (widget.bounds.width() - knobWidth);
		const sgc::Rectf knobRect{
			{knobX, widget.bounds.y()},
			{knobWidth, widget.bounds.height()}
		};

		const auto knobColor = stateColor(widget.visualState);
		screen.drawRect(knobRect, knobColor);
	}

	/// @brief チェックボックスを描画する
	/// @param screen 描画サーフェス
	/// @param widget ウィジェット状態
	void renderCheckbox(Screen& screen, const UiWidgetState& widget) const
	{
		/// 外枠
		const auto bgColor = stateColor(widget.visualState);
		screen.drawRect(widget.bounds, bgColor);

		/// チェックマーク（内側の小さい矩形）
		if (widget.checked)
		{
			constexpr float margin = 4.0f;
			const sgc::Rectf innerRect{
				{widget.bounds.x() + margin, widget.bounds.y() + margin},
				{widget.bounds.width() - margin * 2.0f, widget.bounds.height() - margin * 2.0f}
			};
			const sgc::Colorf checkColor{0.2f, 0.8f, 0.3f, 1.0f};
			screen.drawRect(innerRect, checkColor);
		}
	}

	/// @brief ID → ウィジェット状態
	std::unordered_map<std::string, UiWidgetState> m_widgets;

	/// @brief 描画順序を保持するID列
	std::vector<std::string> m_order;
};

} // namespace mitiru::bridge
