#pragma once

/// @file Accessibility.hpp
/// @brief VNアクセシビリティ機能
/// @details 視覚障害・色覚障害・運動障害・読字障害に対応するアクセシビリティ設定。
///          ハイコントラスト、読字障害フォント、スクリーンリーダー出力、文字サイズ変更、
///          モーション軽減、色覚補正を統合的に管理する。
///
/// @code
/// mitiru::vn::AccessibilityConfig config;
/// config.highContrast = true;
/// config.colorBlindMode = mitiru::vn::ColorBlindMode::Deuteranopia;
/// config.textSizeMultiplier = 1.5f;
///
/// mitiru::vn::AccessibilityManager manager;
/// manager.setConfig(config);
///
/// auto adjusted = manager.applyColorBlindTransform(originalColor);
/// auto text = manager.getScreenReaderText(sceneState);
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/types/Color.hpp>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  色覚モード
// ════════════════════════════════════════════════════════════════════

/// @brief 色覚障害の種別
enum class ColorBlindMode : std::uint8_t
{
	None,           ///< 補正なし
	Protanopia,     ///< 1型色覚（赤色覚障害）
	Deuteranopia,   ///< 2型色覚（緑色覚障害）
	Tritanopia,     ///< 3型色覚（青色覚障害）
};

// ════════════════════════════════════════════════════════════════════
//  色変換ユーティリティ
// ════════════════════════════════════════════════════════════════════

/// @brief 色覚シミュレーション用カラー変換
/// @details 3x3色変換行列による色覚障害シミュレーション。
///          行列データはMachado et al. (2009)の研究に基づく。
class ColorTransform
{
public:
	/// @brief 3x3行列（行優先）
	using Matrix3x3 = std::array<float, 9>;

	/// @brief 色覚モードに応じた変換行列を取得する
	/// @param mode 色覚モード
	/// @return 3x3変換行列
	[[nodiscard]] static Matrix3x3 getMatrix(ColorBlindMode mode) noexcept
	{
		switch (mode)
		{
		case ColorBlindMode::Protanopia:
			return protanopiaMatrix();
		case ColorBlindMode::Deuteranopia:
			return deuteranopiaMatrix();
		case ColorBlindMode::Tritanopia:
			return tritanopiaMatrix();
		case ColorBlindMode::None:
		default:
			return identityMatrix();
		}
	}

	/// @brief 色に変換行列を適用する
	/// @param color 入力色
	/// @param matrix 3x3変換行列
	/// @return 変換後の色
	[[nodiscard]] static sgc::Colorf apply(const sgc::Colorf& color, const Matrix3x3& matrix) noexcept
	{
		float r = clamp01(matrix[0] * color.r + matrix[1] * color.g + matrix[2] * color.b);
		float g = clamp01(matrix[3] * color.r + matrix[4] * color.g + matrix[5] * color.b);
		float b = clamp01(matrix[6] * color.r + matrix[7] * color.g + matrix[8] * color.b);
		return sgc::Colorf{r, g, b, color.a};
	}

	/// @brief 色覚モードに応じて色を変換する
	/// @param color 入力色
	/// @param mode 色覚モード
	/// @return 変換後の色
	[[nodiscard]] static sgc::Colorf applyMode(const sgc::Colorf& color, ColorBlindMode mode) noexcept
	{
		if (mode == ColorBlindMode::None) return color;
		return apply(color, getMatrix(mode));
	}

private:
	[[nodiscard]] static constexpr float clamp01(float v) noexcept
	{
		return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
	}

	/// @brief 単位行列
	[[nodiscard]] static constexpr Matrix3x3 identityMatrix() noexcept
	{
		return {1.0f, 0.0f, 0.0f,
		        0.0f, 1.0f, 0.0f,
		        0.0f, 0.0f, 1.0f};
	}

	/// @brief 1型色覚（Protanopia）変換行列
	[[nodiscard]] static constexpr Matrix3x3 protanopiaMatrix() noexcept
	{
		return {0.56667f, 0.43333f, 0.00000f,
		        0.55833f, 0.44167f, 0.00000f,
		        0.00000f, 0.24167f, 0.75833f};
	}

	/// @brief 2型色覚（Deuteranopia）変換行列
	[[nodiscard]] static constexpr Matrix3x3 deuteranopiaMatrix() noexcept
	{
		return {0.62500f, 0.37500f, 0.00000f,
		        0.70000f, 0.30000f, 0.00000f,
		        0.00000f, 0.30000f, 0.70000f};
	}

	/// @brief 3型色覚（Tritanopia）変換行列
	[[nodiscard]] static constexpr Matrix3x3 tritanopiaMatrix() noexcept
	{
		return {0.95000f, 0.05000f, 0.00000f,
		        0.00000f, 0.43333f, 0.56667f,
		        0.00000f, 0.47500f, 0.52500f};
	}
};

// ════════════════════════════════════════════════════════════════════
//  カラーオーバーライド
// ════════════════════════════════════════════════════════════════════

/// @brief UI要素ごとのカラーオーバーライド
struct ColorOverrides
{
	std::optional<sgc::Colorf> textColor;              ///< テキスト色
	std::optional<sgc::Colorf> backgroundColor;        ///< 背景色
	std::optional<sgc::Colorf> windowColor;            ///< ウィンドウ色
	std::optional<sgc::Colorf> choiceNormalColor;      ///< 選択肢通常色
	std::optional<sgc::Colorf> choiceHighlightColor;   ///< 選択肢ハイライト色
	std::optional<sgc::Colorf> choiceTextColor;        ///< 選択肢テキスト色
	std::optional<sgc::Colorf> nameTagColor;           ///< 名前タグ色
	std::optional<sgc::Colorf> nameTagTextColor;       ///< 名前タグテキスト色
};

// ════════════════════════════════════════════════════════════════════
//  アクセシビリティ設定
// ════════════════════════════════════════════════════════════════════

/// @brief アクセシビリティ設定
struct AccessibilityConfig
{
	bool highContrast = false;                         ///< ハイコントラストモード
	bool dyslexiaFont = false;                         ///< 読字障害対応フォント（広い字間）
	bool screenReaderText = false;                     ///< スクリーンリーダー出力有効
	float textSizeMultiplier = 1.0f;                   ///< テキストサイズ倍率（1.0〜3.0）
	bool reducedMotion = false;                        ///< モーション軽減
	ColorBlindMode colorBlindMode = ColorBlindMode::None; ///< 色覚補正モード
	ColorOverrides customColors;                       ///< カスタムカラーオーバーライド
	float autoAdvanceMinDelay = 3.0f;                  ///< オート送りの最小待機時間（秒）
	bool voiceDescriptions = false;                    ///< 場面描写テキスト出力
	float letterSpacingMultiplier = 1.0f;              ///< 字間倍率（dyslexiaFont時に自動拡大）
	float lineSpacingMultiplier = 1.0f;                ///< 行間倍率

	/// @brief テキストサイズ倍率を安全に設定する
	/// @param multiplier 倍率（1.0〜3.0にクランプ）
	void setTextSizeMultiplier(float multiplier) noexcept
	{
		textSizeMultiplier = std::clamp(multiplier, 1.0f, 3.0f);
	}
};

// ════════════════════════════════════════════════════════════════════
//  スクリーンリーダー出力用構造体
// ════════════════════════════════════════════════════════════════════

/// @brief スクリーンリーダー向けのシーン記述
struct ScreenReaderOutput
{
	std::string sceneDescription;                      ///< 現在のシーン概要
	std::string speakerName;                           ///< 現在の話者名
	std::string dialogueText;                          ///< 現在のセリフ（タグ除去済み）
	std::string backgroundDescription;                 ///< 背景の説明
	std::vector<std::string> visibleCharacters;        ///< 表示中のキャラクター名
	std::vector<std::string> choiceTexts;              ///< 選択肢テキスト（表示中の場合）
	std::string effectDescription;                     ///< 画面エフェクトの説明

	/// @brief 全情報をプレーンテキストとして連結する
	/// @return スクリーンリーダー向けテキスト
	[[nodiscard]] std::string toPlainText() const
	{
		std::string result;

		if (!sceneDescription.empty())
		{
			result += sceneDescription + "\n";
		}
		if (!backgroundDescription.empty())
		{
			result += backgroundDescription + "\n";
		}
		if (!visibleCharacters.empty())
		{
			result += "Characters: ";
			for (std::size_t i = 0; i < visibleCharacters.size(); ++i)
			{
				if (i > 0) result += ", ";
				result += visibleCharacters[i];
			}
			result += "\n";
		}
		if (!effectDescription.empty())
		{
			result += effectDescription + "\n";
		}
		if (!speakerName.empty())
		{
			result += speakerName + ": ";
		}
		if (!dialogueText.empty())
		{
			result += dialogueText + "\n";
		}
		if (!choiceTexts.empty())
		{
			result += "Choices:\n";
			for (std::size_t i = 0; i < choiceTexts.size(); ++i)
			{
				result += "  " + std::to_string(i + 1) + ". " + choiceTexts[i] + "\n";
			}
		}

		return result;
	}
};

// ════════════════════════════════════════════════════════════════════
//  シーン状態（スクリーンリーダー入力用）
// ════════════════════════════════════════════════════════════════════

/// @brief アクセシビリティマネージャに渡すシーン状態
struct AccessibilitySceneState
{
	std::string sceneId;                               ///< シーンID
	std::string backgroundFile;                        ///< 背景ファイル名
	std::string speakerName;                           ///< 話者名
	std::string dialogueText;                          ///< セリフ（リッチテキストタグ含む）
	std::vector<std::string> visibleCharacterNames;    ///< 表示中キャラクター
	std::vector<std::string> choiceTexts;              ///< 選択肢テキスト
	bool hasShakeEffect = false;                       ///< シェイクエフェクト中か
	bool hasFlashEffect = false;                       ///< フラッシュエフェクト中か
	bool hasParticleEffect = false;                    ///< パーティクルエフェクト中か
	std::string particleType;                          ///< パーティクル種別（rain, snowなど）
};

// ════════════════════════════════════════════════════════════════════
//  ハイコントラスト配色
// ════════════════════════════════════════════════════════════════════

/// @brief ハイコントラスト配色のデフォルト値
struct HighContrastPalette
{
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};               ///< 白テキスト
	sgc::Colorf backgroundColor{0.0f, 0.0f, 0.0f, 0.95f};        ///< 黒背景
	sgc::Colorf windowColor{0.05f, 0.05f, 0.05f, 0.98f};         ///< 濃い灰ウィンドウ
	sgc::Colorf choiceNormalColor{0.1f, 0.1f, 0.4f, 1.0f};       ///< 濃青ボタン
	sgc::Colorf choiceHighlightColor{1.0f, 1.0f, 0.0f, 1.0f};    ///< 黄色ハイライト
	sgc::Colorf choiceTextColor{1.0f, 1.0f, 1.0f, 1.0f};         ///< 白テキスト
	sgc::Colorf nameTagColor{0.2f, 0.2f, 0.6f, 1.0f};            ///< 濃い青名前タグ
	sgc::Colorf nameTagTextColor{1.0f, 1.0f, 0.8f, 1.0f};        ///< 淡黄テキスト
};

// ════════════════════════════════════════════════════════════════════
//  AccessibilityManager 本体
// ════════════════════════════════════════════════════════════════════

/// @brief VNアクセシビリティ統合マネージャ
/// @details 全VNサブシステムに対してアクセシビリティ設定を適用する。
///          色覚補正、ハイコントラスト、テキストサイズ変更、モーション軽減、
///          スクリーンリーダー出力を一括管理する。
class AccessibilityManager
{
public:
	/// @brief コンストラクタ
	AccessibilityManager() = default;

	/// @brief 設定付きコンストラクタ
	/// @param config アクセシビリティ設定
	explicit AccessibilityManager(AccessibilityConfig config) noexcept
		: m_config(std::move(config))
	{
		applyDyslexiaDefaults();
	}

	// ── 設定管理 ──────────────────────────────────────────

	/// @brief 設定を取得する
	[[nodiscard]] const AccessibilityConfig& config() const noexcept { return m_config; }

	/// @brief 設定を置き換える
	/// @param config 新しい設定
	void setConfig(AccessibilityConfig config) noexcept
	{
		m_config = std::move(config);
		applyDyslexiaDefaults();
	}

	/// @brief ハイコントラストモードを設定する
	void setHighContrast(bool enabled) noexcept { m_config.highContrast = enabled; }

	/// @brief 読字障害対応モードを設定する
	void setDyslexiaFont(bool enabled) noexcept
	{
		m_config.dyslexiaFont = enabled;
		applyDyslexiaDefaults();
	}

	/// @brief テキストサイズ倍率を設定する
	/// @param multiplier 倍率（1.0〜3.0にクランプ）
	void setTextSizeMultiplier(float multiplier) noexcept
	{
		m_config.setTextSizeMultiplier(multiplier);
	}

	/// @brief モーション軽減を設定する
	void setReducedMotion(bool enabled) noexcept { m_config.reducedMotion = enabled; }

	/// @brief 色覚補正モードを設定する
	void setColorBlindMode(ColorBlindMode mode) noexcept { m_config.colorBlindMode = mode; }

	/// @brief スクリーンリーダー出力を設定する
	void setScreenReaderText(bool enabled) noexcept { m_config.screenReaderText = enabled; }

	/// @brief 場面描写テキスト出力を設定する
	void setVoiceDescriptions(bool enabled) noexcept { m_config.voiceDescriptions = enabled; }

	// ── エフェクト判定 ──────────────────────────────────────────

	/// @brief シェイクエフェクトを適用すべきか
	[[nodiscard]] bool shouldApplyShake() const noexcept { return !m_config.reducedMotion; }

	/// @brief フラッシュエフェクトを適用すべきか
	[[nodiscard]] bool shouldApplyFlash() const noexcept { return !m_config.reducedMotion; }

	/// @brief パーティクルエフェクトを適用すべきか
	[[nodiscard]] bool shouldApplyParticles() const noexcept { return !m_config.reducedMotion; }

	/// @brief テキストウェーブアニメーションを適用すべきか
	[[nodiscard]] bool shouldApplyTextWave() const noexcept { return !m_config.reducedMotion; }

	/// @brief テキストシェイクアニメーションを適用すべきか
	[[nodiscard]] bool shouldApplyTextShake() const noexcept { return !m_config.reducedMotion; }

	/// @brief タイプライター演出の代わりに即時表示すべきか
	[[nodiscard]] bool shouldUseInstantReveal() const noexcept { return m_config.reducedMotion; }

	// ── テキストサイズ ──────────────────────────────────────────

	/// @brief フォントサイズにアクセシビリティ倍率を適用する
	/// @param baseFontSize 元のフォントサイズ
	/// @return 調整後のフォントサイズ
	[[nodiscard]] float adjustFontSize(float baseFontSize) const noexcept
	{
		return baseFontSize * m_config.textSizeMultiplier;
	}

	/// @brief 字間にアクセシビリティ倍率を適用する
	/// @param baseSpacing 元の字間
	/// @return 調整後の字間
	[[nodiscard]] float adjustLetterSpacing(float baseSpacing) const noexcept
	{
		return baseSpacing * m_config.letterSpacingMultiplier;
	}

	/// @brief 行間にアクセシビリティ倍率を適用する
	/// @param baseSpacing 元の行間
	/// @return 調整後の行間
	[[nodiscard]] float adjustLineSpacing(float baseSpacing) const noexcept
	{
		return baseSpacing * m_config.lineSpacingMultiplier;
	}

	/// @brief 選択肢ボタンの拡大率を取得する（テキストサイズ連動）
	/// @return ボタン拡大率
	[[nodiscard]] float choiceButtonScale() const noexcept
	{
		if (m_config.textSizeMultiplier > 1.0f)
		{
			return 1.0f + (m_config.textSizeMultiplier - 1.0f) * 0.5f;
		}
		return 1.0f;
	}

	// ── オート送り ──────────────────────────────────────────

	/// @brief オート送り遅延をアクセシビリティ設定で調整する
	/// @param baseDelay 元の遅延時間（秒）
	/// @return 調整後の遅延時間（最小値保証）
	[[nodiscard]] float adjustAutoAdvanceDelay(float baseDelay) const noexcept
	{
		return std::max(baseDelay, m_config.autoAdvanceMinDelay);
	}

	// ── 色変換 ──────────────────────────────────────────────

	/// @brief 色覚補正を適用する
	/// @param color 入力色
	/// @return 補正後の色
	[[nodiscard]] sgc::Colorf applyColorBlindTransform(const sgc::Colorf& color) const noexcept
	{
		return ColorTransform::applyMode(color, m_config.colorBlindMode);
	}

	/// @brief ハイコントラスト＋色覚補正を統合適用する
	/// @param color 入力色
	/// @param role 色の用途（テキスト、背景など）
	/// @return 最終調整後の色
	[[nodiscard]] sgc::Colorf adjustColor(const sgc::Colorf& color,
	                                       std::string_view role = "") const noexcept
	{
		sgc::Colorf result = color;

		// ハイコントラストモードのカラーオーバーライド適用
		if (m_config.highContrast)
		{
			auto overridden = getHighContrastColor(role);
			if (overridden.has_value())
			{
				result = overridden.value();
			}
		}

		// カスタムカラーオーバーライド適用
		auto customColor = getCustomColor(role);
		if (customColor.has_value())
		{
			result = customColor.value();
		}

		// 色覚補正適用
		result = applyColorBlindTransform(result);

		return result;
	}

	// ── スクリーンリーダー ──────────────────────────────────────

	/// @brief 現在のシーン状態からスクリーンリーダーテキストを生成する
	/// @param state シーン状態
	/// @return スクリーンリーダー出力
	[[nodiscard]] ScreenReaderOutput getScreenReaderText(
		const AccessibilitySceneState& state) const
	{
		ScreenReaderOutput output;

		output.sceneDescription = "Scene: " + state.sceneId;
		output.speakerName = state.speakerName;
		output.dialogueText = stripRichTextTags(state.dialogueText);
		output.visibleCharacters = state.visibleCharacterNames;
		output.choiceTexts = state.choiceTexts;

		// 背景の説明生成
		if (!state.backgroundFile.empty())
		{
			output.backgroundDescription = describeBackground(state.backgroundFile);
		}

		// エフェクト説明生成
		if (m_config.voiceDescriptions)
		{
			output.effectDescription = describeEffects(state);
		}

		return output;
	}

	/// @brief 背景ファイル名からの説明マッピングを登録する
	/// @param filename ファイル名
	/// @param description 説明文
	void registerBackgroundDescription(const std::string& filename,
	                                    const std::string& description)
	{
		m_backgroundDescriptions[filename] = description;
	}

	// ── ハイコントラストパレット ─────────────────────────────────

	/// @brief ハイコントラストパレットを取得する
	[[nodiscard]] const HighContrastPalette& highContrastPalette() const noexcept
	{
		return m_highContrastPalette;
	}

	/// @brief ハイコントラストパレットを設定する
	void setHighContrastPalette(const HighContrastPalette& palette) noexcept
	{
		m_highContrastPalette = palette;
	}

private:
	/// @brief 読字障害対応のデフォルト字間・行間を適用する
	void applyDyslexiaDefaults() noexcept
	{
		if (m_config.dyslexiaFont)
		{
			if (m_config.letterSpacingMultiplier < 1.3f)
			{
				m_config.letterSpacingMultiplier = 1.3f;
			}
			if (m_config.lineSpacingMultiplier < 1.5f)
			{
				m_config.lineSpacingMultiplier = 1.5f;
			}
		}
	}

	/// @brief ハイコントラスト配色を用途名から取得する
	/// @param role 用途名（"text", "background", "window", "choiceNormal", etc.）
	/// @return ハイコントラスト色（該当なしの場合はnullopt）
	[[nodiscard]] std::optional<sgc::Colorf> getHighContrastColor(
		std::string_view role) const noexcept
	{
		if (role == "text")            return m_highContrastPalette.textColor;
		if (role == "background")      return m_highContrastPalette.backgroundColor;
		if (role == "window")          return m_highContrastPalette.windowColor;
		if (role == "choiceNormal")    return m_highContrastPalette.choiceNormalColor;
		if (role == "choiceHighlight") return m_highContrastPalette.choiceHighlightColor;
		if (role == "choiceText")      return m_highContrastPalette.choiceTextColor;
		if (role == "nameTag")         return m_highContrastPalette.nameTagColor;
		if (role == "nameTagText")     return m_highContrastPalette.nameTagTextColor;
		return std::nullopt;
	}

	/// @brief カスタムカラーオーバーライドを用途名から取得する
	[[nodiscard]] std::optional<sgc::Colorf> getCustomColor(
		std::string_view role) const noexcept
	{
		const auto& c = m_config.customColors;
		if (role == "text"            && c.textColor.has_value())            return c.textColor;
		if (role == "background"      && c.backgroundColor.has_value())      return c.backgroundColor;
		if (role == "window"          && c.windowColor.has_value())          return c.windowColor;
		if (role == "choiceNormal"    && c.choiceNormalColor.has_value())    return c.choiceNormalColor;
		if (role == "choiceHighlight" && c.choiceHighlightColor.has_value()) return c.choiceHighlightColor;
		if (role == "choiceText"      && c.choiceTextColor.has_value())      return c.choiceTextColor;
		if (role == "nameTag"         && c.nameTagColor.has_value())         return c.nameTagColor;
		if (role == "nameTagText"     && c.nameTagTextColor.has_value())     return c.nameTagTextColor;
		return std::nullopt;
	}

	/// @brief リッチテキストタグを除去する
	/// @param text タグ付きテキスト
	/// @return プレーンテキスト
	[[nodiscard]] static std::string stripRichTextTags(const std::string& text)
	{
		std::string result;
		result.reserve(text.size());

		bool inTag = false;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '<')
			{
				inTag = true;
				continue;
			}
			if (text[i] == '>')
			{
				inTag = false;
				continue;
			}
			if (!inTag)
			{
				result += text[i];
			}
		}

		return result;
	}

	/// @brief 背景ファイル名から説明を生成する
	/// @param filename 背景ファイル名
	/// @return 背景の説明文
	[[nodiscard]] std::string describeBackground(const std::string& filename) const
	{
		// 登録済みの説明があればそれを返す
		auto it = m_backgroundDescriptions.find(filename);
		if (it != m_backgroundDescriptions.end())
		{
			return it->second;
		}

		// ファイル名からの自動推定
		std::string name = filename;
		auto dotPos = name.rfind('.');
		if (dotPos != std::string::npos)
		{
			name = name.substr(0, dotPos);
		}
		auto slashPos = name.rfind('/');
		if (slashPos != std::string::npos)
		{
			name = name.substr(slashPos + 1);
		}

		// アンダースコア/ハイフンをスペースに変換
		for (char& c : name)
		{
			if (c == '_' || c == '-') c = ' ';
		}

		return "Background: " + name;
	}

	/// @brief 画面エフェクトの説明を生成する
	/// @param state シーン状態
	/// @return エフェクト説明文
	[[nodiscard]] static std::string describeEffects(const AccessibilitySceneState& state)
	{
		std::vector<std::string> descriptions;

		if (state.hasShakeEffect)
		{
			descriptions.push_back("The screen is shaking");
		}
		if (state.hasFlashEffect)
		{
			descriptions.push_back("A bright flash appears");
		}
		if (state.hasParticleEffect && !state.particleType.empty())
		{
			if (state.particleType == "rain")
			{
				descriptions.push_back("Rain is falling");
			}
			else if (state.particleType == "snow")
			{
				descriptions.push_back("Snow is falling");
			}
			else
			{
				descriptions.push_back("Particles: " + state.particleType);
			}
		}

		if (descriptions.empty()) return {};

		std::string result;
		for (std::size_t i = 0; i < descriptions.size(); ++i)
		{
			if (i > 0) result += ". ";
			result += descriptions[i];
		}
		result += ".";
		return result;
	}

	AccessibilityConfig m_config;
	HighContrastPalette m_highContrastPalette;
	std::unordered_map<std::string, std::string> m_backgroundDescriptions;
};

} // namespace mitiru::vn
