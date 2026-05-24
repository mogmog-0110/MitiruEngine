#pragma once

/// @file SampleObserver.hpp
/// @brief Signal/Slot オブザーバーパターン デモシーン
///
/// 3つのシグナル(OnDamage, OnHeal, OnLevelUp)を視覚化する。
/// ボタンクリックでシグナルを発火し、接続されたスロットがフラッシュする。
/// 接続/切断のデモも含む。
/// - ESC: メニューに戻る

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/patterns/Observer.hpp"
#include "sgc/scene/App.hpp"
#include "sgc/ui/Button.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief Signal/Slot オブザーバーパターン サンプル
///
/// 3つのシグナルに複数のスロットを接続し、
/// ボタン操作でemit・connect・disconnectを実演する。
class SampleObserver : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_hp = 100;
		m_level = 1;
		m_log.clear();

		// スロットフラッシュタイマーをリセット
		for (auto& t : m_flashTimers) { t = 0.0f; }

		// 全切断してから再接続
		m_onDamage.disconnectAll();
		m_onHeal.disconnectAll();
		m_onLevelUp.disconnectAll();

		// OnDamage スロット2つ
		m_onDamage.connect([this](int dmg)
		{
			m_hp = std::max(0, m_hp - dmg);
			m_flashTimers[0] = FLASH_DURATION;
			addLog("Damage: -" + std::to_string(dmg) + " HP");
		});
		m_onDamage.connect([this](int /*dmg*/)
		{
			m_flashTimers[1] = FLASH_DURATION;
		});

		// OnHeal スロット2つ
		m_onHeal.connect([this](int heal)
		{
			m_hp = std::min(MAX_HP, m_hp + heal);
			m_flashTimers[2] = FLASH_DURATION;
			addLog("Heal: +" + std::to_string(heal) + " HP");
		});
		m_onHeal.connect([this](int /*heal*/)
		{
			m_flashTimers[3] = FLASH_DURATION;
		});

		// OnLevelUp スロット2つ
		m_onLevelUp.connect([this]()
		{
			++m_level;
			m_flashTimers[4] = FLASH_DURATION;
			addLog("Level Up! -> Lv." + std::to_string(m_level));
		});
		m_extraLevelUpId = m_onLevelUp.connect([this]()
		{
			m_flashTimers[5] = FLASH_DURATION;
		});
		m_extraConnected = true;
	}

	/// @brief 更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		const auto mousePos = input->mousePosition();
		const bool mouseDown = input->isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT);
		const bool mousePressed = input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		// ── ボタン評価 ──
		m_btnDamage = sgc::ui::evaluateButton(
			buttonRect(0), mousePos, mouseDown, mousePressed);
		if (m_btnDamage.clicked) { m_onDamage.emit(15); }

		m_btnHeal = sgc::ui::evaluateButton(
			buttonRect(1), mousePos, mouseDown, mousePressed);
		if (m_btnHeal.clicked) { m_onHeal.emit(10); }

		m_btnLevelUp = sgc::ui::evaluateButton(
			buttonRect(2), mousePos, mouseDown, mousePressed);
		if (m_btnLevelUp.clicked) { m_onLevelUp.emit(); }

		// 接続/切断トグルボタン
		m_btnToggle = sgc::ui::evaluateButton(
			toggleButtonRect(), mousePos, mouseDown, mousePressed);
		if (m_btnToggle.clicked)
		{
			if (m_extraConnected)
			{
				m_onLevelUp.disconnect(m_extraLevelUpId);
				m_extraConnected = false;
				addLog("Disconnected extra LevelUp slot");
			}
			else
			{
				m_extraLevelUpId = m_onLevelUp.connect([this]()
				{
					m_flashTimers[5] = FLASH_DURATION;
				});
				m_extraConnected = true;
				addLog("Reconnected extra LevelUp slot");
			}
		}

		// フラッシュタイマー更新
		for (auto& t : m_flashTimers)
		{
			if (t > 0.0f) { t -= dt; }
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f});

		text->drawTextCentered(
			"Observer Pattern - Signal / Slot", 28.0f,
			sgc::Vec2f{sw * 0.5f, 25.0f}, sgc::Colorf{0.9f, 0.9f, 1.0f});

		// ── ステータス表示 ──
		const std::string hpStr = "HP: " + std::to_string(m_hp) + "/" + std::to_string(MAX_HP);
		text->drawText(hpStr, 22.0f,
			sgc::Vec2f{30.0f, 60.0f}, sgc::Colorf{0.9f, 0.3f, 0.3f});

		// HPバー
		const sgc::AABB2f hpBg{{30.0f, 90.0f}, {230.0f, 108.0f}};
		renderer->drawRect(hpBg, sgc::Colorf{0.15f, 0.15f, 0.2f});
		const float hpRatio = static_cast<float>(m_hp) / static_cast<float>(MAX_HP);
		if (hpRatio > 0.0f)
		{
			const sgc::AABB2f hpFill{{30.0f, 90.0f}, {30.0f + 200.0f * hpRatio, 108.0f}};
			const auto hpColor = (hpRatio > 0.5f)
				? sgc::Colorf{0.3f, 0.8f, 0.3f}
				: sgc::Colorf{0.9f, 0.4f, 0.2f};
			renderer->drawRect(hpFill, hpColor);
		}
		renderer->drawRectFrame(hpBg, 1.0f, sgc::Colorf{0.4f, 0.4f, 0.5f});

		const std::string lvStr = "Level: " + std::to_string(m_level);
		text->drawText(lvStr, 22.0f,
			sgc::Vec2f{260.0f, 60.0f}, sgc::Colorf{0.3f, 0.7f, 1.0f});

		// ── Emitボタン ──
		drawEmitButton(0, "Emit Damage", m_btnDamage.state,
			sgc::Colorf{0.9f, 0.3f, 0.3f});
		drawEmitButton(1, "Emit Heal", m_btnHeal.state,
			sgc::Colorf{0.3f, 0.9f, 0.3f});
		drawEmitButton(2, "Emit LevelUp", m_btnLevelUp.state,
			sgc::Colorf{0.3f, 0.5f, 1.0f});

		// ── 接続/切断トグルボタン ──
		const auto tbRect = toggleButtonRect();
		const sgc::AABB2f tbAABB = tbRect.toAABB2();
		const auto tbColor = m_extraConnected
			? sgc::Colorf{0.8f, 0.3f, 0.3f}
			: sgc::Colorf{0.3f, 0.8f, 0.3f};
		renderer->drawRect(tbAABB, tbColor.withAlpha(0.3f));
		renderer->drawRectFrame(tbAABB, 2.0f, tbColor);
		const std::string tbLabel = m_extraConnected ? "Disconnect Slot" : "Reconnect Slot";
		text->drawTextCentered(tbLabel, 14.0f,
			sgc::Vec2f{tbRect.x() + tbRect.width() * 0.5f,
			           tbRect.y() + tbRect.height() * 0.5f},
			sgc::Colorf::white());

		// ── スロット可視化 ──
		text->drawText("Connected Slots:", 16.0f,
			sgc::Vec2f{SLOT_X, 120.0f}, sgc::Colorf{0.7f, 0.7f, 0.8f});

		drawSlot(0, "Damage -> HP Update", sgc::Colorf{0.9f, 0.3f, 0.3f});
		drawSlot(1, "Damage -> VFX", sgc::Colorf{0.9f, 0.5f, 0.3f});
		drawSlot(2, "Heal -> HP Update", sgc::Colorf{0.3f, 0.9f, 0.3f});
		drawSlot(3, "Heal -> VFX", sgc::Colorf{0.4f, 0.9f, 0.5f});
		drawSlot(4, "LevelUp -> Stats", sgc::Colorf{0.3f, 0.5f, 1.0f});
		drawSlot(5, m_extraConnected ? "LevelUp -> VFX" : "(Disconnected)",
			m_extraConnected ? sgc::Colorf{0.5f, 0.5f, 1.0f}
			                 : sgc::Colorf{0.3f, 0.3f, 0.3f});

		// ── 接続カウント ──
		const std::string countStr =
			"Connections: Damage=" + std::to_string(m_onDamage.connectionCount()) +
			"  Heal=" + std::to_string(m_onHeal.connectionCount()) +
			"  LevelUp=" + std::to_string(m_onLevelUp.connectionCount());
		text->drawText(countStr, 12.0f,
			sgc::Vec2f{SLOT_X, SLOT_START_Y + 6.0f * SLOT_GAP + 10.0f},
			sgc::Colorf{0.5f, 0.5f, 0.6f});

		// ── イベントログ ──
		text->drawText("Event Log:", 16.0f, sgc::Vec2f{30.0f, sh - 140.0f}, sgc::Colorf{0.7f, 0.7f, 0.8f});
		const float logY = sh - 115.0f;
		const int logCount = static_cast<int>(m_log.size());
		const int startIdx = std::max(0, logCount - MAX_LOG_LINES);
		for (int i = startIdx; i < logCount; ++i)
		{
			const float y = logY + static_cast<float>(i - startIdx) * 20.0f;
			text->drawText(m_log[static_cast<std::size_t>(i)], 12.0f,
				sgc::Vec2f{40.0f, y}, sgc::Colorf{0.6f, 0.6f, 0.65f});
		}

		text->drawText("ESC: Back to Menu", 14.0f,
			sgc::Vec2f{sw - 200.0f, sh - 20.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f});
	}

private:
	static constexpr int MAX_HP = 100;
	static constexpr float FLASH_DURATION = 0.5f;
	static constexpr int MAX_LOG_LINES = 5;
	static constexpr float BTN_X = 30.0f;
	static constexpr float BTN_Y_START = 130.0f;
	static constexpr float BTN_W = 160.0f;
	static constexpr float BTN_H = 36.0f;
	static constexpr float BTN_GAP = 46.0f;
	static constexpr float SLOT_X = 420.0f;
	static constexpr float SLOT_START_Y = 145.0f;
	static constexpr float SLOT_GAP = 34.0f;
	static constexpr float SLOT_W = 200.0f;
	static constexpr float SLOT_H = 26.0f;

	sgc::Signal<int> m_onDamage;
	sgc::Signal<int> m_onHeal;
	sgc::Signal<> m_onLevelUp;

	sgc::ConnectionId m_extraLevelUpId{0};
	bool m_extraConnected{true};

	int m_hp{100};
	int m_level{1};
	std::vector<std::string> m_log;

	std::array<float, 6> m_flashTimers{};

	sgc::ui::ButtonResult m_btnDamage{};
	sgc::ui::ButtonResult m_btnHeal{};
	sgc::ui::ButtonResult m_btnLevelUp{};
	sgc::ui::ButtonResult m_btnToggle{};

	// ── レイアウト計算 ──

	/// @brief Emitボタン矩形
	[[nodiscard]] static sgc::Rectf buttonRect(int index) noexcept
	{
		const float y = BTN_Y_START + static_cast<float>(index) * BTN_GAP;
		return {BTN_X, y, BTN_W, BTN_H};
	}

	/// @brief 接続/切断トグルボタン矩形
	[[nodiscard]] static sgc::Rectf toggleButtonRect() noexcept
	{
		return {BTN_X, BTN_Y_START + 3.0f * BTN_GAP + 10.0f, BTN_W, BTN_H};
	}

	/// @brief ログにメッセージを追加する
	void addLog(const std::string& msg)
	{
		m_log.push_back(msg);
		if (m_log.size() > 20) { m_log.erase(m_log.begin()); }
	}

	/// @brief Emitボタンを描画する（状態に応じた色変化付き）
	void drawEmitButton(int index, const char* label,
		sgc::ui::WidgetState state, const sgc::Colorf& accent) const
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const auto rect = buttonRect(index);
		const sgc::AABB2f aabb = rect.toAABB2();

		sgc::Colorf bg = accent.withAlpha(0.2f);
		if (state == sgc::ui::WidgetState::Hovered) { bg = accent.withAlpha(0.35f); }
		if (state == sgc::ui::WidgetState::Pressed) { bg = accent.withAlpha(0.5f); }

		renderer->drawRect(aabb, bg);
		renderer->drawRectFrame(aabb, 2.0f, accent);
		text->drawTextCentered(label, 14.0f,
			sgc::Vec2f{rect.x() + rect.width() * 0.5f, rect.y() + rect.height() * 0.5f},
			sgc::Colorf::white());
	}

	/// @brief スロットを描画する（フラッシュエフェクト付き）
	void drawSlot(int index, const char* label, const sgc::Colorf& color) const
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;

		const float y = SLOT_START_Y + static_cast<float>(index) * SLOT_GAP;
		const sgc::AABB2f slotRect{{SLOT_X, y}, {SLOT_X + SLOT_W, y + SLOT_H}};

		// フラッシュ中は明るく
		const float flash = m_flashTimers[static_cast<std::size_t>(index)];
		const float flashAlpha = (flash > 0.0f) ? (flash / FLASH_DURATION) : 0.0f;

		const sgc::Colorf bg = color.withAlpha(0.1f + flashAlpha * 0.5f);
		renderer->drawRect(slotRect, bg);
		renderer->drawRectFrame(slotRect, (flash > 0.0f) ? 2.0f : 1.0f,
			color.withAlpha(0.5f + flashAlpha * 0.5f));

		// フラッシュインジケーター
		if (flash > 0.0f)
		{
			renderer->drawCircle(
				sgc::Vec2f{SLOT_X + SLOT_W - 14.0f, y + SLOT_H * 0.5f},
				5.0f, color.withAlpha(flashAlpha));
		}

		text->drawText(label, 12.0f,
			sgc::Vec2f{SLOT_X + 8.0f, y + 6.0f},
			color.withAlpha(0.6f + flashAlpha * 0.4f));
	}
};
