#pragma once

/// @file HeadlessPlatform.hpp
/// @brief ヘッドレスプラットフォーム実装
/// @details ウィンドウを持たないヘッドレス環境用の実装。
///          テストやAI訓練などGUI不要の場面で使用する。

#include <memory>
#include <string>
#include <string_view>

#include <mitiru/platform/IPlatform.hpp>
#include <mitiru/platform/IWindow.hpp>

namespace mitiru
{

/// @brief ヘッドレスウィンドウ実装
/// @details 実際のウィンドウを作成せず、論理的なサイズのみ保持する。
class HeadlessWindow final : public IWindow
{
public:
	/// @brief コンストラクタ
	/// @param title ウィンドウタイトル（記録用）
	/// @param width 論理幅
	/// @param height 論理高さ
	explicit HeadlessWindow(std::string_view title, int width, int height)
		: m_title(title)
		, m_width(width)
		, m_height(height)
	{
	}

	/// @brief 閉じるべきかどうか
	/// @return requestClose() が呼ばれていれば true
	[[nodiscard]] bool shouldClose() const override
	{
		return m_shouldClose;
	}

	/// @brief イベントポーリング（ヘッドレスでは何もしない）
	void pollEvents() override
	{
		/// ヘッドレスモードではイベント処理不要
	}

	/// @brief ウィンドウ幅
	[[nodiscard]] int width() const override
	{
		return m_width;
	}

	/// @brief ウィンドウ高さ
	[[nodiscard]] int height() const override
	{
		return m_height;
	}

	/// @brief タイトル設定
	void setTitle(std::string_view title) override
	{
		m_title = title;
	}

	/// @brief 閉じ要求を設定する
	void requestClose() override
	{
		m_shouldClose = true;
	}

private:
	std::string m_title;       ///< ウィンドウタイトル
	int m_width;               ///< 論理幅
	int m_height;              ///< 論理高さ
	bool m_shouldClose = false; ///< 閉じ要求フラグ
};

/// @brief ヘッドレスプラットフォーム実装
/// @details HeadlessWindow を生成するプラットフォーム。
class HeadlessPlatform final : public IPlatform
{
public:
	/// @brief ウィンドウを生成する
	/// @param title ウィンドウタイトル
	/// @param width 幅
	/// @param height 高さ
	/// @return HeadlessWindow インスタンス
	[[nodiscard]] std::unique_ptr<IWindow> createWindow(
		std::string_view title, int width, int height) override
	{
		return std::make_unique<HeadlessWindow>(title, width, height);
	}

	/// @brief プラットフォーム種別
	[[nodiscard]] PlatformType type() const noexcept override
	{
		return PlatformType::Headless;
	}
};

} // namespace mitiru
