#pragma once

/// @file VNRenderer.hpp
/// @brief VN UI要素のScreen描画ヘルパー
/// @details VNTextureManagerとScreenを組み合わせ、スキンのテクスチャを使った
///          VN UI要素（メッセージウィンドウ、選択肢、キャラクタースプライト等）の
///          描画を提供する。9-sliceテクスチャのScreen::drawSprite分割描画が核。
///
/// @code
/// mitiru::vn::VNTextureManager texMgr("assets/vn/");
/// texMgr.loadFromSkin(skin);
///
/// screen.clear();
/// mitiru::vn::VNRenderer::drawMessageWindow(
///     screen, config, texMgr, "Hello!", "Alice", 6, 0.0f);
/// screen.present();
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/vn/NineSlice.hpp>
#include <mitiru/vn/UISkinLoader.hpp>
#include <mitiru/vn/VNTextureManager.hpp>

namespace mitiru::vn
{

/// @brief 9-sliceテクスチャ描画用のソース領域定義
struct NineSliceRegion
{
	sgc::Rectf srcRect;  ///< ソーステクスチャ内のピクセル領域
	sgc::Rectf dstRect;  ///< 描画先のスクリーン領域
};

/// @brief VN UI要素のScreen描画ヘルパー（ステートレス・static関数群）
/// @details Screen::drawSpriteを使ってテクスチャベースのUI描画を行う。
///          9-slice描画ではソーステクスチャを9領域に分割し、
///          各領域をScreen::drawSpriteで個別に描画する。
class VNRenderer
{
public:
	// ── 9-slice描画（核となる機能）──────────────────────────

	/// @brief テクスチャを9-sliceで描画する
	/// @details ソーステクスチャをコーナー・エッジ・センターの9領域に分割し、
	///          コーナーは固定サイズ、エッジは一方向に伸長、センターは両方向に伸長して
	///          Screen::drawSpriteで各領域を個別描画する。
	/// @param screen 描画先Screen
	/// @param texture ソーステクスチャ
	/// @param config 9-slice設定（コーナーサイズ、インセット等）
	/// @param dest 描画先矩形
	/// @param alpha アルファ値（0.0-1.0、テクスチャに乗算）
	static void drawNineSliceImage(Screen& screen,
	                               const render::Texture& texture,
	                               const NineSliceConfig& config,
	                               const sgc::Rectf& dest,
	                               float alpha = 1.0f)
	{
		if (!texture.valid()) { return; }

		const float il = config.edgeInsetLeft;
		const float ir = config.edgeInsetRight;
		const float it = config.edgeInsetTop;
		const float ib = config.edgeInsetBottom;
		const float tw = static_cast<float>(texture.width());
		const float th = static_cast<float>(texture.height());

		// ソーステクスチャの3列×3行の境界（ピクセル座標）
		const float sx0 = 0.0f;
		const float sx1 = il;
		const float sx2 = tw - ir;
		const float sx3 = tw;

		const float sy0 = 0.0f;
		const float sy1 = it;
		const float sy2 = th - ib;
		const float sy3 = th;

		// 描画先の3列×3行の境界（スクリーン座標）
		const float dx0 = dest.x();
		const float dx1 = dest.x() + config.cornerW;
		const float dx2 = dest.x() + dest.width() - config.cornerW;
		const float dx3 = dest.x() + dest.width();

		const float dy0 = dest.y();
		const float dy1 = dest.y() + config.cornerH;
		const float dy2 = dest.y() + dest.height() - config.cornerH;
		const float dy3 = dest.y() + dest.height();

		// 9領域を個別描画
		// Row 0: top-left, top-edge, top-right
		drawSubRegion(screen, texture,
			sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, alpha);
		drawSubRegion(screen, texture,
			sx1, sy0, sx2, sy1, dx1, dy0, dx2, dy1, alpha);
		drawSubRegion(screen, texture,
			sx2, sy0, sx3, sy1, dx2, dy0, dx3, dy1, alpha);

		// Row 1: left-edge, center, right-edge
		drawSubRegion(screen, texture,
			sx0, sy1, sx1, sy2, dx0, dy1, dx1, dy2, alpha);
		drawSubRegion(screen, texture,
			sx1, sy1, sx2, sy2, dx1, dy1, dx2, dy2, alpha);
		drawSubRegion(screen, texture,
			sx2, sy1, sx3, sy2, dx2, dy1, dx3, dy2, alpha);

		// Row 2: bottom-left, bottom-edge, bottom-right
		drawSubRegion(screen, texture,
			sx0, sy2, sx1, sy3, dx0, dy2, dx1, dy3, alpha);
		drawSubRegion(screen, texture,
			sx1, sy2, sx2, sy3, dx1, dy2, dx2, dy3, alpha);
		drawSubRegion(screen, texture,
			sx2, sy2, sx3, sy3, dx2, dy2, dx3, dy3, alpha);
	}

	/// @brief 均一コーナーサイズで9-slice描画する（簡易版）
	/// @param screen 描画先Screen
	/// @param texture ソーステクスチャ
	/// @param cornerSize コーナーの幅・高さ（ソースピクセル単位）
	/// @param dest 描画先矩形
	/// @param alpha アルファ値
	static void drawNineSliceImage(Screen& screen,
	                               const render::Texture& texture,
	                               float cornerSize,
	                               const sgc::Rectf& dest,
	                               float alpha = 1.0f)
	{
		const auto config = NineSliceConfig::uniform(
			0,
			cornerSize,
			static_cast<float>(texture.width()),
			static_cast<float>(texture.height()));
		drawNineSliceImage(screen, texture, config, dest, alpha);
	}

	// ── メッセージウィンドウ描画 ───────────────────────────────

	/// @brief メッセージウィンドウを描画する
	/// @param screen 描画先Screen
	/// @param config メッセージウィンドウ設定
	/// @param texMgr テクスチャマネージャ
	/// @param text 表示テキスト
	/// @param speaker 話者名（空で省略）
	/// @param visibleChars 表示文字数（0で全文表示）
	/// @param indicatorTimer 待機アイコンの点滅タイマー（秒）
	static void drawMessageWindow(Screen& screen,
	                              const MessageWindowConfig& config,
	                              const VNTextureManager& texMgr,
	                              std::string_view text,
	                              std::string_view speaker,
	                              std::size_t visibleChars = 0,
	                              float indicatorTimer = 0.0f)
	{
		const sgc::Rectf& bounds = config.bounds;

		// --- 背景描画 ---
		drawWindowBackground(screen, config, texMgr, bounds);

		// --- ネームプレート描画 ---
		if (config.showNamePlate && !speaker.empty())
		{
			drawNamePlate(screen, config, texMgr, speaker);
		}

		// --- テキスト描画 ---
		const sgc::Rectf textArea{
			bounds.x() + config.paddingLeft,
			bounds.y() + config.paddingTop,
			bounds.width() - config.paddingLeft - config.paddingRight,
			bounds.height() - config.paddingTop - config.paddingBottom
		};

		const std::size_t chars = (visibleChars == 0) ? text.size() : visibleChars;
		const auto displayText = text.substr(0, std::min(chars, text.size()));
		drawWrappedText(screen, displayText, textArea, config.textColor, config.fontSize);

		// --- 待機アイコン描画 ---
		if (config.clickWait.enabled && visibleChars >= text.size() && !text.empty())
		{
			drawWaitIcon(screen, config, texMgr, bounds, indicatorTimer);
		}
	}

	// ── 選択肢ボタン描画 ─────────────────────────────────────

	/// @brief 選択肢ボタンを描画する
	/// @param screen 描画先Screen
	/// @param rect ボタンの矩形領域
	/// @param text ボタンテキスト
	/// @param isSelected 選択状態か
	/// @param texMgr テクスチャマネージャ
	/// @param skin 選択肢ボタンスキン（色・画像情報）
	static void drawChoiceButton(Screen& screen,
	                             const sgc::Rectf& rect,
	                             std::string_view text,
	                             bool isSelected,
	                             const VNTextureManager& texMgr,
	                             const ChoiceButtonSkin& skin = {})
	{
		const auto state = isSelected ? ChoiceButtonState::Selected : ChoiceButtonState::Normal;
		const auto& elemState = isSelected ? skin.selected : skin.normal;

		// 背景：テクスチャがあれば使用、なければソリッドカラー
		const auto* bgTex = texMgr.choiceButtonTexture(state);
		if (bgTex != nullptr)
		{
			screen.drawSprite(*bgTex, rect);
		}
		else
		{
			screen.drawRect(rect, elemState.backgroundColor);
			if (elemState.borderWidth > 0.0f)
			{
				screen.drawRectFrame(rect, elemState.borderColor, elemState.borderWidth);
			}
		}

		// テキスト（左パディング付き、垂直中央寄せ）
		if (!text.empty())
		{
			const float fontSize = 16.0f;
			const float tx = rect.x() + skin.padding.left;
			const float ty = rect.y() + (rect.height() - fontSize) * 0.5f;
			screen.text(text, tx, ty, elemState.textColor, fontSize);
		}
	}

	// ── キャラクタースプライト描画 ────────────────────────────

	/// @brief キャラクタースプライトを描画する
	/// @param screen 描画先Screen
	/// @param texture キャラクターテクスチャ
	/// @param position 描画位置（下端中央基準）
	/// @param alpha 不透明度（0.0-1.0）
	/// @param scale スケール倍率
	static void drawCharacterSprite(Screen& screen,
	                                const render::Texture& texture,
	                                const sgc::Vec2f& position,
	                                float alpha = 1.0f,
	                                float scale = 1.0f)
	{
		if (!texture.valid()) { return; }

		const float w = static_cast<float>(texture.width()) * scale;
		const float h = static_cast<float>(texture.height()) * scale;

		// 下端中央基準で配置
		const sgc::Rectf dstRect{
			position.x - w * 0.5f,
			position.y - h,
			w,
			h
		};

		if (alpha >= 1.0f)
		{
			screen.drawSprite(texture, dstRect);
		}
		else
		{
			// アルファ付きスプライト描画：テクスチャのピクセルを直接描画
			drawSpriteWithAlpha(screen, texture, dstRect, alpha);
		}
	}

	// ── バックログエントリ描画 ────────────────────────────────

	/// @brief バックログの1エントリを描画する
	/// @param screen 描画先Screen
	/// @param rect エントリの描画領域
	/// @param text セリフテキスト
	/// @param speaker 話者名
	/// @param texMgr テクスチャマネージャ（ボイスボタン画像用）
	/// @param hasVoice ボイス再生ボタンを表示するか
	/// @param textColor テキストの色
	/// @param speakerColor 話者名の色
	static void drawBacklogEntry(Screen& screen,
	                             const sgc::Rectf& rect,
	                             std::string_view text,
	                             std::string_view speaker,
	                             [[maybe_unused]] const VNTextureManager& texMgr,
	                             bool hasVoice = false,
	                             const sgc::Colorf& textColor = {1.0f, 1.0f, 1.0f, 1.0f},
	                             const sgc::Colorf& speakerColor = {0.6f, 0.8f, 1.0f, 1.0f})
	{
		// 背景（薄いストライプ）
		screen.drawRect(rect, sgc::Colorf{0.05f, 0.05f, 0.1f, 0.4f});

		float yOffset = rect.y() + 4.0f;
		const float leftMargin = rect.x() + 8.0f;

		// 話者名
		if (!speaker.empty())
		{
			screen.text(speaker, leftMargin, yOffset, speakerColor, 14.0f);
			yOffset += 18.0f;
		}

		// テキスト
		const float textAreaWidth = rect.width() - 16.0f - (hasVoice ? 32.0f : 0.0f);
		const sgc::Rectf textArea{leftMargin, yOffset, textAreaWidth, rect.height() - (yOffset - rect.y()) - 4.0f};
		drawWrappedText(screen, text, textArea, textColor, 14.0f);

		// ボイスボタン（簡易矩形）
		if (hasVoice)
		{
			const float btnSize = 24.0f;
			const sgc::Rectf btnRect{
				rect.x() + rect.width() - btnSize - 8.0f,
				rect.y() + (rect.height() - btnSize) * 0.5f,
				btnSize,
				btnSize
			};
			screen.drawRect(btnRect, sgc::Colorf{0.3f, 0.5f, 0.8f, 0.7f});
			// 再生アイコン（三角形）
			const sgc::Vec2f p0{btnRect.x() + 6.0f, btnRect.y() + 4.0f};
			const sgc::Vec2f p1{btnRect.x() + 6.0f, btnRect.y() + btnSize - 4.0f};
			const sgc::Vec2f p2{btnRect.x() + btnSize - 6.0f, btnRect.y() + btnSize * 0.5f};
			screen.drawTriangle(p0, p1, p2, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.9f});
		}
	}

private:
	// ── ウィンドウ背景描画の内部ヘルパー ───────────────────────

	static void drawWindowBackground(Screen& screen,
	                                 const MessageWindowConfig& config,
	                                 const VNTextureManager& texMgr,
	                                 const sgc::Rectf& bounds)
	{
		switch (config.skin.type)
		{
		case WindowSkinType::Image9Slice:
		{
			const auto* tex = texMgr.messageWindowTexture();
			if (tex != nullptr)
			{
				drawNineSliceImage(screen, *tex, config.skin.nineSlice, bounds);
			}
			else
			{
				// フォールバック：ソリッドカラー
				drawSolidBackground(screen, config.skin.solidColor, bounds);
			}
			break;
		}
		case WindowSkinType::SolidColor:
			drawSolidBackground(screen, config.skin.solidColor, bounds);
			break;
		case WindowSkinType::Custom:
			// カスタムレンダラーはSpriteBatch経由のため、ここではソリッドにフォールバック
			drawSolidBackground(screen, config.skin.solidColor, bounds);
			break;
		}
	}

	static void drawSolidBackground(Screen& screen,
	                                const SolidColorSkin& skin,
	                                const sgc::Rectf& rect)
	{
		screen.drawRect(rect, skin.fillColor);
		if (skin.borderWidth > 0.0f)
		{
			screen.drawRectFrame(rect, skin.borderColor, skin.borderWidth);
		}
	}

	// ── ネームプレート描画 ────────────────────────────────────

	static void drawNamePlate(Screen& screen,
	                          const MessageWindowConfig& config,
	                          const VNTextureManager& texMgr,
	                          std::string_view speaker)
	{
		const sgc::Rectf& npRect = config.namePlateBounds;

		// 背景
		const auto* npTex = texMgr.namePlateTexture();
		if (npTex != nullptr)
		{
			screen.drawSprite(*npTex, npRect);
		}
		else
		{
			screen.drawRect(npRect, config.namePlateColor);
			screen.drawRectFrame(npRect, config.namePlateBorder, 1.0f);
		}

		// テキスト
		const float tx = npRect.x() + 8.0f;
		const float ty = npRect.y() + (npRect.height() - config.nameFontSize) * 0.5f;
		screen.text(speaker, tx, ty, config.nameTextColor, config.nameFontSize);
	}

	// ── 待機アイコン描画 ──────────────────────────────────────

	static void drawWaitIcon(Screen& screen,
	                         const MessageWindowConfig& config,
	                         const VNTextureManager& texMgr,
	                         const sgc::Rectf& bounds,
	                         float timer)
	{
		const auto& cw = config.clickWait;
		const float blinkAlpha = (std::sin(timer * cw.blinkSpeed * 6.2831853f) + 1.0f) * 0.5f;

		const float x = bounds.x() + bounds.width() + cw.offsetX;
		const float y = bounds.y() + bounds.height() + cw.offsetY;

		const auto* iconTex = texMgr.waitIconTexture();
		if (iconTex != nullptr)
		{
			const sgc::Rectf iconRect{x, y, cw.size, cw.size};
			drawSpriteWithAlpha(screen, *iconTex, iconRect, blinkAlpha);
		}
		else
		{
			// フォールバック：小さな矩形で点滅表示
			auto col = cw.color;
			col.a *= blinkAlpha;
			const sgc::Rectf indicator{x, y, cw.size, cw.size};
			screen.drawRect(indicator, col);
		}
	}

	// ── テクスチャサブ領域描画 ────────────────────────────────

	/// @brief テクスチャの一部をScreenに描画する
	/// @details ソーステクスチャからピクセル範囲を切り出し、
	///          一時的なサブテクスチャを作成してdrawSpriteで描画する。
	static void drawSubRegion(Screen& screen,
	                          const render::Texture& texture,
	                          float sx0, float sy0, float sx1, float sy1,
	                          float dx0, float dy0, float dx1, float dy1,
	                          float alpha)
	{
		const float srcW = sx1 - sx0;
		const float srcH = sy1 - sy0;
		const float dstW = dx1 - dx0;
		const float dstH = dy1 - dy0;

		if (srcW <= 0.0f || srcH <= 0.0f || dstW <= 0.0f || dstH <= 0.0f) { return; }

		const int isx0 = std::max(0, static_cast<int>(sx0));
		const int isy0 = std::max(0, static_cast<int>(sy0));
		const int isx1 = std::min(texture.width(), static_cast<int>(sx1 + 0.5f));
		const int isy1 = std::min(texture.height(), static_cast<int>(sy1 + 0.5f));
		const int subW = isx1 - isx0;
		const int subH = isy1 - isy0;

		if (subW <= 0 || subH <= 0) { return; }

		// サブテクスチャを切り出す
		std::vector<std::uint8_t> subPixels(
			static_cast<std::size_t>(subW) * subH * 4);
		const auto& srcPixels = texture.pixels();

		for (int y = 0; y < subH; ++y)
		{
			for (int x = 0; x < subW; ++x)
			{
				const auto srcIdx = static_cast<std::size_t>(
					((isy0 + y) * texture.width() + (isx0 + x)) * 4);
				const auto dstIdx = static_cast<std::size_t>(
					(y * subW + x) * 4);

				if (srcIdx + 3 < srcPixels.size())
				{
					subPixels[dstIdx + 0] = srcPixels[srcIdx + 0];
					subPixels[dstIdx + 1] = srcPixels[srcIdx + 1];
					subPixels[dstIdx + 2] = srcPixels[srcIdx + 2];

					// アルファ値にglobal alphaを乗算
					const float srcAlpha = srcPixels[srcIdx + 3] / 255.0f;
					subPixels[dstIdx + 3] = static_cast<std::uint8_t>(
						srcAlpha * alpha * 255.0f);
				}
			}
		}

		const render::Texture subTex(subW, subH, subPixels);
		const sgc::Rectf dstRect{dx0, dy0, dstW, dstH};
		screen.drawSprite(subTex, dstRect);
	}

	/// @brief アルファ付きでスプライト全体を描画する
	/// @details テクスチャの各ピクセルのアルファにglobalAlphaを乗算して描画する。
	static void drawSpriteWithAlpha(Screen& screen,
	                                const render::Texture& texture,
	                                const sgc::Rectf& dstRect,
	                                float globalAlpha)
	{
		if (!texture.valid() || globalAlpha <= 0.0f) { return; }

		if (globalAlpha >= 1.0f)
		{
			screen.drawSprite(texture, dstRect);
			return;
		}

		// アルファ変調した一時テクスチャを生成
		const auto& srcPixels = texture.pixels();
		std::vector<std::uint8_t> modPixels(srcPixels.size());

		for (std::size_t i = 0; i < srcPixels.size(); i += 4)
		{
			modPixels[i + 0] = srcPixels[i + 0];
			modPixels[i + 1] = srcPixels[i + 1];
			modPixels[i + 2] = srcPixels[i + 2];
			modPixels[i + 3] = static_cast<std::uint8_t>(
				srcPixels[i + 3] / 255.0f * globalAlpha * 255.0f);
		}

		const render::Texture modTex(texture.width(), texture.height(), modPixels);
		screen.drawSprite(modTex, dstRect);
	}

	// ── テキスト折り返し描画 ──────────────────────────────────

	/// @brief テキストを矩形内に折り返して描画する
	/// @details BitmapFontベースの概算で折り返し位置を決定する。
	static void drawWrappedText(Screen& screen,
	                            std::string_view text,
	                            const sgc::Rectf& area,
	                            const sgc::Colorf& color,
	                            float fontSize)
	{
		if (text.empty()) { return; }

		const int scale = std::max(1, static_cast<int>(fontSize) / 8);
		const float charWidth = static_cast<float>(8 * scale);
		const float lineHeight = static_cast<float>(8 * scale + 2);
		const int maxCharsPerLine = (area.width() > 0.0f)
			? std::max(1, static_cast<int>(area.width() / charWidth))
			: 1;

		float y = area.y();
		std::size_t pos = 0;

		while (pos < text.size() && y + lineHeight <= area.y() + area.height())
		{
			// 改行文字を探す
			auto nlPos = text.find('\n', pos);
			std::size_t lineEnd = (nlPos != std::string_view::npos)
				? nlPos
				: text.size();

			// 行の最大幅に収まるまで分割
			while (pos < lineEnd)
			{
				const std::size_t remaining = lineEnd - pos;
				const std::size_t lineLen = std::min(
					remaining, static_cast<std::size_t>(maxCharsPerLine));

				screen.text(text.substr(pos, lineLen),
				            area.x(), y, color, fontSize);

				pos += lineLen;
				y += lineHeight;

				if (y + lineHeight > area.y() + area.height()) { break; }
			}

			// 改行文字をスキップ
			if (nlPos != std::string_view::npos && pos == nlPos)
			{
				++pos;
				y += lineHeight;
			}
		}
	}
};

} // namespace mitiru::vn
