#pragma once

/// @file WaveFunctionCollapse.hpp
/// @brief 波動関数崩壊（WFC）アルゴリズムによるタイルマップ生成
///
/// 隣接ルールと重み付きタイルセットからグリッドを自動生成する。
/// 各セルの可能なタイル集合を制約伝播により絞り込み、
/// 最小エントロピーのセルから順に確定させる。
///
/// @code
/// sgc::procedural::WFCConfig config;
/// config.width = 10;
/// config.height = 10;
/// config.tileSet = {0, 1, 2};
/// config.rules = {{0, 1, 0}, {1, 0, 2}};
/// auto result = sgc::procedural::solveWFC(config);
/// if (result.success) { /* result.grid を使用 */ }
/// @endcode

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sgc::procedural
{

/// @brief タイルID型
using TileId = uint16_t;

/// @brief WFCの隣接ルール
///
/// tileAの指定方向にtileBを配置可能であることを示す。
/// 逆方向のルールは自動的には生成されないため、
/// 双方向の隣接を許可する場合は両方向のルールを追加する必要がある。
struct WFCAdjacency
{
	TileId tileA;    ///< 基準タイル
	TileId tileB;    ///< 隣接タイル
	int direction;   ///< 方向（0=右, 1=上, 2=左, 3=下）
};

/// @brief WFC設定
struct WFCConfig
{
	int width = 10;                       ///< グリッド幅
	int height = 10;                      ///< グリッド高さ
	std::vector<TileId> tileSet;          ///< 使用可能なタイル一覧
	std::vector<WFCAdjacency> rules;      ///< 隣接ルール一覧
	std::vector<float> weights;           ///< タイル別の重み（空=均一）
	uint32_t seed = 0;                    ///< 乱数シード
	int maxIterations = 10000;            ///< 最大反復回数（無限ループ防止）
};

/// @brief WFC結果
struct WFCResult
{
	bool success = false;                 ///< 生成成功フラグ
	std::vector<TileId> grid;             ///< 生成されたグリッド（width * height, 行優先）
	int width = 0;                        ///< グリッド幅
	int height = 0;                       ///< グリッド高さ
};

namespace detail
{

/// @brief 方向の逆を取得する
/// @param dir 方向（0=右, 1=上, 2=左, 3=下）
/// @return 逆方向
[[nodiscard]] constexpr int oppositeDirection(int dir) noexcept
{
	return (dir + 2) % 4;
}

/// @brief 方向に応じた隣接セルのオフセットを取得する
/// @param dir 方向
/// @param[out] dx X方向オフセット
/// @param[out] dy Y方向オフセット
constexpr void directionOffset(int dir, int& dx, int& dy) noexcept
{
	// 0=右, 1=上, 2=左, 3=下
	constexpr int DX[] = {1, 0, -1, 0};
	constexpr int DY[] = {0, -1, 0, 1};
	dx = DX[dir];
	dy = DY[dir];
}

/// @brief 隣接ルックアップテーブル型
///
/// adjacency[方向][tileA] = {tileAの方向dirに配置可能なタイル集合}
using AdjacencyLookup = std::unordered_map<int,
	std::unordered_map<TileId, std::unordered_set<TileId>>>;

/// @brief 隣接ルールからルックアップテーブルを構築する
/// @param rules 隣接ルール一覧
/// @return ルックアップテーブル
[[nodiscard]] inline AdjacencyLookup buildAdjacencyLookup(
	const std::vector<WFCAdjacency>& rules)
{
	AdjacencyLookup lookup;
	for (const auto& rule : rules)
	{
		lookup[rule.direction][rule.tileA].insert(rule.tileB);
	}
	return lookup;
}

/// @brief セルのエントロピー（残り候補数）を計算する
/// @param domain セルの候補タイル集合
/// @return エントロピー値
[[nodiscard]] inline int entropy(const std::vector<bool>& domain, int tileCount) noexcept
{
	int count = 0;
	for (int i = 0; i < tileCount; ++i)
	{
		if (domain[static_cast<size_t>(i)])
		{
			++count;
		}
	}
	return count;
}

} // namespace detail

/// @brief 波動関数崩壊アルゴリズムでタイルグリッドを生成する
///
/// 隣接制約と重みに基づいてグリッドの各セルにタイルを割り当てる。
/// 最小エントロピーのセルを選択し崩壊させ、制約伝播を繰り返す。
///
/// @param config WFC設定
/// @return 生成結果（成功/失敗とグリッドデータ）
[[nodiscard]] inline WFCResult solveWFC(const WFCConfig& config)
{
	WFCResult result;
	result.width = config.width;
	result.height = config.height;

	const int cellCount = config.width * config.height;
	const int tileCount = static_cast<int>(config.tileSet.size());

	// タイルセットが空の場合は失敗
	if (tileCount == 0 || cellCount <= 0)
	{
		return result;
	}

	// タイルIDからインデックスへのマッピング
	std::unordered_map<TileId, int> tileToIndex;
	for (int i = 0; i < tileCount; ++i)
	{
		tileToIndex[config.tileSet[static_cast<size_t>(i)]] = i;
	}

	// 重みの準備（未指定なら均一）
	std::vector<float> weights(static_cast<size_t>(tileCount), 1.0f);
	if (!config.weights.empty())
	{
		const int copyCount = std::min(tileCount, static_cast<int>(config.weights.size()));
		for (int i = 0; i < copyCount; ++i)
		{
			weights[static_cast<size_t>(i)] = config.weights[static_cast<size_t>(i)];
		}
	}

	// 隣接ルックアップテーブルを構築
	const auto adjacency = detail::buildAdjacencyLookup(config.rules);

	// 各セルのドメイン（候補タイル集合）を初期化
	// domain[cellIndex][tileIndex] = そのタイルが候補に残っているか
	std::vector<std::vector<bool>> domains(
		static_cast<size_t>(cellCount),
		std::vector<bool>(static_cast<size_t>(tileCount), true));

	std::mt19937 rng(config.seed);

	// メインループ
	for (int iteration = 0; iteration < config.maxIterations; ++iteration)
	{
		// 1. 最小エントロピーのセルを探す（候補が2以上のセル）
		int minEntropy = std::numeric_limits<int>::max();
		int minCell = -1;

		for (int i = 0; i < cellCount; ++i)
		{
			const int ent = detail::entropy(domains[static_cast<size_t>(i)], tileCount);
			if (ent == 0)
			{
				// 矛盾：候補が0のセルが存在
				return result;
			}
			if (ent > 1 && ent < minEntropy)
			{
				minEntropy = ent;
				minCell = i;
			}
		}

		// 全セルが確定済み → 成功
		if (minCell == -1)
		{
			result.success = true;
			result.grid.resize(static_cast<size_t>(cellCount));
			for (int i = 0; i < cellCount; ++i)
			{
				for (int t = 0; t < tileCount; ++t)
				{
					if (domains[static_cast<size_t>(i)][static_cast<size_t>(t)])
					{
						result.grid[static_cast<size_t>(i)] = config.tileSet[static_cast<size_t>(t)];
						break;
					}
				}
			}
			return result;
		}

		// 2. 最小エントロピーセルを崩壊させる（重み付きランダム選択）
		auto& domain = domains[static_cast<size_t>(minCell)];
		float totalWeight = 0.0f;
		for (int t = 0; t < tileCount; ++t)
		{
			if (domain[static_cast<size_t>(t)])
			{
				totalWeight += weights[static_cast<size_t>(t)];
			}
		}

		std::uniform_real_distribution<float> dist(0.0f, totalWeight);
		float roll = dist(rng);
		int chosenTile = -1;

		for (int t = 0; t < tileCount; ++t)
		{
			if (domain[static_cast<size_t>(t)])
			{
				roll -= weights[static_cast<size_t>(t)];
				if (roll <= 0.0f)
				{
					chosenTile = t;
					break;
				}
			}
		}

		// フォールバック：丸め誤差対策
		if (chosenTile == -1)
		{
			for (int t = tileCount - 1; t >= 0; --t)
			{
				if (domain[static_cast<size_t>(t)])
				{
					chosenTile = t;
					break;
				}
			}
		}

		// セルを崩壊させる
		for (int t = 0; t < tileCount; ++t)
		{
			domain[static_cast<size_t>(t)] = (t == chosenTile);
		}

		// 3. 制約伝播（BFS）
		std::queue<int> propagateQueue;
		propagateQueue.push(minCell);

		while (!propagateQueue.empty())
		{
			const int current = propagateQueue.front();
			propagateQueue.pop();

			const int cx = current % config.width;
			const int cy = current / config.width;

			// 4方向の隣接セルを確認
			for (int dir = 0; dir < 4; ++dir)
			{
				int dx = 0;
				int dy = 0;
				detail::directionOffset(dir, dx, dy);
				const int nx = cx + dx;
				const int ny = cy + dy;

				// グリッド範囲外チェック
				if (nx < 0 || nx >= config.width || ny < 0 || ny >= config.height)
				{
					continue;
				}

				const int neighborIdx = ny * config.width + nx;
				auto& neighborDomain = domains[static_cast<size_t>(neighborIdx)];

				// 隣接セルの候補を絞り込む
				bool changed = false;
				const auto& currentDomain = domains[static_cast<size_t>(current)];

				// 方向dirの隣接ルールを取得
				const auto dirIt = adjacency.find(dir);

				for (int nt = 0; nt < tileCount; ++nt)
				{
					if (!neighborDomain[static_cast<size_t>(nt)])
					{
						continue;
					}

					// 現在のセルの候補タイルのいずれかが、
					// 方向dirにタイルntを許可するか確認
					bool supported = false;
					const TileId neighborTileId = config.tileSet[static_cast<size_t>(nt)];

					for (int ct = 0; ct < tileCount; ++ct)
					{
						if (!currentDomain[static_cast<size_t>(ct)])
						{
							continue;
						}

						const TileId currentTileId = config.tileSet[static_cast<size_t>(ct)];

						if (dirIt != adjacency.end())
						{
							const auto tileIt = dirIt->second.find(currentTileId);
							if (tileIt != dirIt->second.end())
							{
								if (tileIt->second.count(neighborTileId) > 0)
								{
									supported = true;
									break;
								}
							}
						}
					}

					// ルールが空（この方向にルールが定義されていない）場合は制約なし
					if (dirIt == adjacency.end())
					{
						supported = true;
					}

					if (!supported)
					{
						neighborDomain[static_cast<size_t>(nt)] = false;
						changed = true;
					}
				}

				// 変更があれば伝播キューに追加
				if (changed)
				{
					// 矛盾チェック
					const int ent = detail::entropy(neighborDomain, tileCount);
					if (ent == 0)
					{
						return result;
					}
					propagateQueue.push(neighborIdx);
				}
			}
		}
	}

	// 最大反復回数到達
	return result;
}

} // namespace sgc::procedural
