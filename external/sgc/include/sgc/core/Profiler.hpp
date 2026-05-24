#pragma once

/// @file Profiler.hpp
/// @brief 軽量プロファイラ
///
/// コードセクションの実行時間を計測・統計集計する。
/// SGC_PROFILE_SCOPE / SGC_PROFILE_FUNCTION マクロでスコープ計測を簡単に利用できる。
///
/// @code
/// void update() {
///     SGC_PROFILE_FUNCTION();
///     {
///         SGC_PROFILE_SCOPE("Physics");
///         updatePhysics();
///     }
///     {
///         SGC_PROFILE_SCOPE("Render");
///         render();
///     }
/// }
///
/// for (auto& stat : sgc::Profiler::instance().allStats()) {
///     std::cout << stat.name << ": avg " << stat.averageMs << "ms\n";
/// }
/// @endcode

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc
{

/// @brief プロファイルセクションの統計情報
struct ProfileStats
{
	std::string name;       ///< セクション名
	double totalMs{0.0};    ///< 合計時間(ms)
	double averageMs{0.0};  ///< 平均時間(ms)
	double minMs{1e30};     ///< 最小時間(ms)
	double maxMs{0.0};      ///< 最大時間(ms)
	std::size_t callCount{0};  ///< 呼び出し回数
};

/// @brief 軽量プロファイラ（シングルトン）
///
/// セクション名をキーとして実行時間を蓄積し、統計情報を提供する。
class Profiler
{
public:
	using Clock = std::chrono::high_resolution_clock;

	/// @brief シングルトンインスタンスを取得する
	/// @return Profiler参照
	[[nodiscard]] static Profiler& instance()
	{
		static Profiler profiler;
		return profiler;
	}

	/// @brief セクション計測を開始する
	/// @param name セクション名
	void beginSection(const std::string& name)
	{
		std::scoped_lock lock(m_mutex);
		m_activeSections[name] = Clock::now();
	}

	/// @brief セクション計測を終了する
	/// @param name セクション名
	void endSection(const std::string& name)
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_activeSections.find(name);
		if (it == m_activeSections.end()) return;

		const auto elapsed = Clock::now() - it->second;
		const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
		m_activeSections.erase(it);

		auto& data = m_data[name];
		data.name = name;
		data.totalMs += ms;
		++data.callCount;
		data.averageMs = data.totalMs / static_cast<double>(data.callCount);
		if (ms < data.minMs) data.minMs = ms;
		if (ms > data.maxMs) data.maxMs = ms;
	}

	/// @brief 全セクションの統計情報を取得する
	/// @return 統計情報の配列
	[[nodiscard]] std::vector<ProfileStats> allStats() const
	{
		std::scoped_lock lock(m_mutex);
		std::vector<ProfileStats> result;
		result.reserve(m_data.size());
		for (const auto& [name, stats] : m_data)
		{
			result.push_back(stats);
		}
		return result;
	}

	/// @brief 指定セクションの統計情報を取得する
	/// @param name セクション名
	/// @return 統計情報（存在しなければデフォルト値）
	[[nodiscard]] ProfileStats stats(const std::string& name) const
	{
		std::scoped_lock lock(m_mutex);
		const auto it = m_data.find(name);
		if (it != m_data.end()) return it->second;
		return ProfileStats{name};
	}

	/// @brief 全データをリセットする
	void reset()
	{
		std::scoped_lock lock(m_mutex);
		m_data.clear();
		m_activeSections.clear();
	}

private:
	Profiler() = default;

	mutable std::mutex m_mutex;  ///< スレッド安全性のためのミューテックス
	std::unordered_map<std::string, ProfileStats> m_data;          ///< 統計データ
	std::unordered_map<std::string, Clock::time_point> m_activeSections; ///< 計測中セクション
};

/// @brief スコープベースのプロファイル計測（RAIIガード）
class ScopedProfile
{
public:
	/// @brief 計測を開始する
	/// @param name セクション名
	explicit ScopedProfile(const std::string& name)
		: m_name(name)
	{
		Profiler::instance().beginSection(m_name);
	}

	/// @brief 計測を終了する
	~ScopedProfile()
	{
		Profiler::instance().endSection(m_name);
	}

	ScopedProfile(const ScopedProfile&) = delete;
	ScopedProfile& operator=(const ScopedProfile&) = delete;

private:
	std::string m_name;  ///< セクション名
};

} // namespace sgc

/// @brief スコープ内の実行時間を計測するマクロ
/// @param name セクション名
#define SGC_PROFILE_SCOPE(name) \
	sgc::ScopedProfile sgcProfileScope##__LINE__(name)

/// @brief 関数全体の実行時間を計測するマクロ
#define SGC_PROFILE_FUNCTION() \
	SGC_PROFILE_SCOPE(__func__)
