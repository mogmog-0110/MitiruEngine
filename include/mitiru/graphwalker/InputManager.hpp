#pragma once

/// @file InputManager.hpp
/// @brief フレームワーク非依存の入力抽象化レイヤー
///
/// ゲームアクション（移動、ジャンプ等）とキーコードのバインディングを管理し、
/// pressed/held/releasedの状態遷移を毎フレーム計算する。
///
/// @code
/// mitiru::gw::InputManager input;
/// // フレームごとに更新
/// mitiru::gw::RawInputState raw;
/// raw.keysDown.insert(0x41); // 'A'キー
/// input.update(raw);
/// if (input.getState().isPressed(mitiru::gw::GameAction::MoveLeft))
/// {
///     // 左移動開始
/// }
/// @endcode

#include <set>
#include <array>
#include <cstdint>
#include <cstddef>

namespace mitiru::gw
{

/// @brief ゲームアクションの列挙
///
/// キーバインドのターゲットとなる論理的な操作を定義する。
enum class GameAction : std::uint8_t
{
	MoveLeft,          ///< 左移動
	MoveRight,         ///< 右移動
	Jump,              ///< ジャンプ
	Interact,          ///< インタラクト（NPC会話等）
	OpenCalculator,    ///< 電卓を開く
	Pause,             ///< ポーズ
	DebugPanel,        ///< デバッグパネル
	ZoomIn,            ///< ズームイン
	ZoomOut,           ///< ズームアウト
	ResetCamera,       ///< カメラリセット
	MenuUp,            ///< メニュー上
	MenuDown,          ///< メニュー下
	MenuConfirm,       ///< メニュー決定

	Count              ///< アクション数（番兵）
};

/// @brief ゲームアクション数の定数
inline constexpr std::size_t GAME_ACTION_COUNT = static_cast<std::size_t>(GameAction::Count);

/// @brief 入力状態
///
/// 各ゲームアクションのpressed/held/released状態を保持する。
struct InputState
{
	bool pressed[GAME_ACTION_COUNT]{};    ///< このフレームで押された
	bool held[GAME_ACTION_COUNT]{};       ///< 現在押されている
	bool released[GAME_ACTION_COUNT]{};   ///< このフレームで離された

	/// @brief 指定アクションがこのフレームで押されたか
	/// @param action ゲームアクション
	/// @return 押された直後ならtrue
	[[nodiscard]] bool isPressed(GameAction action) const noexcept
	{
		const auto idx = static_cast<std::size_t>(action);
		if (idx >= GAME_ACTION_COUNT) return false;
		return pressed[idx];
	}

	/// @brief 指定アクションが現在押されているか
	/// @param action ゲームアクション
	/// @return 押し続けている間true
	[[nodiscard]] bool isHeld(GameAction action) const noexcept
	{
		const auto idx = static_cast<std::size_t>(action);
		if (idx >= GAME_ACTION_COUNT) return false;
		return held[idx];
	}

	/// @brief 指定アクションがこのフレームで離されたか
	/// @param action ゲームアクション
	/// @return 離された直後ならtrue
	[[nodiscard]] bool isReleased(GameAction action) const noexcept
	{
		const auto idx = static_cast<std::size_t>(action);
		if (idx >= GAME_ACTION_COUNT) return false;
		return released[idx];
	}
};

/// @brief 生の入力状態（フレームワーク側が提供する）
struct RawInputState
{
	std::set<int> keysDown;   ///< 現在押されているキーコードの集合
};

/// @brief フレームワーク非依存の入力マネージャー
///
/// 生のキーコードをゲームアクションに変換し、
/// pressed/held/released遷移を毎フレーム計算する。
/// キーバインドの変更もサポートする。
class InputManager
{
public:
	/// @brief デフォルトキーバインドで初期化する
	///
	/// デフォルトバインド:
	/// - MoveLeft: 'A' (0x41) / Left (0x25)
	/// - MoveRight: 'D' (0x44) / Right (0x27)
	/// - Jump: Space (0x20)
	/// - Interact: 'E' (0x45)
	/// - OpenCalculator: 'C' (0x43)
	/// - Pause: Escape (0x1B)
	/// - DebugPanel: F5 (0x74)
	/// - ZoomIn: '+' / '=' (0xBB)
	/// - ZoomOut: '-' (0xBD)
	/// - ResetCamera: '0' (0x30)
	/// - MenuUp: Up (0x26)
	/// - MenuDown: Down (0x28)
	/// - MenuConfirm: Enter (0x0D)
	InputManager()
	{
		// プライマリバインド
		m_primaryBindings[idx(GameAction::MoveLeft)]       = 0x41; // 'A'
		m_primaryBindings[idx(GameAction::MoveRight)]      = 0x44; // 'D'
		m_primaryBindings[idx(GameAction::Jump)]           = 0x20; // Space
		m_primaryBindings[idx(GameAction::Interact)]       = 0x45; // 'E'
		m_primaryBindings[idx(GameAction::OpenCalculator)] = 0x43; // 'C'
		m_primaryBindings[idx(GameAction::Pause)]          = 0x1B; // Escape
		m_primaryBindings[idx(GameAction::DebugPanel)]     = 0x74; // F5
		m_primaryBindings[idx(GameAction::ZoomIn)]         = 0xBB; // '+'
		m_primaryBindings[idx(GameAction::ZoomOut)]        = 0xBD; // '-'
		m_primaryBindings[idx(GameAction::ResetCamera)]    = 0x30; // '0'
		m_primaryBindings[idx(GameAction::MenuUp)]         = 0x26; // Up
		m_primaryBindings[idx(GameAction::MenuDown)]       = 0x28; // Down
		m_primaryBindings[idx(GameAction::MenuConfirm)]    = 0x0D; // Enter

		// セカンダリバインド（代替キー）
		m_secondaryBindings.fill(-1);
		m_secondaryBindings[idx(GameAction::MoveLeft)]  = 0x25; // Left arrow
		m_secondaryBindings[idx(GameAction::MoveRight)] = 0x27; // Right arrow
	}

	/// @brief 生の入力状態からゲームアクション状態を更新する
	/// @param raw 現在フレームの生キー入力
	///
	/// pressed = 今回押されているが前回は押されていなかった
	/// held    = 今回押されている
	/// released = 今回押されていないが前回は押されていた
	void update(const RawInputState& raw)
	{
		for (std::size_t i = 0; i < GAME_ACTION_COUNT; ++i)
		{
			const bool wasHeld = m_state.held[i];

			// プライマリまたはセカンダリキーのいずれかが押されているか
			const bool isDown =
				raw.keysDown.count(m_primaryBindings[i]) > 0 ||
				(m_secondaryBindings[i] >= 0 &&
				 raw.keysDown.count(m_secondaryBindings[i]) > 0);

			m_state.pressed[i]  = isDown && !wasHeld;
			m_state.held[i]     = isDown;
			m_state.released[i] = !isDown && wasHeld;
		}
	}

	/// @brief 現在の入力状態を取得する
	/// @return 入力状態への参照
	[[nodiscard]] const InputState& getState() const noexcept
	{
		return m_state;
	}

	/// @brief キーバインドを変更する（プライマリキー）
	/// @param action 対象アクション
	/// @param keyCode 新しいキーコード
	void rebind(GameAction action, int keyCode) noexcept
	{
		const auto i = idx(action);
		if (i < GAME_ACTION_COUNT)
		{
			m_primaryBindings[i] = keyCode;
		}
	}

	/// @brief セカンダリキーバインドを変更する
	/// @param action 対象アクション
	/// @param keyCode 新しいキーコード（-1で無効化）
	void rebindSecondary(GameAction action, int keyCode) noexcept
	{
		const auto i = idx(action);
		if (i < GAME_ACTION_COUNT)
		{
			m_secondaryBindings[i] = keyCode;
		}
	}

	/// @brief プライマリキーバインドを取得する
	/// @param action 対象アクション
	/// @return バインドされたキーコード
	[[nodiscard]] int getBinding(GameAction action) const noexcept
	{
		const auto i = idx(action);
		if (i >= GAME_ACTION_COUNT) return -1;
		return m_primaryBindings[i];
	}

	/// @brief セカンダリキーバインドを取得する
	/// @param action 対象アクション
	/// @return バインドされたキーコード（未設定なら-1）
	[[nodiscard]] int getSecondaryBinding(GameAction action) const noexcept
	{
		const auto i = idx(action);
		if (i >= GAME_ACTION_COUNT) return -1;
		return m_secondaryBindings[i];
	}

private:
	InputState m_state;  ///< 現在の入力状態

	/// @brief プライマリキーバインド
	std::array<int, GAME_ACTION_COUNT> m_primaryBindings{};

	/// @brief セカンダリキーバインド（-1=未設定）
	std::array<int, GAME_ACTION_COUNT> m_secondaryBindings{};

	/// @brief GameActionをインデックスに変換する
	[[nodiscard]] static constexpr std::size_t idx(GameAction action) noexcept
	{
		return static_cast<std::size_t>(action);
	}
};

} // namespace mitiru::gw
