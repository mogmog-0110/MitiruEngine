#pragma once

/// @file InputMapper.hpp
/// @brief アクションマッピングレイヤー
/// @details 生の入力（キーコード、マウスボタン、ゲームパッドボタン）を
///          文字列ベースのゲームアクションにマッピングする。
///          プラットフォーム非依存。実行時にリバインド可能。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/input/KeyCode.hpp>

namespace mitiru
{

/// @brief ゲームパッドボタンID（プラットフォーム非依存）
/// @details XInputのビットフラグ値に準拠するが、ヘッダ自体はWin32非依存。
enum class GamepadButtonId : std::uint16_t
{
	DPadUp    = 0x0001,   ///< 十字キー上
	DPadDown  = 0x0002,   ///< 十字キー下
	DPadLeft  = 0x0004,   ///< 十字キー左
	DPadRight = 0x0008,   ///< 十字キー右
	Start     = 0x0010,   ///< Startボタン
	Back      = 0x0020,   ///< Backボタン
	LS        = 0x0040,   ///< 左スティック押し込み
	RS        = 0x0080,   ///< 右スティック押し込み
	LB        = 0x0100,   ///< 左バンパー
	RB        = 0x0200,   ///< 右バンパー
	A         = 0x1000,   ///< Aボタン
	B         = 0x2000,   ///< Bボタン
	X         = 0x4000,   ///< Xボタン
	Y         = 0x8000    ///< Yボタン
};

/// @brief バインディングの種類
enum class BindingType : std::uint8_t
{
	Key,            ///< キーボードキー
	MouseButton,    ///< マウスボタン
	GamepadButton,  ///< ゲームパッドボタン
	GamepadAxis     ///< ゲームパッド軸
};

/// @brief 入力バインディング
/// @details 1つのアクションに対する1つの入力ソースを表す。
struct InputBinding
{
	BindingType type = BindingType::Key;       ///< バインディングの種類
	int code = 0;                              ///< キーコード / ボタンコード / 軸ID

	/// @brief キーバインディングを生成する
	/// @param key キーコード
	/// @return InputBinding
	[[nodiscard]] static constexpr InputBinding fromKey(KeyCode key) noexcept
	{
		return InputBinding{ BindingType::Key, static_cast<int>(key) };
	}

	/// @brief マウスボタンバインディングを生成する
	/// @param button マウスボタン番号（0=Left, 1=Right, 2=Middle）
	/// @return InputBinding
	[[nodiscard]] static constexpr InputBinding fromMouseButton(int button) noexcept
	{
		return InputBinding{ BindingType::MouseButton, button };
	}

	/// @brief ゲームパッドボタンバインディングを生成する
	/// @param button ゲームパッドボタンID
	/// @return InputBinding
	[[nodiscard]] static constexpr InputBinding fromGamepadButton(GamepadButtonId button) noexcept
	{
		return InputBinding{ BindingType::GamepadButton, static_cast<int>(button) };
	}

	/// @brief ゲームパッド軸バインディングを生成する
	/// @param axisId 軸ID
	/// @return InputBinding
	[[nodiscard]] static constexpr InputBinding fromGamepadAxis(int axisId) noexcept
	{
		return InputBinding{ BindingType::GamepadAxis, axisId };
	}
};

/// @brief 入力状態プロバイダ
/// @details InputMapperが入力状態を問い合わせるためのコールバック群。
///          プラットフォーム固有の入力システムとの橋渡しを担う。
struct InputStateProvider
{
	/// @brief キーが押されているか問い合わせる関数
	std::function<bool(int keyCode)> isKeyDown;

	/// @brief キーが今フレームで押されたか問い合わせる関数
	std::function<bool(int keyCode)> isKeyPressed;

	/// @brief マウスボタンが押されているか問い合わせる関数
	std::function<bool(int button)> isMouseButtonDown;

	/// @brief マウスボタンが今フレームで押されたか問い合わせる関数
	std::function<bool(int button)> isMouseButtonPressed;

	/// @brief ゲームパッドボタンが押されているか問い合わせる関数
	std::function<bool(int buttonCode)> isGamepadButtonDown;

	/// @brief ゲームパッドボタンが今フレームで押されたか問い合わせる関数
	std::function<bool(int buttonCode)> isGamepadButtonPressed;

	/// @brief ゲームパッド軸の値を問い合わせる関数
	std::function<float(int axisId)> getGamepadAxis;
};

/// @brief アクションマッピングレイヤー
/// @details 生の入力を文字列ベースのゲームアクションにマッピングする。
///          1つのアクションに複数のバインディングを設定可能。
///          実行時にリバインド可能。
///
/// @code
/// mitiru::InputMapper mapper;
/// mapper.bindKey("Jump", mitiru::KeyCode::Space);
/// mapper.bindGamepadButton("Jump", mitiru::GamepadButtonId::A);
///
/// mitiru::InputStateProvider provider;
/// provider.isKeyDown = [&](int code) { return input.isKeyDownRaw(code); };
/// provider.isKeyPressed = [&](int code) { return /* ... */; };
/// // ... 他のプロバイダも設定 ...
/// mapper.setProvider(provider);
///
/// if (mapper.isActionPressed("Jump"))
///     jump();
/// @endcode
class InputMapper
{
public:
	/// @brief 入力状態プロバイダを設定する
	/// @param provider 入力状態プロバイダ
	void setProvider(const InputStateProvider& provider)
	{
		m_provider = provider;
	}

	/// @brief キーバインディングを追加する
	/// @param actionName アクション名
	/// @param key キーコード
	void bindKey(std::string_view actionName, KeyCode key)
	{
		m_bindings[std::string(actionName)].push_back(
			InputBinding::fromKey(key));
	}

	/// @brief マウスボタンバインディングを追加する
	/// @param actionName アクション名
	/// @param button マウスボタン番号（0=Left, 1=Right, 2=Middle）
	void bindMouseButton(std::string_view actionName, int button)
	{
		m_bindings[std::string(actionName)].push_back(
			InputBinding::fromMouseButton(button));
	}

	/// @brief ゲームパッドボタンバインディングを追加する
	/// @param actionName アクション名
	/// @param button ゲームパッドボタンID
	void bindGamepadButton(std::string_view actionName, GamepadButtonId button)
	{
		m_bindings[std::string(actionName)].push_back(
			InputBinding::fromGamepadButton(button));
	}

	/// @brief ゲームパッド軸バインディングを追加する
	/// @param actionName アクション名
	/// @param axisId 軸ID
	void bindGamepadAxis(std::string_view actionName, int axisId)
	{
		m_bindings[std::string(actionName)].push_back(
			InputBinding::fromGamepadAxis(axisId));
	}

	/// @brief 指定アクションの全バインディングを削除する
	/// @param actionName アクション名
	void unbindAll(std::string_view actionName)
	{
		m_bindings.erase(std::string(actionName));
	}

	/// @brief 全アクションの全バインディングを削除する
	void clearAllBindings()
	{
		m_bindings.clear();
	}

	/// @brief アクションが押されているか（いずれかのバインディングがアクティブ）
	/// @param actionName アクション名
	/// @return 押されていれば true
	[[nodiscard]] bool isActionDown(std::string_view actionName) const
	{
		const auto it = m_bindings.find(std::string(actionName));
		if (it == m_bindings.end()) return false;

		for (const auto& binding : it->second)
		{
			if (isBindingDown(binding)) return true;
		}
		return false;
	}

	/// @brief アクションが今フレームで押されたか
	/// @param actionName アクション名
	/// @return 今フレームで押されたなら true
	[[nodiscard]] bool isActionPressed(std::string_view actionName) const
	{
		const auto it = m_bindings.find(std::string(actionName));
		if (it == m_bindings.end()) return false;

		for (const auto& binding : it->second)
		{
			if (isBindingPressed(binding)) return true;
		}
		return false;
	}

	/// @brief アクションの軸値を取得する（ゲームパッド軸バインディング用）
	/// @param actionName アクション名
	/// @return 軸の値（バインディングなし or プロバイダ未設定なら 0.0）
	[[nodiscard]] float getActionAxis(std::string_view actionName) const
	{
		const auto it = m_bindings.find(std::string(actionName));
		if (it == m_bindings.end()) return 0.0f;

		for (const auto& binding : it->second)
		{
			if (binding.type == BindingType::GamepadAxis &&
				m_provider.getGamepadAxis)
			{
				const float val = m_provider.getGamepadAxis(binding.code);
				if (val != 0.0f) return val;
			}
		}
		return 0.0f;
	}

	/// @brief 指定アクションのバインディング数を取得する
	/// @param actionName アクション名
	/// @return バインディング数
	[[nodiscard]] std::size_t bindingCount(std::string_view actionName) const
	{
		const auto it = m_bindings.find(std::string(actionName));
		if (it == m_bindings.end()) return 0;
		return it->second.size();
	}

	/// @brief 登録済みアクション数を取得する
	/// @return アクション数
	[[nodiscard]] std::size_t actionCount() const noexcept
	{
		return m_bindings.size();
	}

	/// @brief 指定アクションのバインディング一覧を取得する
	/// @param actionName アクション名
	/// @return バインディングの配列（アクション未登録なら空）
	[[nodiscard]] const std::vector<InputBinding>& getBindings(
		std::string_view actionName) const
	{
		static const std::vector<InputBinding> empty;
		const auto it = m_bindings.find(std::string(actionName));
		if (it == m_bindings.end()) return empty;
		return it->second;
	}

private:
	/// @brief バインディングがアクティブ（押下中）か判定する
	/// @param binding 判定対象のバインディング
	/// @return アクティブなら true
	[[nodiscard]] bool isBindingDown(const InputBinding& binding) const
	{
		switch (binding.type)
		{
		case BindingType::Key:
			return m_provider.isKeyDown && m_provider.isKeyDown(binding.code);
		case BindingType::MouseButton:
			return m_provider.isMouseButtonDown &&
				m_provider.isMouseButtonDown(binding.code);
		case BindingType::GamepadButton:
			return m_provider.isGamepadButtonDown &&
				m_provider.isGamepadButtonDown(binding.code);
		case BindingType::GamepadAxis:
			return false;  ///< 軸はDown判定には使わない
		default:
			return false;
		}
	}

	/// @brief バインディングが今フレームで押されたか判定する
	/// @param binding 判定対象のバインディング
	/// @return 今フレームで押されたなら true
	[[nodiscard]] bool isBindingPressed(const InputBinding& binding) const
	{
		switch (binding.type)
		{
		case BindingType::Key:
			return m_provider.isKeyPressed &&
				m_provider.isKeyPressed(binding.code);
		case BindingType::MouseButton:
			return m_provider.isMouseButtonPressed &&
				m_provider.isMouseButtonPressed(binding.code);
		case BindingType::GamepadButton:
			return m_provider.isGamepadButtonPressed &&
				m_provider.isGamepadButtonPressed(binding.code);
		case BindingType::GamepadAxis:
			return false;  ///< 軸はPressed判定には使わない
		default:
			return false;
		}
	}

	/// @brief アクション名 → バインディング一覧
	std::unordered_map<std::string, std::vector<InputBinding>> m_bindings;

	/// @brief 入力状態プロバイダ
	InputStateProvider m_provider;
};

} // namespace mitiru
