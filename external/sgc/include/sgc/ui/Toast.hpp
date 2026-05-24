#pragma once

/// @file Toast.hpp
/// @brief トースト通知評価ユーティリティ
///
/// 一時的なメッセージ通知の表示タイミング・不透明度・配置を計算する。
/// フレームワーク非依存で、描画ロジックは含まない。
/// 不変性を重視し、状態の更新は新しい値を返す。
///
/// @code
/// using namespace sgc::ui;
/// ToastState toast{0.0f, 3.0f, 0.3f, 0.5f};
/// // 毎フレーム
/// toast = advanceToast(toast, deltaTime);
/// auto result = evaluateToast(toast);
/// if (result.isActive) {
///     auto pos = toastPosition(screenBounds, toastSize, 0, Anchor::TopCenter);
///     // result.opacity で不透明度を設定して描画
/// }
/// @endcode

#include "sgc/ui/Anchor.hpp"

namespace sgc::ui
{

/// @brief トースト通知の状態
struct ToastState
{
	float elapsed{0.0f};    ///< 表示開始からの経過秒
	float duration{2.0f};   ///< 表示時間（秒）
	float fadeIn{0.2f};     ///< フェードイン時間
	float fadeOut{0.5f};    ///< フェードアウト時間

	[[nodiscard]] constexpr bool operator==(const ToastState&) const noexcept = default;
};

/// @brief トースト評価結果
struct ToastResult
{
	float opacity{0.0f};    ///< 不透明度 [0.0, 1.0]
	bool isActive{false};   ///< 表示中か
	bool isFinished{false}; ///< 完全に終了したか（duration + fadeOut 経過）
};

/// @brief トーストの不透明度と状態を評価する
///
/// フェードイン → 表示維持 → フェードアウトの3段階で不透明度を計算する。
/// - elapsed < fadeIn: フェードイン中
/// - elapsed < fadeIn + duration: 完全表示中
/// - elapsed < fadeIn + duration + fadeOut: フェードアウト中
/// - elapsed >= fadeIn + duration + fadeOut: 終了
///
/// @param state トーストの現在状態
/// @return 不透明度・表示中フラグ・終了フラグ
[[nodiscard]] constexpr ToastResult evaluateToast(const ToastState& state) noexcept
{
	const float totalTime = state.fadeIn + state.duration + state.fadeOut;

	if (state.elapsed >= totalTime)
	{
		return {0.0f, false, true};
	}

	if (state.elapsed < 0.0f)
	{
		return {0.0f, false, false};
	}

	float opacity = 1.0f;

	if (state.fadeIn > 0.0f && state.elapsed < state.fadeIn)
	{
		// フェードイン中
		opacity = state.elapsed / state.fadeIn;
	}
	else
	{
		const float fadeOutStart = state.fadeIn + state.duration;
		if (state.fadeOut > 0.0f && state.elapsed > fadeOutStart)
		{
			// フェードアウト中
			const float fadeOutElapsed = state.elapsed - fadeOutStart;
			opacity = 1.0f - (fadeOutElapsed / state.fadeOut);
		}
	}

	// クランプ
	if (opacity < 0.0f)
	{
		opacity = 0.0f;
	}
	if (opacity > 1.0f)
	{
		opacity = 1.0f;
	}

	return {opacity, true, false};
}

/// @brief トースト状態の経過時間を進める
///
/// 不変性を維持し、新しいToastStateを返す。元の状態は変更しない。
///
/// @param state 現在のトースト状態
/// @param dt デルタタイム（秒）
/// @return 経過時間が進んだ新しいToastState
[[nodiscard]] constexpr ToastState advanceToast(
	const ToastState& state, float dt) noexcept
{
	return ToastState{
		state.elapsed + dt,
		state.duration,
		state.fadeIn,
		state.fadeOut
	};
}

/// @brief トーストのスタック配置位置を計算する
///
/// 画面領域内で指定されたgravity方向にトーストをスタッキング配置する。
/// slotIndex=0が最新のトーストで、番号が増えるほど奥にスタックされる。
///
/// @param screenBounds 画面（配置領域）の矩形
/// @param toastSize トースト1個のサイズ（幅×高さ）
/// @param slotIndex スロット番号（0=最新）
/// @param gravity トーストが積まれる方向のアンカー
/// @param spacing トースト間のスペース（デフォルト: 8ピクセル）
/// @return トースト左上の座標
[[nodiscard]] constexpr Vec2f toastPosition(
	const Rectf& screenBounds, const Vec2f& toastSize,
	int slotIndex, Anchor gravity,
	float spacing = 8.0f) noexcept
{
	// アンカー基準点
	const Vec2f anchor = anchorPoint(screenBounds, gravity);

	// スタック方向の符号（上方向配置なら下に向かう、下方向配置なら上に向かう）
	const float slot = static_cast<float>(slotIndex);
	const float stackOffset = slot * (toastSize.y + spacing);

	float x = anchor.x;
	float y = anchor.y;

	// 水平位置
	switch (gravity)
	{
	case Anchor::TopLeft:
	case Anchor::CenterLeft:
	case Anchor::BottomLeft:
		// 左寄せ: そのまま
		break;
	case Anchor::TopCenter:
	case Anchor::Center:
	case Anchor::BottomCenter:
		// 中央寄せ
		x -= toastSize.x * 0.5f;
		break;
	case Anchor::TopRight:
	case Anchor::CenterRight:
	case Anchor::BottomRight:
		// 右寄せ
		x -= toastSize.x;
		break;
	}

	// 垂直位置 + スタック方向
	switch (gravity)
	{
	case Anchor::TopLeft:
	case Anchor::TopCenter:
	case Anchor::TopRight:
		// 上から下にスタック
		y += stackOffset;
		break;
	case Anchor::CenterLeft:
	case Anchor::Center:
	case Anchor::CenterRight:
		// 中央から下にスタック
		y -= toastSize.y * 0.5f;
		y += stackOffset;
		break;
	case Anchor::BottomLeft:
	case Anchor::BottomCenter:
	case Anchor::BottomRight:
		// 下から上にスタック
		y -= toastSize.y;
		y -= stackOffset;
		break;
	}

	return {x, y};
}

} // namespace sgc::ui
