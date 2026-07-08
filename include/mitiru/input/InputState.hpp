#pragma once

/// @file InputState.hpp
/// @brief 不変入力状態スナップショット
/// @details あるフレームにおける入力デバイスの状態を保持する。
///          イミュータブルな値型として設計。

#include <array>
#include <cstdint>

#include "mitiru/input/KeyCode.hpp"

namespace mitiru
{

/// @brief マウスボタン識別子
enum class MouseButton : std::uint8_t
{
	Left = 0,    ///< 左ボタン
	Right = 1,   ///< 右ボタン
	Middle = 2   ///< 中ボタン
};

/// @brief 不変の入力状態スナップショット
/// @details フレーム開始時に確定した入力状態を保持する。
///          ゲームロジックはこのスナップショットを参照して処理する。
class InputState
{
public:
	/// @brief キーの最大数
	static constexpr int MAX_KEYS = 256;

	/// @brief マウスボタンの最大数
	static constexpr int MAX_MOUSE_BUTTONS = 3;

	/// @brief デフォルトコンストラクタ（全入力なし状態）
	InputState() noexcept
		: m_keys{}
		, m_prevKeys{}
		, m_mouseButtons{}
		, m_prevMouseButtons{}
		, m_mouseX(0.0f)
		, m_mouseY(0.0f)
		, m_prevMouseX(0.0f)
		, m_prevMouseY(0.0f)
		, m_mouseCaptured(false)
		, m_rawDeltaX(0.0f)
		, m_rawDeltaY(0.0f)
	{
	}

	/// @brief 指定キーが押されているか
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 押されていれば true
	[[nodiscard]] bool isKeyDown(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		return m_keys[static_cast<std::size_t>(keyCode)];
	}

	/// @brief 指定キーが押されているか（KeyCode 版）
	/// @param keyCode 型付きキーコード
	/// @return 押されていれば true
	[[nodiscard]] bool isKeyDown(KeyCode keyCode) const noexcept
	{
		return isKeyDown(static_cast<int>(keyCode));
	}

	/// @brief マウス座標を取得する
	/// @return {x, y} のペア
	[[nodiscard]] std::pair<float, float> mousePosition() const noexcept
	{
		return { m_mouseX, m_mouseY };
	}

	/// @brief このフレームに回したマウスホイール量 (+ = 奥/上、120 = 1 ノッチ)。フレーム頭で 0 に戻る。
	[[nodiscard]] float mouseWheelDelta() const noexcept { return m_mouseWheel; }

	/// @brief マウスホイールの回転を積む (Win32 の WM_MOUSEWHEEL などから呼ぶ)
	void addMouseWheelDelta(float delta) noexcept { m_mouseWheel += delta; }

	/// @brief 明示的に prev を curr に揃える (テスト / バッチ実行用)
	/// @details ランタイムでは `endTick()` が tick 末で prev を進めるため、
	///          render-loop の頭で本メソッドを呼ぶ必要はない。実際 144Hz vsync +
	///          60Hz update のように「pollEvents は走るが update は走らない」
	///          レンダーフレームで本メソッドを呼ぶと、KEYDOWN が curr に入った
	///          直後の次フレーム頭で prev=curr されてエッジが食い潰され、
	///          just-pressed が永久に false になる (ENG-102)。
	///
	///          そのため `Engine::tickOneFrame()` は本メソッドを呼ばず、prev 維持を
	///          完全に `endTick()` に委ねる。本メソッドは `stepFrames()` のように
	///          「1 render frame = 1 tick」の固定ループや、テストでの明示的な
	///          初期化用に残してある。
	///
	///          m_mouseCaptured フラグはフレームをまたいで保持されるので変更しない。
	void beginFrame() noexcept
	{
		m_prevKeys = m_keys;
		m_prevMouseButtons = m_mouseButtons;
		m_prevMouseX = m_mouseX;
		m_prevMouseY = m_mouseY;
		m_rawDeltaX = 0.0f;
		m_rawDeltaY = 0.0f;
		m_mouseWheel = 0.0f;
	}

	/// @brief 全ての held key / mouse button を「離された」状態にする
	/// @details Win32 window が WM_KILLFOCUS を受けた時など、focus を失った
	///          直後に呼ぶ。focus 喪失中は OS から WM_KEYUP が届かないので
	///          押しっぱなしの key state が "stuck" するのを防ぐ。
	///          `prev` 配列は触らないので、`isKeyJustReleased()` が 1 回だけ
	///          edge を発火する (game logic 側に「キー離された」通知が届く)。
	void clearHeldKeys() noexcept
	{
		// 注入キー (AI / replay が injector 経由で押したもの) は消さない。
		// このクリアは「実キーボードの離し損ね」対策であり、バックグラウンドの
		// ゲームを外部から操作するケース (MITIRU_AI) を壊してはいけない。
		for (std::size_t i = 0; i < m_keys.size(); ++i)
		{
			if (!m_injectedKeys[i]) { m_keys[i] = false; }
		}
		for (std::size_t i = 0; i < m_mouseButtons.size(); ++i)
		{
			if (!m_injectedMouse[i]) { m_mouseButtons[i] = false; }
		}
		m_rawDeltaX = 0.0f;
		m_rawDeltaY = 0.0f;
		m_mouseWheel = 0.0f;
	}

	/// @brief 注入入力としてキー状態を設定する (clearHeldKeys の対象外になる)
	/// @details injector (HTTP /api/input/simulate、replay 再生) 専用。
	///          実ウィンドウイベントの setKeyDown が来たらそのキーは実入力扱いに戻る。
	void setKeyDownInjected(int keyCode, bool down) noexcept
	{
		if (keyCode >= 0 && keyCode < MAX_KEYS)
		{
			m_keys[static_cast<std::size_t>(keyCode)] = down;
			m_injectedKeys[static_cast<std::size_t>(keyCode)] = down;
		}
	}

	/// @brief 注入入力としてマウスボタン状態を設定する (clearHeldKeys の対象外)
	void setMouseButtonDownInjected(MouseButton button, bool down) noexcept
	{
		const auto idx = static_cast<std::size_t>(button);
		if (idx < m_mouseButtons.size())
		{
			m_mouseButtons[idx] = down;
			m_injectedMouse[idx] = down;
		}
	}

	/// @brief 1 つの fixed-step tick が終わった直後に呼び、エッジを「消化」する
	/// @details Accumulator-based 固定ステップループでは 1 レンダーフレーム内に
	///          `game.update()` が複数回走ることがある。`beginFrame()` は
	///          レンダーフレーム頭で 1 回だけ呼ばれるため、そのままでは複数回の
	///          tick がすべて同じ "just-pressed" を観測し、ランチャーの選択が
	///          1 入力で N 段ジャンプする等のバグになる。
	///          tick ごとに本メソッドを呼べば次の tick 以降は just-pressed が
	///          false に落ちる。pollEvents は呼ばないので、tick 内で OS から
	///          新しいイベントを拾うわけではなく、純粋に edge を consume する。
	void endTick() noexcept
	{
		m_prevKeys = m_keys;
		m_prevMouseButtons = m_mouseButtons;
		/// マウスデルタも同様に「物理 1 入力 = 1 観測」を保つ。
		/// 1 レンダーフレームに複数回 game.update() が走るケースで、
		/// 2 回目以降が同じ delta を観測するとカメラ等が過剰回転する。
		m_rawDeltaX = 0.0f;
		m_rawDeltaY = 0.0f;
		m_mouseWheel = 0.0f;
		m_prevMouseX = m_mouseX;
		m_prevMouseY = m_mouseY;
	}

	/// @brief 今フレームのマウス移動量を取得する
	/// @details カーソルキャプチャ中は Win32 プラットフォームが蓄積した生デルタを返す。
	///          非キャプチャ時は現在座標と前フレーム座標の差分を返す。
	/// @return {dx, dy} のペア（ピクセル単位、右/下が正）
	/// @note キャプチャ中は OS がカーソルを中心に戻すためウィンドウ座標系は変動する。
	///       必ず本メソッドを使い mousePosition() の差分を自前計算しないこと。
	///
	/// @code
	/// // 一人称カメラの回転例
	/// auto [dx, dy] = input.mouseDelta();
	/// yaw   += dx * sensitivity;
	/// pitch += dy * sensitivity;
	/// @endcode
	[[nodiscard]] std::pair<float, float> mouseDelta() const noexcept
	{
		if (m_mouseCaptured)
		{
			return { m_rawDeltaX, m_rawDeltaY };
		}
		return { m_mouseX - m_prevMouseX, m_mouseY - m_prevMouseY };
	}

	/// @brief カーソルキャプチャ状態を設定する
	/// @param captured true でキャプチャ開始を要求、false で解除を要求する
	/// @details このメソッド自体は OS に副作用を与えない。
	///          実際のカーソル非表示・ClipCursor・SetCapture は Win32Window 側が
	///          毎フレーム applyCursorCapture() を呼ぶことで適用される。
	///
	/// @code
	/// // 一人称モードに入るとき
	/// inputState.setCursorCaptured(true);
	///
	/// // ESC でメニューを開くとき
	/// inputState.setCursorCaptured(false);
	/// @endcode
	void setCursorCaptured(bool captured) noexcept
	{
		m_mouseCaptured = captured;
	}

	/// @brief カーソルがキャプチャ中かどうかを返す
	/// @return キャプチャ中なら true
	[[nodiscard]] bool isCursorCaptured() const noexcept
	{
		return m_mouseCaptured;
	}

	/// @brief プラットフォーム層から生マウスデルタを蓄積する
	/// @param dx X 方向の移動量（ピクセル）
	/// @param dy Y 方向の移動量（ピクセル）
	/// @details Win32 の場合、WM_MOUSEMOVE 受信 → センター差分計算 → SetCursorPos の
	///          サイクルで複数回呼ばれることがあるため、additive で蓄積する。
	///          beginFrame() でリセットされる。
	void setRawMouseDelta(float dx, float dy) noexcept
	{
		m_rawDeltaX += dx;
		m_rawDeltaY += dy;
	}

	/// @brief 指定キーがこのフレームで押されたか（エッジ検出）
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 今フレーム押下かつ前フレーム非押下なら true
	[[nodiscard]] bool isKeyJustPressed(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(keyCode);
		return m_keys[idx] && !m_prevKeys[idx];
	}

	/// @brief 指定キーがこのフレームで押されたか（KeyCode 版）
	/// @param keyCode 型付きキーコード
	/// @return 今フレーム押下かつ前フレーム非押下なら true
	[[nodiscard]] bool isKeyJustPressed(KeyCode keyCode) const noexcept
	{
		return isKeyJustPressed(static_cast<int>(keyCode));
	}

	/// @brief 指定キーがこのフレームで離されたか（エッジ検出）
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 今フレーム非押下かつ前フレーム押下なら true
	[[nodiscard]] bool isKeyJustReleased(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(keyCode);
		return !m_keys[idx] && m_prevKeys[idx];
	}

	/// @brief 指定キーがこのフレームで離されたか（KeyCode 版）
	/// @param keyCode 型付きキーコード
	/// @return 今フレーム非押下かつ前フレーム押下なら true
	[[nodiscard]] bool isKeyJustReleased(KeyCode keyCode) const noexcept
	{
		return isKeyJustReleased(static_cast<int>(keyCode));
	}

	/// @brief マウスボタンがこのフレームで押されたか（エッジ検出）
	/// @param button マウスボタン
	/// @return 今フレーム押下かつ前フレーム非押下なら true
	[[nodiscard]] bool isMouseButtonJustPressed(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return m_mouseButtons[index] && !m_prevMouseButtons[index];
	}

	/// @brief マウスボタンがこのフレームで離されたか（エッジ検出）
	/// @param button マウスボタン
	/// @return 今フレーム非押下かつ前フレーム押下なら true
	[[nodiscard]] bool isMouseButtonJustReleased(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return !m_mouseButtons[index] && m_prevMouseButtons[index];
	}

	/// @brief マウスボタンが押されているか
	/// @param button マウスボタン
	/// @return 押されていれば true
	[[nodiscard]] bool isMouseButtonDown(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return m_mouseButtons[index];
	}

	/// @brief キー状態を設定する
	/// @param keyCode キーコード
	/// @param down 押下状態
	void setKeyDown(int keyCode, bool down) noexcept
	{
		if (keyCode >= 0 && keyCode < MAX_KEYS)
		{
			m_keys[static_cast<std::size_t>(keyCode)] = down;
			// 実ウィンドウイベントが来たキーは実入力扱いに戻す。
			m_injectedKeys[static_cast<std::size_t>(keyCode)] = false;
		}
	}

	/// @brief キー状態を設定する（KeyCode 版）
	/// @param keyCode 型付きキーコード
	/// @param down 押下状態
	void setKeyDown(KeyCode keyCode, bool down) noexcept
	{
		setKeyDown(static_cast<int>(keyCode), down);
	}

	/// @brief マウス座標を設定する
	/// @param x X座標
	/// @param y Y座標
	void setMousePosition(float x, float y) noexcept
	{
		m_mouseX = x;
		m_mouseY = y;
		s_dbgLastSetX = x;
		s_dbgLastSetY = y;
		++s_dbgSetCount;
	}

	// DEBUG: setMousePositionで最後に設定された値（static）
	static inline float s_dbgLastSetX = 0;
	static inline float s_dbgLastSetY = 0;
	static inline int s_dbgSetCount = 0;

	/// @brief マウスボタン状態を設定する
	/// @param button マウスボタン
	/// @param down 押下状態
	void setMouseButtonDown(MouseButton button, bool down) noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index < MAX_MOUSE_BUTTONS)
		{
			m_mouseButtons[index] = down;
			// 実ウィンドウイベントが来たボタンは実入力扱いに戻す。
			m_injectedMouse[index] = false;
		}
	}

private:
	std::array<bool, MAX_KEYS> m_keys;                    ///< キー押下状態
	std::array<bool, MAX_KEYS> m_prevKeys;                ///< 前フレームのキー押下状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_mouseButtons;   ///< マウスボタン押下状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_prevMouseButtons; ///< 前フレームのマウスボタン押下状態
	float m_mouseX;        ///< マウスX座標（現在フレーム）
	float m_mouseY;        ///< マウスY座標（現在フレーム）
	float m_prevMouseX;    ///< マウスX座標（前フレーム、非キャプチャ時のデルタ計算用）
	float m_prevMouseY;    ///< マウスY座標（前フレーム、非キャプチャ時のデルタ計算用）
	bool  m_mouseCaptured; ///< カーソルキャプチャ要求フラグ（OS副作用なし、Win32Window が参照）
	float m_rawDeltaX;     ///< キャプチャ中の蓄積生デルタX（beginFrame でリセット）
	float m_rawDeltaY;     ///< キャプチャ中の蓄積生デルタY（beginFrame でリセット）
	float m_mouseWheel = 0.0f;  ///< このフレームの累積ホイール量（beginFrame / endTick でリセット）
	std::array<bool, MAX_KEYS> m_injectedKeys{};            ///< injector 由来の押下 (focus 喪失クリア対象外)
	std::array<bool, MAX_MOUSE_BUTTONS> m_injectedMouse{};  ///< injector 由来のボタン (同上)
};

} // namespace mitiru
