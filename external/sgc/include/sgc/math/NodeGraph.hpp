#pragma once

/// @file NodeGraph.hpp
/// @brief ノードグラフ — ビジュアル数式ビルダーのグラフ構造
///
/// BLIM式UIでブロック（ノード）同士をワイヤーで接続し、
/// 数式を視覚的に組み立てるためのデータモデル。
/// ExpressionTreeへの変換機能を持つ。
///
/// @code
/// sgc::NodeGraph graph;
/// auto nX = graph.addVariableNode("x", {50, 100});
/// auto n2 = graph.addNumberNode(2.0f, {50, 200});
/// auto nMul = graph.addBinaryOpNode(sgc::BinaryOp::Mul, {200, 150});
/// auto nOut = graph.addOutputNode({350, 150});
///
/// graph.connect({nX, 0}, {nMul, 0});     // x → mul左入力
/// graph.connect({n2, 0}, {nMul, 1});     // 2 → mul右入力
/// graph.connect({nMul, 0}, {nOut, 0});   // mul出力 → output入力
///
/// auto tree = graph.toExpressionTree();  // 2 * x
/// @endcode

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sgc/math/ExpressionTree.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc
{

/// @brief グラフノードの種類
enum class GraphNodeType
{
	NumberLiteral,  ///< 定数出力（出力ポート1つ）
	Variable,       ///< 変数出力（出力ポート1つ）
	BinaryOp,       ///< 二項演算（入力ポート2つ、出力ポート1つ）
	UnaryFunc,      ///< 単項関数（入力ポート1つ、出力ポート1つ）
	Output          ///< 最終出力（入力ポート1つ）
};

/// @brief ポートの方向
enum class PortDirection
{
	Input,   ///< 入力ポート（ワイヤーの受信側）
	Output   ///< 出力ポート（ワイヤーの送信側）
};

/// @brief ポートの識別子（ノードID + ポートインデックス）
struct PortId
{
	uint32_t nodeId = 0;     ///< 所属ノードのID
	uint32_t portIndex = 0;  ///< ポート番号（0始まり）

	[[nodiscard]] constexpr bool operator==(const PortId& rhs) const noexcept = default;
};

/// @brief PortId用ハッシュ関数
struct PortIdHash
{
	[[nodiscard]] std::size_t operator()(const PortId& p) const noexcept
	{
		return std::hash<uint64_t>{}(
			(static_cast<uint64_t>(p.nodeId) << 32) | p.portIndex);
	}
};

/// @brief ポートの定義
struct PortDef
{
	std::string name;            ///< ポート名（表示用）
	PortDirection direction{};   ///< 入力/出力
};

/// @brief ワイヤー（ノード間の接続）
struct Wire
{
	PortId from;  ///< 出力ポート
	PortId to;    ///< 入力ポート

	[[nodiscard]] constexpr bool operator==(const Wire& rhs) const noexcept = default;
};

/// @brief グラフ内の1ノード
struct GraphNode
{
	uint32_t m_id = 0;           ///< ノードID
	GraphNodeType m_type{};      ///< ノード種別
	Vec2f m_position;            ///< UI上の位置
	Vec2f m_size{120, 60};       ///< UI上のサイズ
	std::string m_label;         ///< 表示ラベル

	/// @brief ノード固有データ
	float m_numberValue = 0.0f;         ///< NumberLiteral用の値
	std::string m_variableName;         ///< Variable用の変数名
	BinaryOp m_binaryOp = BinaryOp::Add;   ///< BinaryOp用の演算子
	UnaryFunc m_unaryFunc = UnaryFunc::Sin; ///< UnaryFunc用の関数

	/// @brief ポート定義
	std::vector<PortDef> m_ports;

	/// @brief 指定方向のポート数を返す
	[[nodiscard]] std::size_t portCount(PortDirection dir) const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_ports.begin(), m_ports.end(),
				[dir](const PortDef& p) { return p.direction == dir; }));
	}

	/// @brief 指定インデックスのポートの方向を返す
	[[nodiscard]] PortDirection portDirection(uint32_t index) const
	{
		return m_ports[index].direction;
	}
};

/// @brief ビジュアル数式ビルダーのノードグラフ
///
/// ノードの追加・削除、ワイヤー接続、およびExpressionTreeへの変換を管理する。
/// 各ノードはUI位置情報を持ち、BLIM式レイアウトのサブパネルで描画される。
class NodeGraph
{
public:
	// ── ノード追加 ──────────────────────────────────────────

	/// @brief 数値リテラルノードを追加する
	/// @param value 定数値
	/// @param pos UI上の位置
	/// @return 新しいノードのID
	uint32_t addNumberNode(float value, const Vec2f& pos)
	{
		auto node = std::make_unique<GraphNode>();
		node->m_id = m_nextId++;
		node->m_type = GraphNodeType::NumberLiteral;
		node->m_position = pos;
		node->m_numberValue = value;
		node->m_label = detail::formatNumber(value);
		node->m_ports.push_back({"out", PortDirection::Output});

		const uint32_t id = node->m_id;
		m_nodes.push_back(std::move(node));
		return id;
	}

	/// @brief 変数ノードを追加する
	/// @param name 変数名（例: "x", "y"）
	/// @param pos UI上の位置
	/// @return 新しいノードのID
	uint32_t addVariableNode(const std::string& name, const Vec2f& pos)
	{
		auto node = std::make_unique<GraphNode>();
		node->m_id = m_nextId++;
		node->m_type = GraphNodeType::Variable;
		node->m_position = pos;
		node->m_variableName = name;
		node->m_label = name;
		node->m_ports.push_back({"out", PortDirection::Output});

		const uint32_t id = node->m_id;
		m_nodes.push_back(std::move(node));
		return id;
	}

	/// @brief 二項演算ノードを追加する
	/// @param op 演算子
	/// @param pos UI上の位置
	/// @return 新しいノードのID
	uint32_t addBinaryOpNode(BinaryOp op, const Vec2f& pos)
	{
		auto node = std::make_unique<GraphNode>();
		node->m_id = m_nextId++;
		node->m_type = GraphNodeType::BinaryOp;
		node->m_position = pos;
		node->m_binaryOp = op;
		node->m_label = detail::binaryOpToString(op);
		node->m_ports.push_back({"lhs", PortDirection::Input});
		node->m_ports.push_back({"rhs", PortDirection::Input});
		node->m_ports.push_back({"out", PortDirection::Output});

		const uint32_t id = node->m_id;
		m_nodes.push_back(std::move(node));
		return id;
	}

	/// @brief 単項関数ノードを追加する
	/// @param func 関数種別
	/// @param pos UI上の位置
	/// @return 新しいノードのID
	uint32_t addUnaryFuncNode(UnaryFunc func, const Vec2f& pos)
	{
		auto node = std::make_unique<GraphNode>();
		node->m_id = m_nextId++;
		node->m_type = GraphNodeType::UnaryFunc;
		node->m_position = pos;
		node->m_unaryFunc = func;
		node->m_label = detail::unaryFuncToString(func);
		node->m_ports.push_back({"in", PortDirection::Input});
		node->m_ports.push_back({"out", PortDirection::Output});

		const uint32_t id = node->m_id;
		m_nodes.push_back(std::move(node));
		return id;
	}

	/// @brief 最終出力ノードを追加する
	/// @param pos UI上の位置
	/// @return 新しいノードのID
	uint32_t addOutputNode(const Vec2f& pos)
	{
		auto node = std::make_unique<GraphNode>();
		node->m_id = m_nextId++;
		node->m_type = GraphNodeType::Output;
		node->m_position = pos;
		node->m_label = "y =";
		node->m_ports.push_back({"result", PortDirection::Input});

		const uint32_t id = node->m_id;
		m_nodes.push_back(std::move(node));
		return id;
	}

	// ── ノード削除 ──────────────────────────────────────────

	/// @brief ノードを削除する（関連ワイヤーも削除）
	/// @param nodeId 削除するノードのID
	/// @return 削除に成功したらtrue
	bool removeNode(uint32_t nodeId)
	{
		const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
			[nodeId](const auto& n) { return n->m_id == nodeId; });
		if (it == m_nodes.end()) return false;

		// 関連ワイヤーを削除
		m_wires.erase(
			std::remove_if(m_wires.begin(), m_wires.end(),
				[nodeId](const Wire& w)
				{
					return w.from.nodeId == nodeId || w.to.nodeId == nodeId;
				}),
			m_wires.end());

		m_nodes.erase(it);
		return true;
	}

	// ── ワイヤー接続 ────────────────────────────────────────

	/// @brief 2つのポートをワイヤーで接続する
	/// @param from 出力ポート
	/// @param to 入力ポート
	/// @return 接続に成功したらtrue
	///
	/// @note 入力ポートは1本のワイヤーしか受けられない（既存接続を上書き）
	bool connect(const PortId& from, const PortId& to)
	{
		// ノード存在チェック
		const auto* fromNode = findNode(from.nodeId);
		const auto* toNode = findNode(to.nodeId);
		if (!fromNode || !toNode) return false;

		// ポート範囲チェック
		if (from.portIndex >= fromNode->m_ports.size()) return false;
		if (to.portIndex >= toNode->m_ports.size()) return false;

		// 方向チェック（from=Output, to=Input）
		if (fromNode->m_ports[from.portIndex].direction != PortDirection::Output) return false;
		if (toNode->m_ports[to.portIndex].direction != PortDirection::Input) return false;

		// 自己接続禁止
		if (from.nodeId == to.nodeId) return false;

		// 入力ポートの既存接続を解除
		disconnect(to);

		m_wires.push_back({from, to});
		return true;
	}

	/// @brief 入力ポートへの接続を解除する
	/// @param inputPort 入力ポート
	void disconnect(const PortId& inputPort)
	{
		m_wires.erase(
			std::remove_if(m_wires.begin(), m_wires.end(),
				[&inputPort](const Wire& w) { return w.to == inputPort; }),
			m_wires.end());
	}

	// ── クエリ ──────────────────────────────────────────────

	/// @brief ノードを検索する
	/// @param nodeId ノードID
	/// @return ノードポインタ（見つからなければnullptr）
	[[nodiscard]] GraphNode* findNode(uint32_t nodeId)
	{
		const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
			[nodeId](const auto& n) { return n->m_id == nodeId; });
		return (it != m_nodes.end()) ? it->get() : nullptr;
	}

	/// @brief ノードを検索する（const版）
	[[nodiscard]] const GraphNode* findNode(uint32_t nodeId) const
	{
		const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
			[nodeId](const auto& n) { return n->m_id == nodeId; });
		return (it != m_nodes.end()) ? it->get() : nullptr;
	}

	/// @brief ノード数を返す
	[[nodiscard]] std::size_t nodeCount() const noexcept
	{
		return m_nodes.size();
	}

	/// @brief ワイヤー数を返す
	[[nodiscard]] std::size_t wireCount() const noexcept
	{
		return m_wires.size();
	}

	/// @brief ワイヤーリストへの参照を返す
	[[nodiscard]] const std::vector<Wire>& wires() const noexcept
	{
		return m_wires;
	}

	/// @brief ノードリストへの参照を返す
	[[nodiscard]] const std::vector<std::unique_ptr<GraphNode>>& nodes() const noexcept
	{
		return m_nodes;
	}

	/// @brief 出力ノードを検索する
	/// @return 最初に見つかった出力ノードのID（なければnullopt）
	[[nodiscard]] std::optional<uint32_t> findOutputNodeId() const
	{
		for (const auto& n : m_nodes)
		{
			if (n->m_type == GraphNodeType::Output)
			{
				return n->m_id;
			}
		}
		return std::nullopt;
	}

	/// @brief 入力ポートに接続されているワイヤーを探す
	/// @param inputPort 入力ポート
	/// @return ワイヤーのポインタ（なければnullptr）
	[[nodiscard]] const Wire* findWireToInput(const PortId& inputPort) const
	{
		for (const auto& w : m_wires)
		{
			if (w.to == inputPort) return &w;
		}
		return nullptr;
	}

	// ── 検証 ────────────────────────────────────────────────

	/// @brief グラフが完全か（出力ノードに全入力が接続されている）を返す
	[[nodiscard]] bool isComplete() const
	{
		const auto outputId = findOutputNodeId();
		if (!outputId) return false;

		return isNodeComplete(*outputId);
	}

	/// @brief グラフに循環参照がないかチェックする
	/// @return ループがあればtrue
	[[nodiscard]] bool hasLoop() const
	{
		// DFSで循環検出
		std::unordered_set<uint32_t> visited;
		std::unordered_set<uint32_t> inStack;

		for (const auto& node : m_nodes)
		{
			if (hasCycleDFS(node->m_id, visited, inStack))
			{
				return true;
			}
		}
		return false;
	}

	// ── ExpressionTree変換 ──────────────────────────────────

	/// @brief グラフをExpressionTreeに変換する
	/// @return 式木（変換不可能な場合は空のツリー）
	///
	/// 出力ノードから逆方向にワイヤーを辿り、式木を構築する。
	[[nodiscard]] ExpressionTree toExpressionTree() const
	{
		const auto outputId = findOutputNodeId();
		if (!outputId) return {};

		// 出力ノードの入力ポートに接続されたノードからツリーを構築
		const auto* wire = findWireToInput({*outputId, 0});
		if (!wire) return {};

		ExpressionBuilder builder;
		auto root = buildExprNode(wire->from.nodeId, builder);
		if (!root) return {};

		return builder.build(std::move(root));
	}

	// ── ノード値の更新 ──────────────────────────────────────

	/// @brief 数値ノードの値を変更する
	/// @param nodeId ノードID
	/// @param value 新しい値
	/// @return 更新に成功したらtrue
	bool setNumberValue(uint32_t nodeId, float value)
	{
		auto* node = findNode(nodeId);
		if (!node || node->m_type != GraphNodeType::NumberLiteral) return false;
		node->m_numberValue = value;
		node->m_label = detail::formatNumber(value);
		return true;
	}

	/// @brief 二項演算ノードの演算子を変更する
	/// @param nodeId ノードID
	/// @param op 新しい演算子
	/// @return 更新に成功したらtrue
	bool setBinaryOp(uint32_t nodeId, BinaryOp op)
	{
		auto* node = findNode(nodeId);
		if (!node || node->m_type != GraphNodeType::BinaryOp) return false;
		node->m_binaryOp = op;
		node->m_label = detail::binaryOpToString(op);
		return true;
	}

	/// @brief 単項関数ノードの関数を変更する
	/// @param nodeId ノードID
	/// @param func 新しい関数
	/// @return 更新に成功したらtrue
	bool setUnaryFunc(uint32_t nodeId, UnaryFunc func)
	{
		auto* node = findNode(nodeId);
		if (!node || node->m_type != GraphNodeType::UnaryFunc) return false;
		node->m_unaryFunc = func;
		node->m_label = detail::unaryFuncToString(func);
		return true;
	}

	/// @brief グラフをクリアする
	void clear()
	{
		m_nodes.clear();
		m_wires.clear();
		m_nextId = 0;
	}

private:
	std::vector<std::unique_ptr<GraphNode>> m_nodes;  ///< 全ノード
	std::vector<Wire> m_wires;                         ///< 全ワイヤー
	uint32_t m_nextId = 0;                             ///< 次のノードID

	/// @brief ノードの全入力ポートが接続済みか再帰チェックする
	[[nodiscard]] bool isNodeComplete(uint32_t nodeId) const
	{
		const auto* node = findNode(nodeId);
		if (!node) return false;

		for (uint32_t i = 0; i < node->m_ports.size(); ++i)
		{
			if (node->m_ports[i].direction == PortDirection::Input)
			{
				const auto* wire = findWireToInput({nodeId, i});
				if (!wire) return false;

				// 接続元も再帰チェック
				if (!isNodeComplete(wire->from.nodeId)) return false;
			}
		}
		return true;
	}

	/// @brief DFSで循環検出する
	[[nodiscard]] bool hasCycleDFS(
		uint32_t nodeId,
		std::unordered_set<uint32_t>& visited,
		std::unordered_set<uint32_t>& inStack) const
	{
		if (inStack.count(nodeId)) return true;
		if (visited.count(nodeId)) return false;

		visited.insert(nodeId);
		inStack.insert(nodeId);

		// このノードの出力から接続先を辿る
		for (const auto& w : m_wires)
		{
			if (w.from.nodeId == nodeId)
			{
				if (hasCycleDFS(w.to.nodeId, visited, inStack))
				{
					return true;
				}
			}
		}

		inStack.erase(nodeId);
		return false;
	}

	/// @brief ノードからExprNodeを再帰的に構築する
	[[nodiscard]] std::unique_ptr<ExprNode> buildExprNode(
		uint32_t nodeId, ExpressionBuilder& builder) const
	{
		const auto* node = findNode(nodeId);
		if (!node) return nullptr;

		switch (node->m_type)
		{
		case GraphNodeType::NumberLiteral:
			return builder.number(node->m_numberValue);

		case GraphNodeType::Variable:
			return builder.variable(node->m_variableName);

		case GraphNodeType::BinaryOp:
		{
			// 入力ポート0=lhs, 入力ポート1=rhs
			const auto* lhsWire = findWireToInput({nodeId, 0});
			const auto* rhsWire = findWireToInput({nodeId, 1});
			if (!lhsWire || !rhsWire) return nullptr;

			auto lhs = buildExprNode(lhsWire->from.nodeId, builder);
			auto rhs = buildExprNode(rhsWire->from.nodeId, builder);
			if (!lhs || !rhs) return nullptr;

			switch (node->m_binaryOp)
			{
			case BinaryOp::Add: return builder.add(std::move(lhs), std::move(rhs));
			case BinaryOp::Sub: return builder.sub(std::move(lhs), std::move(rhs));
			case BinaryOp::Mul: return builder.mul(std::move(lhs), std::move(rhs));
			case BinaryOp::Div: return builder.div(std::move(lhs), std::move(rhs));
			case BinaryOp::Pow: return builder.pow(std::move(lhs), std::move(rhs));
			}
			return nullptr;
		}

		case GraphNodeType::UnaryFunc:
		{
			const auto* inputWire = findWireToInput({nodeId, 0});
			if (!inputWire) return nullptr;

			auto child = buildExprNode(inputWire->from.nodeId, builder);
			if (!child) return nullptr;

			switch (node->m_unaryFunc)
			{
			case UnaryFunc::Sin:    return builder.sin(std::move(child));
			case UnaryFunc::Cos:    return builder.cos(std::move(child));
			case UnaryFunc::Tan:    return builder.tan(std::move(child));
			case UnaryFunc::Sqrt:   return builder.sqrt(std::move(child));
			case UnaryFunc::Abs:    return builder.abs(std::move(child));
			case UnaryFunc::Log:    return builder.log(std::move(child));
			case UnaryFunc::Exp:    return builder.exp(std::move(child));
			case UnaryFunc::Floor:  return builder.floor(std::move(child));
			case UnaryFunc::Ceil:   return builder.ceil(std::move(child));
			case UnaryFunc::Negate: return builder.negate(std::move(child));
			}
			return nullptr;
		}

		case GraphNodeType::Output:
		{
			const auto* inputWire = findWireToInput({nodeId, 0});
			if (!inputWire) return nullptr;
			return buildExprNode(inputWire->from.nodeId, builder);
		}
		}

		return nullptr;
	}
};

} // namespace sgc
