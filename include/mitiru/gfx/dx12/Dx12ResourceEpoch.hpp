#pragma once

/// @file Dx12ResourceEpoch.hpp
/// @brief D3D12 の資源とヒープに、作られるたびに別になる世代の番号を持たせる
/// @details 記述子の組が「どの資源を写して書いたか」を COM ポインタの番地で覚えると、手放した資源の次に作る資源が
///          同じ番地に来たとき見分けられない (GpuResource は GPU の完了後に即座に返すので、作り替えの直後に起きる)。
///          番号は ID3D12Object の private data に書くので、GpuResource でも ComPtr でも D3D12MA の配置資源でも
///          同じ関数で引ける。初めて問われた物に振るので、作る側は何もしない。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>

#include <d3d12.h>

namespace mitiru::gfx
{

namespace detail
{

inline constexpr GUID kObjectEpochKey = {0x4e1f9a52, 0x6c3d, 0x4b88, {0x9a, 0x07, 0x2d, 0x71, 0xc5, 0x3e, 0x80, 0xb4}};

/// 次に振る番号。上位に印を持たせるのは、ゲーム DLL と host が別々に数えて同じ資源に振っても重ならないようにするため
/// (ヘッダだけのエンジンは、取り込んだモジュールごとに static を持つ)
[[nodiscard]] inline std::atomic<std::uint64_t>& epochCounter() noexcept
{
	static std::atomic<std::uint64_t> counter{
		(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) & 0x7FFFFFu) << 40};
	return counter;
}

} // namespace detail

/// @brief 世代の番号。null は 0。同じ物には何度問うても同じ番号で、作り替えた物 (別のオブジェクト) には別の番号が返る
/// @details 番号を振れない (private data を置けない) 時は番地から作る。番地の再利用は見分けられなくなるが、
///          メモリが尽きた状況の縮退で、番号が毎回変わって毎フレーム書き込むよりよい
[[nodiscard]] inline std::uint64_t objectEpoch(const ID3D12Object* object) noexcept
{
	if (object == nullptr) { return 0; }
	auto* target = const_cast<ID3D12Object*>(object);
	std::uint64_t epoch = 0;
	UINT size = sizeof(epoch);
	if (SUCCEEDED(target->GetPrivateData(detail::kObjectEpochKey, &size, &epoch)) && size == sizeof(epoch) && epoch != 0)
	{
		return epoch;
	}
	epoch = detail::epochCounter().fetch_add(1) + 1;
	if (FAILED(target->SetPrivateData(detail::kObjectEpochKey, sizeof(epoch), &epoch)))
	{
		return reinterpret_cast<std::uintptr_t>(object) | (std::uint64_t{1} << 63);
	}
	return epoch;
}

} // namespace mitiru::gfx

#endif // _WIN32
