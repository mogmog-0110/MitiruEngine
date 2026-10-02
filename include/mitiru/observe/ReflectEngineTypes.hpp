#pragma once

/// @file ReflectEngineTypes.hpp
/// @brief elemType が "mitiru.<型名>" の reflect field (module/ReflectEngineTypes.hpp が名前を付ける) を、
///        host が JSON に解く。ツール窓 (ai / anim / nav) と /api/ai/state が同じ形を読む。
/// @details host は game の型を include しない (Detour を持たない host もある) ので、byte の位置を
///          ここに書き写し、tests/mitiru/TestReflectEngineTypes.cpp が本物の型の offsetof と突き合わせる。
///          elemSize が書き写した大きさと違えば解かない (知らない版の bytes を読み違えない)。
///          解いた object は "$type" を持つので、ページは木のどこに置かれていても型で見つけられる。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/observe/NumberAppend.hpp>

namespace mitiru::observe
{

namespace engine_types
{

template <class T>
[[nodiscard]] inline T read(const std::uint8_t* p, std::uint32_t at) noexcept
{
	T v{};
	std::memcpy(&v, p + at, sizeof(T));
	return v;
}

[[nodiscard]] inline nlohmann::json num(float v)
{
	if (!std::isfinite(v)) { return std::string(nonFiniteJsonName(v)); }
	return v;
}

[[nodiscard]] inline nlohmann::json vec3(const std::uint8_t* p, std::uint32_t at)
{
	return nlohmann::json::array({ num(read<float>(p, at)), num(read<float>(p, at + 4)), num(read<float>(p, at + 8)) });
}

inline constexpr std::uint32_t kVec3Size = 12;
inline constexpr std::uint32_t kBtNodes = 64;
inline constexpr std::uint32_t kBtStateSize = 332;
inline constexpr std::uint32_t kPerceptionSize = 28;
inline constexpr std::uint32_t kAnimLayers = 16;
inline constexpr std::uint32_t kAnimLayerSize = 16;
inline constexpr std::uint32_t kAnimPoseSize = 8 + kAnimLayers * kAnimLayerSize;
inline constexpr std::uint32_t kAnimEventSize = 8;
inline constexpr std::uint32_t kYawXformSize = 28;
inline constexpr std::uint32_t kNavObstacleSize = 32;
inline constexpr std::uint32_t kCrowdAgentSize = 40;

/// 攻撃トークンの並び。保持の 3 列が H 個ずつ、希望の 3 列が R 個ずつ続き、最後に上限と希望の数が来る。
struct TokenPoolLayout
{
	std::uint32_t holders = 0;
	std::uint32_t requests = 0;
	[[nodiscard]] constexpr std::uint32_t size() const noexcept { return 12 * holders + 12 * requests + 8; }
	[[nodiscard]] constexpr std::uint32_t capacityAt() const noexcept { return 12 * holders + 12 * requests; }
};

/// "mitiru.AttackTokenPool<4,16>" から H と R を読む。形が違えば holders = 0。
[[nodiscard]] inline TokenPoolLayout parseTokenPoolName(std::string_view name) noexcept
{
	constexpr std::string_view kPrefix = "mitiru.AttackTokenPool<";
	TokenPoolLayout out;
	if (name.size() <= kPrefix.size() || name.substr(0, kPrefix.size()) != kPrefix || name.back() != '>') { return out; }
	std::uint32_t values[2]{};
	int which = 0;
	for (const char c : name.substr(kPrefix.size(), name.size() - kPrefix.size() - 1))
	{
		if (c == ',' && which == 0) { which = 1; continue; }
		if (c < '0' || c > '9' || values[which] > 100000) { return TokenPoolLayout{}; }
		values[which] = values[which] * 10 + static_cast<std::uint32_t>(c - '0');
	}
	if (which != 1 || values[0] == 0 || values[1] == 0) { return TokenPoolLayout{}; }
	return TokenPoolLayout{ values[0], values[1] };
}

[[nodiscard]] inline const char* btStatusName(std::uint8_t s) noexcept
{
	switch (s)
	{
	case 1: return "running";
	case 2: return "success";
	case 3: return "failure";
	default: return "idle";
	}
}

/// 最後に Idle でないノードまでを、1 ノード 1 文字 ('.' Idle、R Running、S Success、F Failure) で並べる。
[[nodiscard]] inline std::string btStatusLetters(const std::uint8_t* p)
{
	std::uint32_t end = 0;
	for (std::uint32_t i = 0; i < kBtNodes; ++i) { if (p[i] != 0) { end = i + 1; } }
	std::string out(end, '.');
	for (std::uint32_t i = 0; i < end; ++i)
	{
		const std::uint8_t s = p[i];
		out[i] = s == 1 ? 'R' : s == 2 ? 'S' : s == 3 ? 'F' : '.';
	}
	return out;
}

[[nodiscard]] inline nlohmann::json btState(const std::uint8_t* p)
{
	const auto running = read<std::uint16_t>(p, 328);
	return {
		{ "$type", "mitiru.BtState" },
		{ "root", btStatusName(read<std::uint8_t>(p, 330)) },
		{ "status", btStatusLetters(p) },
		{ "runningNode", running == 0xFFFFu ? -1 : static_cast<int>(running) },
		{ "ticks", read<std::uint32_t>(p, 320) },
		{ "tree", read<std::uint32_t>(p, 324) },
	};
}

[[nodiscard]] inline nlohmann::json perception(const std::uint8_t* p)
{
	static constexpr const char* kLevels[] = { "unaware", "suspicious", "alert" };
	const auto level = read<std::uint8_t>(p, 24);
	return {
		{ "$type", "mitiru.PerceptionMemory" },
		{ "level", level < 3 ? kLevels[level] : "unaware" },
		{ "awareness", num(read<float>(p, 12)) },
		{ "seenNow", read<std::uint8_t>(p, 26) != 0 },
		{ "lastKnown", read<std::uint8_t>(p, 25) != 0 ? vec3(p, 0) : nlohmann::json() },
		{ "lastSeenFrame", read<std::uint32_t>(p, 16) },
		{ "lastCueFrame", read<std::uint32_t>(p, 20) },
	};
}

[[nodiscard]] inline nlohmann::json tokenPool(const std::uint8_t* p, const TokenPoolLayout& L)
{
	nlohmann::json holders = nlohmann::json::array();
	std::int64_t used = 0;
	for (std::uint32_t i = 0; i < L.holders; ++i)
	{
		const auto id = read<std::uint32_t>(p, 4 * i);
		if (id == 0) { continue; }
		const auto cost = read<std::uint32_t>(p, 4 * (L.holders + i));
		used += cost;
		holders.push_back({ { "id", id }, { "cost", cost }, { "since", read<std::uint32_t>(p, 4 * (2 * L.holders + i)) } });
	}
	nlohmann::json requests = nlohmann::json::array();
	const auto requestCount = read<std::int32_t>(p, L.capacityAt() + 4);
	const std::uint32_t base = 12 * L.holders;
	for (std::uint32_t i = 0; i < L.requests && static_cast<std::int32_t>(i) < requestCount; ++i)
	{
		requests.push_back({ { "id", read<std::uint32_t>(p, base + 4 * i) },
		                     { "cost", read<std::uint32_t>(p, base + 4 * (L.requests + i)) },
		                     { "score", num(read<float>(p, base + 4 * (2 * L.requests + i))) } });
	}
	return {
		{ "$type", "mitiru.AttackTokenPool" },
		{ "capacity", read<std::int32_t>(p, L.capacityAt()) },
		{ "used", used },
		{ "holders", std::move(holders) },
		{ "requests", std::move(requests) },
	};
}

[[nodiscard]] inline nlohmann::json animPose(const std::uint8_t* p)
{
	const std::uint32_t count = (std::min)(read<std::uint32_t>(p, 0), kAnimLayers);
	nlohmann::json layers = nlohmann::json::array();
	for (std::uint32_t i = 0; i < count; ++i)
	{
		const std::uint32_t at = 8 + i * kAnimLayerSize;
		const auto flags = read<std::uint8_t>(p, at + 4);
		layers.push_back({ { "clip", read<std::int16_t>(p, at) }, { "mask", read<std::int16_t>(p, at + 2) },
		                   { "time", num(read<float>(p, at + 8)) }, { "weight", num(read<float>(p, at + 12)) },
		                   { "loop", (flags & 1u) != 0 }, { "additive", (flags & 2u) != 0 } });
	}
	return { { "$type", "mitiru.AnimPoseParams" }, { "inPlace", (read<std::uint32_t>(p, 4) & 1u) != 0 },
	         { "layers", std::move(layers) } };
}

[[nodiscard]] inline nlohmann::json animEvent(const std::uint8_t* p)
{
	return { { "$type", "mitiru.AnimEventHit" }, { "name", read<std::uint16_t>(p, 0) },
	         { "clip", read<std::int16_t>(p, 2) }, { "time", num(read<float>(p, 4)) } };
}

/// ルートモーションの差分。r は Y 軸回りだけの四元数なので、角度は 2 atan2(y, w)。
[[nodiscard]] inline nlohmann::json yawXform(const std::uint8_t* p)
{
	constexpr double kRad2Deg = 57.29577951308232;
	const double yaw = 2.0 * std::atan2(static_cast<double>(read<float>(p, 16)), static_cast<double>(read<float>(p, 24)));
	return { { "$type", "mitiru.YawXform" }, { "t", vec3(p, 0) }, { "yawDeg", num(static_cast<float>(yaw * kRad2Deg)) } };
}

[[nodiscard]] inline nlohmann::json navObstacle(const std::uint8_t* p)
{
	static constexpr const char* kShapes[] = { "none", "box", "cylinder" };
	const auto shape = read<std::uint8_t>(p, 28);
	return { { "$type", "mitiru.NavObstacle" }, { "shape", shape < 3 ? kShapes[shape] : "none" },
	         { "center", vec3(p, 0) }, { "half", vec3(p, 12) }, { "yaw", num(read<float>(p, 24)) },
	         { "enabled", read<std::uint8_t>(p, 29) != 0 } };
}

[[nodiscard]] inline nlohmann::json crowdAgent(const std::uint8_t* p)
{
	static constexpr const char* kMoves[] = { "inactive", "idle", "moving", "velocity", "failed", "offMesh" };
	const auto move = read<std::uint8_t>(p, 36);
	return { { "$type", "mitiru.CrowdAgentView" }, { "p", vec3(p, 0) }, { "v", vec3(p, 12) }, { "target", vec3(p, 24) },
	         { "move", move < 6 ? kMoves[move] : "inactive" }, { "partial", read<std::uint8_t>(p, 37) != 0 } };
}

/// 名前から決まる大きさ。知らない名前は 0。
[[nodiscard]] inline std::uint32_t expectedSize(std::string_view name) noexcept
{
	if (name == "mitiru.Vec3f") { return kVec3Size; }
	if (name == "mitiru.BtState") { return kBtStateSize; }
	if (name == "mitiru.PerceptionMemory") { return kPerceptionSize; }
	if (name == "mitiru.AnimPoseParams") { return kAnimPoseSize; }
	if (name == "mitiru.AnimEventHit") { return kAnimEventSize; }
	if (name == "mitiru.YawXform") { return kYawXformSize; }
	if (name == "mitiru.NavObstacle") { return kNavObstacleSize; }
	if (name == "mitiru.CrowdAgentView") { return kCrowdAgentSize; }
	const TokenPoolLayout pool = parseTokenPoolName(name);
	return pool.holders > 0 ? pool.size() : 0;
}

}  // namespace engine_types

/// @brief elemType がエンジンの型で、elemSize がその大きさと合うか (この 2 つが揃えば bytes の形が決まる)。
[[nodiscard]] inline bool isEngineReflectType(const char* elemType, std::uint32_t elemSize) noexcept
{
	if (elemType == nullptr || elemSize == 0) { return false; }
	return engine_types::expectedSize(elemType) == elemSize;
}

/// @brief エンジンの型の値を JSON に解く。isEngineReflectType が偽なら null。
[[nodiscard]] inline nlohmann::json engineReflectTypeToJson(const char* elemType, const std::uint8_t* p, std::uint32_t elemSize)
{
	namespace et = engine_types;
	if (p == nullptr || !isEngineReflectType(elemType, elemSize)) { return nullptr; }
	const std::string_view name(elemType);
	if (name == "mitiru.Vec3f") { return et::vec3(p, 0); }
	if (name == "mitiru.BtState") { return et::btState(p); }
	if (name == "mitiru.PerceptionMemory") { return et::perception(p); }
	if (name == "mitiru.AnimPoseParams") { return et::animPose(p); }
	if (name == "mitiru.AnimEventHit") { return et::animEvent(p); }
	if (name == "mitiru.YawXform") { return et::yawXform(p); }
	if (name == "mitiru.NavObstacle") { return et::navObstacle(p); }
	if (name == "mitiru.CrowdAgentView") { return et::crowdAgent(p); }
	return et::tokenPool(p, et::parseTokenPoolName(name));
}

}  // namespace mitiru::observe
