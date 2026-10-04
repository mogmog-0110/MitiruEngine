#pragma once

// mitiru_host の --preload / --stage / --bake-caches。資産の一覧 (resource/StagePlan.hpp の書式) を読み、最初のフレームと決めたフレームで
// Engine::switchStage に渡す。ゲームが自分で先読みを頼む入口 (Hud::preload) が開くまで、計測とロード画面の確かめに使う。
// --bake-caches は読み込みで作られる cache (FBX の glb、clod、材質の DDS、シェーダー) を配布の前に作り切る。

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/resource/StagePlan.hpp>
#include <mitiru/terrain/OutdoorRegion.hpp>

namespace mitiru::host
{

class HostStreaming
{
public:
	/// @brief --preload F。最初のフレームの前に読み、使い始めるパイプラインも作っておく
	bool setPreload(const std::string& file, std::string& error)
	{
		return readList(file, m_preload, error);
	}

	/// @brief --stage F@N。N フレーム目の頭でステージを F の一覧に切り替える
	bool addStage(const std::string& spec, std::string& error)
	{
		const auto at = spec.rfind('@');
		Stage s;
		try
		{
			if (at == std::string::npos || at + 1 >= spec.size()) { throw std::invalid_argument("no frame"); }
			s.frame = std::stoll(spec.substr(at + 1));
		}
		catch (const std::exception&)
		{
			error = "--stage " + spec + " を読めません。<list>@<frame> の形で書いてください。";
			return false;
		}
		if (!readList(spec.substr(0, at), s.entries, error)) { return false; }
		m_stages.push_back(std::move(s));
		return true;
	}

	/// @brief --bake-caches F。F の資産を全部読み (region は区画の world.json も)、変換の cache と DDS とシェーダーの cache を
	///        作らせてから、読めなかった資産を数えて止める。配布物に cache を入れる `mitiru dist` が使う
	bool setBake(const std::string& file, std::string& error)
	{
		std::vector<std::string> list;
		if (!readList(file, list, error)) { return false; }
		for (const auto& entry : list)
		{
			m_preload.push_back(entry);
			const auto [kind, path] = resource::splitAssetKind(entry);
			if (!kind.empty() || !terrain::isOutdoorRegionPath(path)) { continue; }
			if (const auto region = terrain::loadOutdoorRegion(path); region.region)
			{
				for (const auto& cell : region.region->cells) { m_preload.push_back(cell.world); }
			}
		}
		m_bake = true;
		return true;
	}

	[[nodiscard]] bool active() const noexcept { return !m_preload.empty() || !m_stages.empty(); }
	[[nodiscard]] int bakeFailures() const noexcept { return m_bakeFailures; }

	/// @brief onFrameStart から呼ぶ。frame は 1 始まり
	void onFrame(Engine& engine, long long frame)
	{
		if (frame == 1 && !m_preload.empty())
		{
			const auto n = engine.switchStage(m_preload);
			const int warmed = engine.prewarmPipelines();
			console::verbosef("preload: %zu assets, %d pipelines", n, warmed);
		}
		if (m_bake && frame == 2) { finishBake(engine); }
		for (const auto& s : m_stages)
		{
			if (s.frame != frame) { continue; }
			const auto n = engine.switchStage(s.entries);
			console::verbosef("stage switch at frame %lld: %zu assets", frame, n);
		}
	}

private:
	struct Stage
	{
		long long                frame = 0;
		std::vector<std::string> entries;
	};

	/// @brief 1 フレーム目の読み込みは 1 フレーム目の描画の前に全部終わっている (待つ読み込み)。読めなかった物を数えて止める
	void finishBake(Engine& engine)
	{
		engine.settleLoads();
		for (const auto& entry : m_preload)
		{
			if (resource::splitAssetKind(entry).kind == "sound" || engine.assetReady(entry)) { continue; }
			console::noticef("--bake-caches で読めない: %s", entry.c_str());
			++m_bakeFailures;
		}
		console::noticef("bake: %zu assets, %d failed", m_preload.size(), m_bakeFailures);
		engine.requestStop();
	}

	static bool readList(const std::string& file, std::vector<std::string>& out, std::string& error)
	{
		std::ifstream f(file, std::ios::binary);
		if (!f)
		{
			error = "資産の一覧 " + file + " を開けません。";
			return false;
		}
		std::stringstream ss;
		ss << f.rdbuf();
		out = resource::parseAssetList(ss.str());
		return true;
	}

	std::vector<std::string> m_preload;
	std::vector<Stage>       m_stages;
	bool                     m_bake = false;
	int                      m_bakeFailures = 0;
};

}  // namespace mitiru::host
