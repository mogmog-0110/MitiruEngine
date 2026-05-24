#pragma once

/// @file TextDisplay.hpp
/// @brief タイプライター風テキスト表示
///
/// ビジュアルノベルのテキスト表示を管理する。
/// 文字を1文字ずつ表示し、スキップや自動進行をサポートする。
/// ステートレス評価パターンを使用する。
///
/// @code
/// using namespace sgc::vn;
/// TextDisplayConfig config;
/// config.charsPerSecond = 30.0f;
/// TextDisplayState state;
/// state.fullText = "Hello, world!";
/// state = updateTextDisplay(state, deltaTime, config, false, false);
/// @endcode

#include <cstddef>
#include <string>

namespace sgc::vn
{

/// @brief テキスト表示の設定
struct TextDisplayConfig
{
	float charsPerSecond = 30.0f;        ///< 1秒あたりの表示文字数
	bool autoAdvance = false;            ///< 自動進行モード
	float autoAdvanceDelay = 2.0f;       ///< 自動進行までの待機時間（秒）
};

/// @brief テキスト表示の状態
struct TextDisplayState
{
	std::string fullText;                ///< 表示対象の全テキスト
	std::size_t visibleChars = 0;        ///< 現在表示中の文字数
	float elapsed = 0.0f;                ///< 経過時間（秒）
	float waitTimer = 0.0f;              ///< 自動進行カウントダウン（秒）
	bool isComplete = false;             ///< テキスト全文表示済みか
	bool waitingForInput = false;        ///< 入力待ち状態か
	bool advanced = false;               ///< このフレームで次に進んだか
};

/// @brief テキスト表示状態を更新する
///
/// タイプライター風に文字を1文字ずつ表示する。
/// skipPressedで即時全文表示、advancePressedで次のテキストへ進行する。
///
/// @param prev 前フレームの状態
/// @param dt デルタタイム（秒）
/// @param config 表示設定
/// @param advancePressed 進行入力があったか
/// @param skipPressed スキップ入力があったか
/// @return 更新後の状態
[[nodiscard]] inline TextDisplayState updateTextDisplay(
	const TextDisplayState& prev, float dt, const TextDisplayConfig& config,
	bool advancePressed, bool skipPressed)
{
	TextDisplayState next = prev;
	next.advanced = false;

	const std::size_t totalChars = next.fullText.size();

	// 空テキストは即座に完了
	if (totalChars == 0)
	{
		next.isComplete = true;
		next.visibleChars = 0;
		next.waitingForInput = false;
		return next;
	}

	// スキップ: 即座に全文表示
	if (skipPressed && !next.isComplete)
	{
		next.visibleChars = totalChars;
		next.isComplete = true;
		next.elapsed = 0.0f;
		next.waitTimer = 0.0f;
		next.waitingForInput = !config.autoAdvance;
		return next;
	}

	// テキスト表示中: 文字を進める
	if (!next.isComplete)
	{
		next.elapsed += dt;
		const float charsToShow = config.charsPerSecond * next.elapsed;
		next.visibleChars = static_cast<std::size_t>(charsToShow);

		if (next.visibleChars >= totalChars)
		{
			next.visibleChars = totalChars;
			next.isComplete = true;
			next.waitTimer = 0.0f;
			next.waitingForInput = !config.autoAdvance;
		}

		return next;
	}

	// テキスト表示完了後の処理
	if (config.autoAdvance)
	{
		// 自動進行: カウントダウン
		next.waitTimer += dt;
		if (next.waitTimer >= config.autoAdvanceDelay)
		{
			next.advanced = true;
			next.waitingForInput = false;
		}
	}
	else
	{
		// 手動進行: 入力待ち
		next.waitingForInput = true;
		if (advancePressed)
		{
			next.advanced = true;
			next.waitingForInput = false;
		}
	}

	return next;
}

} // namespace sgc::vn
