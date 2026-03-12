#pragma once

/// @file IDebugOverlay.hpp
/// @brief デバッグオーバーレイの抽象インターフェース
/// @details ImGuiや他のUI実装を差し替え可能にするための抽象層。

#include <string_view>

namespace mitiru::debug
{

/// @brief デバッグオーバーレイの抽象インターフェース
/// @details エンジン統計やデバッグ用ウィジェットを描画するための共通API。
///          ImGui実装やヌル実装を差し替え可能にする。
///
/// @code
/// mitiru::debug::NullDebugOverlay overlay;
/// overlay.beginFrame();
/// overlay.text("FPS", "60.0");
/// overlay.endFrame();
/// @endcode
class IDebugOverlay
{
public:
	/// @brief デストラクタ
	virtual ~IDebugOverlay() = default;

	/// @brief 新しいフレームを開始する
	virtual void beginFrame() = 0;

	/// @brief フレーム描画を終了する
	virtual void endFrame() = 0;

	/// @brief テキストを表示する
	/// @param label ラベル文字列
	/// @param value 値文字列
	virtual void text(std::string_view label, std::string_view value) = 0;

	/// @brief スライダーを表示する
	/// @param label ラベル文字列
	/// @param value 現在値（変更される可能性あり）
	/// @param min 最小値
	/// @param max 最大値
	/// @return 値が変更された場合 true
	virtual bool slider(std::string_view label, float& value, float min, float max) = 0;

	/// @brief チェックボックスを表示する
	/// @param label ラベル文字列
	/// @param value 現在のチェック状態（変更される可能性あり）
	/// @return 値が変更された場合 true
	virtual bool checkbox(std::string_view label, bool& value) = 0;

	/// @brief ボタンを表示する
	/// @param label ラベル文字列
	/// @return ボタンが押された場合 true
	virtual bool button(std::string_view label) = 0;

	/// @brief セパレーターを表示する
	virtual void separator() = 0;

	/// @brief ウィンドウを開始する
	/// @param title ウィンドウタイトル
	/// @return ウィンドウが表示されている場合 true
	virtual bool beginWindow(std::string_view title) = 0;

	/// @brief ウィンドウを終了する
	virtual void endWindow() = 0;
};

} // namespace mitiru::debug
