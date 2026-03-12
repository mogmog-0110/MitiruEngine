#pragma once

/// @file ImGuiBackend.hpp
/// @brief ImGui抽象バックエンドレイヤー
/// @details 実際のImGuiライブラリに依存せず、ImGuiスタイルのウィジェットAPIを
///          抽象化する。テスト用のNullImGuiBackendも提供する。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::imgui
{

/// @brief ImGuiウィジェット操作の結果を格納する構造体
/// @details ボタンクリック、ホバー、値変更など各種状態を統一的に返す。
struct ImGuiWidgetResult
{
	bool clicked = false;       ///< クリックされたか
	bool hovered = false;       ///< ホバー中か
	bool changed = false;       ///< 値が変更されたか
	float valueFloat = 0.0f;    ///< float値（スライダー等）
	int valueInt = 0;           ///< int値
	bool valueBool = false;     ///< bool値（チェックボックス等）
	std::string valueStr;       ///< 文字列値（テキスト入力等）
};

/// @brief ImGuiバックエンドの抽象インターフェース
/// @details 初期化・シャットダウン・フレーム管理・描画を抽象化する。
///          実際のImGui実装やNullバックエンドを差し替え可能にする。
class IImGuiBackend
{
public:
	/// @brief デストラクタ
	virtual ~IImGuiBackend() = default;

	/// @brief バックエンドを初期化する
	/// @return 成功した場合 true
	[[nodiscard]] virtual bool init() = 0;

	/// @brief バックエンドをシャットダウンする
	virtual void shutdown() = 0;

	/// @brief 新しいフレームを開始する
	virtual void newFrame() = 0;

	/// @brief フレーム描画を実行する
	virtual void render() = 0;

	/// @brief テキストを表示する
	/// @param text 表示するテキスト
	virtual void text(std::string_view text) = 0;

	/// @brief ボタンを表示する
	/// @param label ラベル文字列
	/// @return クリックされた場合 true
	[[nodiscard]] virtual bool button(std::string_view label) = 0;

	/// @brief floatスライダーを表示する
	/// @param label ラベル文字列
	/// @param value 現在値（変更される可能性あり）
	/// @param min 最小値
	/// @param max 最大値
	/// @return 値が変更された場合 true
	[[nodiscard]] virtual bool slider(std::string_view label, float& value, float min, float max) = 0;

	/// @brief チェックボックスを表示する
	/// @param label ラベル文字列
	/// @param value チェック状態（変更される可能性あり）
	/// @return 値が変更された場合 true
	[[nodiscard]] virtual bool checkbox(std::string_view label, bool& value) = 0;

	/// @brief テキスト入力フィールドを表示する
	/// @param label ラベル文字列
	/// @param buffer 入力バッファ
	/// @return 値が変更された場合 true
	[[nodiscard]] virtual bool inputText(std::string_view label, std::string& buffer) = 0;

	/// @brief カラーエディターを表示する
	/// @param label ラベル文字列
	/// @param r 赤成分 (0.0〜1.0)
	/// @param g 緑成分 (0.0〜1.0)
	/// @param b 青成分 (0.0〜1.0)
	/// @param a アルファ成分 (0.0〜1.0)
	/// @return 値が変更された場合 true
	[[nodiscard]] virtual bool colorEdit(std::string_view label,
		float& r, float& g, float& b, float& a) = 0;

	/// @brief ツリーノードを開始する
	/// @param label ラベル文字列
	/// @return ノードが展開されている場合 true
	[[nodiscard]] virtual bool treeNode(std::string_view label) = 0;

	/// @brief ツリーノードを終了する
	virtual void treePop() = 0;

	/// @brief テーブルを開始する
	/// @param label テーブルラベル
	/// @param columnCount 列数
	/// @return テーブルが表示されている場合 true
	[[nodiscard]] virtual bool beginTable(std::string_view label, int columnCount) = 0;

	/// @brief テーブルの次の列に移動する
	virtual void tableNextColumn() = 0;

	/// @brief テーブルを終了する
	virtual void endTable() = 0;
};

/// @brief テスト用のNull ImGuiバックエンド実装
/// @details 全ての操作をno-opで実装する。テストや非GUI環境で使用する。
///
/// @code
/// mitiru::imgui::NullImGuiBackend backend;
/// backend.init();
/// backend.newFrame();
/// backend.text("Hello");
/// backend.render();
/// backend.shutdown();
/// @endcode
class NullImGuiBackend : public IImGuiBackend
{
public:
	[[nodiscard]] bool init() override { return true; }
	void shutdown() override {}
	void newFrame() override { ++m_frameCount; }
	void render() override {}

	void text(std::string_view) override {}
	[[nodiscard]] bool button(std::string_view) override { return false; }
	[[nodiscard]] bool slider(std::string_view, float&, float, float) override { return false; }
	[[nodiscard]] bool checkbox(std::string_view, bool&) override { return false; }
	[[nodiscard]] bool inputText(std::string_view, std::string&) override { return false; }
	[[nodiscard]] bool colorEdit(std::string_view, float&, float&, float&, float&) override { return false; }
	[[nodiscard]] bool treeNode(std::string_view) override { return false; }
	void treePop() override {}
	[[nodiscard]] bool beginTable(std::string_view, int) override { return false; }
	void tableNextColumn() override {}
	void endTable() override {}

	/// @brief フレームカウントを取得する（テスト用）
	/// @return newFrame()が呼ばれた回数
	[[nodiscard]] int frameCount() const noexcept { return m_frameCount; }

private:
	int m_frameCount = 0;  ///< フレームカウント
};

// --- ヘルパーフリー関数群 ---

/// @brief テキストラベル付きの値を表示する
/// @param backend バックエンドへのポインタ
/// @param label ラベル文字列
/// @param value 値文字列
inline void labeledText(IImGuiBackend* backend, std::string_view label, std::string_view value)
{
	if (!backend) return;
	const std::string formatted = std::string(label) + ": " + std::string(value);
	backend->text(formatted);
}

/// @brief ボタンを表示してImGuiWidgetResultを返す
/// @param backend バックエンドへのポインタ
/// @param label ボタンラベル
/// @return ウィジェット結果
[[nodiscard]] inline ImGuiWidgetResult evaluateButton(IImGuiBackend* backend, std::string_view label)
{
	ImGuiWidgetResult result;
	if (!backend) return result;
	result.clicked = backend->button(label);
	return result;
}

/// @brief floatスライダーを表示してImGuiWidgetResultを返す
/// @param backend バックエンドへのポインタ
/// @param label スライダーラベル
/// @param value 現在値
/// @param min 最小値
/// @param max 最大値
/// @return ウィジェット結果
[[nodiscard]] inline ImGuiWidgetResult evaluateSlider(
	IImGuiBackend* backend, std::string_view label, float value, float min, float max)
{
	ImGuiWidgetResult result;
	if (!backend) return result;
	float v = value;
	result.changed = backend->slider(label, v, min, max);
	result.valueFloat = v;
	return result;
}

/// @brief チェックボックスを表示してImGuiWidgetResultを返す
/// @param backend バックエンドへのポインタ
/// @param label チェックボックスラベル
/// @param value 現在のチェック状態
/// @return ウィジェット結果
[[nodiscard]] inline ImGuiWidgetResult evaluateCheckbox(
	IImGuiBackend* backend, std::string_view label, bool value)
{
	ImGuiWidgetResult result;
	if (!backend) return result;
	bool v = value;
	result.changed = backend->checkbox(label, v);
	result.valueBool = v;
	return result;
}

/// @brief テキスト入力を表示してImGuiWidgetResultを返す
/// @param backend バックエンドへのポインタ
/// @param label 入力ラベル
/// @param text 現在のテキスト
/// @return ウィジェット結果
[[nodiscard]] inline ImGuiWidgetResult evaluateInputText(
	IImGuiBackend* backend, std::string_view label, std::string text)
{
	ImGuiWidgetResult result;
	if (!backend) return result;
	result.changed = backend->inputText(label, text);
	result.valueStr = std::move(text);
	return result;
}

} // namespace mitiru::imgui
