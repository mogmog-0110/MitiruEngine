#pragma once

/// @file SampleServiceLocator.hpp
/// @brief ServiceLocator（依存性注入）サンプル
///
/// ServiceLocatorを使ったサービス登録・取得・削除を可視化する。
/// インターフェースベースのスコアサービスを登録し、
/// クリックでスコア加算、キーでサービスの登録・解除を操作する。
/// - クリック: スコア加算
/// - 1キー: スコアサービスを登録
/// - 2キー: スコアサービスを登録解除
/// - Rキー: リセット
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/patterns/ServiceLocator.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief スコアサービスのインターフェース
struct IScoreService
{
	virtual ~IScoreService() = default;

	/// @brief 現在のスコアを取得する
	virtual int getScore() const = 0;

	/// @brief スコアを加算する
	/// @param points 加算するポイント
	virtual void addScore(int points) = 0;
};

/// @brief スコアサービスの実装
class SimpleScoreService : public IScoreService
{
public:
	/// @brief 現在のスコアを取得する
	[[nodiscard]] int getScore() const override { return m_score; }

	/// @brief スコアを加算する
	void addScore(int points) override { m_score += points; }

private:
	int m_score{0};
};

/// @brief ServiceLocatorサンプルシーン
///
/// ServiceLocatorパターンによる依存性注入のデモ。
/// サービスの登録・取得・削除をリアルタイムに可視化し、
/// 型安全なサービス管理の仕組みを直感的に理解できる。
class SampleServiceLocator : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_locator.emplace();
		m_log.clear();
		m_clickEffect = 0.0f;
		registerScoreService();
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			onEnter();
			return;
		}

		// 1キー: サービス登録
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			registerScoreService();
		}

		// 2キー: サービス登録解除
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			unregisterScoreService();
		}

		// クリック: スコア加算
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			addScoreViaLocator();
			m_clickEffect = 1.0f;
		}

		// クリックエフェクト減衰
		if (m_clickEffect > 0.0f)
		{
			m_clickEffect -= dt * 3.0f;
			if (m_clickEffect < 0.0f) m_clickEffect = 0.0f;
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		r->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.12f, 1.0f});

		// タイトル
		tr->drawText("ServiceLocator - Dependency Injection", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.9f, 0.6f, 1.0f, 1.0f});
		tr->drawText(
			"[1] Register  [2] Unregister  [Click] Add Score  [R] Reset  [Esc] Back",
			14.0f, {10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// サービス状態パネル
		drawServicePanel(r, tr);

		// スコア表示エリア
		drawScoreDisplay(r, tr, sw);

		// ログ表示
		drawLog(tr, sh);
	}

private:
	static constexpr int SCORE_PER_CLICK = 10;  ///< クリック毎の加算点

	std::optional<sgc::ServiceLocator> m_locator;  ///< サービスロケータ
	std::vector<std::string> m_log;      ///< 操作ログ
	float m_clickEffect{0.0f};           ///< クリックエフェクト用

	/// @brief スコアサービスを登録する
	void registerScoreService()
	{
		if (m_locator->has<IScoreService>())
		{
			addLog("Already registered");
			return;
		}
		m_locator->provide<IScoreService>(
			std::make_shared<SimpleScoreService>());
		addLog("Registered IScoreService");
	}

	/// @brief スコアサービスの登録を解除する
	void unregisterScoreService()
	{
		if (!m_locator->has<IScoreService>())
		{
			addLog("Not registered");
			return;
		}
		m_locator->remove<IScoreService>();
		addLog("Unregistered IScoreService");
	}

	/// @brief ServiceLocator経由でスコアを加算する
	void addScoreViaLocator()
	{
		if (!m_locator->has<IScoreService>())
		{
			addLog("ERROR: No IScoreService!");
			return;
		}
		auto& svc = m_locator->get<IScoreService>();
		svc.addScore(SCORE_PER_CLICK);
		addLog("Score += " + std::to_string(SCORE_PER_CLICK)
			+ " (total: " + std::to_string(svc.getScore()) + ")");
	}

	/// @brief ログを追加する（最大8件）
	/// @param msg ログメッセージ
	void addLog(const std::string& msg)
	{
		m_log.push_back(msg);
		if (m_log.size() > 8)
		{
			m_log.erase(m_log.begin());
		}
	}

	/// @brief サービス状態パネルを描画する
	void drawServicePanel(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const float px = 20.0f;
		const float py = 75.0f;
		const float pw = 350.0f;
		const float ph = 120.0f;

		r->drawRect(sgc::AABB2f{{px, py}, {px + pw, py + ph}},
			sgc::Colorf{0.08f, 0.08f, 0.15f, 0.9f});
		r->drawRectFrame(sgc::AABB2f{{px, py}, {px + pw, py + ph}},
			2.0f, sgc::Colorf{0.4f, 0.4f, 0.8f, 0.8f});

		tr->drawText("Registered Services", 18.0f,
			{px + 10.0f, py + 8.0f},
			sgc::Colorf{0.7f, 0.7f, 1.0f, 1.0f});

		// IScoreService 状態
		const bool hasScore = m_locator->has<IScoreService>();
		const sgc::Colorf statusColor = hasScore
			? sgc::Colorf{0.2f, 1.0f, 0.4f, 1.0f}
			: sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f};
		const std::string statusText = hasScore ? "REGISTERED" : "NOT REGISTERED";

		// ステータスインジケータ（丸）
		r->drawCircle({px + 25.0f, py + 52.0f}, 6.0f, statusColor);

		tr->drawText("IScoreService", 16.0f,
			{px + 40.0f, py + 42.0f},
			sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f});
		tr->drawText(statusText, 14.0f,
			{px + 200.0f, py + 44.0f}, statusColor);

		// スコア値（登録時のみ）
		if (hasScore)
		{
			const int score = m_locator->get<IScoreService>().getScore();
			tr->drawText("Current Score: " + std::to_string(score), 16.0f,
				{px + 40.0f, py + 72.0f},
				sgc::Colorf{1.0f, 0.9f, 0.3f, 1.0f});
		}
		else
		{
			tr->drawText("(service unavailable)", 14.0f,
				{px + 40.0f, py + 72.0f},
				sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
		}
	}

	/// @brief スコア表示エリアを描画する
	void drawScoreDisplay(
		sgc::IRenderer* r, sgc::ITextRenderer* tr, float sw) const
	{
		const float cx = sw * 0.5f;
		const float cy = 300.0f;

		// クリックエフェクト
		if (m_clickEffect > 0.0f)
		{
			const float effectR = 40.0f + m_clickEffect * 30.0f;
			r->drawCircleFrame({cx, cy}, effectR, 3.0f,
				sgc::Colorf{1.0f, 0.8f, 0.2f, m_clickEffect * 0.6f});
		}

		// スコア表示（中央）
		if (m_locator->has<IScoreService>())
		{
			const int score = m_locator->get<IScoreService>().getScore();
			tr->drawTextCentered(std::to_string(score), 48.0f,
				{cx, cy}, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});
			tr->drawTextCentered("Click to add score!", 16.0f,
				{cx, cy + 40.0f}, sgc::Colorf{0.6f, 0.8f, 0.6f, 1.0f});
		}
		else
		{
			tr->drawTextCentered("No Service", 36.0f,
				{cx, cy}, sgc::Colorf{0.5f, 0.3f, 0.3f, 1.0f});
			tr->drawTextCentered("Press [1] to register", 16.0f,
				{cx, cy + 40.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});
		}
	}

	/// @brief 操作ログを描画する
	void drawLog(sgc::ITextRenderer* tr, float sh) const
	{
		tr->drawText("Log:", 14.0f,
			{10.0f, sh - 180.0f},
			sgc::Colorf{0.6f, 0.6f, 0.8f, 1.0f});

		for (std::size_t i = 0; i < m_log.size(); ++i)
		{
			const float alpha = 0.4f + 0.6f
				* (static_cast<float>(i) / static_cast<float>(m_log.size()));
			tr->drawText(m_log[i], 12.0f,
				{20.0f, sh - 160.0f + static_cast<float>(i) * 18.0f},
				sgc::Colorf{0.7f, 0.7f, 0.9f, alpha});
		}
	}
};
