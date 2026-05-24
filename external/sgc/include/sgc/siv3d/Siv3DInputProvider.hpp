#pragma once

/// @file Siv3DInputProvider.hpp
/// @brief IInputProvider の Siv3D 実装
///
/// sgc::IInputProvider を Siv3D の入力APIで実装する。
/// 登録されたキーバインディングに基づいて押下状態を収集する。
///
/// @code
/// sgc::siv3d::Siv3DInputProvider provider;
/// provider.addKey(s3d::KeySpace, 32);
/// provider.addKey(s3d::KeyA, 'A');
///
/// std::vector<int> keys;
/// provider.pollPressedKeys(keys);
/// @endcode

#include <Siv3D.hpp>
#include <vector>

#include "sgc/input/IInputProvider.hpp"
#include "sgc/siv3d/TypeConvert.hpp"

namespace sgc::siv3d
{

/// @brief IInputProvider の Siv3D 実装
///
/// Siv3Dのキー入力とマウス入力を抽象化する。
/// addKey()でSiv3DキーとキーコードIDの対応を登録する。
class Siv3DInputProvider : public IInputProvider
{
public:
	/// @brief Siv3Dキーとキーコードの対応を登録する
	/// @param key Siv3Dの入力キー
	/// @param keyCode 出力するキーコード（ActionMapのバインディングと対応させる）
	void addKey(const s3d::Input& key, int keyCode)
	{
		m_keys.push_back({key, keyCode});
	}

	/// @brief 現在押されているキーコードを収集する
	void pollPressedKeys(std::vector<int>& outPressedKeys) const override
	{
		outPressedKeys.clear();
		for (const auto& entry : m_keys)
		{
			if (entry.key.pressed())
			{
				outPressedKeys.push_back(entry.keyCode);
			}
		}
	}

	/// @brief マウスカーソルの現在座標を取得する
	[[nodiscard]] Vec2f mousePosition() const override
	{
		return fromSiv(s3d::Cursor::PosF());
	}

	/// @brief マウスカーソルの前フレームからの移動量を取得する
	[[nodiscard]] Vec2f mouseDelta() const override
	{
		return fromSiv(s3d::Cursor::DeltaF());
	}

	/// @brief マウスボタンが押下中か
	[[nodiscard]] bool isMouseButtonDown(int button) const override
	{
		return toSivMouseButton(button).pressed();
	}

	/// @brief マウスボタンがこのフレームで押されたか
	[[nodiscard]] bool isMouseButtonPressed(int button) const override
	{
		return toSivMouseButton(button).down();
	}

	/// @brief マウスボタンがこのフレームで離されたか
	[[nodiscard]] bool isMouseButtonReleased(int button) const override
	{
		return toSivMouseButton(button).up();
	}

	/// @brief 指定キーが押下中か
	[[nodiscard]] bool isKeyDown(int keyCode) const override
	{
		for (const auto& entry : m_keys)
		{
			if (entry.keyCode == keyCode)
			{
				return entry.key.pressed();
			}
		}
		return false;
	}

	/// @brief 指定キーがこのフレームで押されたか
	[[nodiscard]] bool isKeyJustPressed(int keyCode) const override
	{
		for (const auto& entry : m_keys)
		{
			if (entry.keyCode == keyCode)
			{
				return entry.key.down();
			}
		}
		return false;
	}

	/// @brief 全登録キーをクリアする
	void clear()
	{
		m_keys.clear();
	}

private:
	/// @brief sgcマウスボタン定数をSiv3Dのマウス入力に変換する
	/// @param button MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
	/// @return 対応するSiv3Dマウス入力
	[[nodiscard]] static s3d::Input toSivMouseButton(int button)
	{
		switch (button)
		{
		case MOUSE_LEFT:   return s3d::MouseL;
		case MOUSE_RIGHT:  return s3d::MouseR;
		case MOUSE_MIDDLE: return s3d::MouseM;
		default:           return s3d::MouseL;
		}
	}

	/// @brief キー登録エントリ
	struct KeyEntry
	{
		s3d::Input key;  ///< Siv3Dの入力キー
		int keyCode;     ///< 出力キーコード
	};

	std::vector<KeyEntry> m_keys;  ///< 登録済みキー一覧
};

} // namespace sgc::siv3d
