#pragma once

/// @file ScreenEnhanced.hpp
/// @brief 拡張描画レイヤー
/// @details Screen を拡張し、SDF テキスト・角丸・グラデーション・
///          シャドウ・ポストプロセスなど高品質な描画メソッドを提供する。
///          GPU バックエンドが未接続の場合は Screen の既存メソッドに
///          フォールバックする（ヘッドレス対応）。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>

namespace mitiru::render
{

// ── 前方宣言 ─────────────────────────────────────
class RenderPipeline2D;

// ── 列挙型 ──────────────────────────────────────

/// @brief ボタンの操作状態
enum class ButtonState
{
	Normal,
	Hovered,
	Pressed,
	Disabled
};

/// @brief 通知の種別
enum class NotificationType
{
	Info,
	Success,
	Warning,
	Error
};

/// @brief ツールチップの矢印方向
enum class ArrowDirection
{
	Up,
	Down,
	Left,
	Right
};

// ── スタイル構造体 ──────────────────────────────────

/// @brief パネルスタイル
/// @details 角丸・ボーダー・シャドウ・グラデーション・
///          フロストガラス等のスタイリングをまとめた構造体。
struct PanelStyle
{
	float cornerRadius = 0.0f;                                          ///< 角丸半径
	sgc::Colorf backgroundColor{0.2f, 0.2f, 0.2f, 1.0f};              ///< 背景色
	sgc::Colorf borderColor{0.4f, 0.4f, 0.4f, 1.0f};                  ///< ボーダー色
	float borderWidth = 0.0f;                                           ///< ボーダー幅
	sgc::Colorf shadowColor{0.0f, 0.0f, 0.0f, 0.5f};                  ///< シャドウ色
	float shadowBlur = 0.0f;                                            ///< シャドウぼかし半径
	sgc::Vec2f shadowOffset{0.0f, 0.0f};                               ///< シャドウオフセット
	float backdropBlur = 0.0f;                                          ///< フロストガラスぼかし量
	float opacity = 1.0f;                                               ///< 全体不透明度 [0..1]
	sgc::Colorf gradientFrom{0.0f, 0.0f, 0.0f, 0.0f};                 ///< グラデーション開始色
	sgc::Colorf gradientTo{0.0f, 0.0f, 0.0f, 0.0f};                   ///< グラデーション終了色
	float gradientAngle = 0.0f;                                         ///< グラデーション角度（度）
};

/// @brief ボタンスタイル
struct ButtonStyle
{
	PanelStyle normal;                                                  ///< 通常状態
	PanelStyle hovered;                                                 ///< ホバー状態
	PanelStyle pressed;                                                 ///< 押下状態
	PanelStyle disabled;                                                ///< 無効状態
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};                    ///< テキスト色
	float fontSize = 16.0f;                                             ///< フォントサイズ
};

/// @brief カードスタイル
struct CardStyle
{
	PanelStyle panel;                                                   ///< パネル部分
	sgc::Colorf titleColor{1.0f, 1.0f, 1.0f, 1.0f};                   ///< タイトル色
	sgc::Colorf bodyColor{0.8f, 0.8f, 0.8f, 1.0f};                    ///< 本文色
	float titleFontSize = 18.0f;                                        ///< タイトルフォントサイズ
	float bodyFontSize = 14.0f;                                         ///< 本文フォントサイズ
	float imageHeight = 120.0f;                                         ///< 画像領域の高さ
	float padding = 12.0f;                                              ///< 内部パディング
};

/// @brief プログレスバースタイル
struct ProgressBarStyle
{
	PanelStyle track;                                                   ///< トラック（背景）
	sgc::Colorf fillColorFrom{0.2f, 0.6f, 1.0f, 1.0f};               ///< フィルグラデーション開始色
	sgc::Colorf fillColorTo{0.4f, 0.8f, 1.0f, 1.0f};                 ///< フィルグラデーション終了色
	float cornerRadius = 4.0f;                                          ///< 角丸半径
	float height = 8.0f;                                                ///< バーの高さ
};

/// @brief ポストプロセス設定
struct PostProcessSettings
{
	bool bloomEnabled = false;          ///< ブルーム有効
	float bloomThreshold = 1.0f;        ///< ブルーム閾値
	float bloomIntensity = 0.5f;        ///< ブルーム強度
	bool vignetteEnabled = false;       ///< ビネット有効
	float vignetteIntensity = 0.3f;     ///< ビネット強度
	float brightness = 1.0f;            ///< 輝度補正
	float contrast = 1.0f;              ///< コントラスト
	float saturation = 1.0f;            ///< 彩度
};

// ── ScreenEnhanced ──────────────────────────────────

/// @brief 拡張描画レイヤー
/// @details Screen をラップし、高品質な描画メソッドを追加する。
///          SDF フォントが初期化されていない場合はビットマップフォントに
///          フォールバックし、GPU パイプラインが未接続でも安全に動作する。
///
/// @code
/// ScreenEnhanced enhanced(screen);
/// enhanced.drawRoundedRect(rect, 8.0f, panelColor);
/// enhanced.drawSdfText({10, 10}, "Hello", 24.0f, white);
/// enhanced.drawButton(btnRect, "OK", ButtonState::Normal, btnStyle);
/// @endcode
class ScreenEnhanced
{
public:
	/// @brief コンストラクタ
	/// @param screen 描画委譲先の Screen
	explicit ScreenEnhanced(Screen& screen) noexcept
		: m_screen(screen)
	{
	}

	/// @brief 内部 Screen への参照を取得する
	[[nodiscard]] Screen& screen() noexcept { return m_screen; }

	/// @brief 内部 Screen への const 参照を取得する
	[[nodiscard]] const Screen& screen() const noexcept { return m_screen; }

	/// @brief SDF フォントが利用可能か
	[[nodiscard]] bool hasSdfFont() const noexcept { return m_sdfFontReady; }

	/// @brief SDF フォントの初期化完了を通知する
	void setSdfFontReady(bool ready) noexcept { m_sdfFontReady = ready; }

	// ── SDF テキスト描画 ─────────────────────────────

	/// @brief SDF レンダリングによる滑らかなテキスト描画
	/// @details SDF フォント未初期化時はビットマップフォントにフォールバックする。
	/// @param pos 描画位置（左上）
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @param color テキスト色
	void drawSdfText(const sgc::Vec2f& pos, std::string_view text,
	                 float fontSize, const sgc::Colorf& color)
	{
		if (m_sdfFontReady)
		{
			/// SDF パスではフォントアトラスから距離フィールドを参照して
			/// ピクセルシェーダーで滑らかなエッジを生成する。
			/// 現在は GPU シェーダー統合前のためフォールバック。
			m_screen.drawText(pos, text, color, fontSize);
			return;
		}
		m_screen.drawText(pos, text, color, fontSize);
	}

	/// @brief アウトライン付き SDF テキスト描画
	/// @param pos 描画位置
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @param textColor テキスト色
	/// @param outlineColor アウトライン色
	/// @param outlineWidth アウトラインの太さ（ピクセル）
	void drawSdfTextWithOutline(const sgc::Vec2f& pos, std::string_view text,
	                            float fontSize, const sgc::Colorf& textColor,
	                            const sgc::Colorf& outlineColor,
	                            float outlineWidth)
	{
		if (m_sdfFontReady)
		{
			/// SDF パスではシェーダーで distance 閾値を2段階に分けて
			/// アウトラインを生成する。
			drawSdfTextOutlineFallback(pos, text, fontSize,
			                           textColor, outlineColor, outlineWidth);
			return;
		}
		drawSdfTextOutlineFallback(pos, text, fontSize,
		                           textColor, outlineColor, outlineWidth);
	}

	/// @brief ドロップシャドウ付き SDF テキスト描画
	/// @param pos 描画位置
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @param textColor テキスト色
	/// @param shadowColor シャドウ色
	/// @param shadowOffset シャドウオフセット
	void drawSdfTextWithShadow(const sgc::Vec2f& pos, std::string_view text,
	                           float fontSize, const sgc::Colorf& textColor,
	                           const sgc::Colorf& shadowColor,
	                           const sgc::Vec2f& shadowOffset)
	{
		/// シャドウを先に描画する（背面）
		m_screen.drawText(
			{pos.x + shadowOffset.x, pos.y + shadowOffset.y},
			text, shadowColor, fontSize);
		/// 本体テキストを描画する
		drawSdfText(pos, text, fontSize, textColor);
	}

	// ── 角丸・シャドウ付きシェイプ描画 ──────────────────────

	/// @brief アンチエイリアス角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param cornerRadius 角丸半径
	/// @param fillColor 塗りつぶし色
	void drawRoundedRect(const sgc::Rectf& rect, float cornerRadius,
	                     const sgc::Colorf& fillColor)
	{
		const float r = std::min(cornerRadius,
			std::min(rect.width(), rect.height()) * 0.5f);

		if (r < 1.0f)
		{
			m_screen.drawRect(rect, fillColor);
			return;
		}

		/// 角丸を近似するために中央十字＋4隅の扇形を三角形ファンで描画する
		drawRoundedRectFill(rect, r, fillColor);
	}

	/// @brief ボーダー付き角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param cornerRadius 角丸半径
	/// @param fillColor 塗りつぶし色
	/// @param borderColor ボーダー色
	/// @param borderWidth ボーダー幅
	void drawRoundedRectWithBorder(const sgc::Rectf& rect, float cornerRadius,
	                               const sgc::Colorf& fillColor,
	                               const sgc::Colorf& borderColor,
	                               float borderWidth)
	{
		/// 外側（ボーダー色）を先に描画する
		drawRoundedRect(rect, cornerRadius, borderColor);
		/// 内側（塗りつぶし色）を上に重ねる
		const sgc::Rectf inner{
			rect.x() + borderWidth,
			rect.y() + borderWidth,
			rect.width() - borderWidth * 2.0f,
			rect.height() - borderWidth * 2.0f
		};
		const float innerRadius = std::max(0.0f, cornerRadius - borderWidth);
		drawRoundedRect(inner, innerRadius, fillColor);
	}

	/// @brief シャドウ付き角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param cornerRadius 角丸半径
	/// @param fillColor 塗りつぶし色
	/// @param shadowColor シャドウ色
	/// @param shadowBlur シャドウぼかし半径
	/// @param shadowOffset シャドウオフセット
	void drawRoundedRectWithShadow(const sgc::Rectf& rect, float cornerRadius,
	                               const sgc::Colorf& fillColor,
	                               const sgc::Colorf& shadowColor,
	                               float shadowBlur,
	                               const sgc::Vec2f& shadowOffset)
	{
		/// シャドウを描画する（ぼかしを拡張した矩形で近似）
		drawShadow(rect, cornerRadius, shadowColor, shadowBlur, shadowOffset);
		/// 本体を描画する
		drawRoundedRect(rect, cornerRadius, fillColor);
	}

	/// @brief カプセル型矩形（ピル）を描画する
	/// @param rect 矩形領域
	/// @param fillColor 塗りつぶし色
	void drawPill(const sgc::Rectf& rect, const sgc::Colorf& fillColor)
	{
		const float radius = std::min(rect.width(), rect.height()) * 0.5f;
		drawRoundedRect(rect, radius, fillColor);
	}

	/// @brief PanelStyle に基づいた完全なパネル描画
	/// @param rect 矩形領域
	/// @param style パネルスタイル
	void drawPanel(const sgc::Rectf& rect, const PanelStyle& style)
	{
		/// シャドウ
		if (style.shadowBlur > 0.0f || style.shadowOffset.x != 0.0f ||
		    style.shadowOffset.y != 0.0f)
		{
			drawShadow(rect, style.cornerRadius, applyOpacity(style.shadowColor, style.opacity),
			           style.shadowBlur, style.shadowOffset);
		}

		/// フロストガラス背景（ぼかし量が設定されている場合）
		if (style.backdropBlur > 0.0f)
		{
			const sgc::Colorf frost{
				style.backgroundColor.r,
				style.backgroundColor.g,
				style.backgroundColor.b,
				style.backgroundColor.a * style.opacity * 0.7f
			};
			drawRoundedRect(rect, style.cornerRadius, frost);
		}

		/// 背景（グラデーションまたは単色）
		const bool hasGradient = (style.gradientFrom.a > 0.001f ||
		                          style.gradientTo.a > 0.001f);
		if (hasGradient)
		{
			drawLinearGradient(rect, style.gradientAngle,
			                   applyOpacity(style.gradientFrom, style.opacity),
			                   applyOpacity(style.gradientTo, style.opacity));
		}
		else
		{
			drawRoundedRect(rect, style.cornerRadius,
			                applyOpacity(style.backgroundColor, style.opacity));
		}

		/// ボーダー
		if (style.borderWidth > 0.0f && style.borderColor.a > 0.001f)
		{
			drawRoundedRectBorderOnly(rect, style.cornerRadius,
			                          applyOpacity(style.borderColor, style.opacity),
			                          style.borderWidth);
		}
	}

	// ── グラデーション描画 ──────────────────────────────

	/// @brief 線形グラデーション矩形を描画する
	/// @param rect 矩形領域
	/// @param angleDeg グラデーション角度（度、0=上→下、90=左→右）
	/// @param colorFrom 開始色
	/// @param colorTo 終了色
	void drawLinearGradient(const sgc::Rectf& rect, float angleDeg,
	                        const sgc::Colorf& colorFrom,
	                        const sgc::Colorf& colorTo)
	{
		/// 角度を簡易的に上→下 / 左→右で近似する
		/// （将来的にはピクセルシェーダーで任意角度対応）
		const float normalizedAngle = std::fmod(angleDeg, 360.0f);
		const bool horizontal = (normalizedAngle > 45.0f &&
		                         normalizedAngle < 135.0f) ||
		                        (normalizedAngle > 225.0f &&
		                         normalizedAngle < 315.0f);

		const int steps = std::max(
			1, static_cast<int>((horizontal ? rect.width() : rect.height()) / 4.0f));
		const float stepSize = (horizontal ? rect.width() : rect.height()) /
		                        static_cast<float>(steps);

		for (int i = 0; i < steps; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(steps);
			const sgc::Colorf color = lerpColor(colorFrom, colorTo, t);

			if (horizontal)
			{
				m_screen.drawRect(
					sgc::Rectf{
						rect.x() + static_cast<float>(i) * stepSize,
						rect.y(), stepSize, rect.height()},
					color);
			}
			else
			{
				m_screen.drawRect(
					sgc::Rectf{
						rect.x(),
						rect.y() + static_cast<float>(i) * stepSize,
						rect.width(), stepSize},
					color);
			}
		}
	}

	/// @brief 放射状グラデーション矩形を描画する
	/// @param rect 矩形領域
	/// @param centerColor 中心色
	/// @param edgeColor 端色
	void drawRadialGradient(const sgc::Rectf& rect,
	                        const sgc::Colorf& centerColor,
	                        const sgc::Colorf& edgeColor)
	{
		/// 同心矩形で近似する（将来はピクセルシェーダーで正確な円形に対応）
		const float cx = rect.x() + rect.width() * 0.5f;
		const float cy = rect.y() + rect.height() * 0.5f;
		const float maxRadius = std::max(rect.width(), rect.height()) * 0.5f;
		constexpr int RING_COUNT = 16;

		/// 外側から内側へ描画する
		for (int i = 0; i < RING_COUNT; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(RING_COUNT);
			const float scale = 1.0f - t;
			const sgc::Colorf color = lerpColor(edgeColor, centerColor, t);
			const float hw = rect.width() * 0.5f * scale;
			const float hh = rect.height() * 0.5f * scale;
			m_screen.drawRect(
				sgc::Rectf{cx - hw, cy - hh, hw * 2.0f, hh * 2.0f},
				color);
		}
		static_cast<void>(maxRadius);
	}

	// ── UI コンポジションヘルパー ───────────────────────────

	/// @brief 完全なボタンを描画する
	/// @param rect ボタン領域
	/// @param text ボタンテキスト
	/// @param state ボタンの操作状態
	/// @param style ボタンスタイル
	void drawButton(const sgc::Rectf& rect, std::string_view text,
	                ButtonState state, const ButtonStyle& style)
	{
		const PanelStyle& panelStyle = selectButtonPanel(state, style);
		drawPanel(rect, panelStyle);

		/// テキストを中央揃えで描画する
		const auto textSize = m_screen.measureText(text, style.fontSize);
		const float tx = rect.x() + (rect.width() - textSize.x) * 0.5f;
		const float ty = rect.y() + (rect.height() - textSize.y) * 0.5f;

		sgc::Colorf textColor = style.textColor;
		if (state == ButtonState::Disabled)
		{
			textColor = {textColor.r * 0.5f, textColor.g * 0.5f,
			             textColor.b * 0.5f, textColor.a * 0.5f};
		}
		drawSdfText({tx, ty}, text, style.fontSize, textColor);
	}

	/// @brief カード（画像＋タイトル＋本文）を描画する
	/// @param rect カード領域
	/// @param title タイトルテキスト
	/// @param body 本文テキスト
	/// @param imageTexture カード画像テクスチャ（nullptr で画像省略）
	/// @param style カードスタイル
	void drawCard(const sgc::Rectf& rect, std::string_view title,
	              std::string_view body,
	              const Texture* imageTexture,
	              const CardStyle& style)
	{
		drawPanel(rect, style.panel);

		float contentY = rect.y() + style.padding;

		/// 画像領域
		if (imageTexture != nullptr && imageTexture->valid())
		{
			const sgc::Rectf imgRect{
				rect.x(), rect.y(),
				rect.width(), style.imageHeight
			};
			m_screen.drawSprite(*imageTexture, imgRect);
			contentY = rect.y() + style.imageHeight + style.padding;
		}

		/// タイトル
		if (!title.empty())
		{
			const sgc::Rectf titleRect{
				rect.x() + style.padding, contentY,
				rect.width() - style.padding * 2.0f,
				style.titleFontSize * 1.5f
			};
			m_screen.drawTextInRect(titleRect, title, style.titleColor,
			                        style.titleFontSize,
			                        Screen::TextAlignH::Left,
			                        Screen::TextAlignV::Top, 0.0f, 0.0f);
			contentY += style.titleFontSize * 1.5f;
		}

		/// 本文
		if (!body.empty())
		{
			const sgc::Rectf bodyRect{
				rect.x() + style.padding, contentY,
				rect.width() - style.padding * 2.0f,
				rect.y() + rect.height() - contentY - style.padding
			};
			m_screen.drawTextWrapped(bodyRect, body, style.bodyColor,
			                         style.bodyFontSize, 0.0f, 0.0f);
		}
	}

	/// @brief ツールチップを描画する
	/// @param rect ツールチップ本体領域
	/// @param text ツールチップテキスト
	/// @param arrowDir 矢印の方向
	void drawTooltip(const sgc::Rectf& rect, std::string_view text,
	                 ArrowDirection arrowDir)
	{
		/// ツールチップ本体
		const PanelStyle tooltipStyle{
			/*.cornerRadius =*/ 6.0f,
			/*.backgroundColor =*/ {0.15f, 0.15f, 0.15f, 0.95f},
			/*.borderColor =*/ {0.3f, 0.3f, 0.3f, 0.8f},
			/*.borderWidth =*/ 1.0f,
			/*.shadowColor =*/ {0.0f, 0.0f, 0.0f, 0.4f},
			/*.shadowBlur =*/ 8.0f,
			/*.shadowOffset =*/ {0.0f, 2.0f}
		};
		drawPanel(rect, tooltipStyle);

		/// テキスト
		const sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 0.95f};
		m_screen.drawTextInRect(rect, text, textColor, 13.0f,
		                        Screen::TextAlignH::Center,
		                        Screen::TextAlignV::Middle, 8.0f, 4.0f);

		/// 矢印三角形を描画する
		drawTooltipArrow(rect, arrowDir, tooltipStyle.backgroundColor);
	}

	/// @brief 通知パネルを描画する
	/// @param rect 通知領域
	/// @param icon アイコン文字（例: "!" "i" など）
	/// @param title タイトル
	/// @param message メッセージ
	/// @param type 通知タイプ
	void drawNotification(const sgc::Rectf& rect, std::string_view icon,
	                      std::string_view title, std::string_view message,
	                      NotificationType type)
	{
		/// タイプ別アクセントカラーを選択する
		const sgc::Colorf accent = notificationAccentColor(type);

		PanelStyle panelStyle{
			/*.cornerRadius =*/ 8.0f,
			/*.backgroundColor =*/ {0.12f, 0.12f, 0.14f, 0.95f},
			/*.borderColor =*/ accent,
			/*.borderWidth =*/ 2.0f,
			/*.shadowColor =*/ {0.0f, 0.0f, 0.0f, 0.3f},
			/*.shadowBlur =*/ 12.0f,
			/*.shadowOffset =*/ {0.0f, 4.0f}
		};
		drawPanel(rect, panelStyle);

		/// アクセントバー（左端）
		const sgc::Rectf barRect{rect.x(), rect.y(), 4.0f, rect.height()};
		drawRoundedRect(barRect, 2.0f, accent);

		/// アイコン
		constexpr float ICON_SIZE = 24.0f;
		constexpr float PADDING = 12.0f;
		const float iconX = rect.x() + PADDING;
		const float iconY = rect.y() + (rect.height() - ICON_SIZE) * 0.5f;
		drawSdfText({iconX, iconY}, icon, ICON_SIZE, accent);

		/// タイトル・メッセージ
		const float textX = iconX + ICON_SIZE + 8.0f;
		const float textW = rect.x() + rect.width() - textX - PADDING;

		if (!title.empty())
		{
			const sgc::Rectf titleRect{textX, rect.y() + PADDING, textW, 20.0f};
			m_screen.drawTextInRect(titleRect, title,
			                        {1.0f, 1.0f, 1.0f, 1.0f}, 15.0f,
			                        Screen::TextAlignH::Left,
			                        Screen::TextAlignV::Top, 0.0f, 0.0f);
		}
		if (!message.empty())
		{
			const float msgY = rect.y() + PADDING + 22.0f;
			const sgc::Rectf msgRect{textX, msgY, textW,
			                         rect.y() + rect.height() - msgY - PADDING};
			m_screen.drawTextWrapped(msgRect, message,
			                         {0.75f, 0.75f, 0.75f, 1.0f}, 13.0f,
			                         0.0f, 0.0f);
		}
	}

	/// @brief プログレスバーを描画する
	/// @param rect 描画領域
	/// @param progress 進捗 [0..1]
	/// @param style プログレスバースタイル
	void drawProgressBar(const sgc::Rectf& rect, float progress,
	                     const ProgressBarStyle& style)
	{
		const float clamped = std::clamp(progress, 0.0f, 1.0f);

		/// トラック（背景）
		drawPanel(rect, style.track);

		/// フィル（前面）
		const float fillW = rect.width() * clamped;
		if (fillW > 0.0f)
		{
			const sgc::Rectf fillRect{rect.x(), rect.y(), fillW, rect.height()};
			drawLinearGradient(fillRect, 90.0f,
			                   style.fillColorFrom, style.fillColorTo);
			/// 角丸マスクとして本体角丸でクリップ（近似：角丸で再描画）
			/// 将来はステンシルバッファでクリッピングする
		}
	}

	// ── ポストプロセス統合 ───────────────────────────────

	/// @brief ポストプロセスパスの開始を宣言する
	/// @details この呼び出し以降のシーン描画はオフスクリーンバッファに
	///          レンダリングされ、endPostProcess() で後処理が適用される。
	void beginPostProcess()
	{
		m_postProcessActive = true;
		/// 将来的にはオフスクリーン RT にバインドする
	}

	/// @brief ポストプロセスパスを終了し、後処理を適用する
	/// @details beginPostProcess() で蓄積されたフレームに対して
	///          ブルーム・ビネット・カラーグレーディング等を適用し、
	///          最終バッファに合成する。
	void endPostProcess()
	{
		if (!m_postProcessActive) return;
		m_postProcessActive = false;
		/// 将来的にはポストプロセスシェーダーパスを実行する
	}

	/// @brief ブルームの有効/無効と設定を変更する
	/// @param enabled 有効フラグ
	/// @param threshold 輝度閾値
	/// @param intensity ブルーム強度
	void setBloom(bool enabled, float threshold = 1.0f,
	              float intensity = 0.5f) noexcept
	{
		m_postProcess.bloomEnabled = enabled;
		m_postProcess.bloomThreshold = threshold;
		m_postProcess.bloomIntensity = intensity;
	}

	/// @brief ビネットの有効/無効と設定を変更する
	/// @param enabled 有効フラグ
	/// @param intensity ビネット強度
	void setVignette(bool enabled, float intensity = 0.3f) noexcept
	{
		m_postProcess.vignetteEnabled = enabled;
		m_postProcess.vignetteIntensity = intensity;
	}

	/// @brief カラーグレーディングパラメータを設定する
	/// @param brightness 輝度倍率
	/// @param contrast コントラスト倍率
	/// @param saturation 彩度倍率
	void setColorGrading(float brightness, float contrast,
	                     float saturation) noexcept
	{
		m_postProcess.brightness = brightness;
		m_postProcess.contrast = contrast;
		m_postProcess.saturation = saturation;
	}

	/// @brief 現在のポストプロセス設定を取得する
	[[nodiscard]] const PostProcessSettings& postProcessSettings() const noexcept
	{
		return m_postProcess;
	}

private:
	Screen& m_screen;                          ///< 描画委譲先
	bool m_sdfFontReady = false;               ///< SDF フォント初期化済みフラグ
	bool m_postProcessActive = false;          ///< ポストプロセスパス中フラグ
	PostProcessSettings m_postProcess;         ///< ポストプロセス設定

	// ── 内部ヘルパー ────────────────────────────────

	/// @brief 2色を線形補間する
	[[nodiscard]] static sgc::Colorf lerpColor(const sgc::Colorf& a,
	                                           const sgc::Colorf& b,
	                                           float t) noexcept
	{
		return sgc::Colorf{
			a.r + (b.r - a.r) * t,
			a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t,
			a.a + (b.a - a.a) * t
		};
	}

	/// @brief 不透明度を色に適用する
	[[nodiscard]] static sgc::Colorf applyOpacity(const sgc::Colorf& color,
	                                              float opacity) noexcept
	{
		return sgc::Colorf{color.r, color.g, color.b, color.a * opacity};
	}

	/// @brief 角丸矩形の塗りつぶしを描画する
	/// @details 中央十字＋4隅の扇形を三角形ファンで分割して描画する。
	void drawRoundedRectFill(const sgc::Rectf& rect, float r,
	                         const sgc::Colorf& color)
	{
		/// 中央の十字部分（3つの矩形で構成）
		/// 上辺・下辺を角丸分だけ内側にした水平帯
		m_screen.drawRect(
			sgc::Rectf{rect.x() + r, rect.y(), rect.width() - r * 2.0f, rect.height()},
			color);
		/// 左側帯（角丸の内側のみ）
		m_screen.drawRect(
			sgc::Rectf{rect.x(), rect.y() + r, r, rect.height() - r * 2.0f},
			color);
		/// 右側帯
		m_screen.drawRect(
			sgc::Rectf{rect.x() + rect.width() - r, rect.y() + r, r, rect.height() - r * 2.0f},
			color);

		/// 4つの角を扇形で描画する
		constexpr int SEGMENTS = 8;
		drawCornerFan(rect.x() + r, rect.y() + r, r, 180.0f, 270.0f, SEGMENTS, color);
		drawCornerFan(rect.x() + rect.width() - r, rect.y() + r, r, 270.0f, 360.0f, SEGMENTS, color);
		drawCornerFan(rect.x() + rect.width() - r, rect.y() + rect.height() - r, r, 0.0f, 90.0f, SEGMENTS, color);
		drawCornerFan(rect.x() + r, rect.y() + rect.height() - r, r, 90.0f, 180.0f, SEGMENTS, color);
	}

	/// @brief 扇形を三角形ファンで描画する
	void drawCornerFan(float cx, float cy, float radius,
	                   float startDeg, float endDeg, int segments,
	                   const sgc::Colorf& color)
	{
		constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
		const float stepDeg = (endDeg - startDeg) / static_cast<float>(segments);

		const sgc::Vec2f center{cx, cy};

		for (int i = 0; i < segments; ++i)
		{
			const float a0 = (startDeg + stepDeg * static_cast<float>(i)) * DEG_TO_RAD;
			const float a1 = (startDeg + stepDeg * static_cast<float>(i + 1)) * DEG_TO_RAD;
			const sgc::Vec2f p0{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius};
			const sgc::Vec2f p1{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius};
			m_screen.drawTriangle(center, p0, p1, color);
		}
	}

public:
	/// @brief ボーダーのみを角丸矩形で描画する（外枠 - 内枠）
	void drawRoundedRectBorderOnly(const sgc::Rectf& rect, float cornerRadius,
	                               const sgc::Colorf& borderColor,
	                               float borderWidth)
	{
		/// 簡易実装: 4辺を矩形で描画する（角丸部分はコーナー扇形で処理）
		const float r = std::min(cornerRadius,
			std::min(rect.width(), rect.height()) * 0.5f);
		const float bw = std::min(borderWidth, r);

		/// 上辺
		m_screen.drawRect(
			sgc::Rectf{rect.x() + r, rect.y(), rect.width() - r * 2.0f, bw},
			borderColor);
		/// 下辺
		m_screen.drawRect(
			sgc::Rectf{rect.x() + r, rect.y() + rect.height() - bw,
			           rect.width() - r * 2.0f, bw},
			borderColor);
		/// 左辺
		m_screen.drawRect(
			sgc::Rectf{rect.x(), rect.y() + r, bw, rect.height() - r * 2.0f},
			borderColor);
		/// 右辺
		m_screen.drawRect(
			sgc::Rectf{rect.x() + rect.width() - bw, rect.y() + r,
			           bw, rect.height() - r * 2.0f},
			borderColor);

		/// 4隅のアーク
		constexpr int SEGMENTS = 8;
		drawCornerArc(rect.x() + r, rect.y() + r, r, bw, 180.0f, 270.0f, SEGMENTS, borderColor);
		drawCornerArc(rect.x() + rect.width() - r, rect.y() + r, r, bw, 270.0f, 360.0f, SEGMENTS, borderColor);
		drawCornerArc(rect.x() + rect.width() - r, rect.y() + rect.height() - r, r, bw, 0.0f, 90.0f, SEGMENTS, borderColor);
		drawCornerArc(rect.x() + r, rect.y() + rect.height() - r, r, bw, 90.0f, 180.0f, SEGMENTS, borderColor);
	}

private:
	/// @brief 角のアーク（枠線部分のみ）を描画する
	void drawCornerArc(float cx, float cy, float outerR, float thickness,
	                   float startDeg, float endDeg, int segments,
	                   const sgc::Colorf& color)
	{
		constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
		const float innerR = std::max(0.0f, outerR - thickness);
		const float stepDeg = (endDeg - startDeg) / static_cast<float>(segments);

		for (int i = 0; i < segments; ++i)
		{
			const float a0 = (startDeg + stepDeg * static_cast<float>(i)) * DEG_TO_RAD;
			const float a1 = (startDeg + stepDeg * static_cast<float>(i + 1)) * DEG_TO_RAD;

			const sgc::Vec2f outer0{cx + std::cos(a0) * outerR, cy + std::sin(a0) * outerR};
			const sgc::Vec2f outer1{cx + std::cos(a1) * outerR, cy + std::sin(a1) * outerR};
			const sgc::Vec2f inner0{cx + std::cos(a0) * innerR, cy + std::sin(a0) * innerR};
			const sgc::Vec2f inner1{cx + std::cos(a1) * innerR, cy + std::sin(a1) * innerR};

			m_screen.drawTriangle(outer0, outer1, inner0, color);
			m_screen.drawTriangle(inner0, outer1, inner1, color);
		}
	}

	/// @brief シャドウ（ぼかし近似）を描画する
	void drawShadow(const sgc::Rectf& rect, float cornerRadius,
	                const sgc::Colorf& shadowColor, float blur,
	                const sgc::Vec2f& offset)
	{
		/// ぼかしを同心の半透明レイヤーで近似する
		constexpr int SHADOW_LAYERS = 6;
		for (int i = SHADOW_LAYERS; i >= 1; --i)
		{
			const float expand = blur * static_cast<float>(i) /
			                     static_cast<float>(SHADOW_LAYERS);
			const float alpha = shadowColor.a *
			    (1.0f - static_cast<float>(i) / static_cast<float>(SHADOW_LAYERS + 1));

			const sgc::Rectf shadowRect{
				rect.x() + offset.x - expand,
				rect.y() + offset.y - expand,
				rect.width() + expand * 2.0f,
				rect.height() + expand * 2.0f
			};
			const sgc::Colorf layerColor{
				shadowColor.r, shadowColor.g, shadowColor.b, alpha
			};
			drawRoundedRect(shadowRect, cornerRadius + expand, layerColor);
		}
	}

	/// @brief SDF テキストアウトラインのフォールバック描画
	void drawSdfTextOutlineFallback(const sgc::Vec2f& pos, std::string_view text,
	                                float fontSize, const sgc::Colorf& textColor,
	                                const sgc::Colorf& outlineColor,
	                                float outlineWidth)
	{
		/// アウトラインを8方向オフセットで近似する
		const float ow = std::max(1.0f, outlineWidth);
		const sgc::Vec2f offsets[] = {
			{-ow, 0.0f}, {ow, 0.0f}, {0.0f, -ow}, {0.0f, ow},
			{-ow, -ow}, {ow, -ow}, {-ow, ow}, {ow, ow}
		};
		for (const auto& off : offsets)
		{
			m_screen.drawText({pos.x + off.x, pos.y + off.y},
			                  text, outlineColor, fontSize);
		}
		m_screen.drawText(pos, text, textColor, fontSize);
	}

	/// @brief ツールチップの矢印三角形を描画する
	void drawTooltipArrow(const sgc::Rectf& rect, ArrowDirection dir,
	                      const sgc::Colorf& color)
	{
		constexpr float ARROW_SIZE = 8.0f;
		const float cx = rect.x() + rect.width() * 0.5f;
		const float cy = rect.y() + rect.height() * 0.5f;

		sgc::Vec2f p0, p1, p2;
		switch (dir)
		{
		case ArrowDirection::Up:
			p0 = {cx, rect.y() - ARROW_SIZE};
			p1 = {cx - ARROW_SIZE, rect.y()};
			p2 = {cx + ARROW_SIZE, rect.y()};
			break;
		case ArrowDirection::Down:
			p0 = {cx, rect.y() + rect.height() + ARROW_SIZE};
			p1 = {cx - ARROW_SIZE, rect.y() + rect.height()};
			p2 = {cx + ARROW_SIZE, rect.y() + rect.height()};
			break;
		case ArrowDirection::Left:
			p0 = {rect.x() - ARROW_SIZE, cy};
			p1 = {rect.x(), cy - ARROW_SIZE};
			p2 = {rect.x(), cy + ARROW_SIZE};
			break;
		case ArrowDirection::Right:
			p0 = {rect.x() + rect.width() + ARROW_SIZE, cy};
			p1 = {rect.x() + rect.width(), cy - ARROW_SIZE};
			p2 = {rect.x() + rect.width(), cy + ARROW_SIZE};
			break;
		}
		m_screen.drawTriangle(p0, p1, p2, color);
	}

	/// @brief 通知タイプ別のアクセントカラーを取得する
	[[nodiscard]] static sgc::Colorf notificationAccentColor(
		NotificationType type) noexcept
	{
		switch (type)
		{
		case NotificationType::Info:
			return {0.2f, 0.6f, 1.0f, 1.0f};
		case NotificationType::Success:
			return {0.2f, 0.8f, 0.4f, 1.0f};
		case NotificationType::Warning:
			return {1.0f, 0.7f, 0.1f, 1.0f};
		case NotificationType::Error:
			return {1.0f, 0.25f, 0.25f, 1.0f};
		}
		return {0.5f, 0.5f, 0.5f, 1.0f};
	}

	/// @brief ボタン状態に対応するパネルスタイルを選択する
	[[nodiscard]] static const PanelStyle& selectButtonPanel(
		ButtonState state, const ButtonStyle& style) noexcept
	{
		switch (state)
		{
		case ButtonState::Hovered:  return style.hovered;
		case ButtonState::Pressed:  return style.pressed;
		case ButtonState::Disabled: return style.disabled;
		default:                    return style.normal;
		}
	}
};

} // namespace mitiru::render
