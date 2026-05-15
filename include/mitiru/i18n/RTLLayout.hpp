#pragma once

/// @file RTLLayout.hpp
/// @brief Right-to-Left（RTL）テキストレイアウトインターフェース
/// @details Unicode BiDiアルゴリズムに基づくテキスト方向解析と、
///          複雑文字体系（アラビア文字・ヘブライ文字等）のテキストシェーピング
///          インターフェースを提供する。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru::i18n
{

/// @brief テキスト方向
enum class TextDirection : int
{
	LTR = 0,   ///< 左から右（Latin, CJK等）
	RTL = 1,   ///< 右から左（Arabic, Hebrew等）
	Mixed = 2  ///< 双方向混在
};

/// @brief BiDiランの情報
/// @details Unicode BiDiアルゴリズムで解析された1つのテキスト区間。
struct BiDiRun
{
	std::size_t startIndex = 0;   ///< テキスト内の開始位置（コードポイント単位）
	std::size_t length = 0;       ///< ランの長さ（コードポイント単位）
	TextDirection direction = TextDirection::LTR; ///< このランの方向
	int embeddingLevel = 0;       ///< BiDi埋め込みレベル
};

/// @brief グリフ位置情報（テキストシェーピング結果）
struct GlyphPosition
{
	std::uint32_t glyphId = 0;     ///< フォント内グリフID
	float xOffset = 0.0f;         ///< X方向オフセット（ピクセル）
	float yOffset = 0.0f;         ///< Y方向オフセット（ピクセル）
	float xAdvance = 0.0f;        ///< X方向送り幅（ピクセル）
	float yAdvance = 0.0f;        ///< Y方向送り幅（ピクセル）
	std::size_t clusterIndex = 0; ///< 元テキストのクラスタインデックス
};

/// @brief テキストシェーピング結果
struct ShapedText
{
	std::vector<GlyphPosition> glyphs;  ///< シェーピング済みグリフ一覧
	float totalWidth = 0.0f;            ///< テキスト全体の幅（ピクセル）
	float totalHeight = 0.0f;           ///< テキスト全体の高さ（ピクセル）
	TextDirection baseDirection = TextDirection::LTR; ///< ベーステキスト方向
};

/// @brief RTLレイアウト設定
struct RTLConfig
{
	TextDirection baseDirection = TextDirection::LTR; ///< デフォルトのベース方向
	bool enableShaping = true;    ///< テキストシェーピング有効
	bool enableLigatures = true;  ///< リガチャ有効
	bool enableKerning = true;    ///< カーニング有効
	bool mirrorBrackets = true;   ///< RTL時に括弧をミラーリング
};

/// @brief BiDiアルゴリズムインターフェース
/// @details Unicode UAX#9 BiDiアルゴリズムの実装を抽象化する。
///          テキストの方向解析とビジュアル順への並べ替えを担当する。
class IBiDiResolver
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IBiDiResolver() = default;

	/// コピー禁止
	IBiDiResolver(const IBiDiResolver&) = delete;
	IBiDiResolver& operator=(const IBiDiResolver&) = delete;

	/// @brief テキストのBiDiランを解析する
	/// @param text UTF-8テキスト
	/// @param baseDirection ベース方向
	/// @return BiDiラン一覧（論理順）
	[[nodiscard]] virtual std::vector<BiDiRun> analyze(
		const std::string& text,
		TextDirection baseDirection = TextDirection::LTR) const = 0;

	/// @brief テキストのベース方向を自動検出する
	/// @param text UTF-8テキスト
	/// @return 検出されたベース方向
	[[nodiscard]] virtual TextDirection detectBaseDirection(
		const std::string& text) const = 0;

	/// @brief 論理順テキストをビジュアル順に並べ替える
	/// @param text UTF-8テキスト
	/// @param baseDirection ベース方向
	/// @return ビジュアル順に並べ替えられたテキスト
	[[nodiscard]] virtual std::string reorderVisual(
		const std::string& text,
		TextDirection baseDirection = TextDirection::LTR) const = 0;

protected:
	/// @brief デフォルトコンストラクタ（派生クラスのみ生成可能）
	IBiDiResolver() = default;

	/// ムーブ許可（派生クラスのみ）
	IBiDiResolver(IBiDiResolver&&) noexcept = default;
	IBiDiResolver& operator=(IBiDiResolver&&) noexcept = default;
};

/// @brief テキストシェーピングインターフェース
/// @details 複雑文字体系のテキストシェーピング（合字、文脈依存字形選択等）を
///          抽象化する。HarfBuzz等のシェーピングエンジンをバックエンドとして使用可能。
class ITextShaper
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITextShaper() = default;

	/// コピー禁止
	ITextShaper(const ITextShaper&) = delete;
	ITextShaper& operator=(const ITextShaper&) = delete;

	/// @brief テキストをシェーピングする
	/// @param text UTF-8テキスト
	/// @param fontSizePx フォントサイズ（ピクセル）
	/// @param direction テキスト方向
	/// @return シェーピング結果
	[[nodiscard]] virtual ShapedText shape(
		const std::string& text,
		float fontSizePx,
		TextDirection direction = TextDirection::LTR) const = 0;

	/// @brief 指定テキストのレイアウト幅を計算する
	/// @param text UTF-8テキスト
	/// @param fontSizePx フォントサイズ（ピクセル）
	/// @return テキスト幅（ピクセル）
	[[nodiscard]] virtual float measureWidth(
		const std::string& text,
		float fontSizePx) const = 0;

	/// @brief シェーピングエンジン名を取得する
	[[nodiscard]] virtual std::string engineName() const = 0;

protected:
	/// @brief デフォルトコンストラクタ（派生クラスのみ生成可能）
	ITextShaper() = default;

	/// ムーブ許可（派生クラスのみ）
	ITextShaper(ITextShaper&&) noexcept = default;
	ITextShaper& operator=(ITextShaper&&) noexcept = default;
};

} // namespace mitiru::i18n
