#pragma once

/// @file PodHsm.hpp
/// @brief 状態遷移に優先度を持たせる flat POD ヘルパ (HE2 の GOCPlayerHsm::ChangeState(id, priority) 相当、
/// ★1-6)。`fsm::StateMachine<StateT>` は scene 系専用で DLL 境界を渡らないため、module 系の
/// GameMemory にそのまま埋め込める別物として用意する (既存を太らせない)。

#include <cstdint>

namespace mitiru::module
{

/// GameMemory に直接埋め込める aggregate (MITIRU_REFLECT_AUTO が hsm.state 等へ再帰する)。
/// state は commit() が確定させるまで変わらない。同フレーム内の複数箇所からの changeState
/// 呼び出し (StatePlugin 相当の「横から差し込み」を含む) が、呼び出し順に依存する
/// 中途半端な値を on_update の後段に見せないための設計。
struct PodHsm
{
	std::uint16_t state           = 0;  ///< 直近 commit で確定した状態 id
	std::uint16_t pending         = 0;  ///< このフレーム中に要求された遷移先 (commit まで反映しない)
	std::uint8_t  pendingPriority = 0;  ///< pending を要求した優先度。0 = 今フレームまだ未要求 (commit でリセット)
	std::uint8_t  depth           = 0;  ///< 現在の state に留まり続けているフレーム数 (255 で飽和、commit が加算)
	std::uint32_t plugins         = 0;  ///< 状態と独立に効くフラグ集合 (HE2 の StatePluginBoost 相当)。ビットの意味はゲーム側で決める
};

/// HE2 の `ChangeState(id, priority)` 相当。今フレームの pending より低い優先度の要求は
/// 黙って弾く (false を返す)。同じ優先度は「後勝ち」であり、呼んだ順で上書きする。
/// state 自体は commit() まで変わらないので、この関数を呼んだだけでは onEnter/onExit は起きない。
inline bool changeState(PodHsm& hsm, std::uint16_t id, std::uint8_t priority) noexcept
{
	if (priority == 0) { priority = 1; }  // 0 は pendingPriority の「未要求」番兵。要求の最低は 1
	if (hsm.pendingPriority != 0 && priority < hsm.pendingPriority) { return false; }
	hsm.pending         = id;
	hsm.pendingPriority = priority;
	return true;
}

/// フレーム末に pending を state へ確定する。onEnter/onExit はゲーム側が
/// 「commit 前後の state を比較して switch (id) で分岐する」形で書く (engine に
/// callback registry を作らない、ADR 0005 の enumerated set を太らせない)。
inline void commit(PodHsm& hsm) noexcept
{
	if (hsm.pendingPriority == 0) { return; }
	if (hsm.pending == hsm.state)
	{
		// 同じ state への再要求は「入り直さない」(HE2 の ChangeStateRestart が別 API なのと同じ)。
		if (hsm.depth < 255) { ++hsm.depth; }
	}
	else
	{
		hsm.state = hsm.pending;
		hsm.depth = 0;
	}
	hsm.pendingPriority = 0;
}

}  // namespace mitiru::module
