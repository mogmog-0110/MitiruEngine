#pragma once

/// @file UIRenderer.hpp
/// @brief UIノードツリーのレンダリングエンジン
/// @details UIStyleSheet・UIVisualState・UIAnimatorと連携し、
///          UINodeツリーを再帰的に描画する。各ノードのロールに応じた
///          専用レンダリング（ボタン・スライダー・プログレスバー等）を行う。

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/ui/UIAnimation.hpp>
#include <mitiru/ui/UINineSlice.hpp>
#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UIStyle.hpp>

namespace mitiru::ui
{

/// @brief ビジュアルステートマネージャ
/// @details 各ノードの現在のビジュアルステートを管理する。
///          入力システムから更新される。
class UIStateManager
{
	std::unordered_map<UINodeId, UIVisualState> m_states;

public:
	/// @brief ノードのビジュアルステートを設定する
	/// @param nodeId ノードID
	/// @param state ビジュアルステート
	void setState(UINodeId nodeId, UIVisualState state)
	{
		m_states[nodeId] = state;
	}

	/// @brief ノードのビジュアルステートを取得する
	/// @param nodeId ノードID
	/// @return ビジュアルステート（未登録の場合はNormal）
	[[nodiscard]] UIVisualState getState(UINodeId nodeId) const
	{
		const auto it = m_states.find(nodeId);
		if (it != m_states.end())
		{
			return it->second;
		}
		return UIVisualState::Normal;
	}

	/// @brief ノードのステートをクリアする
	/// @param nodeId ノードID
	void clearState(UINodeId nodeId)
	{
		m_states.erase(nodeId);
	}

	/// @brief 全ステートをクリアする
	void clearAll()
	{
		m_states.clear();
	}
};

/// @brief UIノードツリーのレンダラー
/// @details UIStyleSheet・UIStateManager・UIAnimatorと連携して
///          UINodeツリーを再帰的に描画する。
///
/// @code
/// mitiru::ui::UIRenderer renderer;
/// mitiru::ui::UIStyleSheet sheet;
/// mitiru::ui::UIStateManager stateManager;
/// mitiru::ui::UIAnimator animator;
///
/// sheet.buildDefaults(mitiru::ui::UITheme::dark());
///
/// // ゲームループ内
/// animator.update(dt);
/// renderer.render(screen, *rootNode, sheet, stateManager, &animator);
/// @endcode
class UIRenderer
{
	std::map<std::string, render::Texture> m_textureCache;

public:
	/// @brief テクスチャキャッシュを設定する
	/// @param cache テクスチャ名からテクスチャへのマッピング
	void setTextureCache(const std::map<std::string, render::Texture>& cache)
	{
		m_textureCache = cache;
	}

	/// @brief テクスチャキャッシュにテクスチャを追加する
	/// @param key テクスチャキー
	/// @param texture テクスチャ
	void addTexture(const std::string& key, const render::Texture& texture)
	{
		m_textureCache[key] = texture;
	}

	/// @brief UIノードツリー全体を描画する
	/// @param screen 描画先Screen
	/// @param root ルートノード
	/// @param sheet スタイルシート
	/// @param stateManager ビジュアルステートマネージャ
	/// @param animator アニメーター（nullptrの場合はアニメーションなし）
	void render(Screen& screen, const UINode& root,
	            const UIStyleSheet& sheet,
	            const UIStateManager& stateManager,
	            const UIAnimator* animator = nullptr) const
	{
		if (!root.visible()) return;
		renderNodeRecursive(screen, root, sheet, stateManager, animator);
	}

	/// @brief 単一ノードを指定スタイルで描画する
	/// @param screen 描画先Screen
	/// @param node 描画対象ノード
	/// @param boxStyle ボックススタイル
	/// @param textStyle テキストスタイル
	/// @param state ビジュアルステート
	/// @param animTransform アニメーション変換（nullptrの場合は変換なし）
	/// @param metrics ウィジェットメトリクス（nullptrの場合はデフォルト値）
	void renderNode(Screen& screen, const UINode& node,
	                const UIBoxStyle& boxStyle,
	                const UITextStyle& textStyle,
	                UIVisualState state,
	                const UIAnimationTransform* animTransform = nullptr,
	                const UIWidgetMetrics* metrics = nullptr) const
	{
		const auto bounds = computeAnimatedBounds(node.bounds(), animTransform);
		const float effectiveAlpha = boxStyle.opacity * (animTransform ? animTransform->alpha : 1.0f);

		if (effectiveAlpha <= 0.001f) return;

		static const UIWidgetMetrics defaultMetrics{};
		const auto& m = metrics ? *metrics : defaultMetrics;

		// 1. 影を描画する
		drawShadow(screen, bounds, boxStyle, effectiveAlpha);

		// 2. 背景を描画する
		drawBackground(screen, bounds, boxStyle, effectiveAlpha);

		// 3. 枠線を描画する
		drawBorder(screen, bounds, boxStyle, effectiveAlpha);

		// 4. ロール固有のコンテンツを描画する
		const auto contentRect = boxStyle.contentRect(bounds);
		drawRoleContent(screen, node, contentRect, boxStyle, textStyle, state, effectiveAlpha, m);
	}

private:
	/// @brief ノードを再帰的に描画する
	void renderNodeRecursive(Screen& screen, const UINode& node,
	                          const UIStyleSheet& sheet,
	                          const UIStateManager& stateManager,
	                          const UIAnimator* animator) const
	{
		if (!node.visible()) return;

		// スタイル解決
		const auto& stateStyles = sheet.resolveForNode(node);
		const auto visualState = stateManager.getState(node.id());
		const auto resolved = stateStyles.resolve(visualState);

		// アニメーション変換取得
		const UIAnimationTransform* animTransform = nullptr;
		UIAnimationTransform transform;
		if (animator && animator->isAnimating(node.id()))
		{
			transform = animator->getTransform(node.id());
			animTransform = &transform;
		}

		// ノード描画
		renderNode(screen, node, resolved.box, resolved.text, visualState, animTransform, &stateStyles.widgetMetrics);

		// 子ノードを再帰描画
		for (const auto& child : node.children())
		{
			if (child)
			{
				renderNodeRecursive(screen, *child, sheet, stateManager, animator);
			}
		}
	}

	/// @brief アニメーション変換を適用したバウンズを計算する
	[[nodiscard]] static sgc::Rectf computeAnimatedBounds(
		const sgc::Rectf& bounds,
		const UIAnimationTransform* transform) noexcept
	{
		if (!transform || transform->isIdentity())
		{
			return bounds;
		}

		const float cx = bounds.x() + bounds.width() * 0.5f;
		const float cy = bounds.y() + bounds.height() * 0.5f;

		const float newW = bounds.width() * transform->scaleX;
		const float newH = bounds.height() * transform->scaleY;

		return sgc::Rectf{
			cx - newW * 0.5f + transform->offsetX,
			cy - newH * 0.5f + transform->offsetY,
			newW,
			newH
		};
	}

	/// @brief 影を描画する
	void drawShadow(Screen& screen, const sgc::Rectf& bounds,
	                const UIBoxStyle& style, float alpha) const
	{
		if (style.shadowColor.a <= 0.01f) return;

		const sgc::Colorf shadowColor{
			style.shadowColor.r,
			style.shadowColor.g,
			style.shadowColor.b,
			style.shadowColor.a * alpha
		};

		if (style.shadowBlur > 0.0f)
		{
			// ぼかし付き影をグラデーション矩形で近似する
			const float blur = style.shadowBlur;
			const sgc::Rectf shadowRect{
				bounds.x() + style.shadowOffset.x - blur,
				bounds.y() + style.shadowOffset.y - blur,
				bounds.width() + blur * 2.0f,
				bounds.height() + blur * 2.0f
			};
			const sgc::Colorf outerColor{
				shadowColor.r, shadowColor.g, shadowColor.b, 0.0f
			};
			screen.drawGradientRect(shadowRect, shadowColor, outerColor);
		}
		else
		{
			const sgc::Rectf shadowRect{
				bounds.x() + style.shadowOffset.x,
				bounds.y() + style.shadowOffset.y,
				bounds.width(),
				bounds.height()
			};
			screen.drawRect(shadowRect, shadowColor);
		}
	}

	/// @brief 背景を描画する
	void drawBackground(Screen& screen, const sgc::Rectf& bounds,
	                     const UIBoxStyle& style, float alpha) const
	{
		// 9スライス背景
		if (style.backgroundNineSlice)
		{
			const auto& config = *style.backgroundNineSlice;
			const auto* tex = findTexture(config.textureId);
			if (tex)
			{
				drawNineSlice(screen.spriteBatch(), *tex, config, bounds, alpha);
				return;
			}
		}

		// テクスチャ背景
		if (style.backgroundImageKey)
		{
			const auto it = m_textureCache.find(*style.backgroundImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, bounds);
				return;
			}
		}

		// ソリッドカラー背景
		if (style.backgroundColor.a > 0.01f)
		{
			const sgc::Colorf bgColor{
				style.backgroundColor.r,
				style.backgroundColor.g,
				style.backgroundColor.b,
				style.backgroundColor.a * alpha
			};
			screen.drawRect(bounds, bgColor);
		}
	}

	/// @brief 枠線を描画する
	void drawBorder(Screen& screen, const sgc::Rectf& bounds,
	                const UIBoxStyle& style, float alpha) const
	{
		if (style.borderColor.a <= 0.01f) return;

		const sgc::Colorf borderColor{
			style.borderColor.r,
			style.borderColor.g,
			style.borderColor.b,
			style.borderColor.a * alpha
		};

		const float bt = style.borderWidth.top;
		const float br = style.borderWidth.right;
		const float bb = style.borderWidth.bottom;
		const float bl = style.borderWidth.left;

		// 上辺
		if (bt > 0.0f)
		{
			screen.drawRect(
				sgc::Rectf{bounds.x(), bounds.y(), bounds.width(), bt},
				borderColor);
		}
		// 下辺
		if (bb > 0.0f)
		{
			screen.drawRect(
				sgc::Rectf{bounds.x(), bounds.y() + bounds.height() - bb, bounds.width(), bb},
				borderColor);
		}
		// 左辺
		if (bl > 0.0f)
		{
			screen.drawRect(
				sgc::Rectf{bounds.x(), bounds.y() + bt, bl, bounds.height() - bt - bb},
				borderColor);
		}
		// 右辺
		if (br > 0.0f)
		{
			screen.drawRect(
				sgc::Rectf{bounds.x() + bounds.width() - br, bounds.y() + bt, br, bounds.height() - bt - bb},
				borderColor);
		}
	}

	/// @brief ロール固有のコンテンツを描画する
	void drawRoleContent(Screen& screen, const UINode& node,
	                      const sgc::Rectf& contentRect,
	                      const UIBoxStyle& boxStyle,
	                      const UITextStyle& textStyle,
	                      UIVisualState state,
	                      float alpha,
	                      const UIWidgetMetrics& metrics) const
	{
		const sgc::Colorf textColor{
			textStyle.color.r,
			textStyle.color.g,
			textStyle.color.b,
			textStyle.color.a * alpha
		};

		switch (node.role())
		{
		case UIRole::Label:
		case UIRole::ScoreLabel:
			drawAlignedText(screen, node.text(), contentRect, textStyle, textColor);
			break;

		case UIRole::Button:
			drawAlignedText(screen, node.text(), contentRect, textStyle, textColor);
			break;

		case UIRole::ProgressBar:
		case UIRole::HealthBar:
			drawProgressBar(screen, node, contentRect, boxStyle, textStyle, textColor, alpha, metrics);
			break;

		case UIRole::Image:
			drawImage(screen, node, contentRect, boxStyle, alpha);
			break;

		case UIRole::Slider:
			drawSlider(screen, node, contentRect, boxStyle, textStyle, textColor, alpha, metrics);
			break;

		case UIRole::Toggle:
			drawToggle(screen, node, contentRect, boxStyle, textStyle, textColor, alpha, metrics);
			break;

		case UIRole::TextInput:
			drawTextInput(screen, node, contentRect, boxStyle, textStyle, textColor, state, alpha, metrics);
			break;

		case UIRole::Dropdown:
			drawDropdown(screen, node, contentRect, boxStyle, textStyle, textColor, alpha, metrics);
			break;

		case UIRole::DialogBox:
		case UIRole::Tooltip:
			drawAlignedText(screen, node.text(), contentRect, textStyle, textColor);
			break;

		case UIRole::MenuItem:
			drawAlignedText(screen, node.text(), contentRect, textStyle, textColor);
			break;

		case UIRole::Container:
		case UIRole::Panel:
		case UIRole::MiniMap:
		case UIRole::Inventory:
		case UIRole::ListView:
		case UIRole::TabBar:
		case UIRole::Custom:
		default:
			// コンテナ類はコンテンツなし（子ノードのみ）
			break;
		}
	}

	/// @brief テキストを配置に応じて描画する
	void drawAlignedText(Screen& screen, const std::string& text,
	                      const sgc::Rectf& rect,
	                      const UITextStyle& style,
	                      const sgc::Colorf& color) const
	{
		if (text.empty()) return;

		// テキスト幅を概算する（BitmapFont: 各文字は fontSize 幅）
		const float charWidth = style.fontSize;
		const float totalSpacing = style.letterSpacing * static_cast<float>(text.size() > 0 ? text.size() - 1 : 0);
		const float textWidth = charWidth * static_cast<float>(text.size()) + totalSpacing;
		const float textHeight = style.fontSize;

		// オーバーフロー処理
		std::string displayText = text;
		if (style.overflow == UITextOverflow::Ellipsis && textWidth > rect.width())
		{
			const int maxChars = std::max(0, static_cast<int>(rect.width() / charWidth) - 3);
			if (maxChars > 0 && static_cast<std::size_t>(maxChars) < text.size())
			{
				displayText = text.substr(0, static_cast<std::size_t>(maxChars)) + "...";
			}
		}
		else if (style.overflow == UITextOverflow::Hidden && textWidth > rect.width())
		{
			const int maxChars = static_cast<int>(rect.width() / charWidth);
			if (maxChars > 0 && static_cast<std::size_t>(maxChars) < text.size())
			{
				displayText = text.substr(0, static_cast<std::size_t>(maxChars));
			}
		}

		// 水平配置
		float tx = rect.x();
		switch (style.textAlign)
		{
		case UITextAlign::Center:
		{
			const float displayWidth = charWidth * static_cast<float>(displayText.size());
			tx = rect.x() + (rect.width() - displayWidth) * 0.5f;
			break;
		}
		case UITextAlign::Right:
		{
			const float displayWidth = charWidth * static_cast<float>(displayText.size());
			tx = rect.x() + rect.width() - displayWidth;
			break;
		}
		case UITextAlign::Left:
		default:
			break;
		}

		// 垂直配置
		float ty = rect.y();
		switch (style.verticalAlign)
		{
		case UIVerticalAlign::Middle:
			ty = rect.y() + (rect.height() - textHeight) * 0.5f;
			break;
		case UIVerticalAlign::Bottom:
			ty = rect.y() + rect.height() - textHeight;
			break;
		case UIVerticalAlign::Top:
		default:
			break;
		}

		// テキスト影
		if (style.textShadowColor.a > 0.01f)
		{
			const sgc::Colorf shadowColor{
				style.textShadowColor.r,
				style.textShadowColor.g,
				style.textShadowColor.b,
				style.textShadowColor.a * color.a
			};
			screen.drawText(
				{tx + style.textShadowOffset.x, ty + style.textShadowOffset.y},
				displayText, shadowColor, style.fontSize);
		}

		// テキスト本体
		screen.drawText({tx, ty}, displayText, color, style.fontSize);
	}

	/// @brief プログレスバーを描画する
	void drawProgressBar(Screen& screen, const UINode& node,
	                      const sgc::Rectf& rect,
	                      const UIBoxStyle& boxStyle,
	                      const UITextStyle& textStyle,
	                      const sgc::Colorf& fillColor,
	                      float alpha,
	                      const UIWidgetMetrics& metrics) const
	{
		const float fill = (node.maxValue() > 0.0f)
			? std::clamp(node.value() / node.maxValue(), 0.0f, 1.0f)
			: 0.0f;

		const float barWidth = rect.width() * fill;
		const sgc::Rectf fillRect{rect.x(), rect.y(), barWidth, rect.height()};

		// フィル部分: 画像 → ジオメトリ
		if (!metrics.progressFillImageKey.empty())
		{
			const auto it = m_textureCache.find(metrics.progressFillImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, fillRect);
			}
			else
			{
				const sgc::Colorf barColor{
					fillColor.r, fillColor.g, fillColor.b, fillColor.a * alpha
				};
				screen.drawRect(fillRect, barColor);
			}
		}
		else
		{
			const sgc::Colorf barColor{
				fillColor.r, fillColor.g, fillColor.b, fillColor.a * alpha
			};
			screen.drawRect(fillRect, barColor);
		}

		// オプション: パーセンテージラベル（textStyle.colorを使用）
		if (!node.text().empty())
		{
			const sgc::Colorf labelColor{
				textStyle.color.r, textStyle.color.g, textStyle.color.b,
				textStyle.color.a * alpha
			};
			UITextStyle centered = textStyle;
			centered.textAlign = UITextAlign::Center;
			centered.verticalAlign = UIVerticalAlign::Middle;
			drawAlignedText(screen, node.text(), rect, centered, labelColor);
		}
	}

	/// @brief 画像を描画する
	void drawImage(Screen& screen, const UINode& node,
	               const sgc::Rectf& rect,
	               const UIBoxStyle& boxStyle,
	               float alpha) const
	{
		const auto imageKey = node.getProperty("image");
		if (!imageKey.empty())
		{
			const auto it = m_textureCache.find(imageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, rect);
				return;
			}
		}

		// フォールバック: プレースホルダー矩形（スタイルの背景色を使用）
		const sgc::Colorf placeholder{
			(boxStyle.backgroundColor.a > 0.01f) ? boxStyle.backgroundColor.r : 0.3f,
			(boxStyle.backgroundColor.a > 0.01f) ? boxStyle.backgroundColor.g : 0.3f,
			(boxStyle.backgroundColor.a > 0.01f) ? boxStyle.backgroundColor.b : 0.3f,
			(boxStyle.backgroundColor.a > 0.01f) ? boxStyle.backgroundColor.a * alpha : 0.5f * alpha
		};
		screen.drawRect(rect, placeholder);

		// Xマーク（スタイルの枠線色を使用）
		const sgc::Colorf lineColor{
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.r : 0.5f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.g : 0.5f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.b : 0.5f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.a * alpha : 0.8f * alpha
		};
		screen.drawLine(
			{rect.x(), rect.y()},
			{rect.x() + rect.width(), rect.y() + rect.height()},
			lineColor, 1.0f);
		screen.drawLine(
			{rect.x() + rect.width(), rect.y()},
			{rect.x(), rect.y() + rect.height()},
			lineColor, 1.0f);
	}

	/// @brief スライダーを描画する
	void drawSlider(Screen& screen, const UINode& node,
	                const sgc::Rectf& rect,
	                const UIBoxStyle& boxStyle,
	                const UITextStyle& textStyle,
	                const sgc::Colorf& accentColor,
	                float alpha,
	                const UIWidgetMetrics& metrics) const
	{
		const float fill = (node.maxValue() > 0.0f)
			? std::clamp(node.value() / node.maxValue(), 0.0f, 1.0f)
			: 0.0f;

		// トラック（全体）: 画像背景 → ジオメトリ
		const float trackHeight = std::max(metrics.sliderTrackMinHeight,
		                                    rect.height() * metrics.sliderTrackHeightRatio);
		const float trackY = rect.y() + (rect.height() - trackHeight) * 0.5f;
		const sgc::Rectf trackRect{rect.x(), trackY, rect.width(), trackHeight};

		drawBoxBackground(screen, trackRect, boxStyle, alpha);

		// フィル部分: 画像 → ジオメトリ
		const float fillWidth = rect.width() * fill;
		const sgc::Rectf fillRect{rect.x(), trackY, fillWidth, trackHeight};

		if (!metrics.sliderFillImageKey.empty())
		{
			const auto it = m_textureCache.find(metrics.sliderFillImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, fillRect);
			}
			else
			{
				const sgc::Colorf fillColor{
					accentColor.r, accentColor.g, accentColor.b, accentColor.a * alpha
				};
				screen.drawRect(fillRect, fillColor);
			}
		}
		else
		{
			const sgc::Colorf fillColor{
				accentColor.r, accentColor.g, accentColor.b, accentColor.a * alpha
			};
			screen.drawRect(fillRect, fillColor);
		}

		// ハンドル: 画像 → ジオメトリ
		const float handleRadius = rect.height() * metrics.sliderHandleRadiusRatio;
		const float handleX = rect.x() + fillWidth;
		const float handleY = rect.y() + rect.height() * 0.5f;

		if (!metrics.sliderHandleImageKey.empty())
		{
			const auto it = m_textureCache.find(metrics.sliderHandleImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				const sgc::Rectf handleRect{
					handleX - handleRadius, handleY - handleRadius,
					handleRadius * 2.0f, handleRadius * 2.0f
				};
				screen.drawSprite(it->second, handleRect);
			}
			else
			{
				const sgc::Colorf handleColor{1.0f, 1.0f, 1.0f, alpha};
				screen.drawCircle({handleX, handleY}, handleRadius, handleColor);
			}
		}
		else
		{
			const sgc::Colorf handleColor{1.0f, 1.0f, 1.0f, alpha};
			screen.drawCircle({handleX, handleY}, handleRadius, handleColor);
		}
	}

	/// @brief トグルを描画する
	void drawToggle(Screen& screen, const UINode& node,
	                const sgc::Rectf& rect,
	                const UIBoxStyle& boxStyle,
	                const UITextStyle& textStyle,
	                const sgc::Colorf& textColor,
	                float alpha,
	                const UIWidgetMetrics& metrics) const
	{
		const bool checked = (node.value() > 0.5f);

		// チェックボックスの枠
		const float boxSize = std::min(rect.height(), metrics.toggleBoxSize);
		const float boxY = rect.y() + (rect.height() - boxSize) * 0.5f;
		const sgc::Rectf boxRect{rect.x(), boxY, boxSize, boxSize};

		// トグル画像があれば使用
		const auto& toggleImageKey = checked ? metrics.toggleOnImageKey : metrics.toggleOffImageKey;
		if (!toggleImageKey.empty())
		{
			const auto it = m_textureCache.find(toggleImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, boxRect);
			}
			else
			{
				drawToggleGeometry(screen, boxRect, boxStyle, textColor, checked, alpha, metrics);
			}
		}
		else
		{
			drawToggleGeometry(screen, boxRect, boxStyle, textColor, checked, alpha, metrics);
		}

		// ラベル
		if (!node.text().empty())
		{
			const float labelX = rect.x() + boxSize + metrics.toggleLabelGap;
			const sgc::Rectf labelRect{
				labelX, rect.y(),
				rect.width() - boxSize - metrics.toggleLabelGap,
				rect.height()
			};
			UITextStyle labelStyle = textStyle;
			labelStyle.verticalAlign = UIVerticalAlign::Middle;
			drawAlignedText(screen, node.text(), labelRect, labelStyle, textColor);
		}
	}

	/// @brief トグルのジオメトリ描画（画像フォールバック）
	void drawToggleGeometry(Screen& screen, const sgc::Rectf& boxRect,
	                         const UIBoxStyle& boxStyle,
	                         const sgc::Colorf& textColor,
	                         bool checked, float alpha,
	                         const UIWidgetMetrics& metrics) const
	{
		// 背景（画像対応）
		drawBoxBackground(screen, boxRect, boxStyle, alpha);

		const sgc::Colorf borderColor{
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.r : 0.6f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.g : 0.6f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.b : 0.6f,
			(boxStyle.borderColor.a > 0.01f) ? boxStyle.borderColor.a * alpha : alpha
		};
		screen.drawRectFrame(boxRect, borderColor, 1.0f);

		if (checked)
		{
			const float innerMargin = metrics.toggleInnerMargin;
			const sgc::Rectf innerRect{
				boxRect.x() + innerMargin,
				boxRect.y() + innerMargin,
				boxRect.width() - innerMargin * 2.0f,
				boxRect.height() - innerMargin * 2.0f
			};
			const sgc::Colorf checkColor{
				textColor.r, textColor.g, textColor.b, textColor.a * alpha
			};
			screen.drawRect(innerRect, checkColor);
		}
	}

	/// @brief テキスト入力を描画する
	void drawTextInput(Screen& screen, const UINode& node,
	                    const sgc::Rectf& rect,
	                    const UIBoxStyle& boxStyle,
	                    const UITextStyle& textStyle,
	                    const sgc::Colorf& textColor,
	                    UIVisualState state,
	                    float alpha,
	                    const UIWidgetMetrics& metrics) const
	{
		// テキスト
		const std::string displayText = node.text().empty()
			? node.getProperty("placeholder")
			: node.text();

		sgc::Colorf displayColor = textColor;
		if (node.text().empty())
		{
			// プレースホルダー色（薄く表示）
			displayColor = sgc::Colorf{
				textColor.r, textColor.g, textColor.b, textColor.a * 0.5f
			};
		}

		drawAlignedText(screen, displayText, rect, textStyle, displayColor);

		// フォーカス時のカーソル
		if (state == UIVisualState::Focused)
		{
			const float charWidth = textStyle.fontSize;
			const float cursorX = rect.x() + charWidth * static_cast<float>(node.text().size());
			const float cursorMarginY = metrics.textInputCursorMarginY;
			const float cursorWidth = metrics.textInputCursorWidth;
			const sgc::Rectf cursorRect{
				cursorX, rect.y() + cursorMarginY,
				cursorWidth, rect.height() - cursorMarginY * 2.0f
			};

			if (!metrics.textInputCursorImageKey.empty())
			{
				const auto it = m_textureCache.find(metrics.textInputCursorImageKey);
				if (it != m_textureCache.end() && it->second.valid())
				{
					screen.drawSprite(it->second, cursorRect);
				}
				else
				{
					const sgc::Colorf cursorColor{
						textColor.r, textColor.g, textColor.b, alpha
					};
					screen.drawRect(cursorRect, cursorColor);
				}
			}
			else
			{
				const sgc::Colorf cursorColor{
					textColor.r, textColor.g, textColor.b, alpha
				};
				screen.drawRect(cursorRect, cursorColor);
			}
		}
	}

	/// @brief ドロップダウンを描画する
	void drawDropdown(Screen& screen, const UINode& node,
	                   const sgc::Rectf& rect,
	                   const UIBoxStyle& boxStyle,
	                   const UITextStyle& textStyle,
	                   const sgc::Colorf& textColor,
	                   float alpha,
	                   const UIWidgetMetrics& metrics) const
	{
		// 現在の値テキスト
		const std::string displayText = node.text().empty()
			? node.getProperty("placeholder")
			: node.text();

		const float arrowSpace = metrics.dropdownArrowSpace;
		const sgc::Rectf textRect{
			rect.x(), rect.y(),
			std::max(0.0f, rect.width() - arrowSpace),
			rect.height()
		};
		drawAlignedText(screen, displayText, textRect, textStyle, textColor);

		// 下向き矢印インジケータ
		const float arrowSize = metrics.dropdownArrowSize;
		const float arrowX = rect.x() + rect.width() - arrowSpace * 0.5f;
		const float arrowY = rect.y() + rect.height() * 0.5f;

		if (!metrics.dropdownArrowImageKey.empty())
		{
			const auto it = m_textureCache.find(metrics.dropdownArrowImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				const sgc::Rectf arrowRect{
					arrowX - arrowSize * 0.5f, arrowY - arrowSize * 0.5f,
					arrowSize, arrowSize
				};
				screen.drawSprite(it->second, arrowRect);
			}
			else
			{
				drawDropdownArrowTriangle(screen, arrowX, arrowY, arrowSize, textColor, alpha);
			}
		}
		else
		{
			drawDropdownArrowTriangle(screen, arrowX, arrowY, arrowSize, textColor, alpha);
		}
	}

	/// @brief ドロップダウン矢印の三角形を描画する
	void drawDropdownArrowTriangle(Screen& screen,
	                                float arrowX, float arrowY, float arrowSize,
	                                const sgc::Colorf& textColor, float alpha) const
	{
		const sgc::Colorf arrowColor{
			textColor.r, textColor.g, textColor.b, textColor.a * alpha
		};
		screen.drawTriangle(
			{arrowX - arrowSize * 0.5f, arrowY - arrowSize * 0.25f},
			{arrowX + arrowSize * 0.5f, arrowY - arrowSize * 0.25f},
			{arrowX, arrowY + arrowSize * 0.5f},
			arrowColor);
	}

	/// @brief ボックス背景を描画する（画像 → 9スライス → ソリッドカラー）
	/// @details UIBoxStyleの背景設定に応じて適切な描画方法を選択する。
	///          ウィジェット部品（トラック、チェックボックス等）の背景描画に使用。
	void drawBoxBackground(Screen& screen, const sgc::Rectf& rect,
	                        const UIBoxStyle& style, float alpha) const
	{
		// 9スライス背景
		if (style.backgroundNineSlice)
		{
			const auto& config = *style.backgroundNineSlice;
			const auto* tex = findTexture(config.textureId);
			if (tex)
			{
				drawNineSlice(screen.spriteBatch(), *tex, config, rect, alpha);
				return;
			}
		}

		// テクスチャ背景
		if (style.backgroundImageKey)
		{
			const auto it = m_textureCache.find(*style.backgroundImageKey);
			if (it != m_textureCache.end() && it->second.valid())
			{
				screen.drawSprite(it->second, rect);
				return;
			}
		}

		// ソリッドカラー背景
		if (style.backgroundColor.a > 0.01f)
		{
			const sgc::Colorf bgColor{
				style.backgroundColor.r,
				style.backgroundColor.g,
				style.backgroundColor.b,
				style.backgroundColor.a * alpha
			};
			screen.drawRect(rect, bgColor);
		}
	}

	/// @brief テクスチャIDからキャッシュを検索する
	/// @details textureIdの文字列表現をキーとして検索する。
	[[nodiscard]] const render::Texture* findTexture(std::uint32_t textureId) const
	{
		const auto key = std::to_string(textureId);
		const auto it = m_textureCache.find(key);
		if (it != m_textureCache.end())
		{
			return &it->second;
		}
		return nullptr;
	}
};

} // namespace mitiru::ui
