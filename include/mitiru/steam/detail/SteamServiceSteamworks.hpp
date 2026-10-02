#pragma once
// SteamService.hpp の detail。Steamworks SDK (1.61 以降) を使う実装。MITIRU_HAS_STEAMWORKS の時だけ読む。
// 1.61 から統計は SteamAPI_Init の時点で読み込まれるので、RequestCurrentStats は呼ばない。

#include <steam/steam_api.h>

#include <string>

namespace mitiru::steam
{

struct SteamService::Impl
{
	bool ready = false;
	bool overlay = false;

	STEAM_CALLBACK(Impl, onOverlay, GameOverlayActivated_t);
};

inline void SteamService::Impl::onOverlay(GameOverlayActivated_t* p)
{
	overlay = p != nullptr && p->m_bActive != 0;
}

inline SteamService::SteamService() = default;
inline SteamService::~SteamService() { shutdown(); }

inline SteamInitResult SteamService::init(const SteamConfig& config)
{
	if (config.restartThroughSteam && config.appId != 0 && SteamAPI_RestartAppIfNecessary(config.appId))
	{
		return SteamInitResult::RestartRequired;
	}
	if (!SteamAPI_Init()) { return SteamInitResult::SteamNotRunning; }
	// コールバックの登録は SteamAPI_Init の後でないと効かない
	m_impl = std::make_unique<Impl>();
	m_impl->ready = true;
	return SteamInitResult::Ready;
}

inline void SteamService::shutdown()
{
	if (!m_impl) { return; }
	m_impl.reset();
	SteamAPI_Shutdown();
}

inline bool SteamService::available() const noexcept { return m_impl && m_impl->ready; }

inline void SteamService::runCallbacks()
{
	if (available()) { SteamAPI_RunCallbacks(); }
}

inline bool SteamService::overlayActive() const noexcept { return available() && m_impl->overlay; }

inline bool SteamService::unlockAchievement(std::string_view id)
{
	return available() && SteamUserStats()->SetAchievement(std::string(id).c_str());
}

inline bool SteamService::clearAchievement(std::string_view id)
{
	return available() && SteamUserStats()->ClearAchievement(std::string(id).c_str());
}

inline bool SteamService::achievementUnlocked(std::string_view id) const
{
	bool achieved = false;
	return available() && SteamUserStats()->GetAchievement(std::string(id).c_str(), &achieved) && achieved;
}

inline bool SteamService::setStat(std::string_view name, std::int32_t value)
{
	return available() && SteamUserStats()->SetStat(std::string(name).c_str(), static_cast<int32>(value));
}

inline bool SteamService::setStat(std::string_view name, float value)
{
	return available() && SteamUserStats()->SetStat(std::string(name).c_str(), value);
}

inline std::optional<std::int32_t> SteamService::statInt(std::string_view name) const
{
	int32 v = 0;
	if (!available() || !SteamUserStats()->GetStat(std::string(name).c_str(), &v)) { return std::nullopt; }
	return static_cast<std::int32_t>(v);
}

inline std::optional<float> SteamService::statFloat(std::string_view name) const
{
	float v = 0.0f;
	if (!available() || !SteamUserStats()->GetStat(std::string(name).c_str(), &v)) { return std::nullopt; }
	return v;
}

inline bool SteamService::storeStats() { return available() && SteamUserStats()->StoreStats(); }

inline bool SteamService::setRichPresence(std::string_view key, std::string_view value)
{
	return available() && SteamFriends()->SetRichPresence(std::string(key).c_str(), std::string(value).c_str());
}

inline void SteamService::clearRichPresence()
{
	if (available()) { SteamFriends()->ClearRichPresence(); }
}

inline bool SteamService::cloudEnabled() const
{
	return available() && SteamRemoteStorage()->IsCloudEnabledForAccount() && SteamRemoteStorage()->IsCloudEnabledForApp();
}

inline bool SteamService::cloudWrite(std::string_view name, std::span<const std::uint8_t> bytes)
{
	return available() && SteamRemoteStorage()->FileWrite(std::string(name).c_str(), bytes.data(), static_cast<int32>(bytes.size()));
}

inline std::optional<std::vector<std::uint8_t>> SteamService::cloudRead(std::string_view name) const
{
	if (!available()) { return std::nullopt; }
	const std::string file(name);
	if (!SteamRemoteStorage()->FileExists(file.c_str())) { return std::nullopt; }
	const int32 size = SteamRemoteStorage()->GetFileSize(file.c_str());
	if (size <= 0) { return std::nullopt; }
	std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
	if (SteamRemoteStorage()->FileRead(file.c_str(), out.data(), size) != size) { return std::nullopt; }
	return out;
}

inline bool SteamService::cloudDelete(std::string_view name)
{
	return available() && SteamRemoteStorage()->FileDelete(std::string(name).c_str());
}

}  // namespace mitiru::steam
