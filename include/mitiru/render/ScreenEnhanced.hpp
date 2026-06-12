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
	// （実装本体は detail/ScreenEnhanced_impl.hpp）

	/// @brief SDF レンダリングによる滑らかなテキスト描画
	/// @details SDF フォント未初期化時はビットマップフォントにフォールバックする。
	/// @param pos 描画位置（左上）
	/// @param text テキスト内容
	/// @param fontSize フォントサイズ
	/// @param color テキスト色
	void drawSdfText(const sgc::Vec2f& pos, std::string_view text,
	                 float fontSize, const sgc::Colorf& color);

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
	                            float outlineWidth);

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
	                           const sgc::Vec2f& shadowOffset);

	// ── 角丸・シャドウ付きシェイプ描画 ──────────────────────

	/// @brief アンチエイリアス角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param cornerRadius 角丸半径
	/// @param fillColor 塗りつぶし色
	void drawRoundedRect(const sgc::Rectf& rect, float cornerRadius,
	                     const sgc::Colorf& fillColor);

	/// @brief ボーダー付き角丸矩形を描画する
	/// @param rect 矩形領域
	/// @param cornerRadius 角丸半径
	/// @param fillColor 塗りつぶし色
	/// @param borderColor ボーダー色
	/// @param borderWidth ボーダー幅
	void drawRoundedRectWithBorder(const sgc::Rectf& rect, float cornerRadius,
	                               const sgc::Colorf& fillColor,
	                               const sgc::Colorf& borderColor,
	                               float borderWidth);

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
	                               const sgc::Vec2f& shadowOffset);

	/// @brief カプセル型矩形（ピル）を描画する
	/// @param rect 矩形領域
	/// @param fillColor 塗りつぶし色
	void drawPill(const sgc::Rectf& rect, const sgc::Colorf& fillColor);

	/// @brief PanelStyle に基づいた完全なパネル描画
	/// @param rect 矩形領域
	/// @param style パネルスタイル
	void drawPanel(const sgc::Rectf& rect, const PanelStyle& style);

	// ── グラデーション描画 ──────────────────────────────

	/// @brief 線形グラデーション矩形を描画する
	/// @param rect 矩形領域
	/// @param angleDeg グラデーション角度（度、0=上→下、90=左→右）
	/// @param colorFrom 開始色
	/// @param colorTo 終了色
	void drawLinearGradient(const sgc::Rectf& rect, float angleDeg,
	                        const sgc::Colorf& colorFrom,
	                        const sgc::Colorf& colorTo);

	/// @brief 放射状グラデーション矩形を描画する
	/// @param rect 矩形領域
	/// @param centerColor 中心色
	/// @param edgeColor 端色
	void drawRadialGradient(const sgc::Rectf& rect,
	                        const sgc::Colorf& centerColor,
	                        const sgc::Colorf& edgeColor);

	// ── UI コンポジションヘルパー ───────────────────────────

	/// @brief 完全なボタンを描画する
	/// @param rect ボタン領域
	/// @param text ボタンテキスト
	/// @param state ボタンの操作状態
	/// @param style ボタンスタイル
	void drawButton(const sgc::Rectf& rect, std::string_view text,
	                ButtonState state, const ButtonStyle& style);

	/// @brief カード（画像＋タイトル＋本文）を描画する
	/// @param rect カード領域
	/// @param title タイトルテキスト
	/// @param body 本文テキスト
	/// @param imageTexture カード画像テクスチャ（nullptr で画像省略）
	/// @param style カードスタイル
	void drawCard(const sgc::Rectf& rect, std::string_view title,
	              std::string_view body,
	              const Texture* imageTexture,
	              const CardStyle& style);

	/// @brief ツールチップを描画する
	/// @param rect ツールチップ本体領域
	/// @param text ツールチップテキスト
	/// @param arrowDir 矢印の方向
	void drawTooltip(const sgc::Rectf& rect, std::string_view text,
	                 ArrowDirection arrowDir);

	/// @brief 通知パネルを描画する
	/// @param rect 通知領域
	/// @param icon アイコン文字（例: "!" "i" など）
	/// @param title タイトル
	/// @param message メッセージ
	/// @param type 通知タイプ
	void drawNotification(const sgc::Rectf& rect, std::string_view icon,
	                      std::string_view title, std::string_view message,
	                      NotificationType type);

	/// @brief プログレスバーを描画する
	/// @param rect 描画領域
	/// @param progress 進捗 [0..1]
	/// @param style プログレスバースタイル
	void drawProgressBar(const sgc::Rectf& rect, float progress,
	                     const ProgressBarStyle& style);

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
	                                           float t) noexcept;

	/// @brief 不透明度を色に適用する
	[[nodiscard]] static sgc::Colorf applyOpacity(const sgc::Colorf& color,
	                                              float opacity) noexcept;

	/// @brief 角丸矩形の塗りつぶしを描画する
	/// @details 中央十字＋4隅の扇形を三角形ファンで分割して描画する。
	void drawRoundedRectFill(const sgc::Rectf& rect, float r,
	                         const sgc::Colorf& color);

	/// @brief 扇形を三角形ファンで描画する
	void drawCornerFan(float cx, float cy, float radius,
	                   float startDeg, float endDeg, int segments,
	                   const sgc::Colorf& color);

public:
	/// @brief ボーダーのみを角丸矩形で描画する（外枠 - 内枠）
	void drawRoundedRectBorderOnly(const sgc::Rectf& rect, float cornerRadius,
	                               const sgc::Colorf& borderColor,
	                               float borderWidth);

private:
	/// @brief 角のアーク（枠線部分のみ）を描画する
	void drawCornerArc(float cx, float cy, float outerR, float thickness,
	                   float startDeg, float endDeg, int segments,
	                   const sgc::Colorf& color);

	/// @brief シャドウ（ぼかし近似）を描画する
	void drawShadow(const sgc::Rectf& rect, float cornerRadius,
	                const sgc::Colorf& shadowColor, float blur,
	                const sgc::Vec2f& offset);

	/// @brief SDF テキストアウトラインのフォールバック描画
	void drawSdfTextOutlineFallback(const sgc::Vec2f& pos, std::string_view text,
	                                float fontSize, const sgc::Colorf& textColor,
	                                const sgc::Colorf& outlineColor,
	                                float outlineWidth);

	/// @brief ツールチップの矢印三角形を描画する
	void drawTooltipArrow(const sgc::Rectf& rect, ArrowDirection dir,
	                      const sgc::Colorf& color);

	/// @brief 通知タイプ別のアクセントカラーを取得する
	[[nodiscard]] static sgc::Colorf notificationAccentColor(
		NotificationType type) noexcept;

	/// @brief ボタン状態に対応するパネルスタイルを選択する
	[[nodiscard]] static const PanelStyle& selectButtonPanel(
		ButtonState state, const ButtonStyle& style) noexcept;
};

} // namespace mitiru::render

// 実装本体（Renderer3D.hpp と同じ末尾 detail include 流儀）
#include <mitiru/render/detail/ScreenEnhanced_impl.hpp>
