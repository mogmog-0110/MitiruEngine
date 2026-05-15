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
#include <mitiru/bridge/BridgeViewPush.hpp>
#include <mitiru/bridge/detail/JsonEscape.hpp>

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
	// ── View Push 統合 ─────────────────────────────────────────

	/// @brief view push ハンドラを登録する（非所有 raw pointer）
	/// @details 登録後、loadScene / start / update / selectChoice のたびに
	///          現在の VN 状態が BridgeViewPush 経由で view 側に自動 push される。
	///          nullptr を渡すと push を無効化する。
	///
	/// push される key:
	/// - `"phase"`        → JSON-quoted フェーズ文字列 (`"\"Displaying\""`)
	/// - `"text"`         → JSON-quoted currentText()
	/// - `"speaker"`      → JSON-quoted 話者名
	/// - `"visibleChars"` → 整数文字列
	/// - `"hasChoices"`   → `"true"` / `"false"`
	/// - `"finished"`     → `"true"` / `"false"`
	///
	/// @code
	/// BridgeViewPush vp("vn", setSink, emitSink);
	/// vn.setViewPush(&vp);
	/// @endcode
	void setViewPush(BridgeViewPush* viewPush) noexcept
	{
		m_viewPush = viewPush;
	}

	// ── シーン管理 ────────────────────────────────────────────

	/// @brief VNシーンをロードする
	/// @param scene ロードするシーン
	void loadScene(sgc::vn::VNScene scene)
	{
		m_scene.emplace(std::move(scene));
		pushCurrentState();
	}

	/// @brief シーンを開始する
	void start()
	{
		if (m_scene.has_value())
		{
			m_scene->start();
		}
		pushCurrentState();
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
		pushCurrentState();
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
		pushCurrentState();
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
	std::optional<sgc::vn::VNScene> m_scene;         ///< VNシーン
	BridgeViewPush*                 m_viewPush{nullptr}; ///< 非所有。nullptr なら push 無効

	// ── push 用キャッシュ（per-frame alloc を避けるため） ──────────────
	// short key 文字列は string_view を取る set() に渡しても安全な静的寿命とする。
	static constexpr const char* kKeyPhase        = "phase";
	static constexpr const char* kKeyText         = "text";
	static constexpr const char* kKeySpeaker      = "speaker";
	static constexpr const char* kKeyVisibleChars = "visibleChars";
	static constexpr const char* kKeyHasChoices   = "hasChoices";
	static constexpr const char* kKeyFinished     = "finished";

	// 値側でリテラル化できるもの — 毎フレーム std::string 化を避ける。
	static constexpr const char* kPhaseIdleJson           = "\"Idle\"";
	static constexpr const char* kPhaseDisplayingJson     = "\"Displaying\"";
	static constexpr const char* kPhaseWaitingInputJson   = "\"WaitingInput\"";
	static constexpr const char* kPhaseShowingChoicesJson = "\"ShowingChoices\"";
	static constexpr const char* kPhaseUnknownJson        = "\"Unknown\"";
	static constexpr const char* kTrueJson      = "true";
	static constexpr const char* kFalseJson     = "false";
	static constexpr const char* kEmptyStrJson  = "\"\"";
	static constexpr const char* kZeroJson      = "0";

	/// @brief 前回 push した状態のスナップショット — 同一なら再 push を抑止する。
	struct StateSnapshot
	{
		bool        hasScene{false};
		const char* phaseJson{nullptr}; ///< 静的リテラル — pointer 比較で identity が取れる
		std::string textJson;           ///< 値依存。動的構築だが、変化時のみ更新
		std::string speakerJson;
		std::size_t visibleChars{0};
		bool        hasChoices{false};
		bool        finished{true};
	};
	StateSnapshot m_lastPushed;          ///< 直近 push 内容
	bool          m_hasPushedOnce{false}; ///< 一度も push していない場合は強制 push

	/// @brief VNフェーズを JSON-quoted 静的リテラルへ変換する（zero alloc）
	[[nodiscard]] static const char* phaseToJsonLiteral(sgc::vn::VNPhase p) noexcept
	{
		switch (p)
		{
		case sgc::vn::VNPhase::Idle:            return kPhaseIdleJson;
		case sgc::vn::VNPhase::Displaying:      return kPhaseDisplayingJson;
		case sgc::vn::VNPhase::WaitingInput:    return kPhaseWaitingInputJson;
		case sgc::vn::VNPhase::ShowingChoices:  return kPhaseShowingChoicesJson;
		}
		return kPhaseUnknownJson;
	}

	/// @brief VNフェーズを文字列に変換する (toJson() 互換のため残存)
	[[nodiscard]] static std::string phaseToString(sgc::vn::VNPhase p)
	{
		switch (p)
		{
		case sgc::vn::VNPhase::Idle:            return "Idle";
		case sgc::vn::VNPhase::Displaying:      return "Displaying";
		case sgc::vn::VNPhase::WaitingInput:    return "WaitingInput";
		case sgc::vn::VNPhase::ShowingChoices:  return "ShowingChoices";
		}
		return "Unknown";
	}

	/// @brief 現在の VN 状態を変化キーのみ push する（short-circuit 付き）。
	/// @note m_viewPush が nullptr なら何もしない。
	void pushCurrentState()
	{
		if (!m_viewPush) return;

		// 「no scene」状態は固定値セットで処理する分岐
		if (!m_scene.has_value())
		{
			pushNoSceneState();
			return;
		}

		const auto& scene = *m_scene;
		const auto  phase = scene.phase();

		StateSnapshot next;
		next.hasScene     = true;
		next.phaseJson    = phaseToJsonLiteral(phase);
		next.visibleChars = scene.textState().visibleChars;
		next.hasChoices   = (phase == sgc::vn::VNPhase::ShowingChoices);
		next.finished     = scene.isFinished();
		// 文字列値はキャッシュ比較のためここで構築する（変化時のみ push 用に再利用）。
		next.textJson    = detail::quotedJson(scene.textState().fullText);
		next.speakerJson = detail::quotedJson(scene.currentSpeaker());

		dispatchDiff(next);
		m_lastPushed    = std::move(next);
		m_hasPushedOnce = true;
	}

	/// @brief 「シーン未ロード」状態を push する（必要なら）
	void pushNoSceneState()
	{
		if (m_hasPushedOnce && !m_lastPushed.hasScene) { return; }

		m_viewPush->set(kKeyFinished,     kTrueJson);
		m_viewPush->set(kKeyPhase,        kPhaseIdleJson);
		m_viewPush->set(kKeyText,         kEmptyStrJson);
		m_viewPush->set(kKeySpeaker,      kEmptyStrJson);
		m_viewPush->set(kKeyVisibleChars, kZeroJson);
		m_viewPush->set(kKeyHasChoices,   kFalseJson);

		m_lastPushed              = {};
		m_lastPushed.hasScene     = false;
		m_lastPushed.phaseJson    = kPhaseIdleJson;
		m_lastPushed.textJson     = kEmptyStrJson;
		m_lastPushed.speakerJson  = kEmptyStrJson;
		m_lastPushed.visibleChars = 0;
		m_lastPushed.hasChoices   = false;
		m_lastPushed.finished     = true;
		m_hasPushedOnce           = true;
	}

	/// @brief 直近 push との差分だけを送る。
	void dispatchDiff(const StateSnapshot& next)
	{
		const bool force = !m_hasPushedOnce || !m_lastPushed.hasScene;

		if (force || next.phaseJson != m_lastPushed.phaseJson)
			{ m_viewPush->set(kKeyPhase, next.phaseJson); }

		if (force || next.textJson != m_lastPushed.textJson)
			{ m_viewPush->set(kKeyText, next.textJson); }

		if (force || next.speakerJson != m_lastPushed.speakerJson)
			{ m_viewPush->set(kKeySpeaker, next.speakerJson); }

		if (force || next.visibleChars != m_lastPushed.visibleChars)
		{
			// short-form decimal — small alloc, only on change.
			m_viewPush->set(kKeyVisibleChars, std::to_string(next.visibleChars));
		}

		if (force || next.hasChoices != m_lastPushed.hasChoices)
			{ m_viewPush->set(kKeyHasChoices, next.hasChoices ? kTrueJson : kFalseJson); }

		if (force || next.finished != m_lastPushed.finished)
			{ m_viewPush->set(kKeyFinished, next.finished ? kTrueJson : kFalseJson); }
	}
};

} // namespace mitiru::bridge
