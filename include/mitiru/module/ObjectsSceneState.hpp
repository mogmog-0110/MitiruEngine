#pragma once

/// @file ObjectsSceneState.hpp
/// @brief MITIRU_GAME_OBJECTS (ADR 0040) の場面の中身を、GameMemory の外に持つ状態の窓口 (ADR 0054) として
///        host へ預ける。預けた game は GameMemory + 場面で全状態になり、巻き戻し・resim・分岐・候補が使える。
/// @details 場面の型 Game に、場面を bytes へ書く saveScene と bytes から戻す restoreScene を持たせ、
/// ファイルスコープで MITIRU_GAME_OBJECTS_SCENE_STATE(Game, Progress, 形の番号) と書く。
/// saveScene は dst へ書いて必要な byte 数を返し、cap が足りなければ書かずに必要量だけ返す。
/// restoreScene は host が進行データを書き戻し、on_rebuild で場面を組み立てた後に呼ばれる。

#include <cstdint>
#include <cstring>

#include <mitiru/module/Game.hpp>
#include <mitiru/module/SideState.hpp>

namespace mitiru::module::detail
{

template<class G, class P>
std::uint64_t objectsSceneSave(void* /*ctx*/, const void* memory, void* dst, std::uint64_t cap) noexcept
{
	try
	{
		// 場面が無ければ進行データから作る (組み立ては進行データを読むだけ)。
		G& scene = ensureObjects<G, P>(*static_cast<P*>(const_cast<void*>(memory)));
		return scene.saveScene(dst, cap);
	}
	catch (...) { return 0; }
}

template<class G, class P>
std::int32_t objectsSceneRestore(void* /*ctx*/, void* memory, const void* src, std::uint64_t size) noexcept
{
	try
	{
		G& scene = ensureObjects<G, P>(*static_cast<P*>(memory));
		return scene.restoreScene(src, size) ? 1 : 0;
	}
	catch (...) { return 0; }
}

template<class G, class P>
[[nodiscard]] SideStateChannel makeObjectsSceneChannel(std::uint32_t version) noexcept
{
	static_assert(requires(const G& g, void* d, std::uint64_t n) { { g.saveScene(d, n) } -> std::convertible_to<std::uint64_t>; },
		"MITIRU_GAME_OBJECTS_SCENE_STATE: Game に std::uint64_t saveScene(void* dst, std::uint64_t cap) const が必要です");
	static_assert(requires(G& g, const void* s, std::uint64_t n) { { g.restoreScene(s, n) } -> std::convertible_to<bool>; },
		"MITIRU_GAME_OBJECTS_SCENE_STATE: Game に bool restoreScene(const void* src, std::uint64_t size) が必要です");
	SideStateChannel ch{};
	std::strncpy(ch.name, "scene", sizeof(ch.name) - 1);
	ch.version = version;
	ch.flags   = kSideStateCoversScene;
	ch.save    = &objectsSceneSave<G, P>;
	ch.restore = &objectsSceneRestore<G, P>;
	return ch;
}

}  // namespace mitiru::module::detail

/// MITIRU_GAME_OBJECTS の場面の中身を窓口 "scene" として預ける。MITIRU_GAME_OBJECTS と同じファイルに書く。
#define MITIRU_GAME_OBJECTS_SCENE_STATE(GameType, ProgressType, version)                              \
	static const ::mitiru::module::detail::SideStateRegistrar                                           \
		MITIRU_SIDE_STATE_CAT(_mitiruObjectsScene_, __LINE__){                                           \
			::mitiru::module::detail::makeObjectsSceneChannel<GameType, ProgressType>(version)}
