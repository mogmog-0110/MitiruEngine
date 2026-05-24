#pragma once

/// @file SampleEventSystem.hpp
/// @brief EventDispatcher（イベントバス）ビジュアルサンプル
///
/// Publish-Subscribeパターンをリアルタイム可視化する。
/// 左側のエミッターをクリックするとイベントが発行され、
/// 右側のリスナーがフラッシュで反応する。
/// - クリック: イベントを発行
/// - Rキー: リセット
/// - ESCキー: メニューに戻る

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "sgc/core/Hash.hpp"
#include "sgc/patterns/EventDispatcher.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

namespace sample_event
{

/// @brief ダメージイベント
struct DamageEvent
{
	int amount{0};
};

/// @brief ヒールイベント
struct HealEvent
{
	int amount{0};
};

/// @brief スコアイベント
struct ScoreEvent
{
	int points{0};
};

} // namespace sample_event

/// @brief EventDispatcherサンプルシーン
///
/// 3種類のイベント（Damage, Heal, Score）を発行し、
/// 4つのリスナーがそれぞれ異なるイベントに反応する様子を可視化する。
class SampleEventSystem : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_dispatcher.clearAll();
		m_eventCount = 0;
		resetListeners();

		// リスナー登録
		m_dispatcher.on<sample_event::DamageEvent>(
			[this](const sample_event::DamageEvent& e)
			{
				flashListener(0, e.amount);
				flashListener(3, e.amount);  ///< "All" リスナー
			});

		m_dispatcher.on<sample_event::HealEvent>(
			[this](const sample_event::HealEvent& e)
			{
				flashListener(1, e.amount);
				flashListener(3, e.amount);  ///< "All" リスナー
			});

		m_dispatcher.on<sample_event::ScoreEvent>(
			[this](const sample_event::ScoreEvent& e)
			{
				flashListener(2, e.points);
				flashListener(3, e.points);  ///< "All" リスナー
			});
	}

	/// @brief 毎フレームの更新処理
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
			m_eventCount = 0;
			resetListeners();
		}

		// フラッシュタイマー減衰
		for (auto& listener : m_listeners)
		{
			if (listener.flash > 0.0f)
			{
				listener.flash -= dt * 3.0f;
				if (listener.flash < 0.0f)
				{
					listener.flash = 0.0f;
				}
			}
		}

		// エミッターボタンのクリック判定
		const auto mousePos = input->mousePosition();
		const bool clicked = input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT);

		for (std::size_t i = 0; i < EMITTER_COUNT; ++i)
		{
			const auto rect = emitterRect(i);
			const auto aabb = rect.toAABB2();
			const bool hovered = (mousePos.x >= aabb.min.x && mousePos.x <= aabb.max.x
				&& mousePos.y >= aabb.min.y && mousePos.y <= aabb.max.y);

			m_emitterHover[i] = hovered;

			if (hovered && clicked)
			{
				emitEvent(i);
			}
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// タイトル
		tr->drawText(
			"Event Dispatcher - Pub/Sub", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{1.0f, 0.7f, 0.4f, 1.0f});

		tr->drawText(
			"Click emitters | R: Reset | ESC: Back", 14.0f,
			{10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		tr->drawText(
			("Events fired: " + std::to_string(m_eventCount)), 14.0f,
			{sw - 180.0f, 38.0f}, sgc::Colorf{0.6f, 0.8f, 0.6f, 1.0f});

		// エミッター（左側）
		tr->drawTextCentered("Emitters", 18.0f,
			{EMITTER_X + EMITTER_W * 0.5f, 80.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

		for (std::size_t i = 0; i < EMITTER_COUNT; ++i)
		{
			drawEmitter(i);
		}

		// 矢印（中央）
		const float arrowX = sw * 0.5f;
		for (int i = 0; i < 5; ++i)
		{
			const float ay = 130.0f + static_cast<float>(i) * 80.0f;
			r->drawLine(
				{arrowX - 30.0f, ay}, {arrowX + 30.0f, ay},
				1.5f, sgc::Colorf{0.4f, 0.4f, 0.5f, 0.5f});
			r->drawTriangle(
				{arrowX + 30.0f, ay},
				{arrowX + 22.0f, ay - 5.0f},
				{arrowX + 22.0f, ay + 5.0f},
				sgc::Colorf{0.4f, 0.4f, 0.5f, 0.5f});
		}

		// ラベル「emit()」
		tr->drawTextCentered("emit()", 12.0f,
			{arrowX, 100.0f}, sgc::Colorf{0.5f, 0.5f, 0.6f, 0.7f});

		// リスナー（右側）
		tr->drawTextCentered("Listeners", 18.0f,
			{LISTENER_X + LISTENER_SIZE * 0.5f, 80.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

		for (std::size_t i = 0; i < LISTENER_COUNT; ++i)
		{
			drawListener(i);
		}
	}

private:
	static constexpr std::size_t EMITTER_COUNT = 3;   ///< エミッター数
	static constexpr std::size_t LISTENER_COUNT = 4;   ///< リスナー数
	static constexpr float EMITTER_X = 60.0f;         ///< エミッター開始X
	static constexpr float EMITTER_W = 200.0f;        ///< エミッター幅
	static constexpr float EMITTER_H = 60.0f;         ///< エミッター高さ
	static constexpr float LISTENER_X = 550.0f;       ///< リスナー開始X
	static constexpr float LISTENER_SIZE = 80.0f;     ///< リスナーサイズ

	/// @brief エミッター情報
	static constexpr std::array<std::string_view, EMITTER_COUNT> EMITTER_NAMES =
	{
		"Damage", "Heal", "Score"
	};

	static constexpr std::array<sgc::Colorf, EMITTER_COUNT> EMITTER_COLORS =
	{{
		{1.0f, 0.3f, 0.3f, 1.0f},
		{0.3f, 1.0f, 0.5f, 1.0f},
		{0.3f, 0.5f, 1.0f, 1.0f}
	}};

	/// @brief リスナー情報
	static constexpr std::array<std::string_view, LISTENER_COUNT> LISTENER_NAMES =
	{
		"HP Bar", "Heal FX", "Score UI", "Logger"
	};

	/// @brief リスナー状態
	struct ListenerState
	{
		float flash{0.0f};       ///< フラッシュ強度
		int lastValue{0};        ///< 最後に受け取った値
	};

	sgc::EventDispatcher m_dispatcher;
	std::array<ListenerState, LISTENER_COUNT> m_listeners{};
	std::array<bool, EMITTER_COUNT> m_emitterHover{};
	std::size_t m_eventCount{0};

	/// @brief リスナーをフラッシュさせる
	void flashListener(std::size_t index, int value)
	{
		if (index < LISTENER_COUNT)
		{
			m_listeners[index].flash = 1.0f;
			m_listeners[index].lastValue = value;
		}
	}

	/// @brief リスナーの状態をリセットする
	void resetListeners()
	{
		for (auto& l : m_listeners)
		{
			l = {};
		}
	}

	/// @brief イベントを発行する
	void emitEvent(std::size_t emitterIndex)
	{
		++m_eventCount;
		switch (emitterIndex)
		{
		case 0:
			m_dispatcher.emit(sample_event::DamageEvent{25});
			break;
		case 1:
			m_dispatcher.emit(sample_event::HealEvent{15});
			break;
		case 2:
			m_dispatcher.emit(sample_event::ScoreEvent{100});
			break;
		default:
			break;
		}
	}

	/// @brief エミッター矩形を取得する
	[[nodiscard]] sgc::Rectf emitterRect(std::size_t index) const
	{
		const float y = 110.0f + static_cast<float>(index) * 90.0f;
		return sgc::Rectf{EMITTER_X, y, EMITTER_W, EMITTER_H};
	}

	/// @brief エミッターを描画する
	void drawEmitter(std::size_t index) const
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const auto rect = emitterRect(index);
		const auto aabb = rect.toAABB2();
		const auto color = EMITTER_COLORS[index];

		const sgc::Colorf bg = m_emitterHover[index]
			? color.withAlpha(0.35f)
			: color.withAlpha(0.15f);

		r->drawRect(aabb, bg);
		r->drawRectFrame(aabb, 2.0f, color.withAlpha(0.7f));

		tr->drawTextCentered(
			std::string{EMITTER_NAMES[index]}, 18.0f,
			rect.center(), color);
	}

	/// @brief リスナーを描画する
	void drawListener(std::size_t index) const
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float y = 110.0f + static_cast<float>(index) * 100.0f;
		const float cx = LISTENER_X + LISTENER_SIZE * 0.5f;
		const float cy = y + LISTENER_SIZE * 0.5f;

		const float flash = m_listeners[index].flash;
		const float radius = 35.0f + flash * 8.0f;

		const sgc::Colorf baseColor{0.3f, 0.4f, 0.6f, 1.0f};
		const sgc::Colorf flashColor{1.0f, 1.0f, 0.5f, 1.0f};
		const sgc::Colorf color = baseColor.lerp(flashColor, flash);

		r->drawCircle({cx, cy}, radius, color.withAlpha(0.3f + flash * 0.5f));
		r->drawCircleFrame({cx, cy}, radius, 2.0f, color);

		tr->drawTextCentered(
			std::string{LISTENER_NAMES[index]}, 12.0f,
			{cx, cy}, sgc::Colorf{0.9f, 0.9f, 0.9f, 1.0f});

		// 受信値表示
		if (flash > 0.1f)
		{
			tr->drawTextCentered(
				std::to_string(m_listeners[index].lastValue), 14.0f,
				{cx, cy + radius + 12.0f},
				sgc::Colorf{1.0f, 1.0f, 0.3f, flash});
		}
	}
};
