#pragma once

/// @file VisualPresets.hpp
/// @brief プリセットビジュアルスタイル集
/// @details 一般的な UI パターン（モダンパネル、グラスモーフィズム、
///          ネオングロー、フラットなど）の PanelStyle プリセットを提供する。
///          ScreenEnhanced::drawPanel() に直接渡して使用できる。

#include <mitiru/render/ScreenEnhanced.hpp>

namespace mitiru::render
{

/// @brief 定型ビジュアルスタイルのファクトリ
/// @details 各メソッドは PanelStyle を返す。状態を持たない静的クラス。
///
/// @code
/// ScreenEnhanced enhanced(screen);
/// enhanced.drawPanel(rect, VisualPresets::modernPanel());
/// enhanced.drawPanel(rect, VisualPresets::glassmorphism());
/// @endcode
struct VisualPresets
{
	VisualPresets() = delete;

	/// @brief モダンパネル。控えめなシャドウ、8px 角丸、微かなグラデーション
	[[nodiscard]] static PanelStyle modernPanel() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 8.0f;
		style.backgroundColor = {0.18f, 0.18f, 0.22f, 1.0f};
		style.borderColor = {0.3f, 0.3f, 0.35f, 0.6f};
		style.borderWidth = 1.0f;
		style.shadowColor = {0.0f, 0.0f, 0.0f, 0.25f};
		style.shadowBlur = 12.0f;
		style.shadowOffset = {0.0f, 4.0f};
		style.gradientFrom = {0.2f, 0.2f, 0.25f, 1.0f};
		style.gradientTo = {0.15f, 0.15f, 0.18f, 1.0f};
		style.gradientAngle = 0.0f;
		return style;
	}

	/// @brief グラスモーフィズム。フロストぼかし、半透明、薄いボーダー
	[[nodiscard]] static PanelStyle glassmorphism() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 12.0f;
		style.backgroundColor = {1.0f, 1.0f, 1.0f, 0.1f};
		style.borderColor = {1.0f, 1.0f, 1.0f, 0.2f};
		style.borderWidth = 1.0f;
		style.shadowColor = {0.0f, 0.0f, 0.0f, 0.15f};
		style.shadowBlur = 16.0f;
		style.shadowOffset = {0.0f, 8.0f};
		style.backdropBlur = 20.0f;
		style.opacity = 0.85f;
		return style;
	}

	/// @brief ネオングロー。明るいボーダーグロー
	[[nodiscard]] static PanelStyle neonGlow() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 6.0f;
		style.backgroundColor = {0.05f, 0.05f, 0.08f, 0.95f};
		style.borderColor = {0.0f, 0.8f, 1.0f, 0.9f};
		style.borderWidth = 2.0f;
		style.shadowColor = {0.0f, 0.8f, 1.0f, 0.4f};
		style.shadowBlur = 24.0f;
		style.shadowOffset = {0.0f, 0.0f};
		return style;
	}

	/// @brief フラットミニマル。シャドウなし、角丸なし、ソリッドカラー
	[[nodiscard]] static PanelStyle flatMinimal() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 0.0f;
		style.backgroundColor = {0.2f, 0.2f, 0.2f, 1.0f};
		style.borderColor = {0.0f, 0.0f, 0.0f, 0.0f};
		style.borderWidth = 0.0f;
		style.shadowBlur = 0.0f;
		return style;
	}

	/// @brief ゲーム HUD。HUD オーバーレイ向け半透明パネル
	[[nodiscard]] static PanelStyle gameHud() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 4.0f;
		style.backgroundColor = {0.0f, 0.0f, 0.0f, 0.6f};
		style.borderColor = {1.0f, 1.0f, 1.0f, 0.15f};
		style.borderWidth = 1.0f;
		style.shadowBlur = 0.0f;
		style.opacity = 0.9f;
		return style;
	}

	/// @brief ツールチップ。小さな角丸、軽いシャドウ
	[[nodiscard]] static PanelStyle tooltip() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 6.0f;
		style.backgroundColor = {0.15f, 0.15f, 0.15f, 0.95f};
		style.borderColor = {0.3f, 0.3f, 0.3f, 0.8f};
		style.borderWidth = 1.0f;
		style.shadowColor = {0.0f, 0.0f, 0.0f, 0.4f};
		style.shadowBlur = 8.0f;
		style.shadowOffset = {0.0f, 2.0f};
		return style;
	}

	/// @brief 通知パネル。タイプ別のアクセントカラー付き
	/// @param type 通知タイプ
	[[nodiscard]] static PanelStyle notification(NotificationType type) noexcept
	{
		PanelStyle style;
		style.cornerRadius = 8.0f;
		style.backgroundColor = {0.12f, 0.12f, 0.14f, 0.95f};
		style.borderWidth = 2.0f;
		style.shadowColor = {0.0f, 0.0f, 0.0f, 0.3f};
		style.shadowBlur = 12.0f;
		style.shadowOffset = {0.0f, 4.0f};

		switch (type)
		{
		case NotificationType::Info:
			style.borderColor = {0.2f, 0.6f, 1.0f, 1.0f};
			style.gradientFrom = {0.1f, 0.15f, 0.25f, 0.95f};
			style.gradientTo = {0.12f, 0.12f, 0.14f, 0.95f};
			break;
		case NotificationType::Success:
			style.borderColor = {0.2f, 0.8f, 0.4f, 1.0f};
			style.gradientFrom = {0.1f, 0.2f, 0.12f, 0.95f};
			style.gradientTo = {0.12f, 0.12f, 0.14f, 0.95f};
			break;
		case NotificationType::Warning:
			style.borderColor = {1.0f, 0.7f, 0.1f, 1.0f};
			style.gradientFrom = {0.25f, 0.2f, 0.1f, 0.95f};
			style.gradientTo = {0.12f, 0.12f, 0.14f, 0.95f};
			break;
		case NotificationType::Error:
			style.borderColor = {1.0f, 0.25f, 0.25f, 1.0f};
			style.gradientFrom = {0.25f, 0.1f, 0.1f, 0.95f};
			style.gradientTo = {0.12f, 0.12f, 0.14f, 0.95f};
			break;
		}

		return style;
	}

	/// @brief モーダルバックドロップ。ダーク半透明オーバーレイ
	[[nodiscard]] static PanelStyle modalBackdrop() noexcept
	{
		PanelStyle style;
		style.cornerRadius = 0.0f;
		style.backgroundColor = {0.0f, 0.0f, 0.0f, 0.6f};
		style.borderWidth = 0.0f;
		style.shadowBlur = 0.0f;
		style.backdropBlur = 8.0f;
		style.opacity = 1.0f;
		return style;
	}
};

} // namespace mitiru::render
