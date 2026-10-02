#pragma once

/// @file ReflectEngineTypes.hpp
/// @brief エンジンの POD (ビヘイビアツリーの状態、知覚、攻撃トークン、姿勢の入力、群衆の agent など) を
///        MITIRU_REFLECT に載せるための型名。game DLL がこのヘッダを読むと、GameMemory に置いたこれらの
///        値を host が解いて gameMemory の JSON に出し、ツール窓 (ai / anim / nav) がそれを読む。
/// @details 記述子は "struct" (直に置いた値) か "vec" (FixedVec の要素) のまま、elemType に
///          "mitiru.<型名>" が入る。host は名前と elemSize が合うときだけ解く。古い host は解けない
///          struct を黙って飛ばすので、ABI の形は変わらない。sgc の Vec3 のほかは前方宣言だけで足りるので、
///          Detour のような重い依存をこのヘッダは持ち込まない。組み方は docs/TOOL_WINDOWS.md の「ゲームが見せる型」。

#include <array>

#include <sgc/math/Vec3.hpp>

#include <mitiru/module/Reflection.hpp>

namespace mitiru::gameai
{
struct BtState;
struct PerceptionMemory;
template <int Holders, int Requests> struct AttackTokenPool;
}

namespace mitiru::animation
{
struct AnimPoseParams;
struct AnimEventHit;
struct YawXform;
}

namespace mitiru::nav
{
struct NavObstacle;
struct CrowdAgentView;
}

namespace mitiru::module
{

namespace detail
{

/// "mitiru.AttackTokenPool<4,16>"。host は数字から並びの位置を求める。
template <int Holders, int Requests>
constexpr std::array<char, 48> attackTokenPoolName() noexcept
{
	std::array<char, 48> out{};
	std::size_t n = 0;
	const auto put = [&](const char* s) { while (*s != '\0') { out[n++] = *s++; } };
	const auto putInt = [&](int v) {
		char digits[12]{};
		int k = 0;
		do { digits[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v > 0);
		while (k > 0) { out[n++] = digits[--k]; }
	};
	put("mitiru.AttackTokenPool<");
	putInt(Holders);
	put(",");
	putInt(Requests);
	put(">");
	return out;
}

}  // namespace detail

template <> struct ReflectName<sgc::Vec3f>                { static constexpr const char* value = "mitiru.Vec3f"; };
template <> struct ReflectName<gameai::BtState>           { static constexpr const char* value = "mitiru.BtState"; };
template <> struct ReflectName<gameai::PerceptionMemory>  { static constexpr const char* value = "mitiru.PerceptionMemory"; };
template <> struct ReflectName<animation::AnimPoseParams> { static constexpr const char* value = "mitiru.AnimPoseParams"; };
template <> struct ReflectName<animation::AnimEventHit>   { static constexpr const char* value = "mitiru.AnimEventHit"; };
template <> struct ReflectName<animation::YawXform>       { static constexpr const char* value = "mitiru.YawXform"; };
template <> struct ReflectName<nav::NavObstacle>          { static constexpr const char* value = "mitiru.NavObstacle"; };
template <> struct ReflectName<nav::CrowdAgentView>       { static constexpr const char* value = "mitiru.CrowdAgentView"; };

template <int Holders, int Requests>
struct ReflectName<gameai::AttackTokenPool<Holders, Requests>>
{
	static constexpr std::array<char, 48> storage = detail::attackTokenPoolName<Holders, Requests>();
	static constexpr const char* value = storage.data();
};

}  // namespace mitiru::module
