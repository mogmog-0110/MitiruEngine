#pragma once

/// @file AiSession.hpp
/// @brief AI自動操作セッション
/// @details Engineを内包し、AIエージェントがゲームをビルド・実行・観測・操作・検証
///          するための高レベルAPIを提供する。
///          ヘッドレスモードをデフォルトとし、フレーム単位の実行制御が可能。

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/control/ReplaySystem.hpp>
#include <mitiru/control/Scripting.hpp>
#include <mitiru/core/Engine.hpp>
#include <mitiru/observe/DiffTracker.hpp>
#include <mitiru/observe/Inspector.hpp>
#include <mitiru/validate/HealthCheck.hpp>
#include <mitiru/validate/Invariant.hpp>

namespace mitiru::ai
{

/// @brief AI自動操作セッション
/// @details AIエージェントがゲームと対話するための統合インターフェース。
///          初期化→ゲーム読込→フレーム実行→観測→入力注入→検証 の
///          一連のワークフローを単一クラスで提供する。
///
/// @code
/// mitiru::ai::AiSession session;
/// session.init();
/// SimpleGame game;
/// session.loadGame(game);
/// session.step(60);
/// auto snap = session.snapshot();
/// @endcode
class AiSession
{
public:
	/// @brief デフォルトコンストラクタ
	AiSession() = default;

	// ─── ライフサイクル ───

	/// @brief セッションを初期化する
	/// @param config エンジン設定（デフォルトでヘッドレスモード）
	void init(EngineConfig config = {})
	{
		config.headless = true;
		config.deterministic = true;
		m_config = config;
	}

	/// @brief ゲームインスタンスを読み込む
	/// @param game ゲームインスタンスへの参照
	/// @note ゲームの寿命はセッションより長くなければならない
	void loadGame(Game& game)
	{
		m_game = &game;
		m_loaded = true;
	}

	// ─── 実行制御 ───

	/// @brief 指定フレーム数だけゲームを進める
	/// @param frames 実行するフレーム数
	void step(int frames = 1)
	{
		if (!m_loaded || m_game == nullptr)
		{
			return;
		}

		m_engine.stepFrames(*m_game, static_cast<std::uint64_t>(frames), m_config);

		/// 記録中ならフレーム情報を記録
		if (m_replaySystem.isRecording())
		{
			m_replaySystem.recordFrame(
				m_engine.frameNumber(), m_engine.inputState());
		}
	}

	/// @brief 条件が真になるまでゲームを進める
	/// @param condition 停止条件（trueで停止）
	/// @param maxFrames 最大実行フレーム数（無限ループ防止）
	/// @return 条件が満たされた場合 true、タイムアウト時 false
	bool runUntil(std::function<bool()> condition, int maxFrames = 600)
	{
		for (int i = 0; i < maxFrames; ++i)
		{
			step(1);
			if (condition())
			{
				return true;
			}
		}
		return false;
	}

	// ─── 観測 ───

	/// @brief シーン状態のJSONスナップショットを取得する
	/// @return JSON文字列
	[[nodiscard]] std::string snapshot() const
	{
		return m_engine.snapshot();
	}

	/// @brief スクリーンショットをRGBAピクセルデータとして取得する
	/// @return RGBA8形式のバイト配列
	[[nodiscard]] std::vector<std::uint8_t> screenshot()
	{
		return m_engine.capture();
	}

	/// @brief ヘルスメトリクスをJSON文字列で取得する
	/// @return JSON形式のメトリクス文字列
	[[nodiscard]] std::string health() const
	{
		return m_healthCheck.toJson();
	}

	/// @brief タグ指定で状態を問い合わせる
	/// @param tag 検索タグ（Inspectorのキープレフィックス）
	/// @return 一致した状態のJSON文字列
	[[nodiscard]] std::string inspect(const std::string& tag) const
	{
		return m_inspector.queryByPrefix(tag);
	}

	/// @brief インスペクターへの参照を取得する
	/// @return Inspector への参照
	[[nodiscard]] observe::Inspector& inspector() noexcept
	{
		return m_inspector;
	}

	// ─── 入力制御 ───

	/// @brief キーを押下する
	/// @param keyCode キーコード
	void pressKey(int keyCode)
	{
		m_engine.inputInjector().inject(InputCommand{
			.type = InputCommandType::KeyDown,
			.keyCode = keyCode,
			.mouseButton = 0,
			.mouseX = 0.0f,
			.mouseY = 0.0f
		});
	}

	/// @brief キーを解放する
	/// @param keyCode キーコード
	void releaseKey(int keyCode)
	{
		m_engine.inputInjector().inject(InputCommand{
			.type = InputCommandType::KeyUp,
			.keyCode = keyCode,
			.mouseButton = 0,
			.mouseX = 0.0f,
			.mouseY = 0.0f
		});
	}

	/// @brief マウスカーソルを移動する
	/// @param x X座標
	/// @param y Y座標
	void moveMouse(float x, float y)
	{
		m_engine.inputInjector().inject(InputCommand{
			.type = InputCommandType::MouseMove,
			.keyCode = 0,
			.mouseButton = 0,
			.mouseX = x,
			.mouseY = y
		});
	}

	/// @brief 指定座標をクリックする
	/// @param x X座標
	/// @param y Y座標
	/// @note マウス移動→ボタン押下→1フレーム進行→ボタン解放の順で実行
	void clickAt(float x, float y)
	{
		moveMouse(x, y);
		m_engine.inputInjector().inject(InputCommand{
			.type = InputCommandType::MouseDown,
			.keyCode = 0,
			.mouseButton = 0,
			.mouseX = x,
			.mouseY = y
		});
		step(1);
		m_engine.inputInjector().inject(InputCommand{
			.type = InputCommandType::MouseUp,
			.keyCode = 0,
			.mouseButton = 0,
			.mouseX = x,
			.mouseY = y
		});
	}

	/// @brief テキスト文字列を1文字ずつキー入力として送信する
	/// @param text 入力する文字列
	/// @note 各文字のASCIIコードをKeyDown/KeyUpとして注入する
	void typeText(std::string_view text)
	{
		for (const char ch : text)
		{
			pressKey(static_cast<int>(ch));
			step(1);
			releaseKey(static_cast<int>(ch));
		}
	}

	/// @brief ボタンラベルからUIボタンを検索しクリックする
	/// @param buttonLabel ボタンのラベル文字列
	/// @note Inspectorに登録された "button.<label>" キーから座標を取得する。
	///       座標が見つからない場合は何もしない。
	void clickButton(std::string_view buttonLabel)
	{
		/// Inspectorから "button.<label>.x" と "button.<label>.y" を取得
		const std::string keyX = "button." + std::string(buttonLabel) + ".x";
		const std::string keyY = "button." + std::string(buttonLabel) + ".y";

		const auto valX = m_inspector.queryState(keyX);
		const auto valY = m_inspector.queryState(keyY);

		if (valX.has_value() && valY.has_value())
		{
			const float x = std::stof(valX.value());
			const float y = std::stof(valY.value());
			clickAt(x, y);
		}
	}

	// ─── 検証 ───

	/// @brief 指定パスの状態が期待値と一致するか検証する
	/// @param jsonPath 状態のキー（Inspectorのキー）
	/// @param expected 期待する値
	/// @return 一致すれば true
	[[nodiscard]] bool assertState(std::string_view jsonPath,
	                               std::string_view expected) const
	{
		const auto val = m_inspector.queryState(std::string(jsonPath));
		if (!val.has_value())
		{
			return false;
		}
		return val.value() == expected;
	}

	/// @brief 登録済みの全不変条件を検証し、失敗した項目名を返す
	/// @return 失敗した不変条件の名前リスト（全て合格なら空配列）
	[[nodiscard]] std::vector<std::string> validate() const
	{
		const auto results = m_invariants.checkAll(m_engine.frameNumber());
		std::vector<std::string> failures;
		for (const auto& r : results)
		{
			if (!r.passed)
			{
				failures.push_back(r.message);
			}
		}
		return failures;
	}

	/// @brief 不変条件レジストリへの参照を取得する
	/// @return InvariantRegistry への参照
	[[nodiscard]] validate::InvariantRegistry& invariants() noexcept
	{
		return m_invariants;
	}

	// ─── 記録・再生 ───

	/// @brief 入力記録を開始する
	void startRecording()
	{
		m_replaySystem.startRecording(m_config.randomSeed,
			static_cast<int>(m_config.targetTps));
	}

	/// @brief 入力記録を停止しリプレイJSONを返す
	/// @return リプレイデータのJSON文字列
	[[nodiscard]] std::string stopRecording()
	{
		const auto data = m_replaySystem.stopRecording();
		return data.toJson();
	}

	/// @brief リプレイJSONを解析して再生する
	/// @param replayJson リプレイデータのJSON文字列
	void playReplay(std::string_view replayJson)
	{
		const auto data = ReplayData::fromJson(std::string(replayJson));
		m_replaySystem.startPlayback(data);

		/// 全フレーム分のコマンドをインジェクターに流し込みながら実行
		const auto totalFrames = data.totalFrames();
		for (std::size_t i = 0; i < totalFrames; ++i)
		{
			const auto commands = m_replaySystem.getPlaybackCommands(
				static_cast<std::uint64_t>(i));
			for (const auto& cmd : commands)
			{
				m_engine.inputInjector().inject(cmd);
			}
			step(1);
		}
	}

	// ─── 差分 ───

	/// @brief ベースラインスナップショットとの差分を計算する
	/// @param baselineSnapshot ベースラインのスナップショットJSON
	/// @return 差分情報のJSON文字列
	/// @note 現在のスナップショットとの文字列比較を行う簡易実装
	[[nodiscard]] std::string diffFromBaseline(std::string_view baselineSnapshot) const
	{
		const auto current = snapshot();
		const bool same = (current == baselineSnapshot);

		std::string json;
		json += R"json({"changed":)json";
		json += same ? "false" : "true";
		json += R"json(,"baseline":)json";
		json += std::string(baselineSnapshot);
		json += R"json(,"current":)json";
		json += current;
		json += "}";
		return json;
	}

	// ─── アクセサ ───

	/// @brief エンジンへの参照を取得する
	/// @return Engine への参照
	[[nodiscard]] Engine& engine() noexcept
	{
		return m_engine;
	}

	/// @brief エンジンへのconst参照を取得する
	/// @return Engine への const 参照
	[[nodiscard]] const Engine& engine() const noexcept
	{
		return m_engine;
	}

	/// @brief 現在のフレーム番号を取得する
	/// @return フレーム番号
	[[nodiscard]] std::uint64_t frameNumber() const noexcept
	{
		return m_engine.frameNumber();
	}

private:
	Engine m_engine;                                  ///< ゲームエンジン本体
	EngineConfig m_config;                            ///< エンジン設定
	Game* m_game = nullptr;                           ///< ゲームインスタンスへのポインタ
	bool m_loaded = false;                            ///< ゲーム読込済みフラグ
	validate::HealthCheck m_healthCheck;              ///< ヘルスメトリクス
	validate::InvariantRegistry m_invariants;         ///< 不変条件レジストリ
	observe::Inspector m_inspector;                   ///< 状態インスペクター
	control::ReplaySystem m_replaySystem;             ///< リプレイシステム
};

} // namespace mitiru::ai
