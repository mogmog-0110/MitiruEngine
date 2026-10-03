#pragma once

/// @file NarrativeCue.hpp
/// @brief カットシーンと会話が共有する合図 (音・BGM・VFX・検証の印・ゲームの関数・アニメのきっかけ)。
/// @details 合図はアセット側のデータで、GameMemory には入らない。いつ鳴ったかは再生の時刻 (SequencePlayback) か
///          会話の位置 (DialogueState) で決まるので、巻き戻して進め直せば同じ所でもう一度出る。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <mitiru/narrative/NarrativeVars.hpp>

namespace mitiru::narrative
{

enum class CueKind : std::uint8_t
{
	Sound,   ///< 効果音 (hud.play)
	Music,   ///< BGM (hud.music)。name が空なら止める
	Vfx,     ///< ゲームが描く演出。duration 秒のあいだ forEachActiveCue に出る
	Mark,    ///< 検証の印 (hud.mark)
	Call,    ///< ゲームの関数を名前で呼ぶ。カットシーンを飛ばしても必ず届く
	Anim,    ///< アニメのきっかけ (状態機械の trigger)。actor と name で渡す
};

struct Cue
{
	CueKind       kind = CueKind::Mark;
	std::string   name;
	std::string   actor;
	std::uint32_t nameId = 0;   ///< nameHash(name)。ゲームが switch で引くとき用
	std::uint32_t actorId = 0;
	float         value = 1.0f;     ///< 音量など
	float         duration = 0.0f;  ///< Vfx の長さ (秒)
};

[[nodiscard]] inline std::optional<CueKind> parseCueKind(std::string_view s) noexcept
{
	if (s == "sound") { return CueKind::Sound; }
	if (s == "music") { return CueKind::Music; }
	if (s == "vfx")   { return CueKind::Vfx; }
	if (s == "mark")  { return CueKind::Mark; }
	if (s == "call")  { return CueKind::Call; }
	if (s == "anim")  { return CueKind::Anim; }
	return std::nullopt;
}

[[nodiscard]] inline Cue makeCue(CueKind kind, std::string name, std::string actor = {}, float value = 1.0f,
                                 float duration = 0.0f)
{
	Cue c;
	c.kind = kind;
	c.nameId = name.empty() ? 0u : nameHash(name);
	c.actorId = actor.empty() ? 0u : nameHash(actor);
	c.name = std::move(name);
	c.actor = std::move(actor);
	c.value = value;
	c.duration = duration;
	return c;
}

}  // namespace mitiru::narrative
