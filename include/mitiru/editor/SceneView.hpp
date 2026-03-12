#pragma once

/// @file SceneView.hpp
/// @brief シーンエディターデータモデル
/// @details カメラ状態、選択管理、グリッドスナップ、矩形選択など
///          シーンビューの状態とインタラクションを管理する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mitiru::editor
{

/// @brief 2Dベクトル（エディター内部用）
struct EditorVec2
{
	float x = 0.0f;  ///< X座標
	float y = 0.0f;  ///< Y座標
};

/// @brief シーンビューアクション種別
enum class SceneViewAction : std::uint8_t
{
	None,     ///< アクションなし
	Select,   ///< 選択
	Move,     ///< 移動
	Rotate,   ///< 回転
	Scale,    ///< スケール
	Place,    ///< 配置
};

/// @brief マウスボタンの状態
struct MouseState
{
	bool leftDown = false;    ///< 左ボタン押下中
	bool rightDown = false;   ///< 右ボタン押下中
	bool leftClicked = false; ///< 左ボタンが今フレームでクリックされた
};

/// @brief キーボード修飾キーの状態
struct KeyModifiers
{
	bool shift = false;   ///< Shiftキー
	bool ctrl = false;    ///< Ctrlキー
	bool alt = false;     ///< Altキー
};

/// @brief 選択矩形（ドラッグ選択用）
struct SelectionRect
{
	EditorVec2 start;     ///< ドラッグ開始位置
	EditorVec2 end;       ///< ドラッグ終了位置
	bool active = false;  ///< ドラッグ中か

	/// @brief 矩形の最小X座標を取得する
	[[nodiscard]] float minX() const noexcept { return std::min(start.x, end.x); }

	/// @brief 矩形の最小Y座標を取得する
	[[nodiscard]] float minY() const noexcept { return std::min(start.y, end.y); }

	/// @brief 矩形の最大X座標を取得する
	[[nodiscard]] float maxX() const noexcept { return std::max(start.x, end.x); }

	/// @brief 矩形の最大Y座標を取得する
	[[nodiscard]] float maxY() const noexcept { return std::max(start.y, end.y); }

	/// @brief 指定座標が矩形内にあるか判定する
	/// @param px X座標
	/// @param py Y座標
	/// @return 矩形内の場合 true
	[[nodiscard]] bool contains(float px, float py) const noexcept
	{
		return px >= minX() && px <= maxX() && py >= minY() && py <= maxY();
	}
};

/// @brief シーンビューの状態
/// @details カメラ位置、ズーム、選択状態、グリッド設定などを保持する。
struct SceneViewState
{
	EditorVec2 cameraPos;                     ///< カメラ位置
	float cameraZoom = 1.0f;                  ///< カメラズーム倍率
	std::vector<std::uint64_t> selectedEntities; ///< 選択中のエンティティIDリスト
	bool gridVisible = true;                  ///< グリッド表示
	float gridSize = 32.0f;                   ///< グリッドサイズ
	bool snapToGrid = false;                  ///< グリッドスナップ有効
	SceneViewAction currentAction = SceneViewAction::None; ///< 現在のアクション
	SelectionRect selectionRect;              ///< 選択矩形
};

/// @brief グリッドスナップを適用する
/// @param pos 元の座標
/// @param gridSize グリッドサイズ
/// @return スナップ後の座標
[[nodiscard]] inline EditorVec2 snapToGrid(EditorVec2 pos, float gridSize) noexcept
{
	if (gridSize <= 0.0f)
	{
		return pos;
	}
	return EditorVec2{
		std::round(pos.x / gridSize) * gridSize,
		std::round(pos.y / gridSize) * gridSize
	};
}

/// @brief シーンビューの入力を処理しアクションを決定する
/// @param state 現在のシーンビュー状態（更新される）
/// @param mousePos マウス位置
/// @param mouse マウスボタン状態
/// @param keys キー修飾状態
/// @return 決定されたアクション
inline SceneViewAction handleInput(
	SceneViewState& state,
	EditorVec2 mousePos,
	const MouseState& mouse,
	const KeyModifiers& keys)
{
	/// 右クリックドラッグでカメラ移動
	if (mouse.rightDown)
	{
		return SceneViewAction::None;
	}

	/// Ctrlキーでスケール操作
	if (keys.ctrl && mouse.leftDown)
	{
		state.currentAction = SceneViewAction::Scale;
		return SceneViewAction::Scale;
	}

	/// Altキーで回転操作
	if (keys.alt && mouse.leftDown)
	{
		state.currentAction = SceneViewAction::Rotate;
		return SceneViewAction::Rotate;
	}

	/// Shiftキーで配置操作
	if (keys.shift && mouse.leftClicked)
	{
		state.currentAction = SceneViewAction::Place;
		return SceneViewAction::Place;
	}

	/// 左クリックドラッグで矩形選択または移動
	if (mouse.leftDown)
	{
		if (!state.selectionRect.active)
		{
			/// ドラッグ開始
			state.selectionRect.active = true;
			state.selectionRect.start = mousePos;
			state.selectionRect.end = mousePos;
		}
		else
		{
			/// ドラッグ中
			state.selectionRect.end = mousePos;
		}

		/// 選択中エンティティがあればMove、なければSelect
		if (!state.selectedEntities.empty())
		{
			state.currentAction = SceneViewAction::Move;
			return SceneViewAction::Move;
		}

		state.currentAction = SceneViewAction::Select;
		return SceneViewAction::Select;
	}

	/// ボタンリリース時にドラッグ終了
	if (state.selectionRect.active)
	{
		state.selectionRect.active = false;
	}

	/// 左クリック（非ドラッグ）で選択
	if (mouse.leftClicked)
	{
		state.currentAction = SceneViewAction::Select;
		return SceneViewAction::Select;
	}

	state.currentAction = SceneViewAction::None;
	return SceneViewAction::None;
}

} // namespace mitiru::editor
