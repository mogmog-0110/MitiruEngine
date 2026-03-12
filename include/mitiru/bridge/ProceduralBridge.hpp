#pragma once

/// @file ProceduralBridge.hpp
/// @brief sgc プロシージャル生成統合ブリッジ
/// @details sgcのダンジョン生成・地形生成・WFC・Lシステムを
///          Mitiruエンジンに統合する。

#include <cstdint>
#include <string>

#include <sgc/procedural/DungeonGenerator.hpp>
#include <sgc/procedural/TerrainGenerator.hpp>
#include <sgc/procedural/WaveFunctionCollapse.hpp>
#include <sgc/procedural/LSystem.hpp>

namespace mitiru::bridge
{

/// @brief sgc プロシージャル生成統合ブリッジ
/// @details ダンジョン・地形・WFC・Lシステムの生成機能を提供する。
///          ステートレスな生成関数のラッパーとして機能する。
///
/// @code
/// mitiru::bridge::ProceduralBridge proc;
///
/// sgc::procedural::DungeonConfig dc;
/// dc.mapWidth = 80;
/// dc.mapHeight = 50;
/// auto dungeon = proc.generateDungeon(dc, 42);
///
/// sgc::procedural::TerrainConfig tc;
/// auto terrain = proc.generateTerrain(tc, 0);
/// @endcode
class ProceduralBridge
{
public:
	// ── ダンジョン生成 ────────────────────────────────────────

	/// @brief BSPアルゴリズムでダンジョンを生成する
	/// @param config 生成設定
	/// @param seed 乱数シード
	/// @return 生成結果（部屋、通路、タイルグリッド）
	[[nodiscard]] sgc::procedural::DungeonResult generateDungeon(
		const sgc::procedural::DungeonConfig& config, uint32_t seed = 0)
	{
		return sgc::procedural::generateDungeon(config, seed);
	}

	// ── 地形生成 ──────────────────────────────────────────────

	/// @brief ノイズベースの地形を生成する
	/// @param config 地形設定
	/// @param seed 乱数シード（configのseedを上書きする）
	/// @return 地形生成結果
	[[nodiscard]] sgc::procedural::TerrainResult generateTerrain(
		const sgc::procedural::TerrainConfig& config, uint32_t seed = 0)
	{
		sgc::procedural::TerrainConfig cfg = config;
		cfg.seed = seed;
		return sgc::procedural::generateTerrain(cfg);
	}

	// ── 波動関数崩壊 ─────────────────────────────────────────

	/// @brief WFCアルゴリズムでタイルグリッドを生成する
	/// @param config WFC設定
	/// @return 生成結果
	[[nodiscard]] sgc::procedural::WFCResult solveWFC(const sgc::procedural::WFCConfig& config)
	{
		return sgc::procedural::solveWFC(config);
	}

	// ── Lシステム ─────────────────────────────────────────────

	/// @brief Lシステム文字列を生成する
	/// @param config Lシステム設定
	/// @return 生成された文字列
	[[nodiscard]] std::string generateLSystem(const sgc::procedural::LSystemConfig& config)
	{
		return sgc::procedural::generateLSystem(config);
	}

	/// @brief Lシステム文字列をタートルグラフィクスとして解釈する
	/// @param lString Lシステム文字列
	/// @param config タートル設定
	/// @return 生成された線分データ
	[[nodiscard]] sgc::procedural::TurtleResult interpretTurtle(
		const std::string& lString, const sgc::procedural::TurtleConfig& config)
	{
		return sgc::procedural::interpretTurtle(lString, config);
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief プロシージャル生成ブリッジの情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		return R"({"type":"ProceduralBridge","capabilities":["dungeon","terrain","wfc","lsystem"]})";
	}
};

} // namespace mitiru::bridge
