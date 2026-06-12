#pragma once

/// @file ScreenEnhanced_impl.hpp
/// @brief ScreenEnhanced の描画実装本体（ScreenEnhanced.hpp から機械的分割）

#include <mitiru/render/ScreenEnhanced.hpp>

namespace mitiru::render
{

// ── SDF テキスト描画 ─────────────────────────────

/// @brief SDF レンダリングによる滑らかなテキスト描画
inline void ScreenEnhanced::drawSdfText(const sgc::Vec2f& pos, std::string_view text,
                                        float fontSize, const sgc::Colorf& color)
{
	if (m_sdfFontReady)
	{
		/// SDF パスではフォントアトラスから距離フィールドを参照して
		/// ピクセルシェーダーで滑らかなエッジを生成する。
		/// 現在は GPU シェーダー統合前のためフォールバック。
		m_screen.text(text, pos.x, pos.y, color, fontSize);
		return;
	}
	m_screen.text(text, pos.x, pos.y, color, fontSize);
}

/// @brief アウトライン付き SDF テキスト描画
inline void ScreenEnhanced::drawSdfTextWithOutline(const sgc::Vec2f& pos, std::string_view text,
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
inline void ScreenEnhanced::drawSdfTextWithShadow(const sgc::Vec2f& pos, std::string_view text,
                                                  float fontSize, const sgc::Colorf& textColor,
                                                  const sgc::Colorf& shadowColor,
                                                  const sgc::Vec2f& shadowOffset)
{
	/// シャドウを先に描画する（背面）
	m_screen.text(text, pos.x + shadowOffset.x, pos.y + shadowOffset.y,
	              shadowColor, fontSize);
	/// 本体テキストを描画する
	drawSdfText(pos, text, fontSize, textColor);
}

// ── 角丸・シャドウ付きシェイプ描画 ──────────────────────

/// @brief アンチエイリアス角丸矩形を描画する
inline void ScreenEnhanced::drawRoundedRect(const sgc::Rectf& rect, float cornerRadius,
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
inline void ScreenEnhanced::drawRoundedRectWithBorder(const sgc::Rectf& rect, float cornerRadius,
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
inline void ScreenEnhanced::drawRoundedRectWithShadow(const sgc::Rectf& rect, float cornerRadius,
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
inline void ScreenEnhanced::drawPill(const sgc::Rectf& rect, const sgc::Colorf& fillColor)
{
	const float radius = std::min(rect.width(), rect.height()) * 0.5f;
	drawRoundedRect(rect, radius, fillColor);
}

/// @brief PanelStyle に基づいた完全なパネル描画
inline void ScreenEnhanced::drawPanel(const sgc::Rectf& rect, const PanelStyle& style)
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
inline void ScreenEnhanced::drawLinearGradient(const sgc::Rectf& rect, float angleDeg,
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
inline void ScreenEnhanced::drawRadialGradient(const sgc::Rectf& rect,
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
inline void ScreenEnhanced::drawButton(const sgc::Rectf& rect, std::string_view text,
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
inline void ScreenEnhanced::drawCard(const sgc::Rectf& rect, std::string_view title,
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
inline void ScreenEnhanced::drawTooltip(const sgc::Rectf& rect, std::string_view text,
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
inline void ScreenEnhanced::drawNotification(const sgc::Rectf& rect, std::string_view icon,
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
inline void ScreenEnhanced::drawProgressBar(const sgc::Rectf& rect, float progress,
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

// ── 内部ヘルパー ────────────────────────────────

/// @brief 2色を線形補間する
inline sgc::Colorf ScreenEnhanced::lerpColor(const sgc::Colorf& a,
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
inline sgc::Colorf ScreenEnhanced::applyOpacity(const sgc::Colorf& color,
                                                float opacity) noexcept
{
	return sgc::Colorf{color.r, color.g, color.b, color.a * opacity};
}

/// @brief 角丸矩形の塗りつぶしを描画する
inline void ScreenEnhanced::drawRoundedRectFill(const sgc::Rectf& rect, float r,
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
inline void ScreenEnhanced::drawCornerFan(float cx, float cy, float radius,
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

/// @brief ボーダーのみを角丸矩形で描画する（外枠 - 内枠）
inline void ScreenEnhanced::drawRoundedRectBorderOnly(const sgc::Rectf& rect, float cornerRadius,
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

/// @brief 角のアーク（枠線部分のみ）を描画する
inline void ScreenEnhanced::drawCornerArc(float cx, float cy, float outerR, float thickness,
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
inline void ScreenEnhanced::drawShadow(const sgc::Rectf& rect, float cornerRadius,
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
inline void ScreenEnhanced::drawSdfTextOutlineFallback(const sgc::Vec2f& pos, std::string_view text,
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
		m_screen.text(text, pos.x + off.x, pos.y + off.y, outlineColor, fontSize);
	}
	m_screen.text(text, pos.x, pos.y, textColor, fontSize);
}

/// @brief ツールチップの矢印三角形を描画する
inline void ScreenEnhanced::drawTooltipArrow(const sgc::Rectf& rect, ArrowDirection dir,
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
inline sgc::Colorf ScreenEnhanced::notificationAccentColor(
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
inline const PanelStyle& ScreenEnhanced::selectButtonPanel(
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

} // namespace mitiru::render
