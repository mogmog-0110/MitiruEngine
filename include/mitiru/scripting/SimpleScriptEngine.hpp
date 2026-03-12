#pragma once

/// @file SimpleScriptEngine.hpp
/// @brief 軽量な式評価スクリプトエンジン
/// @details Lua等の外部依存なしに、算術式・比較・論理演算・if-then-else・
///          組み込み関数をサポートする簡易スクリプトエンジン。
///          AIエージェントが記述する簡単なゲームロジック用途を想定。

#include <mitiru/scripting/IScriptEngine.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mitiru::scripting
{

/// @brief トークン種別
enum class TokenType
{
	Number,		///< 数値リテラル
	String,		///< 文字列リテラル（"..."）
	Identifier,	///< 識別子（変数名・関数名・キーワード）
	Plus,		///< +
	Minus,		///< -
	Star,		///< *
	Slash,		///< /
	Percent,	///< %
	Assign,		///< =
	EqEq,		///< ==
	NotEq,		///< !=
	Less,		///< <
	LessEq,	///< <=
	Greater,	///< >
	GreaterEq,	///< >=
	And,		///< &&
	Or,			///< ||
	Not,		///< !
	LParen,	///< (
	RParen,	///< )
	Comma,		///< ,
	Semicolon,	///< ;
	LBrace,		///< {
	RBrace,		///< }
	Eof,		///< 入力終端
};

/// @brief トークン
struct Token
{
	TokenType type = TokenType::Eof;	///< トークン種別
	std::string text;					///< トークンのテキスト
	double numValue = 0.0;				///< 数値リテラルの場合の値
};

/// @brief 軽量式評価スクリプトエンジン
/// @details IScriptEngineインターフェースを実装する再帰下降パーサーベースの
///          式評価エンジン。変数ストレージ、関数レジストリ、
///          if/then/else構文をサポートする。
///
/// @code
/// SimpleScriptEngine engine;
/// engine.execute("x = 10");
/// engine.execute("y = x * 2 + 5");
/// double y = engine.getGlobalNumber("y"); // 25.0
///
/// engine.execute("if x > 5 then result = 1 else result = 0");
/// double r = engine.getGlobalNumber("result"); // 1.0
///
/// engine.execute("d = sqrt(x * x + y * y)");
/// @endcode
class SimpleScriptEngine final : public IScriptEngine
{
public:
	/// @brief 組み込み関数の型
	using NativeFunction = std::function<double(const std::vector<double>&)>;

	/// @brief コンストラクタ（組み込み関数を登録する）
	SimpleScriptEngine()
	{
		registerBuiltins();
	}

	// --- IScriptEngine インターフェース実装 ---

	/// @brief スクリプト文字列を実行する
	/// @param script 実行するスクリプト（セミコロンで複数文を区切れる）
	/// @return 成功なら true
	bool execute(std::string_view script) override
	{
		try
		{
			m_lastError.clear();
			m_hasReturned = false;
			m_returnValue = 0.0;
			auto tokens = tokenize(script);
			m_pos = 0;
			m_tokens = std::move(tokens);

			while (m_pos < m_tokens.size() && m_tokens[m_pos].type != TokenType::Eof && !m_hasReturned)
			{
				parseStatement();
				// セミコロンがあればスキップ
				if (m_pos < m_tokens.size() && m_tokens[m_pos].type == TokenType::Semicolon)
				{
					++m_pos;
				}
			}
			return true;
		}
		catch (const std::exception& e)
		{
			m_lastError = e.what();
			return false;
		}
	}

	/// @brief ファイルからスクリプトを実行する
	/// @param path ファイルパス
	/// @return 成功なら true
	bool executeFile(std::string_view path) override
	{
		const std::string filePath{path};
		std::ifstream file(filePath);
		if (!file.is_open())
		{
			m_lastError = "Cannot open file: " + filePath;
			return false;
		}
		const std::string content{
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>()};
		return execute(content);
	}

	/// @brief グローバル数値変数を設定する
	/// @param name 変数名
	/// @param value 値
	void setGlobal(std::string_view name, double value) override
	{
		m_numVars[std::string(name)] = value;
	}

	/// @brief グローバル文字列変数を設定する
	/// @param name 変数名
	/// @param value 値
	void setGlobal(std::string_view name, std::string_view value) override
	{
		m_strVars[std::string(name)] = std::string(value);
	}

	/// @brief グローバル数値変数を取得する
	/// @param name 変数名
	/// @return 変数の値（未定義の場合は 0.0）
	[[nodiscard]] double getGlobalNumber(std::string_view name) const override
	{
		auto it = m_numVars.find(std::string(name));
		if (it != m_numVars.end())
		{
			return it->second;
		}
		return 0.0;
	}

	/// @brief グローバル文字列変数を取得する
	/// @param name 変数名
	/// @return 変数の値（未定義の場合は空文字列）
	[[nodiscard]] std::string getGlobalString(std::string_view name) const override
	{
		auto it = m_strVars.find(std::string(name));
		if (it != m_strVars.end())
		{
			return it->second;
		}
		return "";
	}

	/// @brief グローバル関数を呼び出す（名前で登録済み関数を引数なしで呼ぶ）
	/// @param name 関数名
	/// @return 成功なら true
	bool callFunction(std::string_view name) override
	{
		auto it = m_functions.find(std::string(name));
		if (it == m_functions.end())
		{
			m_lastError = "Function not found: " + std::string(name);
			return false;
		}
		try
		{
			it->second({});
			return true;
		}
		catch (const std::exception& e)
		{
			m_lastError = e.what();
			return false;
		}
	}

	/// @brief 最後のエラーメッセージを取得する
	/// @return エラーメッセージ
	[[nodiscard]] std::string lastError() const override
	{
		return m_lastError;
	}

	// --- 拡張API ---

	/// @brief ネイティブ関数を登録する
	/// @param name 関数名
	/// @param func 関数本体
	void registerFunction(const std::string& name, NativeFunction func)
	{
		m_functions[name] = std::move(func);
	}

	/// @brief 変数を設定する（setGlobal の別名）
	/// @param name 変数名
	/// @param value 値
	void setVariable(const std::string& name, double value)
	{
		m_numVars[name] = value;
	}

	/// @brief 変数を取得する（getGlobalNumber の別名）
	/// @param name 変数名
	/// @return 変数の値
	[[nodiscard]] double getVariable(const std::string& name) const
	{
		auto it = m_numVars.find(name);
		return (it != m_numVars.end()) ? it->second : 0.0;
	}

	/// @brief 式を評価して数値を返す
	/// @param expr 式文字列
	/// @return 評価結果
	[[nodiscard]] double evaluate(std::string_view expr)
	{
		auto tokens = tokenize(expr);
		m_pos = 0;
		m_tokens = std::move(tokens);
		return parseExpression();
	}

private:
	/// @brief ユーザー定義関数
	struct UserFunction
	{
		std::vector<std::string> params;   ///< パラメータ名
		std::vector<Token> body;           ///< 関数本体トークン列
	};

	std::unordered_map<std::string, double> m_numVars;			///< 数値変数ストレージ
	std::unordered_map<std::string, std::string> m_strVars;		///< 文字列変数ストレージ
	std::unordered_map<std::string, NativeFunction> m_functions;///< 登録関数
	std::unordered_map<std::string, UserFunction> m_userFuncs;	///< ユーザー定義関数
	std::string m_lastError;									///< 最後のエラー
	std::vector<Token> m_tokens;								///< 現在のトークン列
	size_t m_pos = 0;											///< 現在の読み取り位置
	std::mt19937 m_rng{std::random_device{}()};					///< 乱数生成器
	double m_returnValue = 0.0;									///< return文の値
	bool m_hasReturned = false;									///< return発生フラグ

	/// @brief 組み込み関数を登録する
	void registerBuiltins()
	{
		m_functions["abs"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::abs(args[0]);
		};
		m_functions["min"] = [](const std::vector<double>& args) -> double
		{
			if (args.size() < 2) return args.empty() ? 0.0 : args[0];
			return std::min(args[0], args[1]);
		};
		m_functions["max"] = [](const std::vector<double>& args) -> double
		{
			if (args.size() < 2) return args.empty() ? 0.0 : args[0];
			return std::max(args[0], args[1]);
		};
		m_functions["sqrt"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::sqrt(args[0]);
		};
		m_functions["sin"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::sin(args[0]);
		};
		m_functions["cos"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::cos(args[0]);
		};
		m_functions["floor"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::floor(args[0]);
		};
		m_functions["ceil"] = [](const std::vector<double>& args) -> double
		{
			if (args.empty()) return 0.0;
			return std::ceil(args[0]);
		};
		m_functions["random"] = [this](const std::vector<double>& args) -> double
		{
			if (args.empty())
			{
				std::uniform_real_distribution<double> dist(0.0, 1.0);
				return dist(m_rng);
			}
			if (args.size() == 1)
			{
				std::uniform_real_distribution<double> dist(0.0, args[0]);
				return dist(m_rng);
			}
			std::uniform_real_distribution<double> dist(args[0], args[1]);
			return dist(m_rng);
		};
	}

	// ========== 字句解析 ==========

	/// @brief 文字列をトークン列に分解する
	/// @param source ソース文字列
	/// @return トークン列
	[[nodiscard]] std::vector<Token> tokenize(std::string_view source) const
	{
		std::vector<Token> tokens;
		size_t i = 0;

		while (i < source.size())
		{
			// 空白スキップ
			if (std::isspace(static_cast<unsigned char>(source[i])))
			{
				++i;
				continue;
			}

			// 数値リテラル
			if (std::isdigit(static_cast<unsigned char>(source[i])) || (source[i] == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
			{
				size_t start = i;
				while (i < source.size() && (std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.'))
				{
					++i;
				}
				Token tok;
				tok.type = TokenType::Number;
				tok.text = std::string(source.substr(start, i - start));
				tok.numValue = std::stod(tok.text);
				tokens.push_back(std::move(tok));
				continue;
			}

			// 識別子・キーワード
			if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '_')
			{
				size_t start = i;
				while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
				{
					++i;
				}
				Token tok;
				tok.type = TokenType::Identifier;
				tok.text = std::string(source.substr(start, i - start));
				tokens.push_back(std::move(tok));
				continue;
			}

			// 文字列リテラル
			if (source[i] == '"')
			{
				++i;
				size_t start = i;
				while (i < source.size() && source[i] != '"')
				{
					++i;
				}
				Token tok;
				tok.type = TokenType::String;
				tok.text = std::string(source.substr(start, i - start));
				tokens.push_back(std::move(tok));
				if (i < source.size()) ++i; // 閉じ引用符をスキップ
				continue;
			}

			// 2文字演算子
			if (i + 1 < source.size())
			{
				std::string_view two = source.substr(i, 2);
				if (two == "==") { tokens.push_back({TokenType::EqEq, "==", 0.0}); i += 2; continue; }
				if (two == "!=") { tokens.push_back({TokenType::NotEq, "!=", 0.0}); i += 2; continue; }
				if (two == "<=") { tokens.push_back({TokenType::LessEq, "<=", 0.0}); i += 2; continue; }
				if (two == ">=") { tokens.push_back({TokenType::GreaterEq, ">=", 0.0}); i += 2; continue; }
				if (two == "&&") { tokens.push_back({TokenType::And, "&&", 0.0}); i += 2; continue; }
				if (two == "||") { tokens.push_back({TokenType::Or, "||", 0.0}); i += 2; continue; }
			}

			// 1文字演算子
			switch (source[i])
			{
			case '+': tokens.push_back({TokenType::Plus, "+", 0.0}); break;
			case '-': tokens.push_back({TokenType::Minus, "-", 0.0}); break;
			case '*': tokens.push_back({TokenType::Star, "*", 0.0}); break;
			case '/': tokens.push_back({TokenType::Slash, "/", 0.0}); break;
			case '%': tokens.push_back({TokenType::Percent, "%", 0.0}); break;
			case '=': tokens.push_back({TokenType::Assign, "=", 0.0}); break;
			case '<': tokens.push_back({TokenType::Less, "<", 0.0}); break;
			case '>': tokens.push_back({TokenType::Greater, ">", 0.0}); break;
			case '!': tokens.push_back({TokenType::Not, "!", 0.0}); break;
			case '(': tokens.push_back({TokenType::LParen, "(", 0.0}); break;
			case ')': tokens.push_back({TokenType::RParen, ")", 0.0}); break;
			case ',': tokens.push_back({TokenType::Comma, ",", 0.0}); break;
			case ';': tokens.push_back({TokenType::Semicolon, ";", 0.0}); break;
			case '{': tokens.push_back({TokenType::LBrace, "{", 0.0}); break;
			case '}': tokens.push_back({TokenType::RBrace, "}", 0.0}); break;
			default:
				throw std::runtime_error("Unexpected character: " + std::string(1, source[i]));
			}
			++i;
		}

		tokens.push_back({TokenType::Eof, "", 0.0});
		return tokens;
	}

	// ========== 構文解析・評価 ==========

	/// @brief 現在のトークンを返す
	[[nodiscard]] const Token& current() const
	{
		return m_tokens[m_pos];
	}

	/// @brief 現在のトークンを消費して次へ進む
	/// @return 消費したトークン
	Token advance()
	{
		Token tok = m_tokens[m_pos];
		if (m_pos < m_tokens.size() - 1)
		{
			++m_pos;
		}
		return tok;
	}

	/// @brief 指定種別のトークンを期待して消費する
	/// @param type 期待するトークン種別
	/// @return 消費したトークン
	Token expect(TokenType type)
	{
		if (current().type != type)
		{
			throw std::runtime_error(
				"Expected token type " + std::to_string(static_cast<int>(type))
				+ " but got '" + current().text + "'"
			);
		}
		return advance();
	}

	/// @brief 文を解析・実行する
	void parseStatement()
	{
		if (m_hasReturned)
		{
			return;
		}

		// ブロック文 { ... }
		if (current().type == TokenType::LBrace)
		{
			parseBlock();
			return;
		}

		// if/then/else 文
		if (current().type == TokenType::Identifier && current().text == "if")
		{
			parseIfStatement();
			return;
		}

		// while文
		if (current().type == TokenType::Identifier && current().text == "while")
		{
			parseWhileStatement();
			return;
		}

		// for文
		if (current().type == TokenType::Identifier && current().text == "for")
		{
			parseForStatement();
			return;
		}

		// function定義
		if (current().type == TokenType::Identifier && current().text == "function")
		{
			parseFunctionDef();
			return;
		}

		// return文
		if (current().type == TokenType::Identifier && current().text == "return")
		{
			advance();
			m_returnValue = parseExpression();
			m_hasReturned = true;
			return;
		}

		// 代入文または式文
		if (current().type == TokenType::Identifier && m_pos + 1 < m_tokens.size() && m_tokens[m_pos + 1].type == TokenType::Assign)
		{
			std::string varName = advance().text;
			advance(); // '=' をスキップ

			// 右辺が文字列リテラルの場合
			if (current().type == TokenType::String)
			{
				m_strVars[varName] = advance().text;
			}
			else
			{
				double value = parseExpression();
				m_numVars[varName] = value;
			}
			return;
		}

		// 式文（副作用のある式）
		parseExpression();
	}

	/// @brief ブロック { ... } を解析・実行する
	void parseBlock()
	{
		expect(TokenType::LBrace);
		while (current().type != TokenType::RBrace &&
			current().type != TokenType::Eof &&
			!m_hasReturned)
		{
			parseStatement();
			if (current().type == TokenType::Semicolon)
			{
				advance();
			}
		}
		if (current().type == TokenType::RBrace)
		{
			advance();
		}
	}

	/// @brief while文を解析・実行する
	void parseWhileStatement()
	{
		advance(); // 'while' をスキップ

		/// ループ開始位置を記憶する
		const size_t condStart = m_pos;

		/// 最大反復回数（無限ループ防止）
		constexpr int MAX_ITERATIONS = 100000;
		int iterations = 0;

		while (iterations < MAX_ITERATIONS && !m_hasReturned)
		{
			/// 条件式を評価する
			m_pos = condStart;
			expect(TokenType::LParen);
			double condition = parseExpression();
			expect(TokenType::RParen);

			if (condition == 0.0)
			{
				/// 条件が偽ならブロックをスキップして終了
				skipBlock();
				break;
			}

			/// ブロックを実行する
			parseBlock();
			++iterations;
		}

		if (iterations >= MAX_ITERATIONS)
		{
			throw std::runtime_error("While loop exceeded maximum iterations");
		}
	}

	/// @brief for文を解析・実行する
	/// @details for (init; condition; step) { body }
	void parseForStatement()
	{
		advance(); // 'for' をスキップ
		expect(TokenType::LParen);

		/// 初期化
		parseStatement();
		expect(TokenType::Semicolon);

		/// 条件式の位置を記憶する
		const size_t condStart = m_pos;

		/// まず条件を評価してbodyとstepの位置を特定する
		double condition = parseExpression();
		expect(TokenType::Semicolon);
		const size_t stepStart = m_pos;

		/// stepを一旦スキップして ) の位置を見つける
		int parenDepth = 1;
		while (m_pos < m_tokens.size() && parenDepth > 0)
		{
			if (current().type == TokenType::LParen)
			{
				++parenDepth;
			}
			else if (current().type == TokenType::RParen)
			{
				--parenDepth;
			}
			if (parenDepth > 0)
			{
				advance();
			}
		}
		expect(TokenType::RParen);
		const size_t bodyStart = m_pos;

		constexpr int MAX_ITERATIONS = 100000;
		int iterations = 0;

		/// 最初の条件チェック
		if (condition == 0.0)
		{
			skipBlock();
			return;
		}

		while (iterations < MAX_ITERATIONS && !m_hasReturned)
		{
			/// ブロックを実行する
			m_pos = bodyStart;
			parseBlock();

			/// ステップを実行する
			m_pos = stepStart;
			parseStatement();

			/// 条件を再評価する
			m_pos = condStart;
			condition = parseExpression();

			if (condition == 0.0)
			{
				break;
			}
			++iterations;
		}

		/// bodyの後に戻る
		m_pos = bodyStart;
		skipBlock();
	}

	/// @brief function定義を解析する
	/// @details function name(a, b) { body }
	void parseFunctionDef()
	{
		advance(); // 'function' をスキップ
		const std::string funcName = expect(TokenType::Identifier).text;

		expect(TokenType::LParen);
		std::vector<std::string> params;
		if (current().type != TokenType::RParen)
		{
			params.push_back(expect(TokenType::Identifier).text);
			while (current().type == TokenType::Comma)
			{
				advance();
				params.push_back(expect(TokenType::Identifier).text);
			}
		}
		expect(TokenType::RParen);

		/// ブロック本体のトークンを収集する
		if (current().type != TokenType::LBrace)
		{
			throw std::runtime_error("Expected '{' in function body");
		}

		std::vector<Token> bodyTokens;
		int braceDepth = 0;
		do
		{
			if (current().type == TokenType::LBrace) ++braceDepth;
			if (current().type == TokenType::RBrace) --braceDepth;
			bodyTokens.push_back(current());
			advance();
		} while (braceDepth > 0 && current().type != TokenType::Eof);

		/// ユーザー定義関数を登録する
		UserFunction uf;
		uf.params = std::move(params);
		uf.body = std::move(bodyTokens);
		m_userFuncs[funcName] = std::move(uf);

		/// ネイティブ関数としてラッパーを登録する
		m_functions[funcName] = [this, funcName](
			const std::vector<double>& args) -> double
		{
			auto it = m_userFuncs.find(funcName);
			if (it == m_userFuncs.end())
			{
				return 0.0;
			}
			return callUserFunction(it->second, args);
		};
	}

	/// @brief ユーザー定義関数を呼び出す
	/// @param func 関数定義
	/// @param args 引数
	/// @return 戻り値
	double callUserFunction(const UserFunction& func,
		const std::vector<double>& args)
	{
		/// 現在の状態を保存する
		const auto savedTokens = m_tokens;
		const auto savedPos = m_pos;
		const auto savedReturn = m_hasReturned;
		const auto savedReturnVal = m_returnValue;

		/// パラメータを変数に代入する（元の値を保存）
		std::vector<std::pair<std::string, double>> savedVars;
		for (size_t i = 0; i < func.params.size(); ++i)
		{
			const auto& name = func.params[i];
			auto varIt = m_numVars.find(name);
			if (varIt != m_numVars.end())
			{
				savedVars.push_back({name, varIt->second});
			}
			else
			{
				savedVars.push_back({name, 0.0});
			}
			m_numVars[name] = (i < args.size()) ? args[i] : 0.0;
		}

		/// 関数本体を実行する
		m_tokens = func.body;
		m_tokens.push_back({TokenType::Eof, "", 0.0});
		m_pos = 0;
		m_hasReturned = false;
		m_returnValue = 0.0;

		parseBlock();

		const double result = m_returnValue;

		/// パラメータを復元する
		for (const auto& [name, val] : savedVars)
		{
			m_numVars[name] = val;
		}

		/// 状態を復元する
		m_tokens = savedTokens;
		m_pos = savedPos;
		m_hasReturned = savedReturn;
		m_returnValue = savedReturnVal;

		return result;
	}

	/// @brief ブロック { ... } をスキップする
	void skipBlock()
	{
		if (current().type != TokenType::LBrace)
		{
			return;
		}
		int depth = 0;
		do
		{
			if (current().type == TokenType::LBrace) ++depth;
			if (current().type == TokenType::RBrace) --depth;
			advance();
		} while (depth > 0 && current().type != TokenType::Eof);
	}

	/// @brief if/then/else 文を解析・実行する
	void parseIfStatement()
	{
		advance(); // 'if' をスキップ
		double condition = parseExpression();

		// 'then' を期待
		if (current().type != TokenType::Identifier || current().text != "then")
		{
			throw std::runtime_error("Expected 'then' after if condition");
		}
		advance();

		if (condition != 0.0)
		{
			// then節を実行
			parseStatement();
			// else節をスキップ
			if (current().type == TokenType::Identifier && current().text == "else")
			{
				advance();
				skipStatement();
			}
		}
		else
		{
			// then節をスキップ
			skipStatement();
			// else節を実行
			if (current().type == TokenType::Identifier && current().text == "else")
			{
				advance();
				parseStatement();
			}
		}
	}

	/// @brief 文を読み飛ばす（if/then/else の未実行ブランチ用）
	void skipStatement()
	{
		// 代入文
		if (current().type == TokenType::Identifier && m_pos + 1 < m_tokens.size() && m_tokens[m_pos + 1].type == TokenType::Assign)
		{
			advance(); // 変数名
			advance(); // '='
			if (current().type == TokenType::String)
			{
				advance();
			}
			else
			{
				parseExpression(); // 式を評価するが結果は捨てる（副作用なしの式なので問題ない）
			}
			return;
		}
		// 式文
		parseExpression();
	}

	/// @brief 式を解析する（最低優先度: ||）
	/// @return 評価結果
	double parseExpression()
	{
		return parseOr();
	}

	/// @brief || 演算を解析する
	double parseOr()
	{
		double left = parseAnd();
		while (current().type == TokenType::Or)
		{
			advance();
			double right = parseAnd();
			left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
		}
		return left;
	}

	/// @brief && 演算を解析する
	double parseAnd()
	{
		double left = parseEquality();
		while (current().type == TokenType::And)
		{
			advance();
			double right = parseEquality();
			left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
		}
		return left;
	}

	/// @brief ==, != 演算を解析する
	double parseEquality()
	{
		double left = parseComparison();
		while (current().type == TokenType::EqEq || current().type == TokenType::NotEq)
		{
			auto op = advance().type;
			double right = parseComparison();
			if (op == TokenType::EqEq)
			{
				left = (left == right) ? 1.0 : 0.0;
			}
			else
			{
				left = (left != right) ? 1.0 : 0.0;
			}
		}
		return left;
	}

	/// @brief <, >, <=, >= 演算を解析する
	double parseComparison()
	{
		double left = parseAddSub();
		while (current().type == TokenType::Less || current().type == TokenType::Greater
			|| current().type == TokenType::LessEq || current().type == TokenType::GreaterEq)
		{
			auto op = advance().type;
			double right = parseAddSub();
			switch (op)
			{
			case TokenType::Less:		left = (left < right) ? 1.0 : 0.0; break;
			case TokenType::Greater:	left = (left > right) ? 1.0 : 0.0; break;
			case TokenType::LessEq:		left = (left <= right) ? 1.0 : 0.0; break;
			case TokenType::GreaterEq:	left = (left >= right) ? 1.0 : 0.0; break;
			default: break;
			}
		}
		return left;
	}

	/// @brief +, - 演算を解析する
	double parseAddSub()
	{
		double left = parseMulDiv();
		while (current().type == TokenType::Plus || current().type == TokenType::Minus)
		{
			auto op = advance().type;
			double right = parseMulDiv();
			if (op == TokenType::Plus)
			{
				left += right;
			}
			else
			{
				left -= right;
			}
		}
		return left;
	}

	/// @brief *, /, % 演算を解析する
	double parseMulDiv()
	{
		double left = parseUnary();
		while (current().type == TokenType::Star || current().type == TokenType::Slash || current().type == TokenType::Percent)
		{
			auto op = advance().type;
			double right = parseUnary();
			if (op == TokenType::Star)
			{
				left *= right;
			}
			else if (op == TokenType::Slash)
			{
				if (right == 0.0)
				{
					throw std::runtime_error("Division by zero");
				}
				left /= right;
			}
			else
			{
				if (right == 0.0)
				{
					throw std::runtime_error("Modulo by zero");
				}
				left = std::fmod(left, right);
			}
		}
		return left;
	}

	/// @brief 単項演算（-, !）を解析する
	double parseUnary()
	{
		if (current().type == TokenType::Minus)
		{
			advance();
			return -parseUnary();
		}
		if (current().type == TokenType::Not)
		{
			advance();
			double val = parseUnary();
			return (val == 0.0) ? 1.0 : 0.0;
		}
		return parsePrimary();
	}

	/// @brief 一次式（数値、変数、関数呼び出し、括弧式）を解析する
	double parsePrimary()
	{
		// 数値リテラル
		if (current().type == TokenType::Number)
		{
			return advance().numValue;
		}

		// 括弧式
		if (current().type == TokenType::LParen)
		{
			advance();
			double val = parseExpression();
			expect(TokenType::RParen);
			return val;
		}

		// 識別子（変数または関数呼び出し）
		if (current().type == TokenType::Identifier)
		{
			std::string name = advance().text;

			// true / false リテラル
			if (name == "true") return 1.0;
			if (name == "false") return 0.0;

			// 関数呼び出し
			if (current().type == TokenType::LParen)
			{
				advance(); // '('
				std::vector<double> args;
				if (current().type != TokenType::RParen)
				{
					args.push_back(parseExpression());
					while (current().type == TokenType::Comma)
					{
						advance();
						args.push_back(parseExpression());
					}
				}
				expect(TokenType::RParen);

				auto it = m_functions.find(name);
				if (it == m_functions.end())
				{
					throw std::runtime_error("Unknown function: " + name);
				}
				return it->second(args);
			}

			// 変数参照
			auto it = m_numVars.find(name);
			if (it != m_numVars.end())
			{
				return it->second;
			}
			return 0.0; // 未定義変数は 0.0
		}

		throw std::runtime_error("Unexpected token: " + current().text);
	}
};

} // namespace mitiru::scripting
