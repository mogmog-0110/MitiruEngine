#pragma once

/// @file NullDebugOverlay.hpp
/// @brief ヌルデバッグオーバーレイ（ノーオペレーション実装）
/// @details ヘッドレスモードやテスト時に使用する、何も描画しないデバッグオーバーレイ。

#include <mitiru/debug/IDebugOverlay.hpp>

namespace mitiru::debug
{

/// @brief ヌルデバッグオーバーレイ
/// @details 全操作がノーオペレーションの IDebugOverlay 実装。
///          ヘッドレスモードやユニットテストで使用する。
///
/// @code
/// mitiru::debug::NullDebugOverlay overlay;
/// overlay.beginFrame();  // 何も起きない
/// overlay.text("FPS", "60.0");  // 何も起きない
/// overlay.endFrame();  // 何も起きない
/// @endcode
class NullDebugOverlay : public IDebugOverlay
{
public:
	/// @brief 新しいフレームを開始する（ノーオペレーション）
	void beginFrame() override {}

	/// @brief フレーム描画を終了する（ノーオペレーション）
	void endFrame() override {}

	/// @brief テキストを表示する（ノーオペレーション）
	/// @param label ラベル文字列（無視される）
	/// @param value 値文字列（無視される）
	void text(std::string_view label, std::string_view value) override
	{
		static_cast<void>(label);
		static_cast<void>(value);
	}

	/// @brief スライダーを表示する（ノーオペレーション）
	/// @param label ラベル文字列（無視される）
	/// @param value 値（変更されない）
	/// @param min 最小値（無視される）
	/// @param max 最大値（無視される）
	/// @return 常に false
	bool slider(std::string_view label, float& value, float min, float max) override
	{
		static_cast<void>(label);
		static_cast<void>(value);
		static_cast<void>(min);
		static_cast<void>(max);
		return false;
	}

	/// @brief チェックボックスを表示する（ノーオペレーション）
	/// @param label ラベル文字列（無視される）
	/// @param value チェック状態（変更されない）
	/// @return 常に false
	bool checkbox(std::string_view label, bool& value) override
	{
		static_cast<void>(label);
		static_cast<void>(value);
		return false;
	}

	/// @brief ボタンを表示する（ノーオペレーション）
	/// @param label ラベル文字列（無視される）
	/// @return 常に false
	bool button(std::string_view label) override
	{
		static_cast<void>(label);
		return false;
	}

	/// @brief セパレーターを表示する（ノーオペレーション）
	void separator() override {}

	/// @brief ウィンドウを開始する（ノーオペレーション）
	/// @param title ウィンドウタイトル（無視される）
	/// @return 常に false
	bool beginWindow(std::string_view title) override
	{
		static_cast<void>(title);
		return false;
	}

	/// @brief ウィンドウを終了する（ノーオペレーション）
	void endWindow() override {}
};

} // namespace mitiru::debug
