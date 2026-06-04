#pragma once

/// @file LODSystem.hpp
/// @brief Level-of-Detail メッシュシステム
/// @details カメラ距離に応じてメッシュの詳細度を自動切り替えする。
///          ヒステリシス付きのLODレベル選択と、クロスフェード用ブレンド係数の
///          算出をサポートする。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#endif // _WIN32

namespace mitiru::render
{

/// @brief LODレベルに紐づくメッシュ情報
/// @details 頂点バッファ・インデックスバッファ・インデックス数と
///          このLODが適用される最大距離を保持する。
struct LODLevel
{
#ifdef _WIN32
	Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;   ///< 頂点バッファ
	Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;     ///< インデックスバッファ
#endif
	std::uint32_t indexCount = 0;     ///< インデックス数
	float maxDistance = 0.0f;         ///< このLODが使われる最大距離
	float transitionRange = 5.0f;     ///< クロスフェード遷移範囲（メートル）
};

/// @brief LODグループ（同一オブジェクトタイプのLODレベル集合）
/// @details 距離順にソートされたLODレベルのリストと現在のアクティブレベルを保持する。
struct LODGroup
{
	std::vector<LODLevel> levels;     ///< LODレベル（距離の昇順でソート済み）
	std::uint32_t currentLevel = 0;   ///< 現在のアクティブレベルインデックス
};

/// @brief スムースLOD選択結果
/// @details 選択されたLODレベルとクロスフェード用ブレンド係数を返す。
struct LODSelection
{
	std::uint32_t level = 0;          ///< 選択されたLODレベルインデックス
	float blendFactor = 0.0f;         ///< ブレンド係数（0.0 = 現在レベル、1.0 = 次レベル）
};

/// @brief LODシステム設定
struct LODConfig
{
	float bias = 0.0f;               ///< 距離バイアス（正: LODを遠くにシフト）
	std::uint32_t maxLevel = 0;      ///< 最大LODレベル（0 = 制限なし）
	float crossFadeDuration = 0.5f;  ///< クロスフェード遷移時間（秒）
	float hysteresis = 2.0f;         ///< ヒステリシス距離（チャタリング防止）
};

/// @brief オブジェクトのLOD状態（バッチ更新用）
struct LODObject
{
	std::string groupId;             ///< LODグループID
	float position[3] = {};          ///< オブジェクトのワールド位置
	std::uint32_t currentLevel = 0;  ///< 現在のLODレベル
	/// @brief 対応する描画オブジェクトの nodeId（`Scene3D::RenderObject::nodeId` と一致させる）。
	/// @details これで LOD 判定結果を描画ループへ橋渡しできる。-1 = 未対応（橋渡し対象外）。
	int nodeId = -1;
	/// @brief カリング出力。`update()` が最低LODより遠いと判定したら true。
	/// @details レンダラはこれを読んで描画をスキップする（true なら描かない）。
	///          毎フレーム `update()` が再計算するので呼び出し側でのリセット不要。
	bool culled = false;
};

/// @brief カリングされた LODObject の nodeId 集合を集める（描画ループへ渡す橋渡し）。
/// @details `LODManager::update()` 後に呼び、`DeferredPipeline::render(..., &culled)` に渡すと
///          遠方オブジェクトの描画をスキップできる。nodeId<0（未対応）は除外。
[[nodiscard]] inline std::unordered_set<int> collectCulledNodeIds(
	const std::vector<LODObject>& objects)
{
	std::unordered_set<int> out;
	for (const auto& o : objects)
	{
		if (o.culled && o.nodeId >= 0) { out.insert(o.nodeId); }
	}
	return out;
}

/// @brief LODマネージャー
/// @details カメラ距離に応じて最適なLODレベルを選択する。
///          ヒステリシスによるチャタリング防止と、クロスフェード用の
///          ブレンド係数算出をサポートする。
///
/// @code
/// mitiru::render::LODManager lodManager;
/// lodManager.setConfig({.bias = 0.0f, .hysteresis = 2.0f});
///
/// // LODグループを登録する
/// std::vector<mitiru::render::LODLevel> levels = {
///     {vbHigh, ibHigh, 3000, 50.0f, 5.0f},
///     {vbMid,  ibMid,  1500, 100.0f, 5.0f},
///     {vbLow,  ibLow,   500, 500.0f, 5.0f},
/// };
/// lodManager.registerGroup("tree", levels);
///
/// // 毎フレーム: LOD選択
/// auto& lod = lodManager.selectLOD("tree", cameraPos, objPos);
/// @endcode
class LODManager
{
public:
	/// @brief デフォルトコンストラクタ
	LODManager() noexcept = default;

	/// @brief 設定を取得する
	[[nodiscard]] const LODConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief 設定を変更する
	/// @param cfg 新しい設定
	void setConfig(const LODConfig& cfg) noexcept
	{
		m_config = cfg;
	}

	/// @brief LODグループを登録する
	/// @param id グループID
	/// @param levels LODレベルの配列（距離の昇順でソートされる）
	/// @throws std::invalid_argument levelsが空の場合
	void registerGroup(const std::string& id,
	                   std::vector<LODLevel> levels)
	{
		if (levels.empty())
		{
			throw std::invalid_argument(
				"LODManager::registerGroup: levels must not be empty");
		}

		/// 距離の昇順にソートする
		std::sort(levels.begin(), levels.end(),
			[](const LODLevel& a, const LODLevel& b)
			{
				return a.maxDistance < b.maxDistance;
			});

		LODGroup group;
		group.levels = std::move(levels);
		group.currentLevel = 0;

		m_groups[id] = std::move(group);
	}

	/// @brief グループが登録されているかを確認する
	/// @param id グループID
	/// @return 登録済みならtrue
	[[nodiscard]] bool hasGroup(const std::string& id) const
	{
		return m_groups.find(id) != m_groups.end();
	}

	/// @brief 登録済みグループを取得する
	/// @param id グループID
	/// @return LODグループの定数参照
	/// @throws std::out_of_range グループが存在しない場合
	[[nodiscard]] const LODGroup& getGroup(const std::string& id) const
	{
		auto it = m_groups.find(id);
		if (it == m_groups.end())
		{
			throw std::out_of_range(
				"LODManager::getGroup: group not found: " + id);
		}
		return it->second;
	}

	/// @brief カメラ距離に応じてLODレベルを選択する（グループ状態を変更しない）
	/// @param groupId グループID
	/// @param cameraPos カメラ位置（float[3]）
	/// @param objectPos オブジェクト位置（float[3]）
	/// @return 選択されたLODレベルの定数参照
	/// @throws std::out_of_range グループが存在しない場合
	[[nodiscard]] const LODLevel& selectLOD(
		const std::string& groupId,
		const float cameraPos[3],
		const float objectPos[3]) const
	{
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
		{
			throw std::out_of_range(
				"LODManager::selectLOD: group not found: " + groupId);
		}

		const auto& group = it->second;
		const float dist = computeDistance(cameraPos, objectPos)
		                  - m_config.bias;

		auto level = selectLevelWithHysteresis(
			group, dist, group.currentLevel);

		/// maxLevelで制限する
		if (m_config.maxLevel > 0
		    && level >= m_config.maxLevel)
		{
			level = m_config.maxLevel - 1;
		}

		return group.levels[level];
	}

	/// @brief スムースLOD選択（クロスフェード用ブレンド係数付き）
	/// @param groupId グループID
	/// @param cameraPos カメラ位置（float[3]）
	/// @param objectPos オブジェクト位置（float[3]）
	/// @return LODレベルとブレンド係数
	/// @throws std::out_of_range グループが存在しない場合
	[[nodiscard]] LODSelection selectLODSmooth(
		const std::string& groupId,
		const float cameraPos[3],
		const float objectPos[3])
	{
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
		{
			throw std::out_of_range(
				"LODManager::selectLODSmooth: group not found: " + groupId);
		}

		auto& group = it->second;
		const float dist = computeDistance(cameraPos, objectPos)
		                  - m_config.bias;
		const auto prevLevel = group.currentLevel;

		group.currentLevel = selectLevelWithHysteresis(
			group, dist, prevLevel);

		if (m_config.maxLevel > 0
		    && group.currentLevel >= m_config.maxLevel)
		{
			group.currentLevel = m_config.maxLevel - 1;
		}

		if (group.currentLevel != prevLevel)
		{
			m_transitionCount++;
		}

		/// ブレンド係数を計算する
		LODSelection result;
		result.level = group.currentLevel;
		result.blendFactor = 0.0f;

		const auto& currentLod = group.levels[group.currentLevel];
		const float transRange = currentLod.transitionRange;

		if (transRange > 0.0f)
		{
			const float boundary = currentLod.maxDistance;
			const float distFromBoundary = boundary - dist;

			if (distFromBoundary >= 0.0f && distFromBoundary < transRange)
			{
				result.blendFactor = 1.0f - (distFromBoundary / transRange);
			}
		}

		return result;
	}

	/// @brief 全オブジェクトのLODを一括更新する
	/// @param cameraPos カメラ位置（float[3]）
	/// @param objects LODオブジェクトの配列（currentLevelが更新される）
	void update(const float cameraPos[3],
	            std::vector<LODObject>& objects)
	{
		m_drawCallsSaved = 0;

		for (auto& obj : objects)
		{
			auto it = m_groups.find(obj.groupId);
			if (it == m_groups.end())
			{
				continue;
			}

			auto& group = it->second;
			const float dist = computeDistance(cameraPos, obj.position)
			                  - m_config.bias;
			const auto prevLevel = obj.currentLevel;

			group.currentLevel = obj.currentLevel;
			obj.currentLevel = selectLevelWithHysteresis(
				group, dist, prevLevel);

			if (m_config.maxLevel > 0
			    && obj.currentLevel >= m_config.maxLevel)
			{
				obj.currentLevel = m_config.maxLevel - 1;
			}

			if (obj.currentLevel != prevLevel)
			{
				m_transitionCount++;
			}

			/// 最低LODより遠い場合はカリング対象とし、出力フラグに反映する。
			/// レンダラはこの obj.culled を読んで描画をスキップできる（従来は
			/// カウンタ加算のみでフラグが無く、遠方も最低LODで描画され続けていた）。
			const auto& lastLevel = group.levels.back();
			obj.culled = (dist > lastLevel.maxDistance);
			if (obj.culled)
			{
				m_drawCallsSaved++;
			}
		}
	}

	/// @brief LOD遷移回数を取得する
	[[nodiscard]] int lodTransitionCount() const noexcept
	{
		return m_transitionCount;
	}

	/// @brief カリングで節約されたドローコール数を取得する
	[[nodiscard]] int drawCallsSaved() const noexcept
	{
		return m_drawCallsSaved;
	}

	/// @brief 統計をリセットする
	void resetStats() noexcept
	{
		m_transitionCount = 0;
		m_drawCallsSaved = 0;
	}

private:
	/// @brief 2点間の距離を計算する
	/// @param a 点A（float[3]）
	/// @param b 点B（float[3]）
	/// @return ユークリッド距離
	[[nodiscard]] static float computeDistance(
		const float a[3], const float b[3]) noexcept
	{
		const float dx = a[0] - b[0];
		const float dy = a[1] - b[1];
		const float dz = a[2] - b[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	/// @brief ヒステリシス付きLODレベル選択
	/// @param group LODグループ
	/// @param distance カメラ-オブジェクト間距離
	/// @param currentLevel 現在のLODレベル
	/// @return 選択されたLODレベルインデックス
	[[nodiscard]] std::uint32_t selectLevelWithHysteresis(
		const LODGroup& group,
		float distance,
		std::uint32_t currentLevel) const noexcept
	{
		const auto levelCount =
			static_cast<std::uint32_t>(group.levels.size());

		if (levelCount == 0)
		{
			return 0;
		}

		/// 現在のレベルが範囲外なら補正する
		if (currentLevel >= levelCount)
		{
			currentLevel = levelCount - 1;
		}

		/// 基本的なレベル選択（距離に基づく）
		std::uint32_t targetLevel = levelCount - 1;
		for (std::uint32_t i = 0; i < levelCount; ++i)
		{
			if (distance <= group.levels[i].maxDistance)
			{
				targetLevel = i;
				break;
			}
		}

		/// ヒステリシスを適用する
		/// より詳細なLOD（低いインデックス）への遷移にはヒステリシス分近づく必要がある
		/// より粗いLOD（高いインデックス）への遷移にはヒステリシス分離れる必要がある
		if (targetLevel < currentLevel)
		{
			/// 詳細化：境界距離 - ヒステリシス 以下になるまで遷移しない
			const float boundary =
				group.levels[targetLevel].maxDistance;
			if (distance > boundary - m_config.hysteresis)
			{
				return currentLevel;
			}
		}
		else if (targetLevel > currentLevel)
		{
			/// 簡略化：境界距離 + ヒステリシス 以上になるまで遷移しない
			const float boundary =
				group.levels[currentLevel].maxDistance;
			if (distance < boundary + m_config.hysteresis)
			{
				return currentLevel;
			}
		}

		return targetLevel;
	}

	LODConfig m_config;  ///< LOD設定

	std::unordered_map<std::string, LODGroup> m_groups;  ///< 登録済みLODグループ

	/// 統計
	int m_transitionCount = 0;   ///< LOD遷移回数
	int m_drawCallsSaved = 0;    ///< カリングで節約されたドローコール数
};

} // namespace mitiru::render
