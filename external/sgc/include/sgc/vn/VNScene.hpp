#pragma once

/// @file VNScene.hpp
/// @brief VNシーンオーケストレーター
///
/// ビジュアルノベルの全体的な進行を管理する。
/// コマンドキューに基づいてテキスト表示・キャラクター演出・選択肢を制御する。
///
/// @code
/// using namespace sgc::vn;
/// VNScene scene;
/// scene.addCommand({VNCommand::Type::Say, "sakura", "Hello!", "happy"});
/// scene.addCommand({VNCommand::Type::Choice, "", "", "", CharacterPosition::Center, 0.0f,
///     {{"Yes", "yes_id"}, {"No", "no_id"}}});
/// scene.start();
/// scene.update(dt, advancePressed, skipPressed);
/// @endcode

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "sgc/vn/Backlog.hpp"
#include "sgc/vn/CharacterManager.hpp"
#include "sgc/vn/ChoicePresenter.hpp"
#include "sgc/vn/TextDisplay.hpp"

namespace sgc::vn
{

/// @brief VNシーンの進行フェーズ
enum class VNPhase : uint8_t
{
	Idle,            ///< 何も表示していない
	Displaying,      ///< テキスト表示中
	WaitingInput,    ///< 入力待ち
	ShowingChoices   ///< 選択肢表示中
};

/// @brief VNシーンのコマンド
///
/// VNシーンの各演出指示を表す。
/// typeによって使用するフィールドが異なる。
struct VNCommand
{
	/// @brief コマンドの種類
	enum class Type
	{
		Say,             ///< テキスト表示
		ShowCharacter,   ///< キャラクター表示
		HideCharacter,   ///< キャラクター非表示
		SetExpression,   ///< 表情変更
		Choice,          ///< 選択肢表示
		Wait,            ///< 待機
		Clear            ///< クリア
	};

	Type type;                                               ///< コマンド種別
	std::string characterId;                                 ///< 対象キャラクターID
	std::string text;                                        ///< テキスト内容
	std::string expression;                                  ///< 表情名
	CharacterPosition position = CharacterPosition::Center;  ///< 立ち位置
	float duration = 0.0f;                                   ///< 待機時間（秒）
	std::vector<Choice> choices;                             ///< 選択肢リスト
};

/// @brief VNシーンオーケストレーター
///
/// コマンドキューに基づいてビジュアルノベルの演出を管理する。
/// テキスト表示・キャラクター管理・選択肢・バックログを統括する。
class VNScene
{
public:
	/// @brief コマンドを追加する
	/// @param cmd 追加するコマンド
	void addCommand(VNCommand cmd)
	{
		m_commands.push_back(std::move(cmd));
	}

	/// @brief シーンを開始する
	///
	/// コマンドキューの先頭から実行を開始する。
	void start()
	{
		m_currentIndex = 0;
		m_phase = VNPhase::Idle;
		m_finished = false;
		m_textState = TextDisplayState{};
		m_currentSpeaker.clear();
		processNextCommand();
	}

	/// @brief シーンを更新する
	/// @param dt デルタタイム（秒）
	/// @param advancePressed 進行入力があったか
	/// @param skipPressed スキップ入力があったか
	void update(float dt, bool advancePressed, bool skipPressed)
	{
		if (m_finished)
		{
			return;
		}

		switch (m_phase)
		{
		case VNPhase::Idle:
			break;

		case VNPhase::Displaying:
		{
			m_textState = updateTextDisplay(m_textState, dt, m_textConfig, advancePressed, skipPressed);
			if (m_textState.isComplete)
			{
				m_phase = VNPhase::WaitingInput;
			}
			break;
		}

		case VNPhase::WaitingInput:
		{
			m_textState = updateTextDisplay(m_textState, dt, m_textConfig, advancePressed, skipPressed);
			if (m_textState.advanced)
			{
				// バックログに記録
				m_backlog.addEntry({m_currentSpeaker, m_textState.fullText, ""});
				advanceToNextCommand();
			}
			break;
		}

		case VNPhase::ShowingChoices:
			// 選択肢はselectChoice()で処理される
			break;
		}
	}

	/// @brief 現在のフェーズを取得する
	/// @return フェーズ
	[[nodiscard]] VNPhase phase() const noexcept
	{
		return m_phase;
	}

	/// @brief テキスト表示状態を取得する
	/// @return テキスト表示状態への参照
	[[nodiscard]] const TextDisplayState& textState() const noexcept
	{
		return m_textState;
	}

	/// @brief キャラクターマネージャを取得する
	/// @return キャラクターマネージャへの参照
	[[nodiscard]] const CharacterManager& characters() const noexcept
	{
		return m_characters;
	}

	/// @brief キャラクターマネージャを取得する（非const版）
	/// @return キャラクターマネージャへの参照
	[[nodiscard]] CharacterManager& characters() noexcept
	{
		return m_characters;
	}

	/// @brief バックログを取得する
	/// @return バックログへの参照
	[[nodiscard]] const Backlog& backlog() const noexcept
	{
		return m_backlog;
	}

	/// @brief 現在の話者名を取得する
	/// @return 話者名
	[[nodiscard]] std::string currentSpeaker() const
	{
		return m_currentSpeaker;
	}

	/// @brief シーンが終了したかを判定する
	/// @return 終了済みならtrue
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_finished;
	}

	/// @brief コマンド数を取得する
	/// @return 登録済みコマンド数
	[[nodiscard]] std::size_t commandCount() const noexcept
	{
		return m_commands.size();
	}

	/// @brief 選択肢を選択する
	///
	/// ShowingChoicesフェーズでのみ有効。
	///
	/// @param index 選択する選択肢のインデックス
	void selectChoice(int index)
	{
		if (m_phase != VNPhase::ShowingChoices)
		{
			return;
		}

		if (index < 0 || index >= static_cast<int>(m_currentChoices.size()))
		{
			return;
		}

		// バックログに選択を記録
		const auto& choice = m_currentChoices[static_cast<std::size_t>(index)];
		m_backlog.addEntry({"", "", choice.text});

		m_currentChoices.clear();
		advanceToNextCommand();
	}

	/// @brief 現在の選択肢一覧を取得する
	/// @return 選択肢のスパン
	[[nodiscard]] std::span<const Choice> currentChoices() const noexcept
	{
		return m_currentChoices;
	}

	/// @brief テキスト表示設定を設定する
	/// @param config 設定
	void setTextConfig(const TextDisplayConfig& config)
	{
		m_textConfig = config;
	}

private:
	/// @brief 次のコマンドに進む
	void advanceToNextCommand()
	{
		++m_currentIndex;
		processNextCommand();
	}

	/// @brief 現在のコマンドを処理する
	void processNextCommand()
	{
		while (m_currentIndex < m_commands.size())
		{
			const auto& cmd = m_commands[m_currentIndex];

			switch (cmd.type)
			{
			case VNCommand::Type::Say:
			{
				m_currentSpeaker = cmd.characterId;
				m_textState = TextDisplayState{};
				m_textState.fullText = cmd.text;

				// 表情変更（指定されている場合）
				if (!cmd.expression.empty() && !cmd.characterId.empty())
				{
					m_characters.setExpression(cmd.characterId, cmd.expression);
				}

				m_phase = VNPhase::Displaying;
				return;
			}

			case VNCommand::Type::ShowCharacter:
			{
				m_characters.show(cmd.characterId, cmd.position, cmd.expression.empty() ? "default" : cmd.expression);
				++m_currentIndex;
				continue;
			}

			case VNCommand::Type::HideCharacter:
			{
				m_characters.hide(cmd.characterId);
				++m_currentIndex;
				continue;
			}

			case VNCommand::Type::SetExpression:
			{
				m_characters.setExpression(cmd.characterId, cmd.expression);
				++m_currentIndex;
				continue;
			}

			case VNCommand::Type::Choice:
			{
				m_currentChoices = cmd.choices;
				m_phase = VNPhase::ShowingChoices;
				return;
			}

			case VNCommand::Type::Wait:
			{
				// 簡易実装: 待機は即座にスキップ（将来的にタイマー対応）
				++m_currentIndex;
				continue;
			}

			case VNCommand::Type::Clear:
			{
				m_textState = TextDisplayState{};
				m_currentSpeaker.clear();
				++m_currentIndex;
				continue;
			}
			}

			++m_currentIndex;
		}

		// すべてのコマンドを処理済み
		m_phase = VNPhase::Idle;
		m_finished = true;
	}

	std::vector<VNCommand> m_commands;          ///< コマンドキュー
	std::size_t m_currentIndex = 0;             ///< 現在のコマンドインデックス
	VNPhase m_phase = VNPhase::Idle;            ///< 現在のフェーズ
	bool m_finished = true;                     ///< シーン終了フラグ

	TextDisplayConfig m_textConfig;             ///< テキスト表示設定
	TextDisplayState m_textState;               ///< テキスト表示状態
	std::string m_currentSpeaker;               ///< 現在の話者名
	std::vector<Choice> m_currentChoices;       ///< 現在の選択肢

	CharacterManager m_characters;              ///< キャラクターマネージャ
	Backlog m_backlog;                          ///< バックログ
};

} // namespace sgc::vn
