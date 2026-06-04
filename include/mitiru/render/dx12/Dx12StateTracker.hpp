#pragma once

/// @file Dx12StateTracker.hpp
/// @brief D3D12 バッファのリソース状態を CPU 側で追跡し、暗黙プロモーション/ディケイを尊重した
///        ResourceBarrier を自動発行するユーティリティ (NPR #27)
/// @details D3D12 のバッファは要求と無関係に COMMON で生成され、初回アクセスで暗黙プロモート、
///          `ExecuteCommandLists` 完了で COMMON へディケイする。before 状態を取り違えた
///          ResourceBarrier はデバッグレイヤで `Close` 失敗 → デバイスロストになる。
///          本トラッカは「未追跡 or COMMON からの遷移はバリア不要（暗黙プロモーション）／
///          非 COMMON からの遷移のみ明示バリア」を実装し、提出後に `decayAll()` を呼ぶ運用で
///          このクラスのバグを構造的に防ぐ。
///
///          遷移判断 (`plan`) は副作用なく barrier を返すだけなので GPU 無しで単体テストできる。
///          `to()` はそれを使ってコマンドリストへ実際に発行する薄いラッパ。
///
/// @code
/// Dx12StateTracker tracker;
/// tracker.to(cmd, buf, D3D12_RESOURCE_STATE_COPY_DEST);        // COMMON→COPY_DEST: 暗黙 (バリア無し)
/// cmd->CopyBufferRegion(buf, 0, upload, 0, bytes);
/// tracker.to(cmd, buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); // COPY_DEST→UAV: 明示バリア
/// // ... dispatch ...
/// cmd->Close(); queue->ExecuteCommandLists(...); sync();
/// tracker.decayAll();   // 提出完了 → 全バッファ COMMON へディケイ
/// @endcode

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <d3d12.h>

namespace mitiru::render::dx12
{

/// @brief D3D12 バッファ状態トラッカ（暗黙プロモーション/ディケイ対応）
class Dx12StateTracker
{
public:
	/// @brief 遷移を計画する。必要なら発行すべき ResourceBarrier を返し、内部状態を更新する。
	/// @details 副作用は内部 state の更新のみ（GPU に触れない）。バリア不要なら nullopt。
	///          - 未追跡（= COMMON）からの遷移 → 暗黙プロモーション（nullopt）
	///          - 既に同状態 → nullopt
	///          - 非 COMMON からの別状態への遷移 → transition バリアを返す
	[[nodiscard]] std::optional<D3D12_RESOURCE_BARRIER> plan(
		ID3D12Resource* resource, D3D12_RESOURCE_STATES newState)
	{
		for (auto& e : m_states)
		{
			if (e.first == resource)
			{
				if (e.second == newState) { return std::nullopt; }  // 既に同状態
				std::optional<D3D12_RESOURCE_BARRIER> barrier;
				if (e.second != D3D12_RESOURCE_STATE_COMMON)
				{
					barrier = makeTransition(resource, e.second, newState);  // 明示遷移
				}
				// COMMON からは暗黙プロモーション（バリア不要）。
				e.second = newState;
				return barrier;
			}
		}
		// 未追跡 = COMMON からのプロモーション（バリア不要）。
		m_states.push_back({resource, newState});
		return std::nullopt;
	}

	/// @brief `plan` の結果をコマンドリストへ発行する薄いラッパ。
	void to(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES newState)
	{
		const auto barrier = plan(resource, newState);
		if (barrier && cmd)
		{
			D3D12_RESOURCE_BARRIER b = *barrier;
			cmd->ResourceBarrier(1, &b);
		}
	}

	/// @brief 提出完了後に呼ぶ。全リソースが COMMON へディケイしたとみなして追跡をクリアする。
	void decayAll() noexcept { m_states.clear(); }

	/// @brief 現在追跡している状態（未追跡なら COMMON）。テスト/デバッグ用。
	[[nodiscard]] D3D12_RESOURCE_STATES current(ID3D12Resource* resource) const noexcept
	{
		for (const auto& e : m_states)
		{
			if (e.first == resource) { return e.second; }
		}
		return D3D12_RESOURCE_STATE_COMMON;
	}

	/// @brief 追跡中のリソース数。
	[[nodiscard]] std::size_t trackedCount() const noexcept { return m_states.size(); }

private:
	[[nodiscard]] static D3D12_RESOURCE_BARRIER makeTransition(
		ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) noexcept
	{
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		b.Transition.pResource   = r;
		b.Transition.StateBefore = from;
		b.Transition.StateAfter  = to;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		return b;
	}

	std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> m_states;
};

} // namespace mitiru::render::dx12

#endif // _WIN32
