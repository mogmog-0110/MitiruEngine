#pragma once
// SteamService.hpp の detail。MITIRU_HAS_STEAMWORKS が無いビルドの実装 (どの問いにも「Steam は無い」と答える)。

namespace mitiru::steam
{

struct SteamService::Impl {};

inline SteamService::SteamService() = default;
inline SteamService::~SteamService() = default;
inline SteamInitResult SteamService::init(const SteamConfig&) { return SteamInitResult::NotBuilt; }
inline void SteamService::shutdown() {}
inline bool SteamService::available() const noexcept { return false; }
inline void SteamService::runCallbacks() {}
inline bool SteamService::overlayActive() const noexcept { return false; }
inline bool SteamService::unlockAchievement(std::string_view) { return false; }
inline bool SteamService::clearAchievement(std::string_view) { return false; }
inline bool SteamService::achievementUnlocked(std::string_view) const { return false; }
inline bool SteamService::setStat(std::string_view, std::int32_t) { return false; }
inline bool SteamService::setStat(std::string_view, float) { return false; }
inline std::optional<std::int32_t> SteamService::statInt(std::string_view) const { return std::nullopt; }
inline std::optional<float> SteamService::statFloat(std::string_view) const { return std::nullopt; }
inline bool SteamService::storeStats() { return false; }
inline bool SteamService::setRichPresence(std::string_view, std::string_view) { return false; }
inline void SteamService::clearRichPresence() {}
inline bool SteamService::cloudEnabled() const { return false; }
inline bool SteamService::cloudWrite(std::string_view, std::span<const std::uint8_t>) { return false; }
inline std::optional<std::vector<std::uint8_t>> SteamService::cloudRead(std::string_view) const { return std::nullopt; }
inline bool SteamService::cloudDelete(std::string_view) { return false; }

}  // namespace mitiru::steam
