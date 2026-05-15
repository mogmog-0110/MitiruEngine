#pragma once

/// @file FlowChart.hpp
/// @brief ストーリーフローチャート（分岐可視化・チャプター選択）
/// @details シナリオの分岐構造をグラフで表現し、プレイヤーの進行状況を追跡する。
///          自動レイアウト、ノードの色分け、エンディング管理、ルート完了率、
///          スクリプトからのフローチャート自動生成をサポートする。
///
/// @code
/// mitiru::vn::FlowChart chart;
/// chart.addNode({"prologue", "Prologue", FlowNodeType::Scene, {100, 50}});
/// chart.addNode({"choice_01", "First Choice", FlowNodeType::Choice, {100, 150}});
/// chart.addConnection("prologue", "choice_01");
/// chart.markVisited("prologue");
///
/// auto layout = chart.autoLayout(800.0f, 600.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sgc/math/Vec2.hpp>

namespace mitiru::vn
{

// ── フローノード型 ──────────────────────────────────────────────

/// @brief フローチャートノードの種別
enum class FlowNodeType : std::uint8_t
{
	Scene,    ///< 通常シーン
	Choice,   ///< 選択肢ノード
	Ending,   ///< エンディングノード
};

// ── 外観設定 ────────────────────────────────────────────────────

/// @brief フローチャートノードの外観スタイル
struct FlowChartNodeStyle
{
	float width = 120.0f;          ///< ノード表示幅
	float height = 40.0f;          ///< ノード表示高さ
	float cornerRadius = 4.0f;     ///< 角丸半径
};

/// @brief フローチャート全体の外観設定
struct FlowChartAppearance
{
	FlowChartNodeStyle sceneNode;    ///< Sceneノードのスタイル
	FlowChartNodeStyle choiceNode;   ///< Choiceノードのスタイル
	FlowChartNodeStyle endingNode;   ///< Endingノードのスタイル
	float connectionLineWidth = 2.0f;   ///< 接続線の太さ
	float autoLayoutPaddingX = 160.0f;  ///< 自動レイアウト水平パディング
	float autoLayoutPaddingY = 80.0f;   ///< 自動レイアウト垂直パディング
	float hitTestMargin = 4.0f;         ///< ヒットテスト余白

	/// @brief ノード種別に対応するスタイルを取得する
	[[nodiscard]] const FlowChartNodeStyle& styleFor(FlowNodeType type) const noexcept
	{
		switch (type)
		{
		case FlowNodeType::Choice: return choiceNode;
		case FlowNodeType::Ending: return endingNode;
		default:                   return sceneNode;
		}
	}
};

/// @brief 選択肢オプションの記録（どの選択肢を何回選んだか）
struct ChoiceRecord
{
	std::string label;             ///< 選択肢テキスト
	std::string targetNodeId;      ///< 遷移先ノードID
	std::uint32_t timesChosen = 0; ///< 選択回数
};

/// @brief フローチャートの接続（エッジ）
struct FlowConnection
{
	std::string fromNodeId;   ///< 接続元ノードID
	std::string toNodeId;     ///< 接続先ノードID
	std::string label;        ///< エッジラベル（選択肢テキストなど）
	bool isTaken = false;     ///< このルートを通過したか
};

/// @brief フローチャートの1ノード
struct FlowNode
{
	std::string id;                    ///< ノード識別子
	std::string title;                 ///< 表示タイトル
	FlowNodeType type = FlowNodeType::Scene; ///< ノード種別
	sgc::Vec2f position{0.0f, 0.0f};  ///< 表示位置
	bool isVisited = false;            ///< 訪問済みか
	bool isCurrent = false;            ///< 現在位置か
	std::vector<ChoiceRecord> choices; ///< 選択肢（Choiceノード用）
};

// ── エンディング ────────────────────────────────────────────────

/// @brief エンディング情報
struct EndingInfo
{
	std::string nodeId;        ///< 対応するFlowNodeのID
	std::string title;         ///< エンディング名
	std::string description;   ///< エンディングの説明
	bool isAchieved = false;   ///< 到達済みか
};

// ── ビュー状態 ──────────────────────────────────────────────────

/// @brief フローチャートビューアの表示状態
struct FlowChartViewState
{
	float zoom = 1.0f;         ///< ズーム倍率
	float panX = 0.0f;        ///< パンX
	float panY = 0.0f;        ///< パンY
	float minZoom = 0.3f;     ///< 最小ズーム
	float maxZoom = 3.0f;     ///< 最大ズーム

	/// @brief ズームを適用する
	void applyZoom(float delta) noexcept
	{
		zoom = std::clamp(zoom + delta, minZoom, maxZoom);
	}

	/// @brief パンを適用する
	void applyPan(float dx, float dy) noexcept
	{
		panX += dx;
		panY += dy;
	}

	/// @brief ビュー変換を適用した座標を返す
	/// @param worldPos ワールド座標
	/// @return スクリーン座標
	[[nodiscard]] sgc::Vec2f worldToScreen(const sgc::Vec2f& worldPos) const noexcept
	{
		return sgc::Vec2f{
			worldPos.x * zoom + panX,
			worldPos.y * zoom + panY
		};
	}

	/// @brief スクリーン座標をワールド座標に逆変換する
	/// @param screenPos スクリーン座標
	/// @return ワールド座標
	[[nodiscard]] sgc::Vec2f screenToWorld(const sgc::Vec2f& screenPos) const noexcept
	{
		if (zoom <= 0.0f) { return screenPos; }
		return sgc::Vec2f{
			(screenPos.x - panX) / zoom,
			(screenPos.y - panY) / zoom
		};
	}
};

// ── フローチャート ──────────────────────────────────────────────

/// @brief ストーリーフローチャート管理クラス
/// @details ノードとエッジからなるグラフ構造でストーリーの分岐を管理し、
///          プレイヤーの進行状況を追跡する。
class FlowChart
{
	std::vector<FlowNode> m_nodes;
	std::vector<FlowConnection> m_connections;
	std::vector<EndingInfo> m_endings;
	std::unordered_map<std::string, std::size_t> m_nodeIndex;
	FlowChartViewState m_viewState;
	FlowChartAppearance m_appearance;
	std::string m_currentNodeId;

public:
	// ── ノード管理 ──────────────────────────────────────────

	/// @brief ノードを追加する
	/// @param node フローノード
	void addNode(FlowNode node)
	{
		const auto id = node.id;
		m_nodeIndex[id] = m_nodes.size();
		m_nodes.push_back(std::move(node));
	}

	/// @brief ノードを取得する
	/// @param id ノードID
	/// @return ノード（存在しなければnullopt）
	[[nodiscard]] std::optional<FlowNode> getNode(const std::string& id) const
	{
		auto it = m_nodeIndex.find(id);
		if (it == m_nodeIndex.end()) { return std::nullopt; }
		return m_nodes[it->second];
	}

	/// @brief 全ノードを取得する
	[[nodiscard]] const std::vector<FlowNode>& nodes() const noexcept
	{
		return m_nodes;
	}

	/// @brief 全接続を取得する
	[[nodiscard]] const std::vector<FlowConnection>& connections() const noexcept
	{
		return m_connections;
	}

	// ── 接続管理 ────────────────────────────────────────────

	/// @brief ノード間の接続を追加する
	/// @param fromId 接続元ノードID
	/// @param toId 接続先ノードID
	/// @param label エッジラベル
	void addConnection(const std::string& fromId, const std::string& toId,
	                   const std::string& label = "")
	{
		m_connections.push_back(FlowConnection{fromId, toId, label, false});
	}

	/// @brief ノードから出る接続を取得する
	/// @param nodeId ノードID
	[[nodiscard]] std::vector<const FlowConnection*> connectionsFrom(const std::string& nodeId) const
	{
		std::vector<const FlowConnection*> result;
		for (const auto& conn : m_connections)
		{
			if (conn.fromNodeId == nodeId)
			{
				result.push_back(&conn);
			}
		}
		return result;
	}

	/// @brief ノードに入る接続を取得する
	/// @param nodeId ノードID
	[[nodiscard]] std::vector<const FlowConnection*> connectionsTo(const std::string& nodeId) const
	{
		std::vector<const FlowConnection*> result;
		for (const auto& conn : m_connections)
		{
			if (conn.toNodeId == nodeId)
			{
				result.push_back(&conn);
			}
		}
		return result;
	}

	// ── 進行管理 ────────────────────────────────────────────

	/// @brief ノードを訪問済みにマークする
	/// @param id ノードID
	void markVisited(const std::string& id)
	{
		auto* node = findNode(id);
		if (!node) { return; }
		node->isVisited = true;
	}

	/// @brief 現在位置を設定する
	/// @param id ノードID
	void setCurrentNode(const std::string& id)
	{
		// 前の現在位置をクリア
		if (!m_currentNodeId.empty())
		{
			auto* prev = findNode(m_currentNodeId);
			if (prev) { prev->isCurrent = false; }
		}

		auto* node = findNode(id);
		if (node)
		{
			node->isCurrent = true;
			node->isVisited = true;
		}
		m_currentNodeId = id;
	}

	/// @brief 接続を通過済みにマークする
	/// @param fromId 接続元
	/// @param toId 接続先
	void markConnectionTaken(const std::string& fromId, const std::string& toId)
	{
		for (auto& conn : m_connections)
		{
			if (conn.fromNodeId == fromId && conn.toNodeId == toId)
			{
				conn.isTaken = true;
				break;
			}
		}
	}

	/// @brief 選択肢の選択を記録する
	/// @param choiceNodeId 選択肢ノードID
	/// @param optionIndex 選択肢インデックス
	void recordChoice(const std::string& choiceNodeId, std::size_t optionIndex)
	{
		auto* node = findNode(choiceNodeId);
		if (!node || node->type != FlowNodeType::Choice) { return; }
		if (optionIndex < node->choices.size())
		{
			++node->choices[optionIndex].timesChosen;
			const auto& targetId = node->choices[optionIndex].targetNodeId;
			markConnectionTaken(choiceNodeId, targetId);
		}
	}

	/// @brief 現在のノードIDを取得する
	[[nodiscard]] const std::string& currentNodeId() const noexcept
	{
		return m_currentNodeId;
	}

	// ── エンディング管理 ────────────────────────────────────

	/// @brief エンディングを登録する
	/// @param ending エンディング情報
	void addEnding(EndingInfo ending)
	{
		m_endings.push_back(std::move(ending));
	}

	/// @brief エンディング到達を記録する
	/// @param nodeId エンディングノードID
	void markEndingAchieved(const std::string& nodeId)
	{
		for (auto& ending : m_endings)
		{
			if (ending.nodeId == nodeId)
			{
				ending.isAchieved = true;
				break;
			}
		}
		markVisited(nodeId);
	}

	/// @brief 全エンディングを取得する
	[[nodiscard]] const std::vector<EndingInfo>& endings() const noexcept
	{
		return m_endings;
	}

	/// @brief 到達済みエンディング数
	[[nodiscard]] std::size_t achievedEndingCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_endings.begin(), m_endings.end(),
				[](const EndingInfo& e) { return e.isAchieved; }));
	}

	/// @brief エンディング完了率 [0,100]
	[[nodiscard]] float endingCompletionPercentage() const noexcept
	{
		if (m_endings.empty()) { return 100.0f; }
		return static_cast<float>(achievedEndingCount()) / static_cast<float>(m_endings.size()) * 100.0f;
	}

	// ── 統計 ────────────────────────────────────────────────

	/// @brief 訪問済みノード数
	[[nodiscard]] std::size_t visitedCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_nodes.begin(), m_nodes.end(),
				[](const FlowNode& n) { return n.isVisited; }));
	}

	/// @brief ルート完了率（訪問済みノード / 総ノード）[0,100]
	[[nodiscard]] float routeCompletionPercentage() const noexcept
	{
		if (m_nodes.empty()) { return 100.0f; }
		return static_cast<float>(visitedCount()) / static_cast<float>(m_nodes.size()) * 100.0f;
	}

	/// @brief 通過済み接続数
	[[nodiscard]] std::size_t takenConnectionCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_connections.begin(), m_connections.end(),
				[](const FlowConnection& c) { return c.isTaken; }));
	}

	// ── ビュー ──────────────────────────────────────────────

	/// @brief ビュー状態への参照
	[[nodiscard]] FlowChartViewState& viewState() noexcept { return m_viewState; }
	[[nodiscard]] const FlowChartViewState& viewState() const noexcept { return m_viewState; }

	// ── 外観設定 ────────────────────────────────────────────

	/// @brief 外観設定を設定する
	/// @param appearance 外観設定
	void setAppearance(FlowChartAppearance appearance) noexcept
	{
		m_appearance = std::move(appearance);
	}

	/// @brief 外観設定への参照
	[[nodiscard]] FlowChartAppearance& appearance() noexcept { return m_appearance; }
	[[nodiscard]] const FlowChartAppearance& appearance() const noexcept { return m_appearance; }

	// ── 自動レイアウト ──────────────────────────────────────

	/// @brief ツリーレイアウトアルゴリズムでノード位置を自動計算する
	/// @param width 利用可能な幅
	/// @param height 利用可能な高さ
	/// @details パディングはappearance()の設定値を使用する。
	///          ルートノード（入力接続のないノード）を最上段に配置し、
	///          BFS順にレベルごとに均等配置する。
	void autoLayout(float width, float height)
	{
		const float paddingX = m_appearance.autoLayoutPaddingX;
		const float paddingY = m_appearance.autoLayoutPaddingY;
		if (m_nodes.empty()) { return; }

		// 入力接続のないノードをルートとする
		std::unordered_set<std::string> hasIncoming;
		for (const auto& conn : m_connections)
		{
			hasIncoming.insert(conn.toNodeId);
		}

		std::vector<std::string> roots;
		for (const auto& node : m_nodes)
		{
			if (hasIncoming.find(node.id) == hasIncoming.end())
			{
				roots.push_back(node.id);
			}
		}
		if (roots.empty())
		{
			roots.push_back(m_nodes.front().id);
		}

		// BFSでレベルを割り当て
		std::unordered_map<std::string, int> levels;
		std::queue<std::string> queue;
		for (const auto& rootId : roots)
		{
			levels[rootId] = 0;
			queue.push(rootId);
		}

		int maxLevel = 0;
		while (!queue.empty())
		{
			const auto currentId = queue.front();
			queue.pop();
			const int currentLevel = levels[currentId];
			maxLevel = std::max(maxLevel, currentLevel);

			for (const auto& conn : m_connections)
			{
				if (conn.fromNodeId == currentId && levels.find(conn.toNodeId) == levels.end())
				{
					levels[conn.toNodeId] = currentLevel + 1;
					queue.push(conn.toNodeId);
				}
			}
		}

		// レベルごとにノードを分類
		std::vector<std::vector<std::string>> levelNodes(static_cast<std::size_t>(maxLevel + 1));
		for (const auto& [id, level] : levels)
		{
			levelNodes[static_cast<std::size_t>(level)].push_back(id);
		}

		// 位置を計算
		const float levelCount = static_cast<float>(maxLevel + 1);
		const float levelHeight = (height - paddingY * 2.0f) / std::max(1.0f, levelCount);

		for (std::size_t level = 0; level < levelNodes.size(); ++level)
		{
			const auto& nodeIds = levelNodes[level];
			const float nodeCount = static_cast<float>(nodeIds.size());
			const float nodeWidth = (width - paddingX * 2.0f) / std::max(1.0f, nodeCount);

			for (std::size_t i = 0; i < nodeIds.size(); ++i)
			{
				auto* node = findNode(nodeIds[i]);
				if (!node) { continue; }
				node->position = sgc::Vec2f{
					paddingX + nodeWidth * (static_cast<float>(i) + 0.5f),
					paddingY + levelHeight * (static_cast<float>(level) + 0.5f)
				};
			}
		}

		// レベル未割り当てのノードを最下段に配置
		const float unassignedNodeSpacing = m_appearance.sceneNode.width + m_appearance.hitTestMargin;
		float extraX = paddingX;
		for (auto& node : m_nodes)
		{
			if (levels.find(node.id) == levels.end())
			{
				node.position = sgc::Vec2f{extraX, height - paddingY};
				extraX += unassignedNodeSpacing;
			}
		}
	}

	// ── ヒットテスト ────────────────────────────────────────

	/// @brief 座標上のノードを検索する
	/// @param pos ワールド座標
	/// @return ヒットしたノードID（なければ空文字列）
	[[nodiscard]] std::string hitTest(const sgc::Vec2f& pos) const
	{
		const float margin = m_appearance.hitTestMargin;

		for (const auto& node : m_nodes)
		{
			const auto& style = m_appearance.styleFor(node.type);
			const float hw = style.width * 0.5f + margin;
			const float hh = style.height * 0.5f + margin;

			if (pos.x >= node.position.x - hw && pos.x <= node.position.x + hw &&
			    pos.y >= node.position.y - hh && pos.y <= node.position.y + hh)
			{
				return node.id;
			}
		}
		return "";
	}

	// ── 直列化 ──────────────────────────────────────────────

	/// @brief 進行状態をJSON文字列に出力する
	[[nodiscard]] std::string toJson() const
	{
		std::string json = "{\"visited\":[";
		bool first = true;
		for (const auto& node : m_nodes)
		{
			if (node.isVisited)
			{
				if (!first) { json += ","; }
				json += "\"" + node.id + "\"";
				first = false;
			}
		}
		json += "],\"takenConnections\":[";
		first = true;
		for (const auto& conn : m_connections)
		{
			if (conn.isTaken)
			{
				if (!first) { json += ","; }
				json += "{\"from\":\"" + conn.fromNodeId + "\",\"to\":\"" + conn.toNodeId + "\"}";
				first = false;
			}
		}
		json += "],\"endings\":[";
		first = true;
		for (const auto& ending : m_endings)
		{
			if (ending.isAchieved)
			{
				if (!first) { json += ","; }
				json += "\"" + ending.nodeId + "\"";
				first = false;
			}
		}
		json += "],\"choices\":[";
		first = true;
		for (const auto& node : m_nodes)
		{
			if (node.type != FlowNodeType::Choice) { continue; }
			for (std::size_t i = 0; i < node.choices.size(); ++i)
			{
				if (node.choices[i].timesChosen == 0) { continue; }
				if (!first) { json += ","; }
				json += "{\"node\":\"" + node.id + "\",\"index\":"
				      + std::to_string(i) + ",\"count\":"
				      + std::to_string(node.choices[i].timesChosen) + "}";
				first = false;
			}
		}
		json += "],\"current\":\"" + m_currentNodeId + "\"}";
		return json;
	}

	/// @brief 進行状態をJSON文字列から復元する
	/// @param json JSON文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		// 全状態をリセット
		for (auto& node : m_nodes)
		{
			node.isVisited = false;
			node.isCurrent = false;
			for (auto& choice : node.choices)
			{
				choice.timesChosen = 0;
			}
		}
		for (auto& conn : m_connections)
		{
			conn.isTaken = false;
		}
		for (auto& ending : m_endings)
		{
			ending.isAchieved = false;
		}
		m_currentNodeId.clear();

		// "visited":[...] を解析
		parseStringArray(json, "visited", [this](const std::string& id) {
			markVisited(id);
		});

		// "endings":[...] を解析
		parseStringArray(json, "endings", [this](const std::string& id) {
			markEndingAchieved(id);
		});

		// "current":"..." を解析
		auto currentPos = json.find("\"current\":\"");
		if (currentPos != std::string_view::npos)
		{
			auto valStart = currentPos + 11;
			auto valEnd = json.find('"', valStart);
			if (valEnd != std::string_view::npos)
			{
				auto id = std::string(json.substr(valStart, valEnd - valStart));
				if (!id.empty()) { setCurrentNode(id); }
			}
		}

		// "takenConnections" を解析
		auto takenPos = json.find("\"takenConnections\":[");
		if (takenPos != std::string_view::npos)
		{
			auto bracketStart = json.find('[', takenPos);
			auto bracketEnd = findMatchingBracket(json, bracketStart);
			if (bracketStart != std::string_view::npos && bracketEnd != std::string_view::npos)
			{
				auto content = json.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
				std::size_t pos = 0;
				while (pos < content.size())
				{
					auto fromPos = content.find("\"from\":\"", pos);
					if (fromPos == std::string_view::npos) { break; }
					auto fromStart = fromPos + 8;
					auto fromEnd = content.find('"', fromStart);
					if (fromEnd == std::string_view::npos) { break; }

					auto toPos = content.find("\"to\":\"", fromEnd);
					if (toPos == std::string_view::npos) { break; }
					auto toStart = toPos + 6;
					auto toEnd = content.find('"', toStart);
					if (toEnd == std::string_view::npos) { break; }

					auto fromId = std::string(content.substr(fromStart, fromEnd - fromStart));
					auto toId = std::string(content.substr(toStart, toEnd - toStart));
					markConnectionTaken(fromId, toId);
					pos = toEnd + 1;
				}
			}
		}

		return true;
	}

private:
	[[nodiscard]] FlowNode* findNode(const std::string& id)
	{
		auto it = m_nodeIndex.find(id);
		if (it == m_nodeIndex.end()) { return nullptr; }
		return &m_nodes[it->second];
	}

	[[nodiscard]] const FlowNode* findNode(const std::string& id) const
	{
		auto it = m_nodeIndex.find(id);
		if (it == m_nodeIndex.end()) { return nullptr; }
		return &m_nodes[it->second];
	}

	template <typename Fn>
	static void parseStringArray(std::string_view json, const std::string& key, Fn fn)
	{
		auto keyPos = json.find("\"" + key + "\":[");
		if (keyPos == std::string_view::npos) { return; }
		auto bracketStart = json.find('[', keyPos);
		auto bracketEnd = findMatchingBracket(json, bracketStart);
		if (bracketStart == std::string_view::npos || bracketEnd == std::string_view::npos) { return; }

		auto content = json.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
		std::size_t pos = 0;
		while (pos < content.size())
		{
			auto quoteStart = content.find('"', pos);
			if (quoteStart == std::string_view::npos) { break; }
			auto quoteEnd = content.find('"', quoteStart + 1);
			if (quoteEnd == std::string_view::npos) { break; }
			fn(std::string(content.substr(quoteStart + 1, quoteEnd - quoteStart - 1)));
			pos = quoteEnd + 1;
		}
	}

	[[nodiscard]] static std::size_t findMatchingBracket(std::string_view json, std::size_t openPos)
	{
		if (openPos >= json.size()) { return std::string_view::npos; }
		int depth = 1;
		bool inString = false;
		for (std::size_t i = openPos + 1; i < json.size(); ++i)
		{
			if (json[i] == '"' && (i == 0 || json[i - 1] != '\\'))
			{
				inString = !inString;
			}
			else if (!inString)
			{
				if (json[i] == '[') { ++depth; }
				else if (json[i] == ']')
				{
					--depth;
					if (depth == 0) { return i; }
				}
			}
		}
		return std::string_view::npos;
	}
};

// ── フローチャートビルダー ──────────────────────────────────────

/// @brief シナリオスクリプトからフローチャートを自動生成するビルダー
/// @details @scene, @label, @choice, @jump ディレクティブを解析し、
///          FlowChartのノードと接続を構築する。
///
/// @code
/// std::string script = R"(
/// @scene prologue "Prologue"
/// @label start
/// ...
/// @choice "Go left" left_path
/// @choice "Go right" right_path
/// @scene left_path "Left Path"
/// @jump ending_a
/// @scene right_path "Right Path"
/// @jump ending_b
/// @scene ending_a "Ending A" ending
/// @scene ending_b "Ending B" ending
/// )";
///
/// mitiru::vn::FlowChartBuilder builder;
/// auto chart = builder.build(script);
/// chart.autoLayout(800.0f, 600.0f);
/// @endcode
class FlowChartBuilder
{
public:
	/// @brief スクリプトからFlowChartを構築する
	/// @param script シナリオスクリプト文字列
	/// @return 構築されたFlowChart
	[[nodiscard]] FlowChart build(std::string_view script) const
	{
		FlowChart chart;
		std::vector<ParsedDirective> directives = parse(script);

		std::string lastSceneId;
		std::vector<std::string> pendingChoiceTargets;
		std::string pendingChoiceSourceId;

		for (std::size_t i = 0; i < directives.size(); ++i)
		{
			const auto& dir = directives[i];

			if (dir.type == "@scene")
			{
				FlowNodeType nodeType = FlowNodeType::Scene;
				if (dir.args.size() >= 3 && dir.args[2] == "ending")
				{
					nodeType = FlowNodeType::Ending;
				}

				FlowNode node;
				node.id = dir.args[0];
				node.title = (dir.args.size() >= 2) ? dir.args[1] : dir.args[0];
				node.type = nodeType;
				chart.addNode(node);

				if (nodeType == FlowNodeType::Ending)
				{
					EndingInfo ending;
					ending.nodeId = node.id;
					ending.title = node.title;
					chart.addEnding(ending);
				}

				// 前のシーンからの暗黙接続（choiceやjumpがない場合）
				if (!lastSceneId.empty() && pendingChoiceTargets.empty())
				{
					chart.addConnection(lastSceneId, node.id);
				}

				// pending choice targets がこのシーンを含むか
				for (const auto& target : pendingChoiceTargets)
				{
					if (target == node.id)
					{
						// 既にaddConnectionされている
					}
				}

				lastSceneId = node.id;
				pendingChoiceTargets.clear();
			}
			else if (dir.type == "@choice")
			{
				if (dir.args.size() >= 2 && !lastSceneId.empty())
				{
					const auto& label = dir.args[0];
					const auto& target = dir.args[1];

					// 最初のchoiceでChoiceノードを作成
					if (pendingChoiceTargets.empty())
					{
						pendingChoiceSourceId = lastSceneId;
					}

					chart.addConnection(lastSceneId, target, label);
					pendingChoiceTargets.push_back(target);

					// ソースノードに選択肢を追加
					auto sourceNode = chart.getNode(lastSceneId);
					if (sourceNode.has_value())
					{
						// ノードタイプをChoiceに変更（再構築が必要だが簡略化のため省略）
					}
				}
			}
			else if (dir.type == "@jump")
			{
				if (!dir.args.empty() && !lastSceneId.empty())
				{
					chart.addConnection(lastSceneId, dir.args[0]);
					pendingChoiceTargets.push_back(dir.args[0]); // jumpも明示接続として扱う
				}
			}
			else if (dir.type == "@label")
			{
				// ラベルは参照用のマーカー（現在はノードとして扱わない）
			}
		}

		return chart;
	}

private:
	struct ParsedDirective
	{
		std::string type;               ///< ディレクティブ種別
		std::vector<std::string> args;  ///< 引数群
	};

	/// @brief スクリプトからディレクティブを抽出する
	[[nodiscard]] static std::vector<ParsedDirective> parse(std::string_view script)
	{
		std::vector<ParsedDirective> result;
		std::size_t pos = 0;

		while (pos < script.size())
		{
			// 行頭の@を探す
			auto atPos = script.find('@', pos);
			if (atPos == std::string_view::npos) { break; }

			// 行末を探す
			auto lineEnd = script.find('\n', atPos);
			if (lineEnd == std::string_view::npos) { lineEnd = script.size(); }

			auto line = script.substr(atPos, lineEnd - atPos);
			auto directive = parseLine(line);
			if (!directive.type.empty())
			{
				result.push_back(std::move(directive));
			}

			pos = lineEnd + 1;
		}

		return result;
	}

	/// @brief 1行をパースする
	[[nodiscard]] static ParsedDirective parseLine(std::string_view line)
	{
		ParsedDirective dir;

		// ディレクティブ名を抽出
		std::size_t i = 0;
		while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
		{
			++i;
		}
		dir.type = std::string(line.substr(0, i));

		// 引数を抽出
		while (i < line.size())
		{
			// 空白をスキップ
			while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
			{
				++i;
			}
			if (i >= line.size()) { break; }

			if (line[i] == '"')
			{
				// クォート文字列
				++i;
				std::size_t start = i;
				while (i < line.size() && line[i] != '"') { ++i; }
				dir.args.push_back(std::string(line.substr(start, i - start)));
				if (i < line.size()) { ++i; } // 閉じクォートをスキップ
			}
			else
			{
				// 非クォート引数
				std::size_t start = i;
				while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
				{
					++i;
				}
				dir.args.push_back(std::string(line.substr(start, i - start)));
			}
		}

		return dir;
	}
};

} // namespace mitiru::vn
