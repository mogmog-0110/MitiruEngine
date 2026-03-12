#pragma once

/// @file AiTestRunner.hpp
/// @brief AI駆動テストランナー
/// @details AIエージェントがJSONで記述したテストケースを実行し、
///          結果をレポートとして返すテスト自動化フレームワーク。
///          ScriptActionベースのアクションシーケンスとkey-valueアサーションを組合せる。

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/ai/AiSession.hpp>
#include <mitiru/control/Scripting.hpp>

namespace mitiru::ai
{

/// @brief 個別テスト結果
struct AiTestResult
{
	std::string testName;                ///< テスト名
	bool passed = false;                 ///< 合格したか
	std::vector<std::string> failures;   ///< 失敗した検証の詳細
	std::uint64_t frameAtEnd = 0;        ///< テスト終了時のフレーム番号
	std::string snapshotAtEnd;           ///< テスト終了時のスナップショット

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += R"json({"testName":")json";
		json += testName;
		json += R"json(","passed":)json";
		json += passed ? "true" : "false";
		json += R"json(,"frameAtEnd":)json";
		json += std::to_string(frameAtEnd);
		json += R"json(,"failures":[)json";
		for (std::size_t i = 0; i < failures.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += "\"" + failures[i] + "\"";
		}
		json += "]}";
		return json;
	}
};

/// @brief テスト実行レポート
struct AiTestReport
{
	int totalTests = 0;                        ///< 総テスト数
	int passed = 0;                            ///< 合格数
	int failed = 0;                            ///< 不合格数
	std::vector<AiTestResult> results;         ///< 個別テスト結果

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += R"json({"totalTests":)json";
		json += std::to_string(totalTests);
		json += R"json(,"passed":)json";
		json += std::to_string(passed);
		json += R"json(,"failed":)json";
		json += std::to_string(failed);
		json += R"json(,"results":[)json";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += results[i].toJson();
		}
		json += "]}";
		return json;
	}
};

/// @brief AIテストケース定義
/// @details テスト名・説明・実行アクション・期待状態を保持する。
struct AiTestCase
{
	std::string name;                                   ///< テスト名
	std::string description;                            ///< テストの説明
	std::vector<control::ScriptAction> actions;         ///< 実行アクションシーケンス
	std::map<std::string, std::string> expectedState;   ///< 期待する状態（key-value）
	int stepsAfterActions = 1;                          ///< アクション実行後の追加フレーム数
};

/// @brief AI駆動テストランナー
/// @details テストケースを登録・実行し、結果をレポートとして返す。
///          各テストケースは独立したセッション状態で実行される。
///
/// @code
/// mitiru::ai::AiTestRunner runner(session);
/// runner.addTest(AiTestCase{
///     .name = "press_space",
///     .description = "Space key triggers jump",
///     .actions = {{.type = "key_down", .params = {{"keyCode", "32"}}}},
///     .expectedState = {{"player.jumping", "true"}}
/// });
/// auto report = runner.runAll();
/// @endcode
class AiTestRunner
{
public:
	/// @brief コンストラクタ
	/// @param session AI操作セッションへの参照
	explicit AiTestRunner(AiSession& session) noexcept
		: m_session(session)
	{
	}

	/// @brief テストケースを追加する
	/// @param test テストケース定義
	void addTest(AiTestCase test)
	{
		m_tests.push_back(std::move(test));
	}

	/// @brief 全テストケースを実行する
	/// @return テスト実行レポート
	[[nodiscard]] AiTestReport runAll()
	{
		AiTestReport report;
		report.totalTests = static_cast<int>(m_tests.size());

		for (const auto& test : m_tests)
		{
			auto result = executeTest(test);
			if (result.passed)
			{
				++report.passed;
			}
			else
			{
				++report.failed;
			}
			report.results.push_back(std::move(result));
		}

		return report;
	}

	/// @brief 名前指定でテストケースを実行する
	/// @param name テスト名
	/// @return テスト実行レポート（該当テストのみ）
	[[nodiscard]] AiTestReport runByName(std::string_view name)
	{
		AiTestReport report;

		for (const auto& test : m_tests)
		{
			if (test.name == name)
			{
				report.totalTests = 1;
				auto result = executeTest(test);
				if (result.passed)
				{
					report.passed = 1;
				}
				else
				{
					report.failed = 1;
				}
				report.results.push_back(std::move(result));
				break;
			}
		}

		return report;
	}

	/// @brief レポートをJSON文字列に変換する（静的ユーティリティ）
	/// @param report テスト実行レポート
	/// @return JSON形式の文字列
	[[nodiscard]] static std::string toJson(const AiTestReport& report)
	{
		return report.toJson();
	}

	/// @brief 登録済みテスト数を取得する
	/// @return テスト数
	[[nodiscard]] std::size_t testCount() const noexcept
	{
		return m_tests.size();
	}

	/// @brief 全テストケースをクリアする
	void clear() noexcept
	{
		m_tests.clear();
	}

private:
	/// @brief 1つのテストケースを実行する
	/// @param test テストケース定義
	/// @return テスト結果
	[[nodiscard]] AiTestResult executeTest(const AiTestCase& test)
	{
		AiTestResult result;
		result.testName = test.name;
		result.passed = true;

		/// アクションシーケンスを実行
		for (const auto& action : test.actions)
		{
			executeAction(action);
		}

		/// アクション後の追加フレームを実行
		m_session.step(test.stepsAfterActions);

		/// 期待状態を検証
		for (const auto& [key, expected] : test.expectedState)
		{
			if (!m_session.assertState(key, expected))
			{
				result.passed = false;
				const auto actual = m_session.inspect(key);
				result.failures.push_back(
					"Expected " + key + "=\"" + expected +
					"\", got: " + actual);
			}
		}

		result.frameAtEnd = m_session.frameNumber();
		result.snapshotAtEnd = m_session.snapshot();

		return result;
	}

	/// @brief ScriptActionを解釈して実行する
	/// @param action 実行するアクション
	void executeAction(const control::ScriptAction& action)
	{
		if (action.type == "key_down")
		{
			const auto it = action.params.find("keyCode");
			if (it != action.params.end())
			{
				m_session.pressKey(std::stoi(it->second));
			}
		}
		else if (action.type == "key_up")
		{
			const auto it = action.params.find("keyCode");
			if (it != action.params.end())
			{
				m_session.releaseKey(std::stoi(it->second));
			}
		}
		else if (action.type == "mouse_move")
		{
			const auto itX = action.params.find("x");
			const auto itY = action.params.find("y");
			if (itX != action.params.end() && itY != action.params.end())
			{
				m_session.moveMouse(
					std::stof(itX->second), std::stof(itY->second));
			}
		}
		else if (action.type == "click")
		{
			const auto itX = action.params.find("x");
			const auto itY = action.params.find("y");
			if (itX != action.params.end() && itY != action.params.end())
			{
				m_session.clickAt(
					std::stof(itX->second), std::stof(itY->second));
			}
		}
		else if (action.type == "step")
		{
			const auto it = action.params.find("frames");
			if (it != action.params.end())
			{
				m_session.step(std::stoi(it->second));
			}
			else
			{
				m_session.step(1);
			}
		}
		else if (action.type == "type_text")
		{
			const auto it = action.params.find("text");
			if (it != action.params.end())
			{
				m_session.typeText(it->second);
			}
		}
		else if (action.type == "click_button")
		{
			const auto it = action.params.find("label");
			if (it != action.params.end())
			{
				m_session.clickButton(it->second);
			}
		}
	}

	AiSession& m_session;                ///< AI操作セッションへの参照
	std::vector<AiTestCase> m_tests;     ///< 登録済みテストケース
};

} // namespace mitiru::ai
