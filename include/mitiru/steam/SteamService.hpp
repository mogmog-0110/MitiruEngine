#pragma once

/// @file SteamService.hpp
/// @brief Steamworks (実績・統計・Steam Cloud・リッチプレゼンス・オーバーレイ) の host 側の窓口。
/// @details SDK は Valve の利用規約の下にあり、リポジトリにも公開スナップショットにも入れない。
///          -DMITIRU_WITH_STEAMWORKS=ON で configure し、external/steamworks_sdk/ に SDK が置かれていれば
///          MITIRU_HAS_STEAMWORKS が立ち、本物の実装になる (置き方は tools/fetch_steamworks.py)。
///          無ければ全部が「Steam が無い」と答えるだけの実装になり、同じコードがそのまま通る。
///          ゲーム DLL からは直接呼ばない。host が intent を受けて呼ぶ (docs/STEAMWORKS.md)。

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/save/SaveSlots.hpp>

namespace mitiru::steam
{

struct SteamConfig
{
	std::uint32_t appId = 0;          ///< 0 = steam_appid.txt (exe の隣) に任せる
	bool restartThroughSteam = false; ///< Steam 以外から起動されたら Steam 経由で起動し直す (出荷版だけ)
};

enum class SteamInitResult : std::uint8_t
{
	Ready,
	NotBuilt,          ///< MITIRU_WITH_STEAMWORKS なしでビルドした
	SteamNotRunning,   ///< SDK はあるが Steam クライアントに繋がらない
	RestartRequired,   ///< Steam 経由で起動し直すので、host はすぐ終わる
};

class SteamService
{
public:
	SteamService();
	~SteamService();
	SteamService(const SteamService&) = delete;
	SteamService& operator=(const SteamService&) = delete;

	SteamInitResult init(const SteamConfig& config);
	void shutdown();
	[[nodiscard]] bool available() const noexcept;

	/// 毎フレーム 1 回。コールバック (オーバーレイの開閉など) をここで受け取る。
	void runCallbacks();
	/// オーバーレイ (Shift+Tab) が開いている間 true。host はこの間ゲームを止める。
	[[nodiscard]] bool overlayActive() const noexcept;

	bool unlockAchievement(std::string_view id);
	bool clearAchievement(std::string_view id);
	[[nodiscard]] bool achievementUnlocked(std::string_view id) const;
	bool setStat(std::string_view name, std::int32_t value);
	bool setStat(std::string_view name, float value);
	[[nodiscard]] std::optional<std::int32_t> statInt(std::string_view name) const;
	[[nodiscard]] std::optional<float> statFloat(std::string_view name) const;
	/// 実績と統計の変更をサーバーへ送る。変えるたびではなく、区切り (章の終わりなど) で呼ぶ。
	bool storeStats();

	bool setRichPresence(std::string_view key, std::string_view value);
	void clearRichPresence();

	[[nodiscard]] bool cloudEnabled() const;
	bool cloudWrite(std::string_view name, std::span<const std::uint8_t> bytes);
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> cloudRead(std::string_view name) const;
	bool cloudDelete(std::string_view name);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

/// @brief セーブスロットを Steam Cloud へ写す。Steam が無ければ何もしない。
class SteamCloudMirror final : public save::ISaveMirror
{
public:
	explicit SteamCloudMirror(SteamService& steam) : m_steam(steam) {}

	void pushSlot(std::string_view fileName, std::span<const std::uint8_t> bytes) override
	{
		if (m_steam.cloudEnabled()) { (void)m_steam.cloudWrite(fileName, bytes); }
	}
	void removeSlot(std::string_view fileName) override
	{
		if (m_steam.cloudEnabled()) { (void)m_steam.cloudDelete(fileName); }
	}
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> pullSlot(std::string_view fileName) override
	{
		if (!m_steam.cloudEnabled()) { return std::nullopt; }
		return m_steam.cloudRead(fileName);
	}

private:
	SteamService& m_steam;
};

}  // namespace mitiru::steam

#if defined(MITIRU_HAS_STEAMWORKS)
#include <mitiru/steam/detail/SteamServiceSteamworks.hpp>
#else
#include <mitiru/steam/detail/SteamServiceStub.hpp>
#endif
