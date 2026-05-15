#pragma once

/// @file TouchInput.hpp
/// @brief タッチ・ジェスチャー入力管理
/// @details モバイルプラットフォーム向けのタッチ入力処理とジェスチャー認識。
///          タップ・スワイプ・ピンチ・回転などのジェスチャーを検出する。
///          タッチイベントをマウスイベントに変換するUI互換レイヤーも提供する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <mitiru/input/InputState.hpp>

namespace mitiru
{

/// @brief タッチフェーズ
enum class TouchPhase : std::uint8_t
{
	Began = 0,      ///< タッチ開始
	Moved = 1,      ///< タッチ移動中
	Ended = 2,      ///< タッチ終了
	Cancelled = 3   ///< タッチキャンセル
};

/// @brief スワイプ方向
enum class SwipeDirection : std::uint8_t
{
	Up = 0,     ///< 上方向
	Down = 1,   ///< 下方向
	Left = 2,   ///< 左方向
	Right = 3   ///< 右方向
};

/// @brief タッチポイント
/// @details 1つのタッチ接点の状態を保持する。
struct TouchPoint
{
	int id = 0;               ///< タッチポインタID（マルチタッチ識別用）
	float x = 0.0f;           ///< X座標（ピクセル）
	float y = 0.0f;           ///< Y座標（ピクセル）
	float pressure = 1.0f;    ///< 筆圧（0.0〜1.0、非対応デバイスでは1.0）
	TouchPhase phase = TouchPhase::Began;  ///< タッチフェーズ
};

/// @brief ジェスチャー設定
/// @details ジェスチャー認識のしきい値パラメータ。
struct GestureConfig
{
	float tapMaxDuration = 0.3f;        ///< タップ判定の最大時間（秒）
	float doubleTapInterval = 0.3f;     ///< ダブルタップの最大間隔（秒）
	float swipeMinDistance = 50.0f;      ///< スワイプ判定の最小移動距離（ピクセル）
	float longPressDuration = 0.5f;     ///< 長押し判定の最小時間（秒）
	float pinchMinDistance = 10.0f;     ///< ピンチ判定の最小距離変化（ピクセル）
	float rotateMinAngle = 0.1f;        ///< 回転判定の最小角度変化（ラジアン）

	/// @brief デフォルト設定を取得する
	/// @return 標準的なジェスチャー設定
	[[nodiscard]] static GestureConfig defaults() noexcept
	{
		return GestureConfig{};
	}
};

/// @brief ピンチジェスチャー情報
struct PinchInfo
{
	float scale = 1.0f;       ///< スケールファクター（1.0 = 変化なし）
	float centerX = 0.0f;     ///< ピンチ中心X座標
	float centerY = 0.0f;     ///< ピンチ中心Y座標
};

/// @brief 回転ジェスチャー情報
struct RotateInfo
{
	float angle = 0.0f;       ///< 回転角度（ラジアン）
	float centerX = 0.0f;     ///< 回転中心X座標
	float centerY = 0.0f;     ///< 回転中心Y座標
};

/// @brief タッチ入力マネージャー
/// @details タッチイベントの処理とジェスチャー認識を行う。
///          各種ジェスチャーコールバックを登録して使用する。
///
/// @code
/// TouchInputManager touch;
/// touch.onTap([](float x, float y) {
///     // タップ処理
/// });
/// touch.onSwipe([](SwipeDirection dir, float x, float y) {
///     // スワイプ処理
/// });
///
/// // 入力ループ内
/// touch.onTouchEvent(point);
/// touch.update(deltaTime);
/// @endcode
class TouchInputManager
{
public:
	/// @brief コンストラクタ
	/// @param config ジェスチャー設定
	explicit TouchInputManager(
		const GestureConfig& config = GestureConfig::defaults()) noexcept
		: m_config(config)
	{
	}

	// ── タッチイベント処理 ─────────────────────────────────────

	/// @brief 生タッチイベントを処理する
	/// @param point タッチポイント
	void onTouchEvent(const TouchPoint& point)
	{
		switch (point.phase)
		{
		case TouchPhase::Began:
			handleTouchBegan(point);
			break;
		case TouchPhase::Moved:
			handleTouchMoved(point);
			break;
		case TouchPhase::Ended:
			handleTouchEnded(point);
			break;
		case TouchPhase::Cancelled:
			handleTouchCancelled(point);
			break;
		}
	}

	/// @brief フレーム更新（時間ベースのジェスチャー検出用）
	/// @param deltaTime 前フレームからの経過時間（秒）
	void update(float deltaTime)
	{
		m_elapsed += deltaTime;

		/// 長押し検出
		if (m_activeTouches.size() == 1 && !m_longPressTriggered)
		{
			const auto& touch = m_activeTouches.front();
			const float dx = touch.x - m_touchStartX;
			const float dy = touch.y - m_touchStartY;
			const float dist = std::sqrt(dx * dx + dy * dy);

			if (dist < m_config.swipeMinDistance * 0.5f)
			{
				const float holdTime = m_elapsed - m_touchStartTime;
				if (holdTime >= m_config.longPressDuration && m_longPressCallback)
				{
					m_longPressCallback(touch.x, touch.y);
					m_longPressTriggered = true;
				}
			}
		}
	}

	// ── アクティブタッチ取得 ───────────────────────────────────

	/// @brief 現在アクティブなタッチポイントを取得する
	/// @return アクティブなタッチポイントのベクトル
	[[nodiscard]] const std::vector<TouchPoint>& activeTouches() const noexcept
	{
		return m_activeTouches;
	}

	/// @brief アクティブなタッチ数を取得する
	/// @return アクティブなタッチポイント数
	[[nodiscard]] std::size_t touchCount() const noexcept
	{
		return m_activeTouches.size();
	}

	// ── ジェスチャーコールバック登録 ───────────────────────────

	/// @brief タップコールバックを登録する
	/// @param callback コールバック関数 (x, y)
	void onTap(std::function<void(float, float)> callback)
	{
		m_tapCallback = std::move(callback);
	}

	/// @brief ダブルタップコールバックを登録する
	/// @param callback コールバック関数 (x, y)
	void onDoubleTap(std::function<void(float, float)> callback)
	{
		m_doubleTapCallback = std::move(callback);
	}

	/// @brief 長押しコールバックを登録する
	/// @param callback コールバック関数 (x, y)
	/// @param duration 長押し判定時間（秒、0で設定値を使用）
	void onLongPress(std::function<void(float, float)> callback,
		float duration = 0.0f)
	{
		m_longPressCallback = std::move(callback);
		if (duration > 0.0f)
		{
			m_config.longPressDuration = duration;
		}
	}

	/// @brief スワイプコールバックを登録する
	/// @param callback コールバック関数 (direction, startX, startY)
	/// @param direction フィルタする方向（省略時は全方向）
	void onSwipe(std::function<void(SwipeDirection, float, float)> callback,
		SwipeDirection direction = SwipeDirection::Up)
	{
		static_cast<void>(direction);
		m_swipeCallback = std::move(callback);
	}

	/// @brief ピンチコールバックを登録する
	/// @param callback コールバック関数 (PinchInfo)
	void onPinch(std::function<void(const PinchInfo&)> callback)
	{
		m_pinchCallback = std::move(callback);
	}

	/// @brief 回転コールバックを登録する
	/// @param callback コールバック関数 (RotateInfo)
	void onRotate(std::function<void(const RotateInfo&)> callback)
	{
		m_rotateCallback = std::move(callback);
	}

	// ── マウスイベント変換（UI互換） ──────────────────────────

	/// @brief タッチイベントをマウスイベントに変換してInputStateに反映する
	/// @param state 更新するInputState
	/// @details 最初のタッチポイントを左クリック＋マウス座標に変換する。
	///          デスクトップUI用のコードをモバイルでも動作させるために使用する。
	void applyToMouseState(InputState& state) const
	{
		if (m_activeTouches.empty())
		{
			state.setMouseButtonDown(MouseButton::Left, false);
			return;
		}

		const auto& primary = m_activeTouches.front();
		state.setMousePosition(primary.x, primary.y);
		state.setMouseButtonDown(MouseButton::Left, true);
	}

	// ── 設定 ──────────────────────────────────────────────────

	/// @brief ジェスチャー設定を取得する
	[[nodiscard]] const GestureConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief ジェスチャー設定を変更する
	/// @param config 新しいジェスチャー設定
	void setConfig(const GestureConfig& config) noexcept
	{
		m_config = config;
	}

	/// コピー禁止
	TouchInputManager(const TouchInputManager&) = delete;
	TouchInputManager& operator=(const TouchInputManager&) = delete;

	/// ムーブ許可
	TouchInputManager(TouchInputManager&&) noexcept = default;
	TouchInputManager& operator=(TouchInputManager&&) noexcept = default;

	/// デストラクタ
	~TouchInputManager() = default;

private:
	/// @brief タッチ開始の処理
	void handleTouchBegan(const TouchPoint& point)
	{
		m_activeTouches.push_back(point);

		if (m_activeTouches.size() == 1)
		{
			m_touchStartX = point.x;
			m_touchStartY = point.y;
			m_touchStartTime = m_elapsed;
			m_longPressTriggered = false;
		}

		/// 2本指ジェスチャーの初期距離・角度を記録
		if (m_activeTouches.size() == 2)
		{
			m_prevTwoFingerDistance = twoFingerDistance();
			m_prevTwoFingerAngle = twoFingerAngle();
		}
	}

	/// @brief タッチ移動の処理
	void handleTouchMoved(const TouchPoint& point)
	{
		for (auto& touch : m_activeTouches)
		{
			if (touch.id == point.id)
			{
				touch.x = point.x;
				touch.y = point.y;
				touch.pressure = point.pressure;
				touch.phase = TouchPhase::Moved;
				break;
			}
		}

		/// ピンチ・回転の検出（2本指）
		if (m_activeTouches.size() == 2)
		{
			detectPinch();
			detectRotate();
		}
	}

	/// @brief タッチ終了の処理
	void handleTouchEnded(const TouchPoint& point)
	{
		const float holdTime = m_elapsed - m_touchStartTime;
		const float dx = point.x - m_touchStartX;
		const float dy = point.y - m_touchStartY;
		const float dist = std::sqrt(dx * dx + dy * dy);

		/// スワイプ判定
		if (dist >= m_config.swipeMinDistance && holdTime < 1.0f)
		{
			detectSwipe(dx, dy, m_touchStartX, m_touchStartY);
		}
		/// タップ判定（短時間・移動少ない）
		else if (holdTime <= m_config.tapMaxDuration &&
			dist < m_config.swipeMinDistance * 0.5f &&
			!m_longPressTriggered)
		{
			handleTap(point.x, point.y);
		}

		removeTouchById(point.id);
	}

	/// @brief タッチキャンセルの処理
	void handleTouchCancelled(const TouchPoint& point)
	{
		removeTouchById(point.id);
	}

	/// @brief IDでタッチポイントを削除する
	void removeTouchById(int id)
	{
		m_activeTouches.erase(
			std::remove_if(m_activeTouches.begin(), m_activeTouches.end(),
				[id](const TouchPoint& t) { return t.id == id; }),
			m_activeTouches.end());
	}

	/// @brief タップ処理（シングル・ダブルタップ判定）
	void handleTap(float x, float y)
	{
		const float timeSinceLastTap = m_elapsed - m_lastTapTime;

		if (timeSinceLastTap <= m_config.doubleTapInterval && m_lastTapTime > 0.0f)
		{
			/// ダブルタップ
			if (m_doubleTapCallback)
			{
				m_doubleTapCallback(x, y);
			}
			m_lastTapTime = 0.0f;
		}
		else
		{
			/// シングルタップ
			if (m_tapCallback)
			{
				m_tapCallback(x, y);
			}
			m_lastTapTime = m_elapsed;
		}
	}

	/// @brief スワイプ方向を検出してコールバックを呼ぶ
	void detectSwipe(float dx, float dy, float startX, float startY)
	{
		if (!m_swipeCallback)
		{
			return;
		}

		SwipeDirection dir;
		if (std::abs(dx) > std::abs(dy))
		{
			dir = (dx > 0.0f) ? SwipeDirection::Right : SwipeDirection::Left;
		}
		else
		{
			dir = (dy > 0.0f) ? SwipeDirection::Down : SwipeDirection::Up;
		}

		m_swipeCallback(dir, startX, startY);
	}

	/// @brief ピンチジェスチャーを検出する
	void detectPinch()
	{
		if (m_activeTouches.size() < 2 || !m_pinchCallback)
		{
			return;
		}

		const float dist = twoFingerDistance();
		if (std::abs(dist - m_prevTwoFingerDistance) >= m_config.pinchMinDistance)
		{
			const float scale = (m_prevTwoFingerDistance > 0.0f)
				? dist / m_prevTwoFingerDistance
				: 1.0f;

			PinchInfo info;
			info.scale = scale;
			info.centerX = (m_activeTouches[0].x + m_activeTouches[1].x) * 0.5f;
			info.centerY = (m_activeTouches[0].y + m_activeTouches[1].y) * 0.5f;

			m_pinchCallback(info);
			m_prevTwoFingerDistance = dist;
		}
	}

	/// @brief 回転ジェスチャーを検出する
	void detectRotate()
	{
		if (m_activeTouches.size() < 2 || !m_rotateCallback)
		{
			return;
		}

		const float angle = twoFingerAngle();
		const float delta = angle - m_prevTwoFingerAngle;

		if (std::abs(delta) >= m_config.rotateMinAngle)
		{
			RotateInfo info;
			info.angle = delta;
			info.centerX = (m_activeTouches[0].x + m_activeTouches[1].x) * 0.5f;
			info.centerY = (m_activeTouches[0].y + m_activeTouches[1].y) * 0.5f;

			m_rotateCallback(info);
			m_prevTwoFingerAngle = angle;
		}
	}

	/// @brief 2本指の距離を計算する
	[[nodiscard]] float twoFingerDistance() const
	{
		if (m_activeTouches.size() < 2) return 0.0f;
		const float dx = m_activeTouches[1].x - m_activeTouches[0].x;
		const float dy = m_activeTouches[1].y - m_activeTouches[0].y;
		return std::sqrt(dx * dx + dy * dy);
	}

	/// @brief 2本指の角度を計算する（ラジアン）
	[[nodiscard]] float twoFingerAngle() const
	{
		if (m_activeTouches.size() < 2) return 0.0f;
		const float dx = m_activeTouches[1].x - m_activeTouches[0].x;
		const float dy = m_activeTouches[1].y - m_activeTouches[0].y;
		return std::atan2(dy, dx);
	}

	// ── メンバ ────────────────────────────────────────────────

	GestureConfig m_config;                    ///< ジェスチャー設定
	std::vector<TouchPoint> m_activeTouches;   ///< アクティブなタッチポイント

	/// タッチ開始情報
	float m_touchStartX = 0.0f;     ///< タッチ開始X座標
	float m_touchStartY = 0.0f;     ///< タッチ開始Y座標
	float m_touchStartTime = 0.0f;  ///< タッチ開始時刻
	float m_elapsed = 0.0f;         ///< 累積経過時間

	/// タップ判定用
	float m_lastTapTime = 0.0f;     ///< 最後のタップ時刻

	/// 長押し判定用
	bool m_longPressTriggered = false;  ///< 長押し発火済みフラグ

	/// 2本指ジェスチャー用
	float m_prevTwoFingerDistance = 0.0f;  ///< 前回の2本指距離
	float m_prevTwoFingerAngle = 0.0f;     ///< 前回の2本指角度

	/// ジェスチャーコールバック
	std::function<void(float, float)> m_tapCallback;
	std::function<void(float, float)> m_doubleTapCallback;
	std::function<void(float, float)> m_longPressCallback;
	std::function<void(SwipeDirection, float, float)> m_swipeCallback;
	std::function<void(const PinchInfo&)> m_pinchCallback;
	std::function<void(const RotateInfo&)> m_rotateCallback;
};

} // namespace mitiru
