#pragma once

/// @file AutoSkipController.hpp
/// @brief ビジュアルノベル用オート/スキップコントローラー
/// @details テキスト自動送りとスキップモードを管理する状態マシン。
///          ボイス再生待ち、既読判定、選択肢での自動停止をサポートする。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace mitiru::vn
{

/// @brief コントローラーの動作モード
enum class AutoSkipMode : std::uint8_t
{
	Idle     = 0,  ///< 手動操作（オート/スキップ無効）
	Auto     = 1,  ///< オートモード（一定時間後に自動送り）
	SkipRead = 2,  ///< 既読スキップ（既読テキストのみスキップ）
	SkipAll  = 3,  ///< 全スキップ（未読テキストも含めてスキップ）
};

/// @brief update() が返すアクション指示
enum class AdvanceAction : std::uint8_t
{
	None    = 0,  ///< 何もしない
	Advance = 1,  ///< 次のテキストへ進む（通常送り）
	Skip    = 2,  ///< 次のテキストへスキップ（高速送り）
};

/// @brief オート/スキップの設定パラメータ
struct AutoSkipConfig
{
	float autoBaseDelay       = 3.0f;    ///< オートモードの基本待機時間（秒）
	float autoSpeedScale      = 0.5f;    ///< オートスピードスケール（1=最速, 10=最遅）
	float textLengthFactor    = 0.02f;   ///< 1文字あたりの追加待機時間（秒）
	float skipAdvanceInterval = 0.05f;   ///< スキップ時の自動送り間隔（秒）
	bool  stopAtChoices       = true;    ///< 選択肢で自動停止するか
	bool  waitForVoice        = true;    ///< ボイス再生完了まで待つか
};

/// @brief 既読判定用コールバック型
/// @details 現在のスクリプトラベルと行番号から既読かどうかを返す。
using ReadCheckFn = std::function<bool(std::string_view label, int lineIndex)>;

/// @brief ボイス再生中判定用コールバック型
using VoicePlayingFn = std::function<bool()>;

/// @brief ビジュアルノベル用オート/スキップコントローラー
/// @details 状態マシンとしてIdle/Auto/SkipRead/SkipAllの4モードを管理し、
///          毎フレームのupdate()でAdvanceActionを返す。
///
/// @code
/// mitiru::vn::AutoSkipController controller;
/// controller.setAutoSpeed(5);  // 中速
/// controller.setMode(mitiru::vn::AutoSkipMode::Auto);
///
/// // 毎フレーム
/// auto action = controller.update(dt);
/// if (action == mitiru::vn::AdvanceAction::Advance) {
///     scriptRunner.advanceToNext();
/// }
/// @endcode
class AutoSkipController
{
public:
	/// @brief コンストラクタ
	AutoSkipController() noexcept = default;

	/// @brief 設定付きコンストラクタ
	/// @param config オート/スキップ設定
	explicit AutoSkipController(AutoSkipConfig config) noexcept
		: m_config(config)
	{
	}

	// ── モード制御 ──────────────────────────────────────────

	/// @brief 現在のモードを取得する
	[[nodiscard]] AutoSkipMode mode() const noexcept { return m_mode; }

	/// @brief モードを設定する
	/// @param mode 新しいモード
	void setMode(AutoSkipMode mode) noexcept
	{
		m_mode = mode;
		m_elapsed = 0.0f;
		m_textComplete = false;
	}

	/// @brief オートモードのトグル
	void toggleAuto() noexcept
	{
		if (m_mode == AutoSkipMode::Auto)
		{
			setMode(AutoSkipMode::Idle);
		}
		else
		{
			setMode(AutoSkipMode::Auto);
		}
	}

	/// @brief スキップモードのトグル
	/// @param skipAll trueで全スキップ、falseで既読スキップ
	void toggleSkip(bool skipAll = false) noexcept
	{
		const auto targetMode = skipAll ? AutoSkipMode::SkipAll : AutoSkipMode::SkipRead;
		if (m_mode == targetMode)
		{
			setMode(AutoSkipMode::Idle);
		}
		else
		{
			setMode(targetMode);
		}
	}

	/// @brief ユーザークリックによるキャンセル（手動モードへ戻る）
	void cancelByUserInput() noexcept
	{
		setMode(AutoSkipMode::Idle);
	}

	// ── 状態通知 ──────────────────────────────────────────

	/// @brief テキスト表示完了を通知する
	/// @details オートモードではテキスト表示完了後にタイマーを開始する。
	void notifyTextComplete() noexcept
	{
		m_textComplete = true;
		m_elapsed = 0.0f;
	}

	/// @brief 新しいテキスト行が開始されたことを通知する
	/// @param textLength テキストの文字数
	/// @param scriptLabel 現在のスクリプトラベル
	/// @param lineIndex 現在の行番号
	void notifyNewLine(std::size_t textLength,
	                   std::string_view scriptLabel,
	                   int lineIndex) noexcept
	{
		m_textComplete = false;
		m_elapsed = 0.0f;
		m_currentTextLength = textLength;
		m_currentLabel = scriptLabel;
		m_currentLineIndex = lineIndex;
	}

	/// @brief 選択肢が表示されたことを通知する
	/// @details stopAtChoices設定が有効な場合、Idleモードへ遷移する。
	void notifyChoiceAppeared() noexcept
	{
		if (m_config.stopAtChoices)
		{
			setMode(AutoSkipMode::Idle);
		}
	}

	/// @brief 選択肢が終了したことを通知する
	void notifyChoiceDismissed() noexcept
	{
		m_elapsed = 0.0f;
	}

	// ── コールバック設定 ────────────────────────────────────

	/// @brief 既読判定コールバックを設定する
	/// @param fn 既読判定関数
	void setReadChecker(ReadCheckFn fn) { m_readChecker = std::move(fn); }

	/// @brief ボイス再生中判定コールバックを設定する
	/// @param fn ボイス再生中判定関数
	void setVoicePlayingChecker(VoicePlayingFn fn) { m_voiceChecker = std::move(fn); }

	// ── 速度設定 ────────────────────────────────────────────

	/// @brief オートスピードを設定する（1=最速, 10=最遅）
	/// @param speed スピード値
	void setAutoSpeed(int speed) noexcept
	{
		const int clamped = std::clamp(speed, 1, 10);
		m_config.autoSpeedScale = static_cast<float>(clamped) * 0.1f;
	}

	/// @brief オートスピードを取得する（1=最速, 10=最遅）
	[[nodiscard]] int autoSpeed() const noexcept
	{
		return std::clamp(static_cast<int>(m_config.autoSpeedScale * 10.0f), 1, 10);
	}

	/// @brief スキップ送り間隔を設定する（秒）
	/// @param interval 送り間隔
	void setSkipInterval(float interval) noexcept
	{
		m_config.skipAdvanceInterval = std::max(0.01f, interval);
	}

	// ── 設定アクセス ──────────────────────────────────────

	/// @brief 設定を取得する
	[[nodiscard]] const AutoSkipConfig& config() const noexcept { return m_config; }

	/// @brief 設定を置き換える
	/// @param config 新しい設定
	void setConfig(AutoSkipConfig config) noexcept
	{
		m_config = config;
	}

	// ── メインループ ──────────────────────────────────────

	/// @brief 毎フレーム更新
	/// @param deltaTime 前フレームからの経過時間（秒）
	/// @return 実行すべきアクション
	[[nodiscard]] AdvanceAction update(float deltaTime) noexcept
	{
		switch (m_mode)
		{
		case AutoSkipMode::Idle:
			return AdvanceAction::None;

		case AutoSkipMode::Auto:
			return updateAuto(deltaTime);

		case AutoSkipMode::SkipRead:
			return updateSkipRead(deltaTime);

		case AutoSkipMode::SkipAll:
			return updateSkipAll(deltaTime);
		}

		return AdvanceAction::None;
	}

	// ── 状態クエリ ──────────────────────────────────────────

	/// @brief アクティブ（Idle以外）かどうか
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_mode != AutoSkipMode::Idle;
	}

	/// @brief オートモードかどうか
	[[nodiscard]] bool isAutoMode() const noexcept
	{
		return m_mode == AutoSkipMode::Auto;
	}

	/// @brief スキップモード（SkipRead or SkipAll）かどうか
	[[nodiscard]] bool isSkipMode() const noexcept
	{
		return m_mode == AutoSkipMode::SkipRead || m_mode == AutoSkipMode::SkipAll;
	}

private:
	/// @brief オートモードの更新処理
	[[nodiscard]] AdvanceAction updateAuto(float deltaTime) noexcept
	{
		if (!m_textComplete)
		{
			return AdvanceAction::None;
		}

		/// ボイス再生待ち
		if (m_config.waitForVoice && m_voiceChecker && m_voiceChecker())
		{
			return AdvanceAction::None;
		}

		m_elapsed += deltaTime;

		const float delay = computeAutoDelay();
		if (m_elapsed >= delay)
		{
			m_elapsed = 0.0f;
			m_textComplete = false;
			return AdvanceAction::Advance;
		}

		return AdvanceAction::None;
	}

	/// @brief 既読スキップモードの更新処理
	[[nodiscard]] AdvanceAction updateSkipRead(float deltaTime) noexcept
	{
		/// 既読チェック: 未読テキストに到達したら停止
		if (m_readChecker && !m_readChecker(m_currentLabel, m_currentLineIndex))
		{
			setMode(AutoSkipMode::Idle);
			return AdvanceAction::None;
		}

		return updateSkipCommon(deltaTime);
	}

	/// @brief 全スキップモードの更新処理
	[[nodiscard]] AdvanceAction updateSkipAll(float deltaTime) noexcept
	{
		return updateSkipCommon(deltaTime);
	}

	/// @brief スキップモード共通の更新処理
	[[nodiscard]] AdvanceAction updateSkipCommon(float deltaTime) noexcept
	{
		m_elapsed += deltaTime;

		if (m_elapsed >= m_config.skipAdvanceInterval)
		{
			m_elapsed = 0.0f;
			m_textComplete = false;
			return AdvanceAction::Skip;
		}

		return AdvanceAction::None;
	}

	/// @brief オートモードの待機時間を計算する
	/// @return 待機時間（秒）
	[[nodiscard]] float computeAutoDelay() const noexcept
	{
		const float lengthBonus =
			static_cast<float>(m_currentTextLength) * m_config.textLengthFactor;
		return (m_config.autoBaseDelay + lengthBonus) * m_config.autoSpeedScale;
	}

	AutoSkipConfig m_config;                  ///< 設定パラメータ
	AutoSkipMode m_mode = AutoSkipMode::Idle; ///< 現在のモード
	float m_elapsed = 0.0f;                   ///< 経過時間（秒）
	bool m_textComplete = false;              ///< テキスト表示完了フラグ
	std::size_t m_currentTextLength = 0;      ///< 現在のテキスト文字数
	std::string_view m_currentLabel;          ///< 現在のスクリプトラベル
	int m_currentLineIndex = 0;               ///< 現在の行番号

	ReadCheckFn m_readChecker;                ///< 既読判定コールバック
	VoicePlayingFn m_voiceChecker;            ///< ボイス再生中判定コールバック
};

} // namespace mitiru::vn
