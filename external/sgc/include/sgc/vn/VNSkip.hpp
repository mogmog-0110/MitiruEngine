#pragma once

/// @file VNSkip.hpp
/// @brief スキップ/オートモード制御
///
/// ビジュアルノベルのスキップモード・オートモードの管理を提供する。
/// 既読判定によるスキップ制御、文字数に応じたオート待機時間を実装する。
///
/// @code
/// using namespace sgc::vn;
/// VNFlowController flow;
/// flow.setSkipMode(SkipMode::ReadOnly);
/// flow.markRead(std::hash<std::string>{}("Hello!"));
/// auto config = flow.autoModeConfig();
/// config.enabled = true;
/// config.delayMsPerChar = 50;
/// flow.setAutoModeConfig(config);
/// bool advance = flow.shouldAdvance(42, 13);
/// @endcode

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace sgc::vn
{

/// @brief スキップモード
enum class SkipMode : uint8_t
{
	Off,       ///< スキップ無効
	ReadOnly,  ///< 既読テキストのみスキップ
	All        ///< 全テキストをスキップ
};

/// @brief オートモード設定
struct AutoModeConfig
{
	bool enabled = false;        ///< オートモード有効フラグ
	int delayMsPerChar = 50;     ///< 1文字あたりの待機時間（ミリ秒）
	int minDelayMs = 500;        ///< 最小待機時間（ミリ秒）
	int maxDelayMs = 5000;       ///< 最大待機時間（ミリ秒）
};

/// @brief フロー制御コントローラ
///
/// スキップモードとオートモードを管理し、
/// テキスト進行の判定を提供する。
class VNFlowController
{
public:
	/// @brief スキップモードを設定する
	/// @param mode 設定するスキップモード
	void setSkipMode(SkipMode mode) noexcept
	{
		m_skipMode = mode;
	}

	/// @brief 現在のスキップモードを取得する
	/// @return スキップモード
	[[nodiscard]] SkipMode skipMode() const noexcept
	{
		return m_skipMode;
	}

	/// @brief オートモード設定を設定する
	/// @param config オートモード設定
	void setAutoModeConfig(const AutoModeConfig& config)
	{
		m_autoConfig = config;
	}

	/// @brief オートモード設定を取得する
	/// @return オートモード設定
	[[nodiscard]] const AutoModeConfig& autoModeConfig() const noexcept
	{
		return m_autoConfig;
	}

	/// @brief テキストを既読として記録する
	/// @param textHash テキストのハッシュ値
	void markRead(std::size_t textHash)
	{
		m_readTexts.insert(textHash);
	}

	/// @brief テキストが既読かどうかを判定する
	/// @param textHash テキストのハッシュ値
	/// @return 既読ならtrue
	[[nodiscard]] bool isTextRead(std::size_t textHash) const
	{
		return m_readTexts.contains(textHash);
	}

	/// @brief 既読テキスト数を取得する
	/// @return 既読テキストの数
	[[nodiscard]] std::size_t readCount() const noexcept
	{
		return m_readTexts.size();
	}

	/// @brief 既読情報をクリアする
	void clearReadHistory()
	{
		m_readTexts.clear();
	}

	/// @brief テキストを自動進行すべきか判定する
	///
	/// スキップモード・オートモード・既読状態を総合的に判定して、
	/// 現在のテキストを自動的に進めるべきかを返す。
	///
	/// @param textHash 現在表示中のテキストハッシュ
	/// @param charCount テキストの文字数
	/// @return 自動進行すべきならtrue
	[[nodiscard]] bool shouldAdvance(std::size_t textHash, [[maybe_unused]] std::size_t charCount) const
	{
		// スキップモード判定
		if (m_skipMode == SkipMode::All)
		{
			return true;
		}

		if (m_skipMode == SkipMode::ReadOnly && isTextRead(textHash))
		{
			return true;
		}

		// オートモード判定
		if (m_autoConfig.enabled)
		{
			return true;
		}

		return false;
	}

	/// @brief オートモードの待機時間を計算する
	///
	/// 文字数に応じた待機時間をミリ秒で返す。
	/// minDelayMs〜maxDelayMsの範囲にクランプされる。
	///
	/// @param charCount テキストの文字数
	/// @return 待機時間（ミリ秒）
	[[nodiscard]] int calculateAutoDelay(std::size_t charCount) const noexcept
	{
		const int rawDelay = m_autoConfig.delayMsPerChar * static_cast<int>(charCount);

		if (rawDelay < m_autoConfig.minDelayMs)
		{
			return m_autoConfig.minDelayMs;
		}
		if (rawDelay > m_autoConfig.maxDelayMs)
		{
			return m_autoConfig.maxDelayMs;
		}
		return rawDelay;
	}

private:
	SkipMode m_skipMode = SkipMode::Off;           ///< 現在のスキップモード
	AutoModeConfig m_autoConfig;                   ///< オートモード設定
	std::unordered_set<std::size_t> m_readTexts;   ///< 既読テキストハッシュセット
};

} // namespace sgc::vn
