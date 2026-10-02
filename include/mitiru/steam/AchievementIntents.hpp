#pragma once

/// @file AchievementIntents.hpp
/// @brief ゲーム DLL の実績の依頼 (FrameIntents::achievements、ABI v48) を SteamService へ渡す
/// @details Steam は host が持ち、DLL は名前で頼むだけ (ADR 0005)。Steam の無い host でも依頼はそのまま捨てて
///          ゲームの進行は変わらない。テストでは同じ形の偽物を渡せるよう、受け手は型引数にする。

#include <algorithm>
#include <string_view>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::steam
{

template <class Steam>
bool applyAchievementIntent(Steam& steam, const module::AchievementIntent& a)
{
	const std::string_view id{a.id, static_cast<std::size_t>(std::find(a.id, a.id + sizeof(a.id), '\0') - a.id)};
	const std::string_view text{a.text, static_cast<std::size_t>(std::find(a.text, a.text + sizeof(a.text), '\0') - a.text)};
	switch (a.kind)
	{
	case module::kAchievementUnlock:        return steam.unlockAchievement(id);
	case module::kAchievementClear:         return steam.clearAchievement(id);
	case module::kAchievementStatInt:       return steam.setStat(id, a.intValue);
	case module::kAchievementStatFloat:     return steam.setStat(id, a.floatValue);
	case module::kAchievementStore:         return steam.storeStats();
	case module::kAchievementPresence:      return steam.setRichPresence(id, text);
	case module::kAchievementClearPresence: steam.clearRichPresence(); return true;
	default:                                return false;
	}
}

/// @return 受け手が受け付けた依頼の数
template <class Steam>
int applyAchievementIntents(Steam& steam, const module::FrameIntents& intents)
{
	const int n = std::clamp(intents.achievementCount, 0, module::kMaxAchievementIntents);
	int accepted = 0;
	for (int i = 0; i < n; ++i) { accepted += applyAchievementIntent(steam, intents.achievements[i]) ? 1 : 0; }
	return accepted;
}

}  // namespace mitiru::steam
