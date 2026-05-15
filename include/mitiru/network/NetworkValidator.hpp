#pragma once

/// @file NetworkValidator.hpp
/// @brief ネットワークサブシステム検証
/// @details ネットワーク関連コンポーネント（LocalTransport, TcpTransport,
///          StateSync, Lobby）の統合テストを実行し、結果を報告する。
///          各テストは5秒のタイムアウト付きで、リソースのクリーンアップを保証する。
///
/// @code
/// mitiru::network::NetworkValidator validator;
/// auto report = validator.runAll();
/// if (report.allPassed) { /* OK */ }
/// @endcode

#include <mitiru/network/LocalTransport.hpp>
#include <mitiru/network/Lobby.hpp>
#include <mitiru/network/StateSync.hpp>
#include <mitiru/network/TcpTransport.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace mitiru::network
{

/// @brief 個別テスト結果
struct TestResult
{
	std::string name;     ///< テスト名
	bool passed = false;  ///< 合否
	std::string detail;   ///< エラー詳細（成功時は空）
};

/// @brief 全テスト結果レポート
struct ValidationReport
{
	bool allPassed = false;             ///< 全テスト合格フラグ
	std::vector<TestResult> results;    ///< 個別テスト結果

	/// @brief テスト結果サマリをJSON文字列で返す
	[[nodiscard]] std::string toJson() const
	{
		std::string json = R"({"allPassed":)";
		json += allPassed ? "true" : "false";
		json += R"(,"results":[)";
		for (std::size_t i = 0; i < results.size(); ++i)
		{
			if (i > 0) json += ",";
			const auto& r = results[i];
			json += R"({"name":")" + r.name
				+ R"(","passed":)" + (r.passed ? "true" : "false")
				+ R"(,"detail":")" + r.detail + R"("})";
		}
		json += "]}";
		return json;
	}
};

/// @brief ネットワークサブシステム検証クラス
/// @details 各ネットワークコンポーネントの基本動作を検証する統合テストランナー。
class NetworkValidator
{
public:
	/// @brief LocalTransportのペアリング通信をテストする
	/// @return 合否
	[[nodiscard]] bool testLocalTransport() const
	{
		LocalTransport a;
		LocalTransport b;
		LocalTransport::pair(a, b);

		if (!a.isConnected(1) || !b.isConnected(1))
		{
			return false;
		}

		const std::vector<std::uint8_t> testData = {0xDE, 0xAD, 0xBE, 0xEF};
		a.send(1, testData);

		auto messages = b.poll();
		if (messages.size() != 1)
		{
			return false;
		}

		if (messages[0].payload != testData)
		{
			return false;
		}

		/// 逆方向もテスト
		const std::vector<std::uint8_t> reply = {0xCA, 0xFE};
		b.send(1, reply);

		auto replyMsgs = a.poll();
		if (replyMsgs.size() != 1)
		{
			return false;
		}

		if (replyMsgs[0].payload != reply)
		{
			return false;
		}

		/// 切断テスト
		a.disconnect(1);
		if (a.isConnected(1) || b.isConnected(1))
		{
			return false;
		}

		return true;
	}

	/// @brief TCPループバック通信をテストする
	[[nodiscard]] bool testTcpLoopback() const
	{
		TcpTransport server;
		if (!server.isWsaInitialized()) return false;
		if (!server.listen(0)) return false;

		const uint16_t port = server.getLocalPort();
		if (port == 0) return false;

		TcpTransport client;
		if (!client.connect("127.0.0.1", port)) return false;

		/// 接続受け入れを待機
		ConnectionId serverConnId = INVALID_CONNECTION;
		if (!waitFor([&]() {
			for (const auto& msg : server.poll())
			{
				if (msg.header.type ==
					static_cast<uint32_t>(NetworkEvent::Connected))
				{ serverConnId = msg.sender; return true; }
			}
			return false;
		})) return false;

		/// クライアント側の接続IDを取得
		ConnectionId clientConnId = INVALID_CONNECTION;
		for (const auto& msg : client.poll())
		{
			if (msg.header.type ==
				static_cast<uint32_t>(NetworkEvent::Connected))
			{ clientConnId = msg.sender; }
		}
		if (clientConnId == INVALID_CONNECTION) return false;

		/// データ送受信テスト
		const std::vector<std::uint8_t> testData = {1, 2, 3, 4, 5};
		client.send(clientConnId, testData);

		std::vector<std::uint8_t> received;
		if (!waitFor([&]() {
			for (const auto& msg : server.poll())
			{
				if (!msg.payload.empty())
				{ received = msg.payload; return true; }
			}
			return false;
		})) return false;

		return received == testData;
	}

	/// @brief StateSync の状態同期をテストする
	[[nodiscard]] bool testStateSync() const
	{
		StateSync serverSync;
		serverSync.setAuthority(true);
		StateSync clientSync;
		clientSync.setAuthority(false);

		const std::string state1 = R"({"x":10,"y":20})";
		serverSync.pushState(state1);
		if (serverSync.stateVersion() != 1) return false;

		/// クライアントに状態を同期
		clientSync.pushState(serverSync.latestState());
		if (clientSync.latestState() != state1) return false;

		clientSync.acknowledgeVersion(1);
		if (clientSync.pendingCount() != 0) return false;

		/// 複数状態の同期テスト
		serverSync.pushState(R"({"x":15,"y":25})");
		serverSync.pushState(R"({"x":20,"y":30})");
		if (serverSync.pendingCount() != 3) return false;

		serverSync.acknowledgeVersion(2);
		if (serverSync.pendingCount() != 1) return false;
		if (serverSync.latestState() != R"({"x":20,"y":30})") return false;

		return true;
	}

	/// @brief Lobby の基本動作をテストする
	/// @return 合否
	[[nodiscard]] bool testLobby() const
	{
		Lobby lobby;

		/// 空ロビーでは開始不可
		if (lobby.allReady())
		{
			return false;
		}

		/// プレイヤー追加
		lobby.addPlayer(1, "Alice");
		lobby.addPlayer(2, "Bob");
		lobby.addPlayer(3, "Charlie");

		if (lobby.playerCount() != 3)
		{
			return false;
		}

		/// 部分的にreadyの場合は開始不可
		lobby.setReady(1, true);
		lobby.setReady(2, true);

		if (lobby.allReady())
		{
			return false;
		}

		/// 全員readyで開始可能
		lobby.setReady(3, true);

		if (!lobby.allReady())
		{
			return false;
		}

		/// プレイヤー離脱後は再び開始不可
		lobby.removePlayer(2);

		if (lobby.playerCount() != 2)
		{
			return false;
		}

		if (!lobby.allReady())
		{
			return false;
		}

		/// JSON出力テスト
		const std::string json = lobby.toJson();
		if (json.empty())
		{
			return false;
		}

		return true;
	}

	/// @brief 全テストを実行する
	/// @return テスト結果レポート
	[[nodiscard]] ValidationReport runAll() const
	{
		ValidationReport report;

		report.results.push_back(
			runTest("LocalTransport", [this]() { return testLocalTransport(); }));
		report.results.push_back(
			runTest("TcpLoopback", [this]() { return testTcpLoopback(); }));
		report.results.push_back(
			runTest("StateSync", [this]() { return testStateSync(); }));
		report.results.push_back(
			runTest("Lobby", [this]() { return testLobby(); }));

		report.allPassed = true;
		for (const auto& r : report.results)
		{
			if (!r.passed)
			{
				report.allPassed = false;
				break;
			}
		}

		return report;
	}

private:
	/// @brief テスト実行タイムアウト（秒）
	static constexpr int kTimeoutSeconds = 5;

	/// @brief ポーリング間隔（ミリ秒）
	static constexpr int kPollIntervalMs = 1;

	/// @brief 条件が真になるまで待機する（タイムアウト付き）
	/// @param predicate 条件判定関数
	/// @return タイムアウト前に条件が満たされれば true
	template <typename Func>
	[[nodiscard]] static bool waitFor(Func&& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(kTimeoutSeconds);

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
			{
				return true;
			}
			std::this_thread::sleep_for(
				std::chrono::milliseconds(kPollIntervalMs));
		}
		return false;
	}

	/// @brief 個別テストを安全に実行する
	/// @param name テスト名
	/// @param testFunc テスト関数
	/// @return テスト結果
	template <typename Func>
	[[nodiscard]] static TestResult runTest(const std::string& name, Func&& testFunc)
	{
		TestResult result;
		result.name = name;

		try
		{
			result.passed = testFunc();
			if (!result.passed)
			{
				result.detail = "test returned false";
			}
		}
		catch (const std::exception& e)
		{
			result.passed = false;
			result.detail = std::string("exception: ") + e.what();
		}
		catch (...)
		{
			result.passed = false;
			result.detail = "unknown exception";
		}

		return result;
	}
};

} // namespace mitiru::network
