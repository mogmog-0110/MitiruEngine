#pragma once

/// @file ImGuiOverlay.hpp
/// @brief ImGuiデバッグオーバーレイアダプタ
/// @details MITIRU_HAS_IMGUI が定義されている場合のみ有効になる。
///          ImGuiのAPIをIDebugOverlayインターフェースにマッピングする。

#ifdef MITIRU_HAS_IMGUI

#include <string>

#include <imgui.h>

#include <mitiru/debug/IDebugOverlay.hpp>

namespace mitiru::debug
{

/// @brief ImGuiベースのデバッグオーバーレイ実装
/// @details imgui.h の関数を IDebugOverlay インターフェースにマッピングする。
///          使用するにはImGuiライブラリをプロジェクトにベンダリングし、
///          MITIRU_HAS_IMGUI を定義する必要がある。
///
/// @code
/// // MITIRU_HAS_IMGUI が定義されている場合のみ利用可能
/// mitiru::debug::ImGuiOverlay overlay;
/// overlay.beginFrame();
/// overlay.text("FPS", "60.0");
/// overlay.endFrame();
/// @endcode
class ImGuiOverlay : public IDebugOverlay
{
public:
	/// @brief 新しいフレームを開始する
	void beginFrame() override
	{
		ImGui::NewFrame();
	}

	/// @brief フレーム描画を終了する
	void endFrame() override
	{
		ImGui::Render();
	}

	/// @brief テキストを表示する
	/// @param label ラベル文字列
	/// @param value 値文字列
	void text(std::string_view label, std::string_view value) override
	{
		const std::string formatted =
			std::string(label) + ": " + std::string(value);
		ImGui::TextUnformatted(formatted.c_str());
	}

	/// @brief スライダーを表示する
	/// @param label ラベル文字列
	/// @param value 現在値
	/// @param min 最小値
	/// @param max 最大値
	/// @return 値が変更された場合 true
	bool slider(std::string_view label, float& value, float min, float max) override
	{
		const std::string labelStr(label);
		return ImGui::SliderFloat(labelStr.c_str(), &value, min, max);
	}

	/// @brief チェックボックスを表示する
	/// @param label ラベル文字列
	/// @param value チェック状態
	/// @return 値が変更された場合 true
	bool checkbox(std::string_view label, bool& value) override
	{
		const std::string labelStr(label);
		return ImGui::Checkbox(labelStr.c_str(), &value);
	}

	/// @brief ボタンを表示する
	/// @param label ラベル文字列
	/// @return ボタンが押された場合 true
	bool button(std::string_view label) override
	{
		const std::string labelStr(label);
		return ImGui::Button(labelStr.c_str());
	}

	/// @brief セパレーターを表示する
	void separator() override
	{
		ImGui::Separator();
	}

	/// @brief ウィンドウを開始する
	/// @param title ウィンドウタイトル
	/// @return ウィンドウが表示されている場合 true
	bool beginWindow(std::string_view title) override
	{
		const std::string titleStr(title);
		return ImGui::Begin(titleStr.c_str());
	}

	/// @brief ウィンドウを終了する
	void endWindow() override
	{
		ImGui::End();
	}
};

} // namespace mitiru::debug

#endif // MITIRU_HAS_IMGUI
