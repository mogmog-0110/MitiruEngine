#pragma once

/// @file CommandLine.hpp
/// @brief 軽量コマンドライン引数パーサー
/// @details 外部依存なしのシンプルなCLI引数パーサー。
///          フラグ、オプション（型付き）、位置引数をサポートする。
///
/// @code
/// int main(int argc, char* argv[])
/// {
///     mitiru::util::CommandLineParser parser;
///     parser.addFlag("verbose", "v", "Enable verbose output")
///           .addOption<int>("width", "w", "Window width", 1280)
///           .addOption<std::string>("config", "c", "Config file path", "config.json")
///           .addPositional("scene", "Scene file to load");
///
///     if (!parser.parse(argc, argv))
///     {
///         parser.printHelp();
///         return 1;
///     }
///
///     bool verbose = parser.hasFlag("verbose");
///     int width = parser.getOption<int>("width");
///     std::string scene = parser.getPositional(0);
/// }
/// @endcode

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mitiru::util
{

/// @brief 軽量コマンドライン引数パーサー
class CommandLineParser
{
public:
	/// @brief フラグ（bool型スイッチ）を追加する
	/// @param name 長い名前（--name）
	/// @param shortName 短い名前（-s）、空文字列で省略
	/// @param description ヘルプ表示用の説明
	/// @return 自身への参照（チェイン呼び出し用）
	CommandLineParser& addFlag(const std::string& name,
	                           const std::string& shortName,
	                           const std::string& description)
	{
		FlagDef def;
		def.name = name;
		def.shortName = shortName;
		def.description = description;
		def.value = false;

		m_flags[name] = def;
		if (!shortName.empty())
		{
			if (m_shortToLong.count(shortName) > 0)
			{
				throw std::runtime_error("Duplicate short name: -" + shortName);
			}
			m_shortToLong[shortName] = name;
		}
		m_flagOrder.push_back(name);
		return *this;
	}

	/// @brief 型付きオプションを追加する
	/// @tparam T オプションの値の型（string, int, float, bool）
	/// @param name 長い名前（--name）
	/// @param shortName 短い名前（-s）、空文字列で省略
	/// @param description ヘルプ表示用の説明
	/// @param defaultValue デフォルト値
	/// @return 自身への参照（チェイン呼び出し用）
	template <typename T>
	CommandLineParser& addOption(const std::string& name,
	                             const std::string& shortName,
	                             const std::string& description,
	                             const T& defaultValue)
	{
		OptionDef def;
		def.name = name;
		def.shortName = shortName;
		def.description = description;
		def.value = toString(defaultValue);
		def.defaultValue = def.value;
		def.required = false;

		m_options[name] = def;
		if (!shortName.empty())
		{
			if (m_shortToLong.count(shortName) > 0)
			{
				throw std::runtime_error("Duplicate short name: -" + shortName);
			}
			m_shortToLong[shortName] = name;
		}
		m_optionOrder.push_back(name);
		return *this;
	}

	/// @brief 位置引数を追加する
	/// @param name 引数名（ヘルプ表示用）
	/// @param description ヘルプ表示用の説明
	/// @return 自身への参照（チェイン呼び出し用）
	CommandLineParser& addPositional(const std::string& name,
	                                 const std::string& description)
	{
		PositionalDef def;
		def.name = name;
		def.description = description;
		m_positionalDefs.push_back(def);
		return *this;
	}

	/// @brief コマンドライン引数をパースする
	/// @param argc 引数の数
	/// @param argv 引数配列
	/// @return パース成功時true、エラー時false
	bool parse(int argc, char* argv[])
	{
		m_programName = (argc > 0) ? argv[0] : "program";
		m_errors.clear();
		m_positionalValues.clear();

		int i = 1;
		while (i < argc)
		{
			std::string arg = argv[i];

			if (arg == "--help" || arg == "-h")
			{
				m_helpRequested = true;
				return false;
			}

			if (arg.starts_with("--"))
			{
				// 長いオプション
				std::string name = arg.substr(2);

				// --name=value 形式
				std::string value;
				auto eqPos = name.find('=');
				if (eqPos != std::string::npos)
				{
					value = name.substr(eqPos + 1);
					name = name.substr(0, eqPos);
				}

				if (auto flagIt = m_flags.find(name); flagIt != m_flags.end())
				{
					flagIt->second.value = true;
				}
				else if (auto optIt = m_options.find(name); optIt != m_options.end())
				{
					if (value.empty())
					{
						if (i + 1 < argc)
						{
							value = argv[++i];
						}
						else
						{
							m_errors.push_back("Option --" + name + " requires a value");
							return false;
						}
					}
					optIt->second.value = value;
				}
				else
				{
					m_errors.push_back("Unknown option: --" + name);
					return false;
				}
			}
			else if (arg.starts_with("-") && arg.size() > 1)
			{
				// 短いオプション
				std::string shortName = arg.substr(1);

				auto longIt = m_shortToLong.find(shortName);
				if (longIt == m_shortToLong.end())
				{
					m_errors.push_back("Unknown option: -" + shortName);
					return false;
				}

				const auto& longName = longIt->second;

				if (auto flagIt = m_flags.find(longName); flagIt != m_flags.end())
				{
					flagIt->second.value = true;
				}
				else if (auto optIt = m_options.find(longName); optIt != m_options.end())
				{
					if (i + 1 < argc)
					{
						optIt->second.value = argv[++i];
					}
					else
					{
						m_errors.push_back("Option -" + shortName + " requires a value");
						return false;
					}
				}
			}
			else
			{
				// 位置引数
				m_positionalValues.push_back(arg);
			}

			++i;
		}

		return true;
	}

	/// @brief フラグが指定されたか判定する
	/// @param name フラグ名
	/// @return フラグが指定されていればtrue
	[[nodiscard]] bool hasFlag(const std::string& name) const
	{
		auto it = m_flags.find(name);
		if (it == m_flags.end())
		{
			return false;
		}
		return it->second.value;
	}

	/// @brief オプションの値を取得する
	/// @tparam T 期待する型
	/// @param name オプション名
	/// @return オプションの値
	/// @throws std::runtime_error オプションが存在しない場合
	template <typename T>
	[[nodiscard]] T getOption(const std::string& name) const
	{
		auto it = m_options.find(name);
		if (it == m_options.end())
		{
			throw std::runtime_error("Unknown option: " + name);
		}
		return fromString<T>(it->second.value);
	}

	/// @brief 位置引数を取得する
	/// @param index 位置引数のインデックス（0始まり）
	/// @return 引数文字列
	/// @throws std::out_of_range インデックスが範囲外の場合
	[[nodiscard]] std::string getPositional(std::size_t index) const
	{
		if (index >= m_positionalValues.size())
		{
			throw std::out_of_range("Positional argument index out of range: "
			                        + std::to_string(index));
		}
		return m_positionalValues[index];
	}

	/// @brief 位置引数の数を返す
	[[nodiscard]] std::size_t positionalCount() const noexcept
	{
		return m_positionalValues.size();
	}

	/// @brief ヘルプが要求されたか
	[[nodiscard]] bool helpRequested() const noexcept { return m_helpRequested; }

	/// @brief パースエラー一覧を返す
	[[nodiscard]] const std::vector<std::string>& errors() const noexcept { return m_errors; }

	/// @brief フォーマット済みヘルプテキストを出力する
	void printHelp() const
	{
		std::cerr << "Usage: " << m_programName;

		if (!m_flags.empty() || !m_options.empty())
		{
			std::cerr << " [options]";
		}
		for (const auto& pos : m_positionalDefs)
		{
			std::cerr << " <" << pos.name << ">";
		}
		std::cerr << "\n\n";

		// 位置引数
		if (!m_positionalDefs.empty())
		{
			std::cerr << "Arguments:\n";
			for (const auto& pos : m_positionalDefs)
			{
				std::cerr << "  " << padRight(pos.name, 20) << pos.description << "\n";
			}
			std::cerr << "\n";
		}

		// オプション
		std::cerr << "Options:\n";
		std::cerr << "  " << padRight("-h, --help", 28) << "Show this help message\n";

		for (const auto& name : m_flagOrder)
		{
			const auto& f = m_flags.at(name);
			std::string label = formatLabel(f.shortName, f.name);
			std::cerr << "  " << padRight(label, 28) << f.description << "\n";
		}

		for (const auto& name : m_optionOrder)
		{
			const auto& o = m_options.at(name);
			std::string label = formatLabel(o.shortName, o.name) + " <value>";
			std::string desc = o.description;
			if (!o.defaultValue.empty())
			{
				desc += " (default: " + o.defaultValue + ")";
			}
			std::cerr << "  " << padRight(label, 28) << desc << "\n";
		}
	}

	/// @brief バージョン情報を出力する
	/// @param name アプリケーション名
	/// @param version バージョン文字列
	static void printVersion(std::string_view name, std::string_view version)
	{
		std::cerr << name << " " << version << "\n";
	}

private:
	struct FlagDef
	{
		std::string name;
		std::string shortName;
		std::string description;
		bool value = false;
	};

	struct OptionDef
	{
		std::string name;
		std::string shortName;
		std::string description;
		std::string value;
		std::string defaultValue;
		bool required = false;
	};

	struct PositionalDef
	{
		std::string name;
		std::string description;
	};

	// ── 型変換ヘルパー ──

	template <typename T>
	[[nodiscard]] static std::string toString(const T& value)
	{
		std::ostringstream oss;
		oss << value;
		return oss.str();
	}

	[[nodiscard]] static std::string toString(const std::string& value)
	{
		return value;
	}

	[[nodiscard]] static std::string toString(bool value)
	{
		return value ? "true" : "false";
	}

	template <typename T>
	[[nodiscard]] static T fromString(const std::string& str)
	{
		if constexpr (std::is_same_v<T, std::string>)
		{
			return str;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return str == "true" || str == "1" || str == "yes";
		}
		else if constexpr (std::is_same_v<T, int>)
		{
			try { return std::stoi(str); }
			catch (...) { throw std::runtime_error("Invalid integer value: " + str); }
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			try { return std::stof(str); }
			catch (...) { throw std::runtime_error("Invalid float value: " + str); }
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			try { return std::stod(str); }
			catch (...) { throw std::runtime_error("Invalid double value: " + str); }
		}
		else
		{
			T result;
			std::istringstream iss(str);
			iss >> result;
			if (iss.fail() || !iss.eof())
			{
				throw std::runtime_error("Invalid value: " + str);
			}
			return result;
		}
	}

	// ── フォーマットヘルパー ──

	[[nodiscard]] static std::string formatLabel(const std::string& shortName,
	                                             const std::string& longName)
	{
		if (!shortName.empty())
		{
			return "-" + shortName + ", --" + longName;
		}
		return "    --" + longName;
	}

	[[nodiscard]] static std::string padRight(const std::string& str, std::size_t width)
	{
		if (str.size() >= width)
		{
			return str + " ";
		}
		return str + std::string(width - str.size(), ' ');
	}

	std::string m_programName;
	std::unordered_map<std::string, FlagDef> m_flags;
	std::unordered_map<std::string, OptionDef> m_options;
	std::unordered_map<std::string, std::string> m_shortToLong;
	std::vector<PositionalDef> m_positionalDefs;
	std::vector<std::string> m_positionalValues;
	std::vector<std::string> m_flagOrder;
	std::vector<std::string> m_optionOrder;
	std::vector<std::string> m_errors;
	bool m_helpRequested = false;
};

} // namespace mitiru::util
