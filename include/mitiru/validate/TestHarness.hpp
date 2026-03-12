#pragma once

/// @file TestHarness.hpp
/// @brief ゲームロジック用テストランナー
/// @details ゲーム内からゲームロジックの自動テストを実行する組み込みテストハーネス。
///          AIエージェントがエンジン経由でテストを投入・実行するために使用する。

#include <functional>
#include <string>
#include <vector>

namespace mitiru
{
class Engine;  ///< 前方宣言
} // namespace mitiru

namespace mitiru::validate
{

/// @brief テスト結果
struct TestResult
{
	std::string name;        ///< テスト名
	bool passed = false;     ///< テストに合格したか
	std::string message;     ///< 結果メッセージ

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"name\":\"" + name + "\",";
		json += "\"passed\":" + std::string(passed ? "true" : "false") + ",";
		json += "\"message\":\"" + message + "\"";
		json += "}";
		return json;
	}
};

/// @brief ゲームテスト定義
struct GameTest
{
	std::string name;                              ///< テスト名
	std::function<bool(Engine&)> testFunc;          ///< テスト関数（trueで合格）
};

/// @brief ゲームロジック用テストハーネス
/// @details テストを登録し、エンジン経由で一括実行する。
///
/// @code
/// mitiru::validate::TestHarness harness;
/// harness.addTest({"spawn_entity", [](Engine& e) {
///     // テストロジック
///     return true;
/// }});
/// auto results = harness.runAll(engine);
/// @endcode
class TestHarness
{
public:
	/// @brief テストを追加する
	/// @param test テスト定義
	void addTest(GameTest test)
	{
		m_tests.push_back(std::move(test));
	}

	/// @brief 全テストを実行する
	/// @param engine テスト対象のエンジン
	/// @return テスト結果のリスト
	[[nodiscard]] std::vector<TestResult> runAll(Engine& engine) const
	{
		std::vector<TestResult> results;
		results.reserve(m_tests.size());

		for (const auto& test : m_tests)
		{
			TestResult result;
			result.name = test.name;

			try
			{
				result.passed = test.testFunc(engine);
				result.message = result.passed ? "OK" : "FAILED";
			}
			catch (const std::exception& ex)
			{
				result.passed = false;
				result.message = std::string("EXCEPTION: ") + ex.what();
			}
			catch (...)
			{
				result.passed = false;
				result.message = "UNKNOWN EXCEPTION";
			}

			results.push_back(std::move(result));
		}

		return results;
	}

	/// @brief 全テスト結果をJSON文字列として返す
	/// @param engine テスト対象のエンジン
	/// @return JSON配列形式の文字列
	[[nodiscard]] std::string toJson(Engine& engine) const
	{
		const auto results = runAll(engine);
		std::string json;
		json += "[";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += results[i].toJson();
		}
		json += "]";
		return json;
	}

	/// @brief 登録テスト数を取得する
	/// @return テスト数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_tests.size();
	}

	/// @brief 全テストをクリアする
	void clear() noexcept
	{
		m_tests.clear();
	}

private:
	std::vector<GameTest> m_tests;  ///< 登録済みテスト
};

} // namespace mitiru::validate
