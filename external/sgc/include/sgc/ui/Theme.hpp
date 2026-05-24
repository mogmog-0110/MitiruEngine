#pragma once

/// @file Theme.hpp
/// @brief UIテーマシステム
///
/// 色・フォントサイズのパレットを constexpr struct で提供する。
/// 描画は含まない。ウィジェット状態に対応するカラー解決を行う。
///
/// @code
/// constexpr auto theme = sgc::ui::Theme::dark();
/// auto btnColor = theme.button.colorFor(sgc::ui::WidgetState::Hovered);
/// textRenderer->drawText("Title", theme.fontSizeTitle, pos, theme.text);
/// @endcode

#include "sgc/types/Color.hpp"
#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief ボタン状態別カラーテーマ
struct ButtonTheme
{
	Colorf normal;    ///< 通常状態の色
	Colorf hovered;   ///< ホバー状態の色
	Colorf pressed;   ///< 押下状態の色
	Colorf disabled;  ///< 無効状態の色

	/// @brief ウィジェット状態に対応する色を取得する
	/// @param state ウィジェット状態
	/// @return 対応する色
	[[nodiscard]] constexpr Colorf colorFor(WidgetState state) const noexcept
	{
		switch (state)
		{
		case WidgetState::Hovered:  return hovered;
		case WidgetState::Pressed:  return pressed;
		case WidgetState::Disabled: return disabled;
		case WidgetState::Normal:   return normal;
		case WidgetState::Focused:  return hovered;  // Focused はHoveredと同じ色
		default:                    return normal;
		}
	}
};

/// @brief UIテーマ（全constexpr）
///
/// 背景色、テキスト色、アクセント色、ボタンテーマ、フォントサイズを定義する。
/// dark() / light() でプリセットを生成できる。
struct Theme
{
	Colorf background;       ///< 背景色
	Colorf surface;          ///< サーフェス色（カード・パネル等）
	Colorf text;             ///< メインテキスト色
	Colorf textSecondary;    ///< セカンダリテキスト色
	Colorf accent;           ///< アクセント色
	ButtonTheme button;      ///< ボタンテーマ
	float fontSizeTitle;     ///< タイトルフォントサイズ
	float fontSizeBody;      ///< 本文フォントサイズ
	float fontSizeSmall;     ///< 小さいフォントサイズ

	/// @brief ダークテーマプリセット
	/// @return ダークテーマ
	[[nodiscard]] static constexpr Theme dark() noexcept
	{
		return Theme{
			.background = Colorf{0.1f, 0.1f, 0.12f, 1.0f},
			.surface = Colorf{0.18f, 0.18f, 0.22f, 1.0f},
			.text = Colorf{0.93f, 0.93f, 0.93f, 1.0f},
			.textSecondary = Colorf{0.6f, 0.6f, 0.65f, 1.0f},
			.accent = Colorf{0.3f, 0.6f, 1.0f, 1.0f},
			.button = ButtonTheme{
				.normal = Colorf{0.25f, 0.25f, 0.3f, 1.0f},
				.hovered = Colorf{0.35f, 0.35f, 0.42f, 1.0f},
				.pressed = Colorf{0.2f, 0.2f, 0.25f, 1.0f},
				.disabled = Colorf{0.15f, 0.15f, 0.18f, 1.0f}
			},
			.fontSizeTitle = 48.0f,
			.fontSizeBody = 24.0f,
			.fontSizeSmall = 16.0f
		};
	}

	/// @brief ライトテーマプリセット
	/// @return ライトテーマ
	[[nodiscard]] static constexpr Theme light() noexcept
	{
		return Theme{
			.background = Colorf{0.95f, 0.95f, 0.96f, 1.0f},
			.surface = Colorf{1.0f, 1.0f, 1.0f, 1.0f},
			.text = Colorf{0.1f, 0.1f, 0.1f, 1.0f},
			.textSecondary = Colorf{0.45f, 0.45f, 0.5f, 1.0f},
			.accent = Colorf{0.2f, 0.5f, 0.9f, 1.0f},
			.button = ButtonTheme{
				.normal = Colorf{0.85f, 0.85f, 0.88f, 1.0f},
				.hovered = Colorf{0.78f, 0.78f, 0.82f, 1.0f},
				.pressed = Colorf{0.7f, 0.7f, 0.74f, 1.0f},
				.disabled = Colorf{0.9f, 0.9f, 0.92f, 1.0f}
			},
			.fontSizeTitle = 48.0f,
			.fontSizeBody = 24.0f,
			.fontSizeSmall = 16.0f
		};
	}
};

} // namespace sgc::ui
