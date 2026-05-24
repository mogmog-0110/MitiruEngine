#pragma once

/// @file SampleMessageChannel.hpp
/// @brief MessageBuffer/Dispatcherのメッセージフロー可視化デモ
///
/// メッセージの作成・送信・ディスパッチの流れを可視化する。
/// - 1: Moveメッセージ送信 (type=1)
/// - 2: Chatメッセージ送信 (type=2)
/// - 3: Statusメッセージ送信 (type=3)
/// - R: ログクリア
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/net/MessageChannel.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief MessageChannel可視化サンプルシーン
///
/// MessageBuffer, MessageReader, MessageDispatcherを使った
/// メッセージのシリアライズ・ディスパッチの流れを可視化する。
class SampleMessageChannel : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief 受信ログエントリ
	struct LogEntry
	{
		std::uint32_t type;    ///< メッセージ種別
		std::string detail;    ///< 受信内容
		float age{0.0f};       ///< 経過時間
	};

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_log.clear();
		m_sendCount = 0;

		// ディスパッチャにハンドラを登録
		m_dispatcher.clear();

		m_dispatcher.registerHandler(MSG_MOVE,
			[this](const sgc::MessageHeader& h, sgc::MessageReader& reader)
			{
				auto xResult = reader.read<float>();
				auto yResult = reader.read<float>();
				if (xResult && yResult)
				{
					m_log.push_back(LogEntry{
						h.type,
						"Move to ("
							+ std::to_string(static_cast<int>(xResult.value()))
							+ ", "
							+ std::to_string(static_cast<int>(yResult.value()))
							+ ")",
						0.0f
					});
				}
			});

		m_dispatcher.registerHandler(MSG_CHAT,
			[this](const sgc::MessageHeader& h, sgc::MessageReader& reader)
			{
				auto idResult = reader.read<std::uint32_t>();
				auto lenResult = reader.read<std::uint32_t>();
				if (idResult && lenResult)
				{
					m_log.push_back(LogEntry{
						h.type,
						"Chat from user " + std::to_string(idResult.value())
							+ " (len=" + std::to_string(lenResult.value()) + ")",
						0.0f
					});
				}
			});

		m_dispatcher.registerHandler(MSG_STATUS,
			[this](const sgc::MessageHeader& h, sgc::MessageReader& reader)
			{
				auto codeResult = reader.read<std::uint32_t>();
				if (codeResult)
				{
					m_log.push_back(LogEntry{
						h.type,
						"Status code: " + std::to_string(codeResult.value()),
						0.0f
					});
				}
			});
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rでログクリア
		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_log.clear();
			m_sendCount = 0;
			return;
		}

		// 1: Moveメッセージ送信
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			sendMoveMessage();
		}

		// 2: Chatメッセージ送信
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			sendChatMessage();
		}

		// 3: Statusメッセージ送信
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			sendStatusMessage();
		}

		// マウスクリックでMoveメッセージ（マウス座標を送信）
		if (input->isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT))
		{
			const auto pos = input->mousePosition();
			sendMoveMessageAt(pos.x, pos.y);
		}

		// ログエントリの経過時間を更新
		for (auto& entry : m_log)
		{
			entry.age += dt;
		}

		// 古いエントリを削除
		while (m_log.size() > MAX_LOG_ENTRIES)
		{
			m_log.erase(m_log.begin());
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* renderer = getData().renderer;
		auto* text = getData().textRenderer;
		const float sw = getData().screenWidth;
		const float sh = getData().screenHeight;

		// 背景
		renderer->clearBackground(sgc::Colorf{0.05f, 0.05f, 0.1f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"MessageChannel Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.6f, 0.4f, 1.0f, 1.0f});

		// ── フローパネル（送信→ディスパッチ→ハンドラ）──
		const float panelW = 200.0f;
		const float dispX = sw * 0.5f - panelW * 0.5f;
		drawFlowPanels(renderer, text, 15.0f, dispX, sw - panelW - 15.0f, 80.0f, panelW);

		// フロー矢印
		const float arrowY = 140.0f;
		renderer->drawLine(sgc::Vec2f{panelW + 25.0f, arrowY},
			sgc::Vec2f{dispX - 10.0f, arrowY}, 2.0f, sgc::Colorf{0.5f, 0.5f, 0.8f, 0.7f});
		renderer->drawLine(sgc::Vec2f{dispX + panelW + 10.0f, arrowY},
			sgc::Vec2f{sw - panelW - 25.0f, arrowY}, 2.0f, sgc::Colorf{0.5f, 0.8f, 0.5f, 0.7f});

		// ── 受信ログ ──
		const float logY = 300.0f;
		renderer->drawRect(
			sgc::AABB2f{{10.0f, logY}, {sw - 10.0f, sh - 40.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.5f});
		renderer->drawRectFrame(
			sgc::AABB2f{{10.0f, logY}, {sw - 10.0f, sh - 40.0f}},
			1.0f, sgc::Colorf{0.3f, 0.3f, 0.5f, 0.6f});

		text->drawText("Dispatch Log:", 18.0f,
			sgc::Vec2f{20.0f, logY + 6.0f},
			sgc::Colorf{0.8f, 0.8f, 0.9f, 1.0f});

		const float lineH = 22.0f;
		const int maxVisible = static_cast<int>((sh - 40.0f - logY - 35.0f) / lineH);
		const int total = static_cast<int>(m_log.size());
		const int start = (total > maxVisible) ? total - maxVisible : 0;

		for (int i = start; i < total; ++i)
		{
			const auto& entry = m_log[i];
			const float ly = logY + 32.0f + static_cast<float>(i - start) * lineH;
			const auto color = messageColor(entry.type);
			const float alpha = (entry.age < 1.0f) ? 1.0f : 0.7f;

			text->drawText(
				"[" + messageName(entry.type) + "] " + entry.detail,
				14.0f,
				sgc::Vec2f{30.0f, ly},
				color.withAlpha(alpha));
		}

		// 操作説明
		text->drawText(
			"[1] Move  [2] Chat  [3] Status  [Click] Move(pos)  [R] Clear  [Esc] Back",
			14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr std::uint32_t MSG_MOVE = 1;      ///< 移動メッセージ型
	static constexpr std::uint32_t MSG_CHAT = 2;       ///< チャットメッセージ型
	static constexpr std::uint32_t MSG_STATUS = 3;     ///< ステータスメッセージ型
	static constexpr std::size_t MAX_LOG_ENTRIES = 20;  ///< 最大ログ行数

	sgc::MessageDispatcher m_dispatcher;  ///< メッセージディスパッチャ
	std::vector<LogEntry> m_log;           ///< 受信ログ
	int m_sendCount{0};                    ///< 送信カウンタ

	/// @brief Moveメッセージを送信する（擬似座標）
	void sendMoveMessage()
	{
		++m_sendCount;
		const float x = static_cast<float>((m_sendCount * 73) % 800);
		const float y = static_cast<float>((m_sendCount * 37) % 600);
		sendMoveMessageAt(x, y);
	}

	/// @brief 指定座標でMoveメッセージを送信する
	void sendMoveMessageAt(float x, float y)
	{
		sgc::MessageBuffer buf;
		buf.write<float>(x);
		buf.write<float>(y);

		sgc::MessageHeader header{MSG_MOVE,
			static_cast<std::uint32_t>(buf.size())};
		m_dispatcher.dispatch(header, buf);
	}

	/// @brief Chatメッセージを送信する
	void sendChatMessage()
	{
		++m_sendCount;
		sgc::MessageBuffer buf;
		const auto userId = static_cast<std::uint32_t>(m_sendCount % 5 + 1);
		const auto msgLen = static_cast<std::uint32_t>(10 + m_sendCount % 50);
		buf.write<std::uint32_t>(userId);
		buf.write<std::uint32_t>(msgLen);

		sgc::MessageHeader header{MSG_CHAT,
			static_cast<std::uint32_t>(buf.size())};
		m_dispatcher.dispatch(header, buf);
	}

	/// @brief Statusメッセージを送信する
	void sendStatusMessage()
	{
		++m_sendCount;
		sgc::MessageBuffer buf;
		const auto code = static_cast<std::uint32_t>(200 + (m_sendCount % 4) * 100);
		buf.write<std::uint32_t>(code);

		sgc::MessageHeader header{MSG_STATUS,
			static_cast<std::uint32_t>(buf.size())};
		m_dispatcher.dispatch(header, buf);
	}

	/// @brief メッセージ種別に対応する色を返す
	[[nodiscard]] static sgc::Colorf messageColor(std::uint32_t type) noexcept
	{
		switch (type)
		{
		case MSG_MOVE:   return sgc::Colorf{0.3f, 0.8f, 1.0f, 1.0f};
		case MSG_CHAT:   return sgc::Colorf{0.4f, 1.0f, 0.4f, 1.0f};
		case MSG_STATUS: return sgc::Colorf{1.0f, 0.8f, 0.3f, 1.0f};
		default:         return sgc::Colorf::white();
		}
	}

	/// @brief メッセージ種別名を返す
	[[nodiscard]] static std::string messageName(std::uint32_t type)
	{
		switch (type)
		{
		case MSG_MOVE:   return "Move";
		case MSG_CHAT:   return "Chat";
		case MSG_STATUS: return "Status";
		default:         return "Unknown";
		}
	}

	/// @brief パネルの枠と題名を描画する
	static void drawPanel(sgc::IRenderer* r, sgc::ITextRenderer* t,
		float x, float y, float w, float h,
		const std::string& title, const sgc::Colorf& bg, const sgc::Colorf& accent)
	{
		r->drawRect(sgc::AABB2f{{x, y}, {x + w, y + h}}, bg);
		r->drawRectFrame(sgc::AABB2f{{x, y}, {x + w, y + h}}, 2.0f, accent);
		t->drawText(title, 20.0f, sgc::Vec2f{x + 10.0f, y + 8.0f}, accent);
	}

	/// @brief 3つのフローパネルを描画する
	void drawFlowPanels(sgc::IRenderer* r, sgc::ITextRenderer* t,
		float senderX, float dispX, float handlerX, float y, float w) const
	{
		const float h = 120.0f;
		drawPanel(r, t, senderX, y, w, h, "Sender",
			sgc::Colorf{0.05f, 0.05f, 0.15f, 0.8f}, sgc::Colorf{0.4f, 0.4f, 0.8f, 0.7f});
		t->drawText("MessageBuffer", 14.0f, sgc::Vec2f{senderX + 10.0f, y + 36.0f},
			sgc::Colorf{0.6f, 0.6f, 0.7f, 1.0f});

		drawPanel(r, t, dispX, y, w, h, "Dispatcher",
			sgc::Colorf{0.1f, 0.05f, 0.15f, 0.8f}, sgc::Colorf{0.7f, 0.4f, 0.9f, 0.7f});
		t->drawText("Handlers: " + std::to_string(m_dispatcher.handlerCount()), 14.0f,
			sgc::Vec2f{dispX + 10.0f, y + 36.0f}, sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		drawPanel(r, t, handlerX, y, w, h, "Handlers",
			sgc::Colorf{0.05f, 0.1f, 0.05f, 0.8f}, sgc::Colorf{0.4f, 0.8f, 0.4f, 0.7f});
		t->drawText("Move / Chat / Status", 14.0f,
			sgc::Vec2f{handlerX + 10.0f, y + 36.0f}, sgc::Colorf{0.6f, 0.8f, 0.6f, 1.0f});
	}
};
