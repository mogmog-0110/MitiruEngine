/// @file mitiru-example.cpp
/// @brief AI自動化パイプラインの使用例
/// @details AiSession・AiTestRunner・AiDebuggerの基本的な使い方を示す
///          スタンドアロンサンプルプログラム。
///
/// ビルド方法（スタンドアロン）:
/// @code
/// cl /std:c++20 /EHsc /I../../include mitiru-example.cpp /Fe:mitiru-example.exe
/// @endcode

#include <mitiru/Mitiru.hpp>
#include <mitiru/ai/AiPipeline.hpp>

#include <iostream>
#include <string>

/// @brief サンプル用の最小ゲーム実装
struct SimpleGame : mitiru::Game
{
	int m_frameCount = 0;       ///< 経過フレーム数
	bool m_spacePressed = false; ///< Spaceキー押下状態

	/// @brief ゲームロジックを更新する
	void update() override
	{
		++m_frameCount;
	}

	/// @brief 描画処理（最小実装）
	/// @param screen 描画先サーフェス
	void draw(mitiru::Screen& screen) override
	{
		static_cast<void>(screen);
	}

	/// @brief 論理サイズを返す
	/// @param outsideWidth 外部幅
	/// @param outsideHeight 外部高さ
	/// @return 固定の論理サイズ (800x600)
	[[nodiscard]] mitiru::Size layout(int outsideWidth, int outsideHeight) override
	{
		static_cast<void>(outsideWidth);
		static_cast<void>(outsideHeight);
		return {800, 600};
	}
};

/// @brief メインエントリポイント
int main()
{
	std::cout << "=== Mitiru AI Pipeline Example ===" << std::endl;

	// ─── 1. AiSession: 基本的なフレーム実行と観測 ───
	std::cout << "\n--- AiSession Demo ---" << std::endl;

	mitiru::ai::AiSession session;
	SimpleGame game;

	/// ヘッドレス・決定論的モードで初期化
	session.init(mitiru::EngineConfig{
		.headless = true,
		.deterministic = true,
		.targetTps = 60.0f,
		.enableObserver = true,
		.observePort = 8080
	});
	session.loadGame(game);

	/// 100フレーム実行
	session.step(100);
	std::cout << "After 100 frames:" << std::endl;
	std::cout << "  Snapshot: " << session.snapshot() << std::endl;
	std::cout << "  Frame: " << session.frameNumber() << std::endl;

	/// 入力注入
	session.pressKey(32);  // Space
	session.step(10);
	session.releaseKey(32);

	std::cout << "After Space input:" << std::endl;
	std::cout << "  Snapshot: " << session.snapshot() << std::endl;

	/// 不変条件の登録と検証
	session.invariants().add("frame_count_positive", [&game]()
	{
		return game.m_frameCount > 0;
	});

	const auto issues = session.validate();
	std::cout << "Validation: " << (issues.empty() ? "ALL PASSED" : "FAILURES") << std::endl;
	for (const auto& issue : issues)
	{
		std::cout << "  Issue: " << issue << std::endl;
	}

	// ─── 2. AiDebugger: スクリーンショット比較とベースライン ───
	std::cout << "\n--- AiDebugger Demo ---" << std::endl;

	mitiru::ai::AiDebugger debugger(session);

	/// ベースラインを保存
	debugger.markBaseline();
	std::cout << "Baseline marked at frame " << debugger.baselineFrame() << std::endl;

	/// 50フレーム進めて差分を確認
	session.step(50);
	const auto diff = debugger.diffFromBaseline();
	std::cout << "Diff from baseline: " << diff << std::endl;

	/// 空のスクリーンショット比較（ヘッドレスでは空データ）
	const auto ssA = session.screenshot();
	session.step(10);
	const auto ssB = session.screenshot();
	const auto ssDiff = debugger.compareScreenshots(ssA, ssB, 800, 600);
	std::cout << "Screenshot diff: " << ssDiff.toJson() << std::endl;

	// ─── 3. AiTestRunner: 自動テスト実行 ───
	std::cout << "\n--- AiTestRunner Demo ---" << std::endl;

	mitiru::ai::AiTestRunner runner(session);

	/// インスペクターに状態を登録（テスト検証用）
	session.inspector().registerState("game.running", "true");

	/// テストケースを追加
	runner.addTest(mitiru::ai::AiTestCase{
		.name = "verify_game_running",
		.description = "Verify game is in running state",
		.actions = {},
		.expectedState = {{"game.running", "true"}},
		.stepsAfterActions = 1
	});

	runner.addTest(mitiru::ai::AiTestCase{
		.name = "step_and_check",
		.description = "Step 10 frames and check state",
		.actions = {{.type = "step", .params = {{"frames", "10"}}}},
		.expectedState = {{"game.running", "true"}},
		.stepsAfterActions = 1
	});

	/// 全テスト実行
	const auto report = runner.runAll();
	std::cout << "Test Report: " << report.toJson() << std::endl;
	std::cout << "  Total: " << report.totalTests << std::endl;
	std::cout << "  Passed: " << report.passed << std::endl;
	std::cout << "  Failed: " << report.failed << std::endl;

	// ─── 4. 記録・再生 ───
	std::cout << "\n--- Recording Demo ---" << std::endl;

	session.startRecording();
	session.pressKey(65);  // 'A'
	session.step(5);
	session.releaseKey(65);
	session.step(5);
	const auto replayJson = session.stopRecording();
	std::cout << "Replay JSON: " << replayJson << std::endl;

	std::cout << "\n=== Done ===" << std::endl;
	return 0;
}
