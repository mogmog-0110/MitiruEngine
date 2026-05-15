#pragma once

/// @file VNGameHybrid.hpp
/// @brief VN＋ゲームプレイハイブリッドフレームワーク
/// @details ビジュアルノベルモードとゲームプレイモード（アクション・RPGバトル・ミニゲーム）を
///          シームレスに切り替え、共有状態を維持するフレームワーク。
///
/// @note ここで定義される `mitiru::game::VNScript` / `VNCommand` は、
///       `HybridGameManager` が扱うプログラム的なコマンドリスト構築 DSL である。
///       **テキストファイルからシナリオをパースしたい場合は
///       `mitiru::vn::ScenarioScript`** (`include/mitiru/vn/ScenarioScript.hpp`)
///       を使うこと。両者は別系統で、下記の使い分けとなる:
///       - `VNScript` — C++ コード中でコマンドを積む用途 (procedural tutorial など)
///       - `ScenarioScript` — `.scenario` テキストをパースして実行する用途 (推奨)
///       混同すると期待通りに動かないので注意。
///
/// @code
/// mitiru::game::HybridGameManager manager;
/// manager.setFlag("met_hero", true);
/// manager.enterVNMode();
///
/// // VNスクリプトからゲームプレイを起動
/// manager.addGameplayTrigger({"battle", {{"enemies", "slime,goblin"}, {"bgm", "battle.ogg"}}});
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mitiru::game
{

// ─── 動作モード ───

/// @brief ハイブリッドゲームの動作モード
enum class GameMode : std::uint8_t
{
	VN,       ///< ビジュアルノベルモード
	Gameplay  ///< ゲームプレイモード
};

/// @brief ゲームプレイの種類
enum class GameplayType : std::uint8_t
{
	Battle,    ///< バトル
	Minigame,  ///< ミニゲーム
	Puzzle,    ///< パズル
	Explore,   ///< 探索
	Custom     ///< カスタム
};

// ─── 共有ステート ───

/// @brief 共有フラグ値（型安全）
using FlagValue = std::variant<bool, int, float, std::string>;

/// @brief VNとゲームプレイ間で共有される状態
struct SharedState
{
	std::map<std::string, FlagValue> flags;       ///< ゲームフラグ
	std::map<std::uint32_t, int> inventory;       ///< アイテムID → 所持数
	std::map<std::string, float> stats;           ///< 名前付きステータス

	// ─── フラグ操作 ───

	/// @brief フラグを設定する
	void setFlag(const std::string& key, FlagValue value)
	{
		flags.insert_or_assign(key, std::move(value));
	}

	/// @brief フラグを取得する
	/// @return フラグ値（未設定の場合はデフォルト値）
	template <typename T>
	[[nodiscard]] T getFlag(const std::string& key, const T& defaultValue = {}) const
	{
		auto it = flags.find(key);
		if (it == flags.end()) return defaultValue;
		const auto* val = std::get_if<T>(&it->second);
		return val ? *val : defaultValue;
	}

	/// @brief フラグが存在するか
	[[nodiscard]] bool hasFlag(const std::string& key) const
	{
		return flags.find(key) != flags.end();
	}

	// ─── インベントリ操作 ───

	/// @brief アイテムを追加する
	void addItem(std::uint32_t itemId, int count = 1)
	{
		inventory[itemId] += count;
	}

	/// @brief アイテムを消費する
	[[nodiscard]] bool removeItem(std::uint32_t itemId, int count = 1)
	{
		auto it = inventory.find(itemId);
		if (it == inventory.end() || it->second < count) return false;
		it->second -= count;
		if (it->second <= 0) inventory.erase(it);
		return true;
	}

	/// @brief アイテムを所持しているか
	[[nodiscard]] bool hasItem(std::uint32_t itemId, int count = 1) const
	{
		auto it = inventory.find(itemId);
		return it != inventory.end() && it->second >= count;
	}

	// ─── ステータス操作 ───

	/// @brief ステータスを設定する
	void setStat(const std::string& key, float value)
	{
		stats.insert_or_assign(key, value);
	}

	/// @brief ステータスを取得する
	[[nodiscard]] float getStat(const std::string& key, float defaultValue = 0.0f) const
	{
		auto it = stats.find(key);
		return (it != stats.end()) ? it->second : defaultValue;
	}
};

// ─── VNシーンデータ ───

/// @brief VN選択肢
struct VNChoice
{
	std::string text;                  ///< 選択肢テキスト
	std::string jumpLabel;             ///< ジャンプ先ラベル
	std::function<bool()> condition;   ///< 表示条件（nullptrなら常に表示）
};

/// @brief VNコマンドの種類
enum class VNCommandType : std::uint8_t
{
	Dialogue,     ///< 台詞表示
	Choice,       ///< 選択肢
	SetFlag,      ///< フラグ設定
	Jump,         ///< ラベルジャンプ
	Gameplay,     ///< ゲームプレイ遷移
	Wait,         ///< 待機
	Background,   ///< 背景変更
	Character,    ///< キャラクター表示/非表示
	BGM,          ///< BGM再生
	SE            ///< SE再生
};

/// @brief VNコマンド
struct VNCommand
{
	VNCommandType type = VNCommandType::Dialogue;
	std::string speaker;                           ///< 話者名
	std::string text;                              ///< テキスト/パラメータ
	std::vector<VNChoice> choices;                 ///< 選択肢リスト
	std::map<std::string, std::string> params;     ///< パラメータマップ
};

/// @brief VNスクリプト（コマンド列）
struct VNScript
{
	std::vector<VNCommand> commands;                     ///< コマンドリスト
	std::map<std::string, std::size_t> labels;           ///< ラベル → インデックス

	/// @brief コマンドを追加する
	void addCommand(VNCommand cmd)
	{
		commands.push_back(std::move(cmd));
	}

	/// @brief ラベルを設定する
	void setLabel(const std::string& name)
	{
		labels[name] = commands.size();
	}
};

// ─── トリガー ───

/// @brief ゲームプレイトリガー（VN → ゲームプレイ遷移条件）
struct GameplayTrigger
{
	std::string gameplayId;                            ///< ゲームプレイ識別子
	std::map<std::string, std::string> params;         ///< パラメータ
	GameplayType type = GameplayType::Battle;          ///< ゲームプレイタイプ
	std::function<bool(const SharedState&)> condition;  ///< 発動条件
};

/// @brief VNトリガー（ゲームプレイ → VN遷移条件）
struct VNTrigger
{
	std::string eventId;                               ///< イベント識別子
	std::string scriptLabel;                           ///< 遷移先VNスクリプトラベル
	std::function<bool(const SharedState&)> condition;  ///< 発動条件
};

// ─── ハイブリッドゲームマネージャ ───

/// @brief VN＋ゲームプレイハイブリッド管理
/// @details 二つのモード間のシームレスな遷移と共有状態管理を提供する。
class HybridGameManager
{
public:
	/// @brief デフォルトコンストラクタ
	HybridGameManager() = default;

	// ─── モード遷移 ───

	/// @brief VNモードに遷移する
	void enterVNMode()
	{
		m_previousMode = m_currentMode;
		m_currentMode = GameMode::VN;

		if (m_onModeChange)
		{
			m_onModeChange(m_currentMode);
		}
	}

	/// @brief ゲームプレイモードに遷移する
	/// @param type ゲームプレイタイプ
	void enterGameplayMode(GameplayType type = GameplayType::Battle)
	{
		m_previousMode = m_currentMode;
		m_currentMode = GameMode::Gameplay;
		m_currentGameplayType = type;

		if (m_onModeChange)
		{
			m_onModeChange(m_currentMode);
		}
	}

	/// @brief 前のモードに戻る
	void returnToPreviousMode()
	{
		if (m_previousMode == GameMode::VN)
		{
			enterVNMode();
		}
		else
		{
			enterGameplayMode(m_currentGameplayType);
		}
	}

	/// @brief 現在VNモードか
	[[nodiscard]] bool isInVNMode() const noexcept { return m_currentMode == GameMode::VN; }

	/// @brief 現在ゲームプレイモードか
	[[nodiscard]] bool isInGameplayMode() const noexcept { return m_currentMode == GameMode::Gameplay; }

	/// @brief 現在のモードを取得する
	[[nodiscard]] GameMode currentMode() const noexcept { return m_currentMode; }

	/// @brief 現在のゲームプレイタイプを取得する
	[[nodiscard]] GameplayType currentGameplayType() const noexcept { return m_currentGameplayType; }

	// ─── 共有状態 ───

	/// @brief 共有状態への参照を取得する
	[[nodiscard]] SharedState& sharedState() noexcept { return m_sharedState; }
	[[nodiscard]] const SharedState& sharedState() const noexcept { return m_sharedState; }

	/// @brief フラグを設定する（ショートカット）
	void setFlag(const std::string& key, FlagValue value)
	{
		m_sharedState.setFlag(key, std::move(value));
	}

	/// @brief フラグを取得する（ショートカット）
	template <typename T>
	[[nodiscard]] T getFlag(const std::string& key, const T& defaultValue = {}) const
	{
		return m_sharedState.getFlag<T>(key, defaultValue);
	}

	// ─── VNスクリプト管理 ───

	/// @brief VNスクリプトを設定する
	void setScript(VNScript script)
	{
		m_script = std::move(script);
		m_scriptIndex = 0;
	}

	/// @brief 現在のVNコマンドを取得する
	/// @return 現在のコマンド（スクリプト終了ならnullptr）
	[[nodiscard]] const VNCommand* currentCommand() const noexcept
	{
		if (m_scriptIndex >= m_script.commands.size()) return nullptr;
		return &m_script.commands[m_scriptIndex];
	}

	/// @brief 次のVNコマンドに進める
	/// @return ゲームプレイ遷移トリガーが発火した場合true
	bool advanceScript()
	{
		if (m_scriptIndex >= m_script.commands.size()) return false;

		const auto& cmd = m_script.commands[m_scriptIndex];

		// コマンド種類に応じた処理
		switch (cmd.type)
		{
		case VNCommandType::SetFlag:
			m_sharedState.setFlag(cmd.text, true);
			break;

		case VNCommandType::Jump:
		{
			auto it = m_script.labels.find(cmd.text);
			if (it != m_script.labels.end())
			{
				m_scriptIndex = it->second;
				return false;
			}
			break;
		}

		case VNCommandType::Gameplay:
		{
			// ゲームプレイ遷移コマンド
			enterGameplayMode(parseGameplayType(cmd.text));
			m_pendingReturnIndex = m_scriptIndex + 1;
			return true;
		}

		default:
			break;
		}

		++m_scriptIndex;
		return false;
	}

	/// @brief VN選択肢を選択する
	/// @param choiceIndex 選択した選択肢インデックス
	void selectChoice(int choiceIndex)
	{
		const auto* cmd = currentCommand();
		if (!cmd || cmd->type != VNCommandType::Choice) return;
		if (choiceIndex < 0 || choiceIndex >= static_cast<int>(cmd->choices.size())) return;

		const auto& choice = cmd->choices[static_cast<std::size_t>(choiceIndex)];
		if (!choice.jumpLabel.empty())
		{
			auto it = m_script.labels.find(choice.jumpLabel);
			if (it != m_script.labels.end())
			{
				m_scriptIndex = it->second;
				return;
			}
		}
		++m_scriptIndex;
	}

	/// @brief ゲームプレイ終了後にVNスクリプトに復帰する
	void returnToScript()
	{
		m_scriptIndex = m_pendingReturnIndex;
		enterVNMode();
	}

	// ─── トリガー管理 ───

	/// @brief ゲームプレイトリガーを追加する
	void addGameplayTrigger(GameplayTrigger trigger)
	{
		m_gameplayTriggers.push_back(std::move(trigger));
	}

	/// @brief VNトリガーを追加する
	void addVNTrigger(VNTrigger trigger)
	{
		m_vnTriggers.push_back(std::move(trigger));
	}

	/// @brief ゲームプレイトリガーを評価する
	/// @return 発火したトリガー（なければnullptr）
	[[nodiscard]] const GameplayTrigger* evaluateGameplayTriggers() const
	{
		for (const auto& trigger : m_gameplayTriggers)
		{
			if (trigger.condition && trigger.condition(m_sharedState))
			{
				return &trigger;
			}
		}
		return nullptr;
	}

	/// @brief VNトリガーを評価する
	/// @return 発火したトリガー（なければnullptr）
	[[nodiscard]] const VNTrigger* evaluateVNTriggers() const
	{
		for (const auto& trigger : m_vnTriggers)
		{
			if (trigger.condition && trigger.condition(m_sharedState))
			{
				return &trigger;
			}
		}
		return nullptr;
	}

	/// @brief トリガーを自動チェックし、必要に応じてモード遷移する
	/// @return 遷移が発生したかどうか
	bool processAutoTriggers()
	{
		if (isInVNMode())
		{
			if (const auto* trigger = evaluateGameplayTriggers())
			{
				enterGameplayMode(trigger->type);
				m_pendingReturnIndex = m_scriptIndex;
				return true;
			}
		}
		else
		{
			if (const auto* trigger = evaluateVNTriggers())
			{
				auto it = m_script.labels.find(trigger->scriptLabel);
				if (it != m_script.labels.end())
				{
					m_scriptIndex = it->second;
					enterVNMode();
					return true;
				}
			}
		}
		return false;
	}

	// ─── コールバック ───

	/// @brief モード変更コールバックを設定する
	void setOnModeChange(std::function<void(GameMode)> callback)
	{
		m_onModeChange = std::move(callback);
	}

private:
	/// @brief 文字列からGameplayTypeを解析する
	[[nodiscard]] static GameplayType parseGameplayType(const std::string& str) noexcept
	{
		if (str == "battle")   return GameplayType::Battle;
		if (str == "minigame") return GameplayType::Minigame;
		if (str == "puzzle")   return GameplayType::Puzzle;
		if (str == "explore")  return GameplayType::Explore;
		return GameplayType::Custom;
	}

	GameMode m_currentMode = GameMode::VN;
	GameMode m_previousMode = GameMode::VN;
	GameplayType m_currentGameplayType = GameplayType::Battle;

	SharedState m_sharedState;

	VNScript m_script;
	std::size_t m_scriptIndex = 0;
	std::size_t m_pendingReturnIndex = 0;

	std::vector<GameplayTrigger> m_gameplayTriggers;
	std::vector<VNTrigger> m_vnTriggers;

	std::function<void(GameMode)> m_onModeChange;
};

} // namespace mitiru::game
