#pragma once

/// @file UIStyle.hpp
/// @brief 包括的なUIビジュアルスタイルシステム
/// @details UIThemeの限定的なスタイル機能を置き換え、ボックス・テキスト・
///          ビジュアルステートごとのスタイルオーバーライドをサポートする。
///          UIStyleSheetはロールまたは文字列クラスからステートスタイルへの
///          マッピングを管理し、UIThemeのカラーパレットをデフォルト値として継承する。

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Texture.hpp>
#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UITheme.hpp>

namespace mitiru::ui
{

// ── ビジュアルステート ─────────────────────────────────────────

/// @brief UI要素のビジュアルステート
enum class UIVisualState : std::uint8_t
{
	Normal,    ///< 通常状態
	Hovered,   ///< ホバー状態
	Pressed,   ///< 押下状態
	Focused,   ///< フォーカス状態
	Disabled,  ///< 無効状態
};

// ── 余白・枠線 ────────────────────────────────────────────────

/// @brief 四辺の余白値
struct UIEdgeInsets
{
	float top    = 0.0f;  ///< 上辺
	float right  = 0.0f;  ///< 右辺
	float bottom = 0.0f;  ///< 下辺
	float left   = 0.0f;  ///< 左辺

	/// @brief 均一な余白を生成する
	/// @param value 全辺の余白値
	/// @return 均一な UIEdgeInsets
	[[nodiscard]] static constexpr UIEdgeInsets uniform(float value) noexcept
	{
		return {value, value, value, value};
	}

	/// @brief 水平・垂直で指定する
	/// @param horizontal 左右の余白
	/// @param vertical 上下の余白
	/// @return UIEdgeInsets
	[[nodiscard]] static constexpr UIEdgeInsets symmetric(float horizontal, float vertical) noexcept
	{
		return {vertical, horizontal, vertical, horizontal};
	}
};

/// @brief 2Dオフセット
struct UIOffset
{
	float x = 0.0f;  ///< X方向オフセット
	float y = 0.0f;  ///< Y方向オフセット
};

// ── 9スライスコンフィグ（UIモジュール用）────────────────────

/// @brief UI用の9スライス設定
/// @details vn::NineSliceConfigと同等の機能をUI名前空間で提供する。
struct UINineSliceConfig
{
	std::uint32_t textureId = 0;   ///< テクスチャ識別子
	float cornerW   = 16.0f;      ///< コーナー幅（ソースピクセル）
	float cornerH   = 16.0f;      ///< コーナー高さ（ソースピクセル）
	float edgeInsetLeft   = 16.0f; ///< 左エッジインセット
	float edgeInsetRight  = 16.0f; ///< 右エッジインセット
	float edgeInsetTop    = 16.0f; ///< 上エッジインセット
	float edgeInsetBottom = 16.0f; ///< 下エッジインセット
	float textureW  = 64.0f;      ///< ソーステクスチャ幅
	float textureH  = 64.0f;      ///< ソーステクスチャ高さ

	/// @brief 均一コーナーサイズで生成する
	[[nodiscard]] static UINineSliceConfig uniform(
		std::uint32_t texId, float corner, float texW, float texH) noexcept
	{
		UINineSliceConfig cfg;
		cfg.textureId       = texId;
		cfg.cornerW         = corner;
		cfg.cornerH         = corner;
		cfg.edgeInsetLeft   = corner;
		cfg.edgeInsetRight  = corner;
		cfg.edgeInsetTop    = corner;
		cfg.edgeInsetBottom = corner;
		cfg.textureW        = texW;
		cfg.textureH        = texH;
		return cfg;
	}
};

// ── フォントウェイト ──────────────────────────────────────────

/// @brief フォントウェイト
enum class UIFontWeight : std::uint8_t
{
	Normal, ///< 通常
	Bold,   ///< 太字
};

// ── テキスト配置 ──────────────────────────────────────────────

/// @brief 水平テキスト配置
enum class UITextAlign : std::uint8_t
{
	Left,   ///< 左揃え
	Center, ///< 中央揃え
	Right,  ///< 右揃え
};

/// @brief 垂直テキスト配置
enum class UIVerticalAlign : std::uint8_t
{
	Top,    ///< 上揃え
	Middle, ///< 中央揃え
	Bottom, ///< 下揃え
};

// ── テキストオーバーフロー ────────────────────────────────────

/// @brief テキストオーバーフロー処理
enum class UITextOverflow : std::uint8_t
{
	Visible,  ///< はみ出しをそのまま描画
	Hidden,   ///< はみ出し部分を非表示
	Ellipsis, ///< 省略記号で打ち切り
};

// ── ウィジェットメトリクス ────────────────────────────────────

/// @brief ウィジェット固有の寸法・マージン設定
/// @details 各ウィジェット描画で使用されるハードコード値を
///          外部から設定可能にする。デフォルト値は従来の挙動と同一。
struct UIWidgetMetrics
{
	// Slider
	float sliderTrackHeightRatio  = 0.3f;  ///< トラック高さ（rect高さに対する比率）
	float sliderTrackMinHeight    = 4.0f;  ///< トラック最小高さ（ピクセル）
	float sliderHandleRadiusRatio = 0.4f;  ///< ハンドル半径（rect高さに対する比率）

	// Toggle
	float toggleBoxSize     = 20.0f; ///< チェックボックスサイズ上限
	float toggleInnerMargin = 3.0f;  ///< チェック状態時の内側マージン
	float toggleLabelGap    = 8.0f;  ///< チェックボックスとラベルの間隔

	// Dropdown
	float dropdownArrowSize  = 8.0f;  ///< 矢印の大きさ
	float dropdownArrowSpace = 20.0f; ///< 矢印領域の幅

	// TextInput
	float textInputCursorWidth   = 2.0f; ///< カーソル幅
	float textInputCursorMarginX = 2.0f; ///< カーソル水平マージン（未使用、将来用）
	float textInputCursorMarginY = 2.0f; ///< カーソル上下マージン
	float textInputTextOffsetX   = 4.0f; ///< テキスト先頭オフセット（未使用、将来用）

	// ウィジェット部品用画像キー（空文字列の場合はジオメトリ描画にフォールバック）
	std::string sliderHandleImageKey;   ///< スライダーハンドル画像
	std::string sliderFillImageKey;     ///< スライダーフィル部分画像
	std::string toggleOnImageKey;       ///< トグルON状態画像
	std::string toggleOffImageKey;      ///< トグルOFF状態画像
	std::string progressFillImageKey;   ///< プログレスバーフィル画像
	std::string dropdownArrowImageKey;  ///< ドロップダウン矢印画像
	std::string textInputCursorImageKey; ///< テキスト入力カーソル画像
};

// ── ボックススタイル ──────────────────────────────────────────

/// @brief UIボックスのビジュアルスタイル
/// @details 背景色・画像・枠線・パディング・マージン・影・透明度を含む。
struct UIBoxStyle
{
	sgc::Colorf backgroundColor{0.0f, 0.0f, 0.0f, 0.0f}; ///< 背景色（アルファ対応）
	std::optional<std::string> backgroundImageKey;          ///< 背景画像テクスチャキー
	std::optional<UINineSliceConfig> backgroundNineSlice;   ///< 9スライス設定
	sgc::Colorf borderColor{0.0f, 0.0f, 0.0f, 0.0f};     ///< 枠線色
	UIEdgeInsets borderWidth{};                             ///< 枠線幅（辺ごと）
	float borderRadius = 0.0f;                              ///< コーナー丸み（描画ヒント）
	UIEdgeInsets padding{};                                 ///< 内側余白
	UIEdgeInsets margin{};                                  ///< 外側余白
	sgc::Colorf shadowColor{0.0f, 0.0f, 0.0f, 0.0f};     ///< 影の色
	UIOffset shadowOffset{};                                ///< 影のオフセット
	float shadowBlur = 0.0f;                                ///< 影のぼかし半径
	float opacity = 1.0f;                                   ///< 不透明度（0.0-1.0）

	/// @brief パディングを適用した内側領域を計算する
	/// @param bounds 外側バウンズ
	/// @return パディング後の内側領域
	[[nodiscard]] sgc::Rectf contentRect(const sgc::Rectf& bounds) const noexcept
	{
		return sgc::Rectf{
			bounds.x() + padding.left,
			bounds.y() + padding.top,
			std::max(0.0f, bounds.width() - padding.left - padding.right),
			std::max(0.0f, bounds.height() - padding.top - padding.bottom)
		};
	}

	/// @brief マージンを適用した外側領域を計算する
	/// @param bounds 元のバウンズ
	/// @return マージン適用後の領域
	[[nodiscard]] sgc::Rectf marginRect(const sgc::Rectf& bounds) const noexcept
	{
		return sgc::Rectf{
			bounds.x() - margin.left,
			bounds.y() - margin.top,
			bounds.width() + margin.left + margin.right,
			bounds.height() + margin.top + margin.bottom
		};
	}
};

// ── テキストスタイル ──────────────────────────────────────────

/// @brief UIテキストのビジュアルスタイル
struct UITextStyle
{
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};         ///< テキスト色
	float fontSize = 16.0f;                               ///< フォントサイズ
	UIFontWeight fontWeight = UIFontWeight::Normal;        ///< フォントウェイト
	UITextAlign textAlign = UITextAlign::Left;             ///< 水平配置
	UIVerticalAlign verticalAlign = UIVerticalAlign::Top;  ///< 垂直配置
	float lineHeight = 1.2f;                               ///< 行高（フォントサイズの倍数）
	float letterSpacing = 0.0f;                            ///< 文字間隔（ピクセル）
	sgc::Colorf textShadowColor{0.0f, 0.0f, 0.0f, 0.0f}; ///< テキスト影色
	UIOffset textShadowOffset{};                           ///< テキスト影オフセット
	UITextOverflow overflow = UITextOverflow::Visible;     ///< オーバーフロー処理
};

// ── スタイルオーバーライド ────────────────────────────────────

/// @brief ボックススタイルのオプショナルオーバーライド
/// @details nulloptのフィールドはベーススタイルから継承される。
struct UIBoxStyleOverride
{
	std::optional<sgc::Colorf> backgroundColor;
	std::optional<sgc::Colorf> borderColor;
	std::optional<UIEdgeInsets> borderWidth;
	std::optional<float> borderRadius;
	std::optional<sgc::Colorf> shadowColor;
	std::optional<UIOffset> shadowOffset;
	std::optional<float> shadowBlur;
	std::optional<float> opacity;
};

/// @brief テキストスタイルのオプショナルオーバーライド
struct UITextStyleOverride
{
	std::optional<sgc::Colorf> color;
	std::optional<float> fontSize;
	std::optional<UIFontWeight> fontWeight;
};

// ── ステートスタイル ──────────────────────────────────────────

/// @brief ビジュアルステートごとのスタイル設定
/// @details 基本（Normal）スタイルと各ステートのオーバーライドを保持する。
///          resolve() でステートに応じた最終的なスタイルを生成する。
struct UIStateStyles
{
	UIBoxStyle boxStyle;          ///< Normalステートのボックススタイル
	UITextStyle textStyle;        ///< Normalステートのテキストスタイル
	UIWidgetMetrics widgetMetrics; ///< ウィジェット固有メトリクス

	std::optional<UIBoxStyleOverride> hovered;   ///< ホバー時オーバーライド
	std::optional<UIBoxStyleOverride> pressed;   ///< 押下時オーバーライド
	std::optional<UIBoxStyleOverride> focused;   ///< フォーカス時オーバーライド
	std::optional<UIBoxStyleOverride> disabled;  ///< 無効時オーバーライド

	std::optional<UITextStyleOverride> hoveredText;   ///< ホバー時テキストオーバーライド
	std::optional<UITextStyleOverride> pressedText;   ///< 押下時テキストオーバーライド
	std::optional<UITextStyleOverride> focusedText;   ///< フォーカス時テキストオーバーライド
	std::optional<UITextStyleOverride> disabledText;  ///< 無効時テキストオーバーライド

	/// @brief 解決済みスタイルペア
	struct Resolved
	{
		UIBoxStyle box;    ///< 解決済みボックススタイル
		UITextStyle text;  ///< 解決済みテキストスタイル
	};

	/// @brief ビジュアルステートに応じた最終スタイルを解決する
	/// @param state 現在のビジュアルステート
	/// @return ベーススタイルにオーバーライドを適用した解決済みスタイル
	[[nodiscard]] Resolved resolve(UIVisualState state) const
	{
		Resolved result;
		result.box = boxStyle;
		result.text = textStyle;

		const UIBoxStyleOverride* boxOverride = nullptr;
		const UITextStyleOverride* textOverride = nullptr;

		switch (state)
		{
		case UIVisualState::Hovered:
			if (hovered)  boxOverride  = &*hovered;
			if (hoveredText)  textOverride = &*hoveredText;
			break;
		case UIVisualState::Pressed:
			if (pressed)  boxOverride  = &*pressed;
			if (pressedText)  textOverride = &*pressedText;
			break;
		case UIVisualState::Focused:
			if (focused)  boxOverride  = &*focused;
			if (focusedText)  textOverride = &*focusedText;
			break;
		case UIVisualState::Disabled:
			if (disabled) boxOverride  = &*disabled;
			if (disabledText) textOverride = &*disabledText;
			break;
		case UIVisualState::Normal:
		default:
			break;
		}

		if (boxOverride)
		{
			applyBoxOverride(result.box, *boxOverride);
		}
		if (textOverride)
		{
			applyTextOverride(result.text, *textOverride);
		}

		return result;
	}

private:
	/// @brief ボックスオーバーライドを適用する
	static void applyBoxOverride(UIBoxStyle& base, const UIBoxStyleOverride& over)
	{
		if (over.backgroundColor)  base.backgroundColor = *over.backgroundColor;
		if (over.borderColor)      base.borderColor     = *over.borderColor;
		if (over.borderWidth)      base.borderWidth      = *over.borderWidth;
		if (over.borderRadius)     base.borderRadius     = *over.borderRadius;
		if (over.shadowColor)      base.shadowColor      = *over.shadowColor;
		if (over.shadowOffset)     base.shadowOffset      = *over.shadowOffset;
		if (over.shadowBlur)       base.shadowBlur        = *over.shadowBlur;
		if (over.opacity)          base.opacity           = *over.opacity;
	}

	/// @brief テキストオーバーライドを適用する
	static void applyTextOverride(UITextStyle& base, const UITextStyleOverride& over)
	{
		if (over.color)      base.color      = *over.color;
		if (over.fontSize)   base.fontSize   = *over.fontSize;
		if (over.fontWeight) base.fontWeight  = *over.fontWeight;
	}
};

// ── スタイルシート ────────────────────────────────────────────

/// @brief UIスタイルシート
/// @details UIRoleまたは文字列クラス名からUIStateStylesへのマッピングを管理する。
///          UIThemeのカラーパレットをデフォルト値として継承する。
///
/// @code
/// mitiru::ui::UIStyleSheet sheet;
/// sheet.buildDefaults(mitiru::ui::UITheme::dark());
///
/// mitiru::ui::UIStateStyles buttonStyles;
/// buttonStyles.boxStyle.backgroundColor = {0.2f, 0.5f, 1.0f, 1.0f};
/// buttonStyles.textStyle.color = {1.0f, 1.0f, 1.0f, 1.0f};
/// buttonStyles.textStyle.textAlign = mitiru::ui::UITextAlign::Center;
/// buttonStyles.hovered = mitiru::ui::UIBoxStyleOverride{};
/// buttonStyles.hovered->backgroundColor = {0.3f, 0.6f, 1.0f, 1.0f};
/// sheet.setStyle(mitiru::ui::UIRole::Button, buttonStyles);
///
/// auto resolved = sheet.getStyle(mitiru::ui::UIRole::Button).resolve(state);
/// @endcode
class UIStyleSheet
{
	std::map<UIRole, UIStateStyles> m_roleStyles;
	std::map<std::string, UIStateStyles> m_classStyles;

public:
	/// @brief UIThemeからデフォルトスタイルを構築する
	/// @param theme ベースとなるテーマ
	void buildDefaults(const UITheme& theme)
	{
		const auto& colors = theme.colors();
		const auto& metrics = theme.metrics();

		// Container / Panel
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = colors.background;
			s.boxStyle.borderColor     = colors.border;
			s.boxStyle.borderWidth     = UIEdgeInsets::uniform(metrics.borderWidth);
			s.boxStyle.padding         = UIEdgeInsets::uniform(metrics.padding);
			s.textStyle.color          = colors.foreground;
			s.textStyle.fontSize       = metrics.fontSize;
			m_roleStyles[UIRole::Container] = s;
			m_roleStyles[UIRole::Panel]     = s;
		}

		// Label
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f};
			s.boxStyle.padding         = UIEdgeInsets::uniform(metrics.padding);
			s.textStyle.color          = colors.foreground;
			s.textStyle.fontSize       = metrics.fontSize;
			m_roleStyles[UIRole::Label]      = s;
			m_roleStyles[UIRole::ScoreLabel] = s;
		}

		// Button
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = colors.accent;
			s.boxStyle.borderColor     = colors.border;
			s.boxStyle.borderWidth     = UIEdgeInsets::uniform(metrics.borderWidth);
			s.boxStyle.borderRadius    = metrics.cornerRadius;
			s.boxStyle.padding         = UIEdgeInsets::symmetric(metrics.padding * 2.0f, metrics.padding);
			s.textStyle.color          = {1.0f, 1.0f, 1.0f, 1.0f};
			s.textStyle.fontSize       = metrics.fontSize;
			s.textStyle.textAlign      = UITextAlign::Center;
			s.textStyle.verticalAlign  = UIVerticalAlign::Middle;

			// Hovered: brighten
			s.hovered = UIBoxStyleOverride{};
			s.hovered->backgroundColor = sgc::Colorf{
				std::min(1.0f, colors.accent.r * 1.2f),
				std::min(1.0f, colors.accent.g * 1.2f),
				std::min(1.0f, colors.accent.b * 1.2f),
				colors.accent.a
			};

			// Pressed: darken
			s.pressed = UIBoxStyleOverride{};
			s.pressed->backgroundColor = sgc::Colorf{
				colors.accent.r * 0.7f,
				colors.accent.g * 0.7f,
				colors.accent.b * 0.7f,
				colors.accent.a
			};

			// Disabled
			s.disabled = UIBoxStyleOverride{};
			s.disabled->backgroundColor = colors.disabled;
			s.disabledText = UITextStyleOverride{};
			s.disabledText->color = sgc::Colorf{0.6f, 0.6f, 0.6f, 0.7f};

			m_roleStyles[UIRole::Button] = s;
		}

		// ProgressBar / HealthBar
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = sgc::Colorf{
				colors.background.r * 0.5f,
				colors.background.g * 0.5f,
				colors.background.b * 0.5f,
				colors.background.a
			};
			s.boxStyle.borderColor  = colors.border;
			s.boxStyle.borderWidth  = UIEdgeInsets::uniform(metrics.borderWidth);
			s.textStyle.color       = colors.accent;
			s.textStyle.fontSize    = metrics.fontSize;
			m_roleStyles[UIRole::ProgressBar] = s;
			m_roleStyles[UIRole::HealthBar]   = s;
		}

		// Image
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f};
			s.textStyle.color          = colors.foreground;
			m_roleStyles[UIRole::Image] = s;
		}

		// Slider
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = sgc::Colorf{
				colors.background.r * 0.6f,
				colors.background.g * 0.6f,
				colors.background.b * 0.6f,
				colors.background.a
			};
			s.boxStyle.borderRadius = metrics.cornerRadius;
			s.textStyle.color       = colors.accent;
			s.textStyle.fontSize    = metrics.fontSize;
			m_roleStyles[UIRole::Slider] = s;
		}

		// Toggle
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = colors.background;
			s.boxStyle.borderColor     = colors.border;
			s.boxStyle.borderWidth     = UIEdgeInsets::uniform(metrics.borderWidth);
			s.textStyle.color          = colors.foreground;
			s.textStyle.fontSize       = metrics.fontSize;
			m_roleStyles[UIRole::Toggle] = s;
		}

		// TextInput
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = sgc::Colorf{
				colors.background.r * 0.8f,
				colors.background.g * 0.8f,
				colors.background.b * 0.8f,
				colors.background.a
			};
			s.boxStyle.borderColor  = colors.border;
			s.boxStyle.borderWidth  = UIEdgeInsets::uniform(metrics.borderWidth);
			s.boxStyle.padding      = UIEdgeInsets::symmetric(metrics.padding, metrics.padding * 0.5f);
			s.textStyle.color       = colors.foreground;
			s.textStyle.fontSize    = metrics.fontSize;

			s.focused = UIBoxStyleOverride{};
			s.focused->borderColor = colors.accent;
			m_roleStyles[UIRole::TextInput] = s;
		}

		// Dropdown
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = colors.background;
			s.boxStyle.borderColor     = colors.border;
			s.boxStyle.borderWidth     = UIEdgeInsets::uniform(metrics.borderWidth);
			s.boxStyle.padding         = UIEdgeInsets::symmetric(metrics.padding, metrics.padding * 0.5f);
			s.textStyle.color          = colors.foreground;
			s.textStyle.fontSize       = metrics.fontSize;
			m_roleStyles[UIRole::Dropdown] = s;
		}

		// DialogBox / Tooltip / MenuItem / ListView / TabBar
		for (auto role : {UIRole::DialogBox, UIRole::Tooltip, UIRole::MenuItem,
		                   UIRole::ListView, UIRole::TabBar, UIRole::MiniMap,
		                   UIRole::Inventory, UIRole::Custom})
		{
			UIStateStyles s;
			s.boxStyle.backgroundColor = colors.background;
			s.boxStyle.borderColor     = colors.border;
			s.boxStyle.borderWidth     = UIEdgeInsets::uniform(metrics.borderWidth);
			s.boxStyle.padding         = UIEdgeInsets::uniform(metrics.padding);
			s.textStyle.color          = colors.foreground;
			s.textStyle.fontSize       = metrics.fontSize;
			m_roleStyles[role] = s;
		}
	}

	/// @brief ロールにスタイルを設定する
	/// @param role 対象ロール
	/// @param styles ステートスタイル
	void setStyle(UIRole role, const UIStateStyles& styles)
	{
		m_roleStyles[role] = styles;
	}

	/// @brief ロールのスタイルを取得する
	/// @param role 対象ロール
	/// @return ステートスタイル（未登録の場合はデフォルト構築）
	[[nodiscard]] const UIStateStyles& getStyle(UIRole role) const
	{
		static const UIStateStyles defaultStyle{};
		const auto it = m_roleStyles.find(role);
		if (it != m_roleStyles.end())
		{
			return it->second;
		}
		return defaultStyle;
	}

	/// @brief 文字列クラス名にスタイルを設定する
	/// @param className クラス名
	/// @param styles ステートスタイル
	void setClassStyle(const std::string& className, const UIStateStyles& styles)
	{
		m_classStyles[className] = styles;
	}

	/// @brief 文字列クラス名のスタイルを取得する
	/// @param className クラス名
	/// @return ステートスタイルへのポインタ（未登録の場合はnullptr）
	[[nodiscard]] const UIStateStyles* getClassStyle(const std::string& className) const
	{
		const auto it = m_classStyles.find(className);
		if (it != m_classStyles.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	/// @brief ノードに対する最適なスタイルを解決する
	/// @details カスタムプロパティ "class" があればクラススタイルを優先し、
	///          なければロールベースのスタイルを返す。
	/// @param node 対象ノード
	/// @return ステートスタイルへの参照
	[[nodiscard]] const UIStateStyles& resolveForNode(const UINode& node) const
	{
		const auto className = node.getProperty("class");
		if (!className.empty())
		{
			const auto* classStyle = getClassStyle(className);
			if (classStyle)
			{
				return *classStyle;
			}
		}
		return getStyle(node.role());
	}

	/// @brief ロールスタイルのマップを取得する
	[[nodiscard]] const std::map<UIRole, UIStateStyles>& roleStyles() const noexcept
	{
		return m_roleStyles;
	}

	/// @brief クラススタイルのマップを取得する
	[[nodiscard]] const std::map<std::string, UIStateStyles>& classStyles() const noexcept
	{
		return m_classStyles;
	}

	/// @brief スタイルシートをJSON文字列にシリアライズする
	/// @return JSON文字列（カラー値のみ出力）
	[[nodiscard]] std::string toJson() const
	{
		nlohmann::json roles = nlohmann::json::object();
		for (const auto& [role, styles] : m_roleStyles)
		{
			nlohmann::json entry;
			entry["bg"] = colorToJsonArray(styles.boxStyle.backgroundColor);
			entry["fg"] = colorToJsonArray(styles.textStyle.color);
			entry["border"] = colorToJsonArray(styles.boxStyle.borderColor);
			entry["fontSize"] = styles.textStyle.fontSize;
			roles[std::to_string(static_cast<int>(role))] = entry;
		}

		nlohmann::json j;
		j["roles"] = roles;
		return j.dump();
	}

private:
	/// @brief カラー値をJSON配列に変換する
	[[nodiscard]] static nlohmann::json colorToJsonArray(const sgc::Colorf& c)
	{
		return nlohmann::json::array({c.r, c.g, c.b, c.a});
	}
};

} // namespace mitiru::ui
