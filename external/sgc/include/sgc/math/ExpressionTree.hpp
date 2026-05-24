#pragma once

/// @file ExpressionTree.hpp
/// @brief 数式の抽象構文木（AST）— ビジュアルノードベース数式ビルダーのバックエンド
///
/// ブロック/ノードの接続を式木に変換し、評価・文字列化する。
/// BLIMスタイルの数式ビルダーUIのデータモデルとして使用する。
///
/// @code
/// sgc::ExpressionBuilder b;
/// // sin(x) + 2 * y
/// auto tree = b.build(
///     b.add(
///         b.sin(b.variable("x")),
///         b.mul(b.number(2.0f), b.variable("y"))
///     )
/// );
///
/// sgc::VariableBindings vars{{"x", 3.14159f}, {"y", 5.0f}};
/// float result = tree.evaluate(vars);
/// std::string formula = tree.toString();  // "(sin(x) + (2 * y))"
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sgc
{

/// @brief 式木ノードの一意識別子（UI連携用）
using NodeId = uint32_t;

/// @brief 変数名→値のバインディング
using VariableBindings = std::unordered_map<std::string, float>;

/// @brief 二項演算の種類
enum class BinaryOp
{
	Add,  ///< 加算 (+)
	Sub,  ///< 減算 (-)
	Mul,  ///< 乗算 (*)
	Div,  ///< 除算 (/)
	Pow   ///< 累乗 (^)
};

/// @brief 単項関数の種類
enum class UnaryFunc
{
	Sin,     ///< 正弦 sin
	Cos,     ///< 余弦 cos
	Tan,     ///< 正接 tan
	Sqrt,    ///< 平方根 sqrt
	Abs,     ///< 絶対値 abs
	Log,     ///< 自然対数 ln
	Exp,     ///< 指数関数 exp
	Floor,   ///< 切り捨て floor
	Ceil,    ///< 切り上げ ceil
	Negate   ///< 単項マイナス (-)
};

// 前方宣言
struct ExprNode;

/// @brief 数値リテラルのペイロード
struct NumberPayload
{
	float value = 0.0f;  ///< リテラル値
};

/// @brief 変数参照のペイロード
struct VariablePayload
{
	std::string name;  ///< 変数名（例: "x", "y", "t"）
};

/// @brief 二項演算のペイロード
struct BinaryPayload
{
	BinaryOp op = BinaryOp::Add;           ///< 演算子
	std::unique_ptr<ExprNode> lhs;         ///< 左オペランド
	std::unique_ptr<ExprNode> rhs;         ///< 右オペランド
};

/// @brief 単項関数のペイロード
struct UnaryPayload
{
	UnaryFunc func = UnaryFunc::Sin;       ///< 関数種別
	std::unique_ptr<ExprNode> child;       ///< 引数ノード
};

/// @brief 式木の1ノード — variant で4種類のペイロードを保持
struct ExprNode
{
	NodeId m_id = 0;  ///< UI連携用の一意ID
	std::variant<NumberPayload, VariablePayload, BinaryPayload, UnaryPayload> m_payload;
};

namespace detail
{

/// @brief 二項演算子の優先度を返す（括弧制御用）
[[nodiscard]] inline constexpr int binaryPrecedence(BinaryOp op) noexcept
{
	switch (op)
	{
	case BinaryOp::Add: return 1;
	case BinaryOp::Sub: return 1;
	case BinaryOp::Mul: return 2;
	case BinaryOp::Div: return 2;
	case BinaryOp::Pow: return 3;
	}
	return 0;
}

/// @brief 二項演算子を文字列に変換する
[[nodiscard]] inline std::string binaryOpToString(BinaryOp op)
{
	switch (op)
	{
	case BinaryOp::Add: return "+";
	case BinaryOp::Sub: return "-";
	case BinaryOp::Mul: return "*";
	case BinaryOp::Div: return "/";
	case BinaryOp::Pow: return "^";
	}
	return "?";
}

/// @brief 単項関数を文字列に変換する
[[nodiscard]] inline std::string unaryFuncToString(UnaryFunc func)
{
	switch (func)
	{
	case UnaryFunc::Sin:    return "sin";
	case UnaryFunc::Cos:    return "cos";
	case UnaryFunc::Tan:    return "tan";
	case UnaryFunc::Sqrt:   return "sqrt";
	case UnaryFunc::Abs:    return "abs";
	case UnaryFunc::Log:    return "log";
	case UnaryFunc::Exp:    return "exp";
	case UnaryFunc::Floor:  return "floor";
	case UnaryFunc::Ceil:   return "ceil";
	case UnaryFunc::Negate: return "-";
	}
	return "?";
}

/// @brief ノードを再帰的に評価する
[[nodiscard]] inline float evaluateNode(
	const ExprNode& node, const VariableBindings& vars)
{
	return std::visit([&](const auto& payload) -> float
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, NumberPayload>)
		{
			return payload.value;
		}
		else if constexpr (std::is_same_v<T, VariablePayload>)
		{
			const auto it = vars.find(payload.name);
			if (it == vars.end())
			{
				return std::numeric_limits<float>::quiet_NaN();
			}
			return it->second;
		}
		else if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			const float lhs = evaluateNode(*payload.lhs, vars);
			const float rhs = evaluateNode(*payload.rhs, vars);

			switch (payload.op)
			{
			case BinaryOp::Add: return lhs + rhs;
			case BinaryOp::Sub: return lhs - rhs;
			case BinaryOp::Mul: return lhs * rhs;
			case BinaryOp::Div:
				if (rhs == 0.0f)
				{
					return std::numeric_limits<float>::quiet_NaN();
				}
				return lhs / rhs;
			case BinaryOp::Pow: return std::pow(lhs, rhs);
			}
			return 0.0f;
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			const float val = evaluateNode(*payload.child, vars);

			switch (payload.func)
			{
			case UnaryFunc::Sin:    return std::sin(val);
			case UnaryFunc::Cos:    return std::cos(val);
			case UnaryFunc::Tan:    return std::tan(val);
			case UnaryFunc::Sqrt:   return std::sqrt(val);
			case UnaryFunc::Abs:    return std::abs(val);
			case UnaryFunc::Log:    return std::log(val);
			case UnaryFunc::Exp:    return std::exp(val);
			case UnaryFunc::Floor:  return std::floor(val);
			case UnaryFunc::Ceil:   return std::ceil(val);
			case UnaryFunc::Negate: return -val;
			}
			return 0.0f;
		}
		else
		{
			return 0.0f;
		}
	}, node.m_payload);
}

/// @brief 数値をきれいな文字列にフォーマットする（余計な末尾ゼロを除去）
[[nodiscard]] inline std::string formatNumber(float val)
{
	// 整数の場合はそのまま
	if (val == std::floor(val) && std::abs(val) < 1e9f)
	{
		std::ostringstream oss;
		oss << static_cast<int>(val);
		return oss.str();
	}

	std::ostringstream oss;
	oss << val;
	return oss.str();
}

/// @brief ノードを再帰的にinfix文字列に変換する
/// @param node 対象ノード
/// @param parentPrecedence 親の演算子優先度（括弧制御用、-1=ルート）
[[nodiscard]] inline std::string nodeToString(const ExprNode& node, int parentPrecedence = -1)
{
	return std::visit([&](const auto& payload) -> std::string
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, NumberPayload>)
		{
			return formatNumber(payload.value);
		}
		else if constexpr (std::is_same_v<T, VariablePayload>)
		{
			return payload.name;
		}
		else if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			const int prec = binaryPrecedence(payload.op);
			const auto lhsStr = nodeToString(*payload.lhs, prec);
			const auto rhsStr = nodeToString(*payload.rhs, prec);

			std::string result = lhsStr + " "
				+ binaryOpToString(payload.op) + " " + rhsStr;

			// 親の優先度より低い場合は括弧で囲む
			if (parentPrecedence > prec)
			{
				result = "(" + result + ")";
			}

			return result;
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			if (payload.func == UnaryFunc::Negate)
			{
				return "(-" + nodeToString(*payload.child, 99) + ")";
			}
			return unaryFuncToString(payload.func)
				+ "(" + nodeToString(*payload.child, -1) + ")";
		}
		else
		{
			return "?";
		}
	}, node.m_payload);
}

/// @brief ノードのツリー内のノード数を再帰的にカウントする
[[nodiscard]] inline std::size_t countNodes(const ExprNode& node)
{
	return std::visit([](const auto& payload) -> std::size_t
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, NumberPayload> || std::is_same_v<T, VariablePayload>)
		{
			return 1;
		}
		else if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			return 1 + countNodes(*payload.lhs) + countNodes(*payload.rhs);
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			return 1 + countNodes(*payload.child);
		}
		else
		{
			return 1;
		}
	}, node.m_payload);
}

/// @brief ツリー内の全変数名を収集する
inline void collectVariablesImpl(
	const ExprNode& node, std::vector<std::string>& out)
{
	std::visit([&](const auto& payload)
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, VariablePayload>)
		{
			// 重複チェック
			if (std::find(out.begin(), out.end(), payload.name) == out.end())
			{
				out.push_back(payload.name);
			}
		}
		else if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			collectVariablesImpl(*payload.lhs, out);
			collectVariablesImpl(*payload.rhs, out);
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			collectVariablesImpl(*payload.child, out);
		}
	}, node.m_payload);
}

/// @brief 全ノードIDを収集する（一意性チェック用）
inline void collectNodeIds(
	const ExprNode& node, std::vector<NodeId>& out)
{
	out.push_back(node.m_id);

	std::visit([&](const auto& payload)
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			collectNodeIds(*payload.lhs, out);
			collectNodeIds(*payload.rhs, out);
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			collectNodeIds(*payload.child, out);
		}
	}, node.m_payload);
}

/// @brief 定数畳み込みを再帰的に適用する
inline void foldConstants(std::unique_ptr<ExprNode>& node)
{
	if (!node) return;

	std::visit([&](auto& payload)
	{
		using T = std::decay_t<decltype(payload)>;

		if constexpr (std::is_same_v<T, BinaryPayload>)
		{
			// 子を先に畳み込む（ボトムアップ）
			foldConstants(payload.lhs);
			foldConstants(payload.rhs);

			// 両方が数値リテラルなら計算して置き換え
			const auto* lhsNum = std::get_if<NumberPayload>(&payload.lhs->m_payload);
			const auto* rhsNum = std::get_if<NumberPayload>(&payload.rhs->m_payload);

			if (lhsNum && rhsNum)
			{
				const VariableBindings empty;
				const float result = evaluateNode(*node, empty);
				const NodeId id = node->m_id;
				node = std::make_unique<ExprNode>();
				node->m_id = id;
				node->m_payload = NumberPayload{result};
			}
		}
		else if constexpr (std::is_same_v<T, UnaryPayload>)
		{
			foldConstants(payload.child);

			const auto* childNum = std::get_if<NumberPayload>(&payload.child->m_payload);
			if (childNum)
			{
				const VariableBindings empty;
				const float result = evaluateNode(*node, empty);
				const NodeId id = node->m_id;
				node = std::make_unique<ExprNode>();
				node->m_id = id;
				node->m_payload = NumberPayload{result};
			}
		}
	}, node->m_payload);
}

} // namespace detail

/// @brief 数式の抽象構文木（式木）
///
/// ビジュアルブロック/ノードの接続を内部表現し、
/// 変数バインディングによる評価・infix文字列変換を提供する。
/// ムーブオンリー（コピー不可）。
///
/// @see ExpressionBuilder 構築用ビルダー
class ExpressionTree
{
public:
	/// @brief デフォルトコンストラクタ（空のツリー）
	ExpressionTree() = default;

	/// @brief ルートノードから構築する
	/// @param root ルートノード（所有権を移動）
	explicit ExpressionTree(std::unique_ptr<ExprNode> root)
		: m_root(std::move(root))
	{
	}

	/// @brief ムーブコンストラクタ
	ExpressionTree(ExpressionTree&&) noexcept = default;

	/// @brief ムーブ代入
	ExpressionTree& operator=(ExpressionTree&&) noexcept = default;

	// コピー禁止
	ExpressionTree(const ExpressionTree&) = delete;
	ExpressionTree& operator=(const ExpressionTree&) = delete;

	/// @brief 変数バインディングを使って式を評価する
	/// @param vars 変数名→値のマッピング
	/// @return 評価結果（未定義変数やゼロ除算はNaN）
	[[nodiscard]] float evaluate(const VariableBindings& vars) const
	{
		if (!m_root) return 0.0f;
		return detail::evaluateNode(*m_root, vars);
	}

	/// @brief 式をinfix文字列に変換する
	/// @return 数式文字列（例: "sin(x) + 2 * y"）
	[[nodiscard]] std::string toString() const
	{
		if (!m_root) return "";
		return detail::nodeToString(*m_root);
	}

	/// @brief ルートノードへの読み取りアクセス（UI走査用）
	/// @return ルートノードのポインタ（空ならnullptr）
	[[nodiscard]] const ExprNode* root() const noexcept
	{
		return m_root.get();
	}

	/// @brief ツリー内のノード総数を返す
	/// @return ノード数
	[[nodiscard]] std::size_t nodeCount() const noexcept
	{
		if (!m_root) return 0;
		return detail::countNodes(*m_root);
	}

	/// @brief ツリー内で使用されている全変数名を収集する
	/// @return 変数名のリスト（重複なし）
	[[nodiscard]] std::vector<std::string> collectVariables() const
	{
		std::vector<std::string> result;
		if (m_root)
		{
			detail::collectVariablesImpl(*m_root, result);
		}
		return result;
	}

	/// @brief 定数畳み込み最適化を適用する
	///
	/// `(3 + 4) * x` → `7 * x` のように、
	/// 定数のみで構成されるサブツリーを計算結果に置き換える。
	void foldConstants()
	{
		if (m_root)
		{
			detail::foldConstants(m_root);
		}
	}

	/// @brief ツリーが空かどうかを返す
	/// @return ルートノードがなければtrue
	[[nodiscard]] bool empty() const noexcept
	{
		return !m_root;
	}

private:
	std::unique_ptr<ExprNode> m_root;  ///< ルートノード
};

/// @brief 式木を構築するビルダー
///
/// 各ファクトリメソッドがノードを生成し、一意のNodeIdを割り当てる。
/// 最終的に build() でExpressionTreeに変換する。
///
/// @code
/// ExpressionBuilder b;
/// auto tree = b.build(
///     b.add(b.variable("x"), b.number(1.0f))
/// );
/// @endcode
class ExpressionBuilder
{
public:
	/// @brief 数値リテラルノードを作成する
	/// @param val リテラル値
	/// @return ノード（所有権付き）
	[[nodiscard]] std::unique_ptr<ExprNode> number(float val)
	{
		auto node = std::make_unique<ExprNode>();
		node->m_id = allocateId();
		node->m_payload = NumberPayload{val};
		return node;
	}

	/// @brief 変数参照ノードを作成する
	/// @param name 変数名
	/// @return ノード（所有権付き）
	[[nodiscard]] std::unique_ptr<ExprNode> variable(const std::string& name)
	{
		auto node = std::make_unique<ExprNode>();
		node->m_id = allocateId();
		node->m_payload = VariablePayload{name};
		return node;
	}

	/// @brief 加算ノード (lhs + rhs)
	[[nodiscard]] std::unique_ptr<ExprNode> add(
		std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs)
	{
		return makeBinary(BinaryOp::Add, std::move(lhs), std::move(rhs));
	}

	/// @brief 減算ノード (lhs - rhs)
	[[nodiscard]] std::unique_ptr<ExprNode> sub(
		std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs)
	{
		return makeBinary(BinaryOp::Sub, std::move(lhs), std::move(rhs));
	}

	/// @brief 乗算ノード (lhs * rhs)
	[[nodiscard]] std::unique_ptr<ExprNode> mul(
		std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs)
	{
		return makeBinary(BinaryOp::Mul, std::move(lhs), std::move(rhs));
	}

	/// @brief 除算ノード (lhs / rhs)
	[[nodiscard]] std::unique_ptr<ExprNode> div(
		std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs)
	{
		return makeBinary(BinaryOp::Div, std::move(lhs), std::move(rhs));
	}

	/// @brief 累乗ノード (lhs ^ rhs)
	[[nodiscard]] std::unique_ptr<ExprNode> pow(
		std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs)
	{
		return makeBinary(BinaryOp::Pow, std::move(lhs), std::move(rhs));
	}

	/// @brief sin関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> sin(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Sin, std::move(child));
	}

	/// @brief cos関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> cos(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Cos, std::move(child));
	}

	/// @brief tan関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> tan(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Tan, std::move(child));
	}

	/// @brief sqrt関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> sqrt(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Sqrt, std::move(child));
	}

	/// @brief abs関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> abs(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Abs, std::move(child));
	}

	/// @brief log（自然対数）関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> log(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Log, std::move(child));
	}

	/// @brief exp関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> exp(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Exp, std::move(child));
	}

	/// @brief floor関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> floor(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Floor, std::move(child));
	}

	/// @brief ceil関数ノード
	[[nodiscard]] std::unique_ptr<ExprNode> ceil(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Ceil, std::move(child));
	}

	/// @brief 単項マイナスノード (-child)
	[[nodiscard]] std::unique_ptr<ExprNode> negate(std::unique_ptr<ExprNode> child)
	{
		return makeUnary(UnaryFunc::Negate, std::move(child));
	}

	/// @brief ルートノードからExpressionTreeを構築する
	/// @param root ルートノード
	/// @return 構築された式木
	[[nodiscard]] ExpressionTree build(std::unique_ptr<ExprNode> root)
	{
		return ExpressionTree{std::move(root)};
	}

private:
	uint32_t m_nextId = 0;  ///< 次に割り当てるノードID

	/// @brief 新しいノードIDを割り当てる
	[[nodiscard]] NodeId allocateId() noexcept
	{
		return m_nextId++;
	}

	/// @brief 二項演算ノードを作成する
	[[nodiscard]] std::unique_ptr<ExprNode> makeBinary(
		BinaryOp op,
		std::unique_ptr<ExprNode> lhs,
		std::unique_ptr<ExprNode> rhs)
	{
		auto node = std::make_unique<ExprNode>();
		node->m_id = allocateId();
		node->m_payload = BinaryPayload{op, std::move(lhs), std::move(rhs)};
		return node;
	}

	/// @brief 単項関数ノードを作成する
	[[nodiscard]] std::unique_ptr<ExprNode> makeUnary(
		UnaryFunc func,
		std::unique_ptr<ExprNode> child)
	{
		auto node = std::make_unique<ExprNode>();
		node->m_id = allocateId();
		node->m_payload = UnaryPayload{func, std::move(child)};
		return node;
	}
};

} // namespace sgc
