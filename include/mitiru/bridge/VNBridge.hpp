#pragma once

/// @file VNBridge.hpp
/// @brief sgc VN（ビジュアルノベル）統合ブリッジ
/// @details sgcのVNScene、テキスト表示、キャラクター管理、バックログ、
///          選択肢、テキストエフェクトをMitiruエンジンに統合する。

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <sgc/vn/VNScene.hpp>
#include <sgc/vn/TextDisplay.hpp>
#include <sgc/vn/CharacterManager.hpp>
#include <sgc/vn/Backlog.hpp>
#include <sgc/vn/ChoicePresenter.hpp>
#include <sgc/vn/TextEffects.hpp>

namespace mitiru::bridge
{

/// @brief sgc VN統合ブリッジ
/// @details VNシーンの読み込み・進行・選択肢処理・バックログ参照を統合管理する。
///
/// @code
/// mitiru::bridge::VNBridge vn;
///
/// sgc::vn::VNScene scene;
/// scene.addCommand({sgc::vn::VNCommand::Type::Say, "npc", "Hello!", ""});
/// vn.loadScene(std::move(scene));
/// vn.start();
/// vn.update(0.5f, false, false);
/// @endcode
class VNBridge
{
public:
	// ── シーン管理 ────────────────────────────────────────────

	/// @brief VNシーンをロードする
	/// @param scene ロードするシーン
	void loadScene(sgc::vn::VNScene scene)
	{
		m_scene.emplace(std::move(scene));
	}

	/// @brief シーンを開始する
	void start()
	{
		if (m_scene.has_value())
		{
			m_scene->start();
		}
	}

	/// @brief シーンを更新する
	/// @param dt デルタタイム（秒）
	/// @param advancePressed 進行入力があったか
	/// @param skipPressed スキップ入力があったか
	void update(float dt, bool advancePressed, bool skipPressed)
	{
		if (m_scene.has_value())
		{
			m_scene->update(dt, advancePressed, skipPressed);
		}
	}

	// ── 状態クエリ ────────────────────────────────────────────

	/// @brief 現在のフェーズを取得する
	/// @return VNフェーズ（シーン未ロード時はIdle）
	[[nodiscard]] sgc::vn::VNPhase phase() const
	{
		if (m_scene.has_value())
		{
			return m_scene->phase();
		}
		return sgc::vn::VNPhase::Idle;
	}

	/// @brief 現在のテキストを取得する
	/// @return 表示中のテキスト（シーン未ロード時は空文字列）
	[[nodiscard]] std::string currentText() const
	{
		if (m_scene.has_value())
		{
			return m_scene->textState().fullText;
		}
		return {};
	}

	/// @brief 現在の話者名を取得する
	/// @return 話者名（シーン未ロード時は空文字列）
	[[nodiscard]] std::string currentSpeaker() const
	{
		if (m_scene.has_value())
		{
			return m_scene->currentSpeaker();
		}
		return {};
	}

	/// @brief 現在表示されている文字数を取得する
	/// @return 表示文字数
	[[nodiscard]] std::size_t visibleChars() const
	{
		if (m_scene.has_value())
		{
			return m_scene->textState().visibleChars;
		}
		return 0;
	}

	/// @brief シーンが終了したかを判定する
	/// @return 終了済みならtrue（シーン未ロード時もtrue）
	[[nodiscard]] bool isFinished() const
	{
		if (m_scene.has_value())
		{
			return m_scene->isFinished();
		}
		return true;
	}

	// ── キャラクター管理 ──────────────────────────────────────

	/// @brief キャラクターマネージャを取得する（非const版）
	/// @return キャラクターマネージャへの参照
	/// @note シーン未ロード時はloadScene()を先に呼ぶこと
	sgc::vn::CharacterManager& characters()
	{
		return m_scene->characters();
	}

	/// @brief キャラクターマネージャを取得する
	/// @return キャラクターマネージャへのconst参照
	const sgc::vn::CharacterManager& characters() const
	{
		return m_scene->characters();
	}

	// ── 選択肢処理 ────────────────────────────────────────────

	/// @brief 選択肢が表示中かを判定する
	/// @return 選択肢表示中ならtrue
	[[nodiscard]] bool hasChoices() const
	{
		if (m_scene.has_value())
		{
			return m_scene->phase() == sgc::vn::VNPhase::ShowingChoices;
		}
		return false;
	}

	/// @brief 選択肢を選択する
	/// @param index 選択肢のインデックス
	void selectChoice(int index)
	{
		if (m_scene.has_value())
		{
			m_scene->selectChoice(index);
		}
	}

	// ── バックログ ────────────────────────────────────────────

	/// @brief バックログを取得する
	/// @return バックログへのconst参照
	[[nodiscard]] const sgc::vn::Backlog& backlog() const
	{
		return m_scene->backlog();
	}

	// ── テキストエフェクト ────────────────────────────────────

	/// @brief タグ付きテキストをセグメントに分解する
	/// @param text タグ付きテキスト
	/// @return テキストセグメントの配列
	[[nodiscard]] std::vector<sgc::vn::TextSegment> parseEffects(const std::string& text) const
	{
		return sgc::vn::parseTextEffects(text);
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief VN状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"hasScene\":" + std::string(m_scene.has_value() ? "true" : "false");

		if (m_scene.has_value())
		{
			json += ",\"phase\":\"" + phaseToString(m_scene->phase()) + "\"";
			json += ",\"finished\":" + std::string(m_scene->isFinished() ? "true" : "false");
			json += ",\"speaker\":\"" + m_scene->currentSpeaker() + "\"";
			json += ",\"backlogSize\":" + std::to_string(m_scene->backlog().size());
		}

		json += "}";
		return json;
	}

private:
	std::optional<sgc::vn::VNScene> m_scene;  ///< VNシーン

	/// @brief VNフェーズを文字列に変換する
	/// @param p フェーズ
	/// @return 文字列表現
	[[nodiscard]] static std::string phaseToString(sgc::vn::VNPhase p)
	{
		switch (p)
		{
		case sgc::vn::VNPhase::Idle:           return "Idle";
		case sgc::vn::VNPhase::Displaying:     return "Displaying";
		case sgc::vn::VNPhase::WaitingInput:   return "WaitingInput";
		case sgc::vn::VNPhase::ShowingChoices:  return "ShowingChoices";
		}
		return "Unknown";
	}
};

} // namespace mitiru::bridge
