#pragma once

/// @file CommandScript.hpp
/// @brief .mcmd コマンドスクリプトのロード・実行
/// @details MitiruEngineコマンドスクリプト（.mcmd）を読み込み、
///          CommandSystem経由で逐次実行する。変数置換・条件分岐・
///          ループ・遅延・エコー出力をサポートする。
///
/// スクリプト書式:
///   - 1行 = 1コマンド
///   - '#' で始まる行はコメント
///   - 空行は無視
///   - '$name' は変数参照（set コマンドで定義）
///   - 組み込み命令: set, if/endif, repeat/endrepeat, wait, echo

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mitiru
{

// Forward declaration
class CommandSystem;

/// @brief コマンド実行結果
struct CommandResult
{
	bool success = false;     ///< 実行成功フラグ
	std::string message;      ///< 結果メッセージ
};

/// @brief .mcmd スクリプトの1行パース結果
struct ScriptLine
{
	std::string raw;          ///< 元テキスト
	int lineNumber = 0;       ///< 行番号（1始まり）
};

/// @brief .mcmd コマンドスクリプトエンジン
/// @details ファイルまたは文字列からスクリプトを読み込み、
///          CommandSystem経由でコマンドを逐次実行する。
///
/// @code
/// mitiru::CommandScript script;
/// script.loadFromFile("setup.mcmd");
/// auto results = script.execute(cmdSystem);
/// @endcode
///
/// スクリプト例:
/// @code
/// # Setup scene for testing
/// set output_dir screenshots
/// scene.load "test_scene.json"
/// render.shader toon
/// render.outline hull 2.0 0 0 0
/// audio.volume bgm 0.5
/// physics.gravity 0 -9.81 0
/// system.screenshot "$output_dir/test_output.png"
/// echo "Test setup complete"
///
/// repeat 3
///   scene.step
///   system.screenshot "$output_dir/frame_$i.png"
/// endrepeat
///
/// if $debug_mode == true
///   render.wireframe on
/// endif
/// @endcode
class CommandScript
{
public:
	/// @brief echo出力コールバック型
	using EchoHandler = std::function<void(const std::string&)>;

	/// @brief wait実行コールバック型（秒数を受け取る）
	using WaitHandler = std::function<void(float)>;

	/// @brief ファイルからスクリプトを読み込む
	/// @param path ファイルパス（.mcmd）
	/// @return 読み込み成功なら true
	bool loadFromFile(const std::string& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			return false;
		}

		m_lines.clear();
		std::string line;
		int lineNum = 0;
		while (std::getline(file, line))
		{
			++lineNum;
			m_lines.push_back({line, lineNum});
		}
		m_sourcePath = path;
		return true;
	}

	/// @brief 文字列からスクリプトを読み込む
	/// @param text スクリプト全文
	void loadFromString(const std::string& text)
	{
		m_lines.clear();
		std::istringstream stream(text);
		std::string line;
		int lineNum = 0;
		while (std::getline(stream, line))
		{
			++lineNum;
			m_lines.push_back({line, lineNum});
		}
		m_sourcePath = "<string>";
	}

	/// @brief スクリプトを実行する
	/// @param cmdSystem CommandSystem のインスタンス
	/// @return 各コマンドの実行結果
	[[nodiscard]] std::vector<CommandResult> execute(CommandSystem& cmdSystem)
	{
		std::vector<CommandResult> results;
		m_variables.clear();

		executeBlock(cmdSystem, results, 0, m_lines.size());

		return results;
	}

	/// @brief echo出力ハンドラを設定する
	/// @param handler 出力ハンドラ
	void setEchoHandler(EchoHandler handler)
	{
		m_echoHandler = std::move(handler);
	}

	/// @brief wait実行ハンドラを設定する
	/// @param handler 待機ハンドラ
	/// @details デフォルトは std::this_thread::sleep_for を使用する。
	///          テスト時にはノーオペレーションハンドラを設定できる。
	void setWaitHandler(WaitHandler handler)
	{
		m_waitHandler = std::move(handler);
	}

	/// @brief 変数を設定する（スクリプト実行前に外部から注入）
	/// @param name 変数名
	/// @param value 変数値
	void setVariable(const std::string& name, const std::string& value)
	{
		m_variables[name] = value;
	}

	/// @brief 変数の値を取得する
	/// @param name 変数名
	/// @return 変数値。未定義の場合は空文字列
	[[nodiscard]] std::string getVariable(const std::string& name) const
	{
		const auto it = m_variables.find(name);
		if (it != m_variables.end())
		{
			return it->second;
		}
		return {};
	}

	/// @brief 読み込み済み行数を取得する
	/// @return 行数
	[[nodiscard]] std::size_t lineCount() const noexcept
	{
		return m_lines.size();
	}

	/// @brief ソースパスを取得する
	/// @return ファイルパスまたは "<string>"
	[[nodiscard]] const std::string& sourcePath() const noexcept
	{
		return m_sourcePath;
	}

private:
	/// @brief ブロック実行（再帰対応）
	/// @param cmdSystem CommandSystem のインスタンス
	/// @param results 結果バッファ
	/// @param begin 開始行インデックス
	/// @param end 終了行インデックス（排他）
	void executeBlock(CommandSystem& cmdSystem,
	                  std::vector<CommandResult>& results,
	                  std::size_t begin, std::size_t end)
	{
		std::size_t i = begin;
		while (i < end)
		{
			const auto& scriptLine = m_lines[i];
			const std::string line = trimLine(scriptLine.raw);

			// 空行とコメントをスキップ
			if (line.empty() || line[0] == '#')
			{
				++i;
				continue;
			}

			// 変数置換
			const std::string expanded = substituteVariables(line);

			// 組み込み: set <name> <value>
			if (expanded.rfind("set ", 0) == 0)
			{
				handleSet(expanded);
				++i;
				continue;
			}

			// 組み込み: echo <message>
			if (expanded.rfind("echo ", 0) == 0)
			{
				handleEcho(expanded.substr(5));
				++i;
				continue;
			}

			// 組み込み: wait <seconds>
			if (expanded.rfind("wait ", 0) == 0)
			{
				handleWait(expanded.substr(5));
				++i;
				continue;
			}

			// 組み込み: if <expr>
			if (expanded.rfind("if ", 0) == 0)
			{
				i = handleIf(cmdSystem, results, expanded, i, end);
				continue;
			}

			// 組み込み: repeat <N>
			if (expanded.rfind("repeat ", 0) == 0)
			{
				i = handleRepeat(cmdSystem, results, expanded, i, end);
				continue;
			}

			// 文脈外で現れた block 終端キーワードはスキップ
			if (expanded == "endif" || expanded == "endrepeat")
			{
				++i;
				continue;
			}

			// 通常コマンド。CommandSystem へ dispatch
			auto result = cmdSystem.executeString(expanded);
			results.push_back(result);
			++i;
		}
	}

	/// @brief set コマンドを処理する
	/// @param line "set <name> <value>" 形式の行
	void handleSet(const std::string& line)
	{
		// "set name value..." 形式
		const auto nameStart = line.find(' ');
		if (nameStart == std::string::npos)
		{
			return;
		}
		const auto nameEnd = line.find(' ', nameStart + 1);
		if (nameEnd == std::string::npos)
		{
			// "set name"。空文字列を設定
			m_variables[line.substr(nameStart + 1)] = "";
			return;
		}
		const std::string name = line.substr(nameStart + 1,
			nameEnd - nameStart - 1);
		const std::string value = line.substr(nameEnd + 1);
		m_variables[name] = value;
	}

	/// @brief echo コマンドを処理する
	/// @param message 出力メッセージ
	void handleEcho(const std::string& message)
	{
		// 前後の引用符があれば除去
		std::string msg = message;
		if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
		{
			msg = msg.substr(1, msg.size() - 2);
		}

		if (m_echoHandler)
		{
			m_echoHandler(msg);
		}
	}

	/// @brief wait コマンドを処理する
	/// @param secondsStr 待機秒数（文字列）
	void handleWait(const std::string& secondsStr)
	{
		float seconds = 0.0f;
		try
		{
			seconds = std::stof(secondsStr);
		}
		catch (...)
		{
			return;
		}

		if (m_waitHandler)
		{
			m_waitHandler(seconds);
		}
		else
		{
			// 既定: thread sleep
			const auto ms = static_cast<int>(seconds * 1000.0f);
			if (ms > 0)
			{
				std::this_thread::sleep_for(
					std::chrono::milliseconds(ms));
			}
		}
	}

	/// @brief if/endif ブロックを処理する
	/// @param cmdSystem CommandSystem のインスタンス
	/// @param results 結果バッファ
	/// @param condLine "if <expr>" の行
	/// @param ifIndex if 行のインデックス
	/// @param blockEnd ブロック終端
	/// @return 次に処理すべき行インデックス
	std::size_t handleIf(CommandSystem& cmdSystem,
	                     std::vector<CommandResult>& results,
	                     const std::string& condLine,
	                     std::size_t ifIndex, std::size_t blockEnd)
	{
		// 対応する endif を探す
		std::size_t depth = 1;
		std::size_t endifIndex = ifIndex + 1;
		while (endifIndex < blockEnd && depth > 0)
		{
			const std::string trimmed = trimLine(m_lines[endifIndex].raw);
			if (trimmed.rfind("if ", 0) == 0)
			{
				++depth;
			}
			else if (trimmed == "endif")
			{
				--depth;
			}
			if (depth > 0)
			{
				++endifIndex;
			}
		}

		const bool condition = evaluateCondition(condLine.substr(3));
		if (condition)
		{
			executeBlock(cmdSystem, results, ifIndex + 1, endifIndex);
		}

		return endifIndex + 1;
	}

	/// @brief repeat/endrepeat ブロックを処理する
	/// @param cmdSystem CommandSystem のインスタンス
	/// @param results 結果バッファ
	/// @param repeatLine "repeat <N>" の行
	/// @param repeatIndex repeat 行のインデックス
	/// @param blockEnd ブロック終端
	/// @return 次に処理すべき行インデックス
	std::size_t handleRepeat(CommandSystem& cmdSystem,
	                         std::vector<CommandResult>& results,
	                         const std::string& repeatLine,
	                         std::size_t repeatIndex,
	                         std::size_t blockEnd)
	{
		// 対応する endrepeat を探す
		std::size_t depth = 1;
		std::size_t endrepeatIndex = repeatIndex + 1;
		while (endrepeatIndex < blockEnd && depth > 0)
		{
			const std::string trimmed = trimLine(m_lines[endrepeatIndex].raw);
			if (trimmed.rfind("repeat ", 0) == 0)
			{
				++depth;
			}
			else if (trimmed == "endrepeat")
			{
				--depth;
			}
			if (depth > 0)
			{
				++endrepeatIndex;
			}
		}

		// repeat 回数をパース
		int count = 0;
		try
		{
			count = std::stoi(repeatLine.substr(7));
		}
		catch (...)
		{
			return endrepeatIndex + 1;
		}

		// block を N 回実行し、$i に反復インデックスを設定する
		for (int iteration = 0; iteration < count; ++iteration)
		{
			m_variables["i"] = std::to_string(iteration);
			executeBlock(cmdSystem, results,
				repeatIndex + 1, endrepeatIndex);
		}

		return endrepeatIndex + 1;
	}

	/// @brief 条件式を評価する
	/// @param expr 条件式（例: "$flag == true"）
	/// @return 条件が真なら true
	[[nodiscard]] bool evaluateCondition(const std::string& expr) const
	{
		const std::string expanded = substituteVariables(trimLine(expr));

		// Support: <lhs> == <rhs>
		const auto eqPos = expanded.find("==");
		if (eqPos != std::string::npos)
		{
			const std::string lhs = trimLine(expanded.substr(0, eqPos));
			const std::string rhs = trimLine(expanded.substr(eqPos + 2));
			return lhs == rhs;
		}

		// Support: <lhs> != <rhs>
		const auto neqPos = expanded.find("!=");
		if (neqPos != std::string::npos)
		{
			const std::string lhs = trimLine(expanded.substr(0, neqPos));
			const std::string rhs = trimLine(expanded.substr(neqPos + 2));
			return lhs != rhs;
		}

		// Truthy check: non-empty and not "false" and not "0"
		return !expanded.empty()
			&& expanded != "false"
			&& expanded != "0";
	}

	/// @brief 変数参照 ($name) を展開する
	/// @param line 入力行
	/// @return 変数置換後の行
	[[nodiscard]] std::string substituteVariables(const std::string& line) const
	{
		std::string result;
		result.reserve(line.size());

		for (std::size_t i = 0; i < line.size(); ++i)
		{
			if (line[i] == '$' && i + 1 < line.size())
			{
				// 変数名を抽出 (英数字 + アンダースコア)
				std::size_t j = i + 1;
				while (j < line.size()
					&& (std::isalnum(static_cast<unsigned char>(line[j]))
						|| line[j] == '_'))
				{
					++j;
				}
				const std::string varName = line.substr(i + 1, j - i - 1);
				const auto it = m_variables.find(varName);
				if (it != m_variables.end())
				{
					result += it->second;
				}
				else
				{
					// 未定義なら元の $name をそのまま残す
					result += line.substr(i, j - i);
				}
				i = j - 1; // loop が ++i するので -1
			}
			else
			{
				result += line[i];
			}
		}

		return result;
	}

	/// @brief 行の前後空白を除去する
	/// @param s 入力文字列
	/// @return トリム後の文字列
	[[nodiscard]] static std::string trimLine(const std::string& s)
	{
		const auto start = s.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
		{
			return {};
		}
		const auto end = s.find_last_not_of(" \t\r\n");
		return s.substr(start, end - start + 1);
	}

	std::vector<ScriptLine> m_lines;                     ///< パース済み行
	std::string m_sourcePath;                             ///< ソースパス
	std::unordered_map<std::string, std::string> m_variables; ///< 変数テーブル
	EchoHandler m_echoHandler;                            ///< echo出力ハンドラ
	WaitHandler m_waitHandler;                            ///< wait実行ハンドラ
};

} // namespace mitiru
