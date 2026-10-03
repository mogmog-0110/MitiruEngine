#pragma once

/// @file RollbackInput.hpp
/// @brief ロールバック対戦で回線に流す 1 人分の入力 (PadInput) と、それを InputSnapshot へ戻す規約
///
/// 回線には InputSnapshot (10 KB 余り) を流さず、キー 32 個のビットとパッド 1 台分と、キー割り当て後の操作 (24 byte) だけを流す。
/// 受けた側は全員分を 1 枚の InputSnapshot に合成して on_update に渡す。合成の規約は couch の協力プレイと
/// 同じで、キーは 1P = WASD 側、2P = 矢印側、パッドは n 人目 = gamepads[n]。ゲーム DLL は回線を知らず、
/// ローカルの多人数プレイとして書けばそのままオンラインになる。人数 (netPlayerCount) と人ごとの操作
/// (actions*ByPlayer) も合成に入る (ABI v50)。
///
/// 合成した InputSnapshot はボタンと固定値 (dt・解像度・seed) だけから決まる。マウスや音声クロックの
/// ように端末ごとに違う値は 0 にする。端末ごとの値が混ざると、巻き戻して再計算した結果が端末ごとにずれる。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>

#include "mitiru/input/KeyCode.hpp"
#include "mitiru/module/ModuleApi.hpp"

namespace mitiru::network::rollback
{

inline constexpr int kMaxPlayers = 4;

/// @brief 1 人分の入力。キーは keymap のボタン、パッドは InputSnapshot::gamepads[その人の番号] になる
/// @details 軸は 1/127 刻みの整数で流す。自分の入力も回線を通った値で進めるので、全員が同じ値を見る。
struct PadInput
{
	std::uint32_t buttons = 0;       ///< bit i = keymap の i 番のキー
	std::uint32_t padButtons = 0;    ///< gamepad:: のビット
	std::int8_t   padAxes[6] = {};   ///< gamepad::Axis の添字。値 / 127 が軸
	std::uint8_t  padConnected = 0;
	std::uint8_t  _pad = 0;
	std::uint64_t actions = 0;       ///< キー割り当て後の操作 (InputSnapshot::actionsDown の bit)
};
static_assert(sizeof(PadInput) == 24, "回線に流す大きさ。変えたら相手と揃わない");

/// @brief ボタン i が InputSnapshot のどの VK コードになるか (0 = 割り当て無し)
struct Keymap
{
	std::array<std::uint8_t, 32> vk{};
};

namespace detail
{
[[nodiscard]] constexpr std::uint8_t vk(KeyCode k) noexcept { return static_cast<std::uint8_t>(k); }
} // namespace detail

/// @brief 1P: 移動 WASD、決定 Space、他 F / G / E / Q
inline constexpr Keymap kKeymapPlayer1{{detail::vk(KeyCode::W), detail::vk(KeyCode::A), detail::vk(KeyCode::S),
	detail::vk(KeyCode::D), detail::vk(KeyCode::Space), detail::vk(KeyCode::F), detail::vk(KeyCode::G),
	detail::vk(KeyCode::E), detail::vk(KeyCode::Q)}};

/// @brief 2P: 移動 矢印、決定 Enter、他 右 Shift / / (スラッシュ) / . / ,
inline constexpr Keymap kKeymapPlayer2{{detail::vk(KeyCode::Up), detail::vk(KeyCode::Left), detail::vk(KeyCode::Down),
	detail::vk(KeyCode::Right), detail::vk(KeyCode::Enter), detail::vk(KeyCode::RShift), detail::vk(KeyCode::Slash),
	detail::vk(KeyCode::Period), detail::vk(KeyCode::Comma)}};

/// @brief 実キーボードの状態から、keymap のキーが押されているかをボタンにする (自分の入力を回線へ出す側)
[[nodiscard]] inline PadInput sampleKeys(const module::InputSnapshot& real, const Keymap& map) noexcept
{
	PadInput out;
	for (std::size_t i = 0; i < map.vk.size(); ++i)
	{
		if (map.vk[i] != 0 && real.keysDown[map.vk[i]] != 0) out.buttons |= 1u << i;
	}
	return out;
}

/// @brief 実機のパッド (繋がっている全台の合成) を自分の入力にする
[[nodiscard]] inline PadInput samplePad(const module::InputSnapshot& real) noexcept
{
	PadInput out;
	if (real.gamepadConnected == 0) return out;
	out.padConnected = 1;
	out.padButtons = real.gamepadButtonsDown;
	for (std::size_t i = 0; i < 6; ++i)
	{
		const float v = std::clamp(real.gamepadAxes[i], -1.0f, 1.0f) * 127.0f;
		out.padAxes[i] = static_cast<std::int8_t>(v < 0.0f ? v - 0.5f : v + 0.5f);
	}
	return out;
}

/// @brief オンラインの自分の入力: キーは 1P の keymap で読み、パッドとキー割り当て後の操作を足す (何番の人でも手元は WASD 側)
[[nodiscard]] inline PadInput sampleLocal(const module::InputSnapshot& real, const Keymap& map = kKeymapPlayer1) noexcept
{
	PadInput out = samplePad(real);
	out.buttons = sampleKeys(real, map).buttons;
	out.actions = real.actionsDown;
	return out;
}

/// @brief on_update / on_rebuild の呼び方を差し替える口。host は落ちた game を止める guard を通して呼ぶ
struct RollbackCalls
{
	void* ctx = nullptr;
	bool (*update)(void* ctx, const module::InputSnapshot* in, module::FrameIntents* out) = nullptr;  ///< false = 落ちた
	bool (*rebuild)(void* ctx) = nullptr;
	/// host 権威の参加者が、描くための写しで自分の分だけを 1 フレーム進める (mitiru_module_net_predict)。null なら先に進めない
	bool (*predict)(void* ctx, void* drawMemory, const module::InputSnapshot* local, std::uint8_t player) = nullptr;
};

/// @brief GameMemory と保存した状態を FNV-1a で 32bit に畳む (desync 検出用)。h を渡すと続きから畳む
[[nodiscard]] inline std::uint32_t stateChecksum(const void* data, std::size_t size, std::uint32_t h = 2166136261u) noexcept
{
	const auto* p = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
	return h;
}

/// @brief 合成に使う端末共通の固定値
struct SnapshotBase
{
	float dt = 1.0f / 60.0f;
	std::uint16_t logicalW = 1280;
	std::uint16_t logicalH = 720;
	std::uint64_t rngSeed = 1;
};

namespace detail
{
/// @brief player p のパッドを gamepads[p] へ、全員の合成を gamepad* へ (host の couch 対戦と同じ合成)
inline void composePads(module::InputSnapshot& out, std::span<const PadInput> now, std::span<const PadInput> prev) noexcept
{
	std::uint32_t wasDown = 0;
	const std::size_t players = std::min({now.size(), prev.size(), std::size(out.gamepads)});
	for (std::size_t p = 0; p < players; ++p)
	{
		module::GamepadState& g = out.gamepads[p];
		const std::uint32_t before = prev[p].padConnected != 0 ? prev[p].padButtons : 0u;
		wasDown |= before;
		if (now[p].padConnected == 0)
		{
			g.buttonsJustReleased = before;   // 押したまま抜いたパッドも離した瞬間を出す
			continue;
		}
		g.connected = 1;
		g.buttonsDown = now[p].padButtons;
		g.buttonsJustPressed = now[p].padButtons & ~before;
		g.buttonsJustReleased = before & ~now[p].padButtons;
		for (std::size_t i = 0; i < 6; ++i) g.axes[i] = static_cast<float>(now[p].padAxes[i]) / 127.0f;
		if (out.gamepadConnected == 0) std::memcpy(out.gamepadAxes, g.axes, sizeof(g.axes));
		out.gamepadConnected = 1;
		out.gamepadButtonsDown |= g.buttonsDown;
	}
	out.gamepadButtonsJustPressed = out.gamepadButtonsDown & ~wasDown;
	out.gamepadButtonsJustReleased = wasDown & ~out.gamepadButtonsDown;
}
/// @brief 人数と人ごとの操作。全員の合成 (actionsDown) は誰かが押していれば立つ
inline void composeActions(module::InputSnapshot& out, std::span<const PadInput> now, std::span<const PadInput> prev) noexcept
{
	const std::size_t players = std::min({now.size(), prev.size(), static_cast<std::size_t>(module::kMaxNetPlayers)});
	std::uint64_t was = 0;
	out.netPlayerCount = static_cast<std::uint8_t>(players);
	for (std::size_t p = 0; p < players; ++p)
	{
		out.actionsDownByPlayer[p] = now[p].actions;
		out.actionsPressedByPlayer[p] = now[p].actions & ~prev[p].actions;
		out.actionsReleasedByPlayer[p] = prev[p].actions & ~now[p].actions;
		out.actionsDown |= now[p].actions;
		was |= prev[p].actions;
	}
	out.actionsPressed = out.actionsDown & ~was;
	out.actionsReleased = was & ~out.actionsDown;
}
} // namespace detail

/// @brief 全員の入力 (今 / 1 フレーム前) から InputSnapshot を作る。押した瞬間・離した瞬間は前との差
/// @details キーの人数は now / prev / keymaps の短い方、パッドは now / prev の短い方 (4 人まで)。
///          out は丸ごと上書きする。押した・離した瞬間は全員を合わせた押下の差で決めるので、
///          2 人が同じキーに割り当てられていても down と released が同時に立たない
inline void composeSnapshot(module::InputSnapshot& out, std::span<const PadInput> now, std::span<const PadInput> prev,
	std::span<const Keymap> keymaps, const SnapshotBase& base) noexcept
{
	std::memset(&out, 0, sizeof(out));
	detail::composePads(out, now, prev);
	std::uint8_t wasDown[sizeof(out.keysDown)] = {};
	const std::size_t players = std::min({now.size(), prev.size(), keymaps.size()});
	for (std::size_t p = 0; p < players; ++p)
	{
		const Keymap& map = keymaps[p];
		for (std::size_t i = 0; i < map.vk.size(); ++i)
		{
			if (map.vk[i] == 0) continue;
			out.keysDown[map.vk[i]] |= static_cast<std::uint8_t>((now[p].buttons >> i) & 1u);
			wasDown[map.vk[i]] |= static_cast<std::uint8_t>((prev[p].buttons >> i) & 1u);
		}
	}
	for (std::size_t k = 0; k < sizeof(out.keysDown); ++k)
	{
		out.keysJustPressed[k] = (out.keysDown[k] && !wasDown[k]) ? 1 : 0;
		out.keysJustReleased[k] = (!out.keysDown[k] && wasDown[k]) ? 1 : 0;
	}
	detail::composeActions(out, now, prev);
	out.rngSeed = base.rngSeed;
	out.effectiveDt = base.dt;
	out.logicalW = base.logicalW;
	out.logicalH = base.logicalH;
	for (float& d : out.dtByLayer) d = base.dt;
}

} // namespace mitiru::network::rollback
