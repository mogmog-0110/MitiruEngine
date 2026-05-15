#pragma once

/// @file CommandSystem.hpp
/// @brief 統合コマンドシステム
/// @details GUI/CLI/スクリプトすべてのパスから同一のコマンドを実行できる
///          中央レジストリ。コマンド登録・検索・実行・履歴・マクロ記録再生を提供する。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace mitiru
{

// ── コマンド引数型 ─────────────────────────────────

/// @brief コマンド引数（バリアント型）
using CommandArg = std::variant<std::monostate, bool, int, float, std::string>;

// ── コマンド結果 ──────────────────────────────────

/// @brief コマンド実行結果
struct CommandResult
{
	bool success = true;                ///< 成功したかどうか
	std::string message;                ///< 結果メッセージ（エラー理由等）
	std::vector<std::string> output;    ///< 複数行出力

	/// @brief 成功結果を生成する
	static CommandResult ok(const std::string& msg = {})
	{
		return {true, msg, {}};
	}

	/// @brief 成功結果を生成する（複数行出力付き）
	static CommandResult ok(const std::string& msg, const std::vector<std::string>& lines)
	{
		return {true, msg, lines};
	}

	/// @brief 失敗結果を生成する
	static CommandResult fail(const std::string& msg)
	{
		return {false, msg, {}};
	}
};

// ── コマンド定義 ──────────────────────────────────

/// @brief コマンド定義
struct CommandDef
{
	std::string name;                     ///< "scene.add_node" 形式の完全修飾名
	std::string category;                 ///< "scene" 等のカテゴリ名
	std::string description;              ///< コマンドの説明
	std::string usage;                    ///< 使用例 "scene.add_node <name> [parent_id]"
	std::vector<std::string> argNames;    ///< 引数名 {"name", "parent_id"}
	std::vector<std::string> argTypes;    ///< 引数型 {"string", "int"}
	std::vector<bool> argRequired;        ///< 必須フラグ {true, false}

	/// @brief コマンド実行関数
	std::function<CommandResult(const std::vector<CommandArg>&)> execute;
};

// ── コマンドシステム ─────────────────────────────────

/// @brief 統合コマンドレジストリ・実行エンジン
/// @details エンジン内のすべての操作をコマンドとして登録し、
///          GUI・CLI・スクリプトから統一的に実行できる。
///
/// @code
/// mitiru::CommandSystem cmd;
///
/// // コマンド登録
/// mitiru::CommandDef def;
/// def.name = "system.fps";
/// def.category = "system";
/// def.description = "Print current FPS";
/// def.usage = "system.fps";
/// def.execute = [](const std::vector<mitiru::CommandArg>&) {
///     return mitiru::CommandResult::ok("60 FPS");
/// };
/// cmd.registerCommand(def);
///
/// // テキストから実行
/// auto result = cmd.executeString("system.fps");
/// @endcode
class CommandSystem
{
public:
	/// @brief コマンドを登録する
	/// @param def コマンド定義
	void registerCommand(const CommandDef& def)
	{
		m_commandMap[def.name] = m_commands.size();
		m_commands.push_back(def);
	}

	/// @brief コマンドを名前と引数で実行する
	/// @param name コマンド名
	/// @param args 引数リスト
	/// @return 実行結果
	CommandResult execute(const std::string& name,
	                      const std::vector<CommandArg>& args = {})
	{
		const auto* def = findCommand(name);
		if (!def)
		{
			return CommandResult::fail("Unknown command: " + name);
		}

		// 必須引数チェック
		for (std::size_t i = 0; i < def->argRequired.size(); ++i)
		{
			if (def->argRequired[i] && i >= args.size())
			{
				return CommandResult::fail(
					"Missing required argument: " + def->argNames[i]
					+ "\nUsage: " + def->usage);
			}
		}

		// マクロ記録中ならコマンドラインを記録する
		if (m_recording)
		{
			m_currentMacroLines.push_back(
				reconstructCommandLine(name, args));
		}

		return def->execute(args);
	}

	/// @brief テキストからコマンドを解析・実行する
	/// @param commandLine "scene.add_node MyNode 0" 形式のテキスト
	/// @return 実行結果
	CommandResult executeString(const std::string& commandLine)
	{
		const auto trimmed = trim(commandLine);
		if (trimmed.empty() || trimmed[0] == '#')
		{
			return CommandResult::ok();
		}

		const auto tokens = tokenize(trimmed);
		if (tokens.empty())
		{
			return CommandResult::ok();
		}

		const auto& cmdName = tokens[0];
		const auto* def = findCommand(cmdName);
		if (!def)
		{
			return CommandResult::fail("Unknown command: " + cmdName);
		}

		// トークンを型変換して引数リストを構築する
		std::vector<CommandArg> args;
		for (std::size_t i = 1; i < tokens.size(); ++i)
		{
			const std::size_t argIdx = i - 1;
			if (argIdx < def->argTypes.size())
			{
				args.push_back(parseArg(tokens[i], def->argTypes[argIdx]));
			}
			else
			{
				// 定義外の追加引数は文字列として渡す
				args.push_back(tokens[i]);
			}
		}

		// 履歴に追加
		pushHistory(trimmed);

		// マクロ記録中なら生テキストを記録する
		if (m_recording)
		{
			m_currentMacroLines.push_back(trimmed);
		}
		else
		{
			// execute() 経由だと二重記録されるので直接実行する
			return def->execute(args);
		}

		return def->execute(args);
	}

	/// @brief 登録済みコマンド一覧を取得する
	/// @return コマンド定義のベクタ
	[[nodiscard]] const std::vector<CommandDef>& commands() const noexcept
	{
		return m_commands;
	}

	/// @brief 指定カテゴリのコマンドを取得する
	/// @param category カテゴリ名
	/// @return コマンド定義へのポインタのベクタ
	[[nodiscard]] std::vector<const CommandDef*> commandsInCategory(
		const std::string& category) const
	{
		std::vector<const CommandDef*> result;
		for (const auto& cmd : m_commands)
		{
			if (cmd.category == category)
			{
				result.push_back(&cmd);
			}
		}
		return result;
	}

	/// @brief 全カテゴリ名を取得する（ソート済み・重複なし）
	/// @return カテゴリ名のベクタ
	[[nodiscard]] std::vector<std::string> categories() const
	{
		std::vector<std::string> result;
		for (const auto& cmd : m_commands)
		{
			if (std::find(result.begin(), result.end(), cmd.category) == result.end())
			{
				result.push_back(cmd.category);
			}
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	/// @brief 名前でコマンドを検索する
	/// @param name コマンド名
	/// @return コマンド定義へのポインタ（見つからなければnullptr）
	[[nodiscard]] const CommandDef* findCommand(const std::string& name) const
	{
		const auto it = m_commandMap.find(name);
		if (it == m_commandMap.end())
		{
			return nullptr;
		}
		return &m_commands[it->second];
	}

	/// @brief 前方一致でオートコンプリート候補を返す
	/// @param prefix 入力途中のテキスト
	/// @return マッチするコマンド名のベクタ
	[[nodiscard]] std::vector<std::string> autocomplete(const std::string& prefix) const
	{
		std::vector<std::string> matches;
		const auto lowerPrefix = toLower(prefix);
		for (const auto& cmd : m_commands)
		{
			if (toLower(cmd.name).rfind(lowerPrefix, 0) == 0)
			{
				matches.push_back(cmd.name);
			}
		}
		std::sort(matches.begin(), matches.end());
		return matches;
	}

	/// @brief 部分一致でコマンドを検索する（コマンドパレット用）
	/// @param query 検索文字列
	/// @return マッチするコマンド定義へのポインタのベクタ
	[[nodiscard]] std::vector<const CommandDef*> fuzzySearch(const std::string& query) const
	{
		std::vector<const CommandDef*> result;
		const auto lowerQuery = toLower(query);
		for (const auto& cmd : m_commands)
		{
			const auto lowerName = toLower(cmd.name);
			const auto lowerDesc = toLower(cmd.description);
			if (lowerName.find(lowerQuery) != std::string::npos
			    || lowerDesc.find(lowerQuery) != std::string::npos)
			{
				result.push_back(&cmd);
			}
		}
		return result;
	}

	// ── 履歴 ──────────────────────────────────

	/// @brief コマンド履歴に追加する
	/// @param line コマンドライン文字列
	void pushHistory(const std::string& line)
	{
		m_history.push_back(line);
		if (m_history.size() > kMaxHistory)
		{
			m_history.erase(m_history.begin());
		}
	}

	/// @brief コマンド履歴を取得する
	/// @return 履歴のベクタ
	[[nodiscard]] const std::vector<std::string>& history() const noexcept
	{
		return m_history;
	}

	/// @brief 履歴をクリアする
	void clearHistory() noexcept
	{
		m_history.clear();
	}

	// ── バッチ実行 ─────────────────────────────────

	/// @brief 複数行スクリプトをバッチ実行する
	/// @param multiLineScript 改行区切りのコマンド列
	/// @return 各コマンドの実行結果
	std::vector<CommandResult> executeBatch(const std::string& multiLineScript)
	{
		std::vector<CommandResult> results;
		std::istringstream stream(multiLineScript);
		std::string line;
		while (std::getline(stream, line))
		{
			const auto trimmed = trim(line);
			if (trimmed.empty() || trimmed[0] == '#')
			{
				continue;
			}
			results.push_back(executeString(trimmed));
		}
		return results;
	}

	// ── マクロ記録・再生 ──────────────────────────────

	/// @brief マクロ記録を開始する
	/// @param macroName マクロ名
	void beginRecording(const std::string& macroName)
	{
		m_recording = true;
		m_currentMacroName = macroName;
		m_currentMacroLines.clear();
	}

	/// @brief マクロ記録を終了する
	void endRecording()
	{
		if (m_recording)
		{
			m_macros[m_currentMacroName] = m_currentMacroLines;
			m_recording = false;
			m_currentMacroName.clear();
			m_currentMacroLines.clear();
		}
	}

	/// @brief マクロを再生する
	/// @param macroName マクロ名
	void playMacro(const std::string& macroName)
	{
		const auto it = m_macros.find(macroName);
		if (it == m_macros.end())
		{
			return;
		}
		for (const auto& line : it->second)
		{
			executeString(line);
		}
	}

	/// @brief マクロのコマンド列を取得する
	/// @param name マクロ名
	/// @return コマンド列（見つからなければ空ベクタ参照）
	[[nodiscard]] const std::vector<std::string>& getMacro(const std::string& name) const
	{
		const auto it = m_macros.find(name);
		if (it != m_macros.end())
		{
			return it->second;
		}
		static const std::vector<std::string> kEmpty;
		return kEmpty;
	}

	/// @brief マクロ名一覧を取得する
	/// @return マクロ名のベクタ
	[[nodiscard]] std::vector<std::string> macroNames() const
	{
		std::vector<std::string> names;
		for (const auto& [name, lines] : m_macros)
		{
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	/// @brief マクロ記録中かどうか
	[[nodiscard]] bool isRecording() const noexcept
	{
		return m_recording;
	}

	/// @brief 登録済みコマンド数を取得する
	[[nodiscard]] std::size_t commandCount() const noexcept
	{
		return m_commands.size();
	}

private:
	static constexpr std::size_t kMaxHistory = 200;

	std::vector<CommandDef> m_commands;                         ///< 登録済みコマンド
	std::unordered_map<std::string, std::size_t> m_commandMap;  ///< 名前→インデックス
	std::vector<std::string> m_history;                         ///< コマンド履歴

	// マクロ
	bool m_recording = false;                                    ///< 記録中フラグ
	std::string m_currentMacroName;                              ///< 記録中マクロ名
	std::vector<std::string> m_currentMacroLines;                ///< 記録中コマンド列
	std::unordered_map<std::string, std::vector<std::string>> m_macros; ///< マクロストア

	// ── ユーティリティ ───────────────────────────────

	/// @brief 文字列をトークンに分割する（引用符対応）
	[[nodiscard]] static std::vector<std::string> tokenize(const std::string& line)
	{
		std::vector<std::string> tokens;
		std::string current;
		bool inQuote = false;
		char quoteChar = '\0';

		for (std::size_t i = 0; i < line.size(); ++i)
		{
			const char c = line[i];

			if (inQuote)
			{
				if (c == quoteChar)
				{
					inQuote = false;
				}
				else
				{
					current += c;
				}
			}
			else if (c == '"' || c == '\'')
			{
				inQuote = true;
				quoteChar = c;
			}
			else if (c == ' ' || c == '\t')
			{
				if (!current.empty())
				{
					tokens.push_back(current);
					current.clear();
				}
			}
			else
			{
				current += c;
			}
		}
		if (!current.empty())
		{
			tokens.push_back(current);
		}
		return tokens;
	}

	/// @brief 型名に基づいてトークンをCommandArgに変換する
	[[nodiscard]] static CommandArg parseArg(const std::string& token,
	                                         const std::string& typeName)
	{
		if (typeName == "bool")
		{
			return (token == "true" || token == "1" || token == "on");
		}
		if (typeName == "int")
		{
			try
			{
				return std::stoi(token);
			}
			catch (...)
			{
				return 0;
			}
		}
		if (typeName == "float")
		{
			try
			{
				return std::stof(token);
			}
			catch (...)
			{
				return 0.0f;
			}
		}
		return token;
	}

	/// @brief 引数リストからコマンドラインを再構築する
	[[nodiscard]] static std::string reconstructCommandLine(
		const std::string& name, const std::vector<CommandArg>& args)
	{
		std::string line = name;
		for (const auto& arg : args)
		{
			line += ' ';
			std::visit([&line](const auto& v) {
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, std::monostate>)
				{
					// skip
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					line += v ? "true" : "false";
				}
				else if constexpr (std::is_same_v<T, int>)
				{
					line += std::to_string(v);
				}
				else if constexpr (std::is_same_v<T, float>)
				{
					line += std::to_string(v);
				}
				else if constexpr (std::is_same_v<T, std::string>)
				{
					if (v.find(' ') != std::string::npos)
					{
						line += '"' + v + '"';
					}
					else
					{
						line += v;
					}
				}
			}, arg);
		}
		return line;
	}

	/// @brief 先頭・末尾の空白を除去する
	[[nodiscard]] static std::string trim(const std::string& s)
	{
		const auto start = s.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
		{
			return {};
		}
		const auto end = s.find_last_not_of(" \t\r\n");
		return s.substr(start, end - start + 1);
	}

	/// @brief 小文字に変換する
	[[nodiscard]] static std::string toLower(const std::string& s)
	{
		std::string result = s;
		std::transform(result.begin(), result.end(), result.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}
};

} // namespace mitiru
