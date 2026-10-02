#pragma once

/// @file RollbackInput.hpp
/// @brief ロールバック対戦で回線に流す 1 人分の入力 (PadInput) と、それを InputSnapshot へ戻す規約
///
/// 回線には InputSnapshot (8 KB 余り) を流さず、ボタン 32 個のビットだけを流す。受けた側は全員分の
/// ボタンを 1 枚の InputSnapshot に合成して on_update に渡す。合成の規約は「1 台のキーボードを
/// 2 人で分ける」couch 対戦と同じ (1P = WASD 側、2P = 矢印側) なので、ゲーム DLL は回線を知らず、
/// ローカル 2 人対戦として書けばそのままオンライン対戦になる (ModuleApi は変えない)。
///
/// 合成した InputSnapshot はボタンと固定値 (dt・解像度・seed) だけから決まる。マウスや音声クロックの
/// ように端末ごとに違う値は 0 にする。端末ごとの値が混ざると、巻き戻して再計算した結果が端末ごとにずれる。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "mitiru/input/KeyCode.hpp"
#include "mitiru/module/ModuleApi.hpp"

namespace mitiru::network::rollback
{

inline constexpr int kMaxPlayers = 4;

/// @brief 1 人分のボタン (bit i = keymap の i 番のキー)
struct PadInput
{
	std::uint32_t buttons = 0;
};

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

/// @brief 合成に使う端末共通の固定値
struct SnapshotBase
{
	float dt = 1.0f / 60.0f;
	std::uint16_t logicalW = 1280;
	std::uint16_t logicalH = 720;
	std::uint64_t rngSeed = 1;
};

/// @brief 全員のボタン (今 / 1 フレーム前) から InputSnapshot を作る。押した瞬間・離した瞬間は前との差
/// @details 人数は now / prev / keymaps の短い方。out は丸ごと上書きする。押した・離した瞬間は全員を
///          合わせた押下の差で決めるので、2 人が同じキーに割り当てられていても down と released が同時に立たない
inline void composeSnapshot(module::InputSnapshot& out, std::span<const PadInput> now, std::span<const PadInput> prev,
	std::span<const Keymap> keymaps, const SnapshotBase& base) noexcept
{
	std::memset(&out, 0, sizeof(out));
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
	out.rngSeed = base.rngSeed;
	out.effectiveDt = base.dt;
	out.logicalW = base.logicalW;
	out.logicalH = base.logicalH;
	for (float& d : out.dtByLayer) d = base.dt;
}

} // namespace mitiru::network::rollback
