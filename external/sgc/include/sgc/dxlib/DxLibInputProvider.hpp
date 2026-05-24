#pragma once

/// @file DxLibInputProvider.hpp
/// @brief IInputProvider の DxLib 実装
///
/// sgc::IInputProvider を DxLib の入力APIで実装する。
/// 登録されたキーバインディングに基づいて押下状態を収集する。
///
/// @note このファイルはDxLib.hに依存するため、CI対象外。
///
/// @code
/// sgc::dxlib::DxLibInputProvider provider;
/// provider.addKey(KEY_INPUT_SPACE, 32);
/// provider.addKey(KEY_INPUT_A, 'A');
///
/// std::vector<int> keys;
/// provider.pollPressedKeys(keys);
/// @endcode

#include <DxLib.h>
#include <vector>

#include "sgc/input/IInputProvider.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::dxlib
{

/// @brief IInputProvider の DxLib 実装
///
/// DxLibのキー入力とマウス入力を抽象化する。
/// addKey()でDxLibキーコードとsgcキーコードの対応を登録する。
class DxLibInputProvider : public IInputProvider
{
public:
	/// @brief DxLibキーコードとsgcキーコードの対応を登録する
	/// @param dxKeyCode DxLibのキーコード（KEY_INPUT_*）
	/// @param sgcKeyCode 出力するキーコード（ActionMapのバインディングと対応させる）
	void addKey(int dxKeyCode, int sgcKeyCode)
	{
		m_keys.push_back({dxKeyCode, sgcKeyCode});
	}

	/// @brief 現在押されているキーコードを収集する
	void pollPressedKeys(std::vector<int>& outPressedKeys) const override
	{
		outPressedKeys.clear();

		// DxLibの全キー状態を一括取得
		char keyState[256];
		GetHitKeyStateAll(keyState);

		for (const auto& entry : m_keys)
		{
			if (entry.dxKeyCode >= 0
				&& entry.dxKeyCode < 256
				&& keyState[entry.dxKeyCode] != 0)
			{
				outPressedKeys.push_back(entry.sgcKeyCode);
			}
		}
	}

	/// @brief マウスカーソルの現在座標を取得する
	[[nodiscard]] Vec2f mousePosition() const override
	{
		int x = 0;
		int y = 0;
		GetMousePoint(&x, &y);
		return Vec2f{static_cast<float>(x), static_cast<float>(y)};
	}

	/// @brief マウスカーソルの前フレームからの移動量を取得する
	///
	/// DxLibにはマウスデルタAPIがないため、前フレームの座標との差分を計算する。
	/// フレーム開始時に一度呼ぶことを推奨する。
	[[nodiscard]] Vec2f mouseDelta() const override
	{
		const auto current = mousePosition();
		const auto delta = current - m_prevMousePos;
		m_prevMousePos = current;
		return delta;
	}

	/// @brief マウスボタンが押下中か
	[[nodiscard]] bool isMouseButtonDown(int button) const override
	{
		return (GetMouseInput() & toDxMouseBit(button)) != 0;
	}

	/// @brief マウスボタンがこのフレームで押されたか
	///
	/// 前フレームの状態との差分で判定する。
	/// フレーム更新前にupdateMouseState()を呼ぶ必要がある。
	[[nodiscard]] bool isMouseButtonPressed(int button) const override
	{
		const int bit = toDxMouseBit(button);
		const int current = GetMouseInput();
		return (current & bit) != 0 && (m_prevMouseButtons & bit) == 0;
	}

	/// @brief マウスボタンがこのフレームで離されたか
	[[nodiscard]] bool isMouseButtonReleased(int button) const override
	{
		const int bit = toDxMouseBit(button);
		const int current = GetMouseInput();
		return (current & bit) == 0 && (m_prevMouseButtons & bit) != 0;
	}

	/// @brief 指定キーが押下中か
	[[nodiscard]] bool isKeyDown(int keyCode) const override
	{
		char currentState[256];
		GetHitKeyStateAll(currentState);

		for (const auto& entry : m_keys)
		{
			if (entry.sgcKeyCode == keyCode
				&& entry.dxKeyCode >= 0
				&& entry.dxKeyCode < 256)
			{
				return currentState[entry.dxKeyCode] != 0;
			}
		}
		return false;
	}

	/// @brief 指定キーがこのフレームで押されたか
	[[nodiscard]] bool isKeyJustPressed(int keyCode) const override
	{
		char currentState[256];
		GetHitKeyStateAll(currentState);

		for (const auto& entry : m_keys)
		{
			if (entry.sgcKeyCode == keyCode
				&& entry.dxKeyCode >= 0
				&& entry.dxKeyCode < 256)
			{
				return currentState[entry.dxKeyCode] != 0
					&& m_prevKeyState[entry.dxKeyCode] == 0;
			}
		}
		return false;
	}

	/// @brief フレーム開始時に入力状態を保存する
	///
	/// isKeyJustPressed/isMouseButtonPressed/Released の前フレーム比較に必要。
	/// ゲームループの先頭で毎フレーム呼ぶ。
	void update()
	{
		m_prevMouseButtons = GetMouseInput();
		GetHitKeyStateAll(m_prevKeyState);
	}

	/// @brief フレーム開始時にマウスボタン状態を保存する（後方互換）
	/// @deprecated update() を使用してください
	void updateMouseState()
	{
		update();
	}

	/// @brief 全登録キーをクリアする
	void clear()
	{
		m_keys.clear();
	}

private:
	/// @brief sgcマウスボタン定数をDxLibのビットマスクに変換する
	/// @param button MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
	/// @return 対応するDxLibマウスビット
	[[nodiscard]] static int toDxMouseBit(int button)
	{
		switch (button)
		{
		case MOUSE_LEFT:   return MOUSE_INPUT_LEFT;
		case MOUSE_RIGHT:  return MOUSE_INPUT_RIGHT;
		case MOUSE_MIDDLE: return MOUSE_INPUT_MIDDLE;
		default:           return MOUSE_INPUT_LEFT;
		}
	}

	/// @brief キー登録エントリ
	struct KeyEntry
	{
		int dxKeyCode;   ///< DxLibキーコード
		int sgcKeyCode;  ///< sgc出力キーコード
	};

	std::vector<KeyEntry> m_keys;             ///< 登録済みキー一覧
	mutable Vec2f m_prevMousePos{};           ///< 前フレームのマウス座標（delta計算用）
	int m_prevMouseButtons{0};               ///< 前フレームのマウスボタン状態
	char m_prevKeyState[256]{};              ///< 前フレームのキー状態
};

} // namespace sgc::dxlib
