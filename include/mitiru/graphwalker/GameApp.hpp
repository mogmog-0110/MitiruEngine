#pragma once

/// @file GameApp.hpp
/// @brief GraphWalkerメインゲームアプリケーション（オーケストレータ）
///
/// ゲーム全体のライフサイクル管理を行う。シーン遷移（タイトル→プレイ→クリア）、
/// サブシステムの統合（パーティクル、BGM、用語辞典）を担当する。
/// 描画非依存でテスト可能。
///
/// @code
/// mitiru::gw::GameApp app;
/// app.init(1280.0f, 720.0f);
/// while (app.isRunning()) {
///     app.update(dt, input);
/// }
/// @endcode

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "mitiru/graphwalker/Common.hpp"
#include "mitiru/graphwalker/Effects.hpp"
#include "mitiru/graphwalker/BgmManager.hpp"
#include "mitiru/graphwalker/GlossaryData.hpp"

namespace mitiru::gw
{

/// @brief ゲームシーンの列挙型
enum class GameScene
{
	TitleScene,  ///< タイトル画面
	PlayScene,   ///< プレイ中
	ClearScene,  ///< クリア画面
};

/// @brief GraphWalker用の入力状態
struct GWInputState
{
	bool left = false;       ///< 左移動
	bool right = false;      ///< 右移動
	bool jump = false;       ///< ジャンプ（押下）
	bool confirm = false;    ///< 決定キー（押下）
	bool cancel = false;     ///< キャンセルキー（押下）
	bool up = false;         ///< 上（メニュー選択）
	bool down = false;       ///< 下（メニュー選択）
	bool interact = false;   ///< インタラクト（NPC会話等）
	bool pause = false;      ///< ポーズ
};

/// @brief クリア時の統計情報
struct ClearStats
{
	float playTime = 0.0f;               ///< プレイ時間（秒）
	int collectedItems = 0;              ///< 収集アイテム数
	int totalItems = 0;                  ///< 全アイテム数
	int achievementsUnlocked = 0;        ///< 解除済み実績数
	int zonesVisited = 0;                ///< 訪問ゾーン数
};

/// @brief メインゲームアプリケーション
///
/// ゲーム全体のオーケストレータ。シーン遷移、サブシステム統合、
/// 更新ループを管理する。描画は外部に委譲する。
class GameApp
{
public:
	/// @brief コンストラクタ
	GameApp() = default;

	/// @brief ゲームを初期化する
	/// @param screenW 画面幅
	/// @param screenH 画面高さ
	void init(float screenW, float screenH)
	{
		m_screenW = screenW;
		m_screenH = screenH;
		m_running = true;
		m_currentScene = GameScene::TitleScene;

		// 共有データ初期化
		m_sharedData = SharedData{};
		m_sharedData.state = GameState::Title;

		// サブシステム初期化
		m_emitter = ParticleEmitter{};
		m_bgm = BgmManager{};
		m_glossary = GlossaryDatabase{};
		m_glossary.loadBuiltinEntries();

		// エフェクト初期化
		m_cameraEffect = CameraEffect{};
		m_screenFade = ScreenFade{};
		m_screenFade.alpha = 1.0f;
		m_screenFade.fadeIn(2.0f); // タイトル画面へのフェードイン

		m_announcement = ZoneAnnouncement{};

		// タイトルメニュー初期化
		m_selectedSlot = 0;
		m_titleMenuIndex = 0;

		m_initialized = true;
	}

	/// @brief メインゲームループの1ティック
	/// @param dt デルタタイム（秒）
	/// @param input 入力状態
	void update(float dt, const GWInputState& input)
	{
		if (!m_running || !m_initialized)
		{
			return;
		}

		// 共通エフェクト更新
		m_emitter.update(dt);
		m_cameraEffect.update(dt);
		m_screenFade.update(dt);
		m_announcement.update(dt);
		m_bgm.update(dt);

		// シーン別更新
		switch (m_currentScene)
		{
		case GameScene::TitleScene:
			updateTitle(dt, input);
			break;
		case GameScene::PlayScene:
			updatePlaying(dt, input);
			break;
		case GameScene::ClearScene:
			updateClear(dt, input);
			break;
		}
	}

	/// @brief 現在のシーンを取得する
	/// @return 現在のGameScene
	[[nodiscard]] GameScene getCurrentScene() const { return m_currentScene; }

	/// @brief 共有データを取得する
	/// @return SharedDataのconst参照
	[[nodiscard]] const SharedData& getSharedData() const { return m_sharedData; }

	/// @brief 共有データへの非const参照を取得する
	/// @return SharedDataの参照
	[[nodiscard]] SharedData& getSharedData() { return m_sharedData; }

	/// @brief パーティクルエミッターを取得する
	/// @return ParticleEmitterのconst参照
	[[nodiscard]] const ParticleEmitter& getEmitter() const { return m_emitter; }

	/// @brief BGMマネージャを取得する
	/// @return BgmManagerのconst参照
	[[nodiscard]] const BgmManager& getBgmManager() const { return m_bgm; }

	/// @brief 用語辞典データベースを取得する
	/// @return GlossaryDatabaseのconst参照
	[[nodiscard]] const GlossaryDatabase& getGlossary() const { return m_glossary; }

	/// @brief カメラエフェクトを取得する
	/// @return CameraEffectのconst参照
	[[nodiscard]] const CameraEffect& getCameraEffect() const { return m_cameraEffect; }

	/// @brief スクリーンフェードを取得する
	/// @return ScreenFadeのconst参照
	[[nodiscard]] const ScreenFade& getScreenFade() const { return m_screenFade; }

	/// @brief ゾーンアナウンスを取得する
	/// @return ZoneAnnouncementのconst参照
	[[nodiscard]] const ZoneAnnouncement& getAnnouncement() const { return m_announcement; }

	/// @brief タイトルメニューの選択インデックスを取得する
	/// @return 選択インデックス
	[[nodiscard]] int getTitleMenuIndex() const { return m_titleMenuIndex; }

	/// @brief 選択中のセーブスロットを取得する
	/// @return スロット番号
	[[nodiscard]] int getSelectedSlot() const { return m_selectedSlot; }

	/// @brief クリア統計情報を取得する
	/// @return ClearStats
	[[nodiscard]] ClearStats getClearStats() const
	{
		ClearStats stats;
		stats.playTime = m_sharedData.playTime;
		stats.collectedItems = static_cast<int>(m_sharedData.collectedItems.size());
		stats.achievementsUnlocked = countAchievements();
		return stats;
	}

	/// @brief 実行中か判定する
	/// @return 実行中ならtrue
	[[nodiscard]] bool isRunning() const { return m_running; }

	/// @brief 初期化済みか判定する
	/// @return 初期化済みならtrue
	[[nodiscard]] bool isInitialized() const { return m_initialized; }

	/// @brief ゲームを終了する
	void quit() { m_running = false; }

	/// @brief シーンを変更する
	/// @param scene 遷移先シーン
	void changeScene(GameScene scene)
	{
		m_currentScene = scene;

		switch (scene)
		{
		case GameScene::TitleScene:
			m_sharedData.state = GameState::Title;
			m_titleMenuIndex = 0;
			break;
		case GameScene::PlayScene:
			m_sharedData.state = GameState::Playing;
			m_bgm.setZone(m_sharedData.currentZone);
			m_announcement.show("Tutorial Zone", NeonColor::zonePrimary(m_sharedData.currentZone));
			break;
		case GameScene::ClearScene:
			m_sharedData.state = GameState::Clear;
			break;
		}

		// シーン遷移時にフェード
		m_screenFade.alpha = 1.0f;
		m_screenFade.fadeIn(1.5f);
	}

	/// @brief 画面幅を取得する
	[[nodiscard]] float getScreenWidth() const { return m_screenW; }

	/// @brief 画面高さを取得する
	[[nodiscard]] float getScreenHeight() const { return m_screenH; }

private:
	/// @brief タイトルシーンの更新
	/// @param dt デルタタイム
	/// @param input 入力状態
	void updateTitle(float /*dt*/, const GWInputState& input)
	{
		// メニュー操作
		if (input.up)
		{
			m_titleMenuIndex = std::max(0, m_titleMenuIndex - 1);
		}
		if (input.down)
		{
			m_titleMenuIndex = std::min(m_titleMenuMaxIndex, m_titleMenuIndex + 1);
		}

		// スロット選択
		if (m_titleMenuIndex < GameConstants::SAVE_SLOT_COUNT)
		{
			m_selectedSlot = m_titleMenuIndex;
		}

		// 決定
		if (input.confirm)
		{
			if (m_titleMenuIndex < GameConstants::SAVE_SLOT_COUNT)
			{
				// スロット選択 → ゲーム開始
				m_sharedData.currentSlot = m_selectedSlot;
				changeScene(GameScene::PlayScene);
			}
			else if (m_titleMenuIndex == m_titleMenuMaxIndex)
			{
				// 終了
				quit();
			}
		}
	}

	/// @brief プレイシーンの更新
	/// @param dt デルタタイム
	/// @param input 入力状態
	void updatePlaying(float dt, const GWInputState& input)
	{
		// プレイ時間カウント
		m_sharedData.playTime += dt;

		// ポーズ処理
		if (input.pause)
		{
			if (m_sharedData.state == GameState::Playing)
			{
				m_sharedData.state = GameState::Paused;
			}
			else if (m_sharedData.state == GameState::Paused)
			{
				m_sharedData.state = GameState::Playing;
			}
		}

		// キャンセルでタイトルに戻る
		if (input.cancel)
		{
			changeScene(GameScene::TitleScene);
		}
	}

	/// @brief クリアシーンの更新
	/// @param dt デルタタイム
	/// @param input 入力状態
	void updateClear(float /*dt*/, const GWInputState& input)
	{
		// 決定でタイトルに戻る
		if (input.confirm)
		{
			changeScene(GameScene::TitleScene);
		}
	}

	/// @brief 解除済み実績数をカウントする
	/// @return 解除数
	[[nodiscard]] int countAchievements() const
	{
		int count = 0;
		for (const auto& [id, achieved] : m_sharedData.achievements)
		{
			if (achieved)
			{
				++count;
			}
		}
		return count;
	}

	// ── サブシステム ──────────────────────────────────────
	SharedData m_sharedData{};             ///< シーン間共有データ
	ParticleEmitter m_emitter{};           ///< パーティクルエミッター
	BgmManager m_bgm{};                   ///< BGMマネージャ
	GlossaryDatabase m_glossary{};         ///< 用語辞典
	CameraEffect m_cameraEffect{};        ///< カメラエフェクト
	ScreenFade m_screenFade{};             ///< スクリーンフェード
	ZoneAnnouncement m_announcement{};     ///< ゾーンアナウンス

	// ── 状態 ──────────────────────────────────────────────
	GameScene m_currentScene = GameScene::TitleScene; ///< 現在のシーン
	float m_screenW = 0.0f;                ///< 画面幅
	float m_screenH = 0.0f;                ///< 画面高さ
	bool m_running = false;                ///< 実行中フラグ
	bool m_initialized = false;            ///< 初期化済みフラグ

	// ── タイトルメニュー ──────────────────────────────────
	int m_selectedSlot = 0;                ///< 選択中のセーブスロット
	int m_titleMenuIndex = 0;              ///< タイトルメニューインデックス
	static constexpr int m_titleMenuMaxIndex = GameConstants::SAVE_SLOT_COUNT; ///< メニュー最大インデックス（スロット数+終了）
};

} // namespace mitiru::gw
