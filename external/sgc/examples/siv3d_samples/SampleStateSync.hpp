#pragma once

/// @file SampleStateSync.hpp
/// @brief StateSyncManagerバージョン同期デモ
///
/// HostとClientの2ノードを並べて表示し、状態のバージョン管理と
/// 差分同期の仕組みを可視化する。
/// - 1/2/3: Hostのスロット1/2/3を更新
/// - Space: HostからClientへ差分同期
/// - R: リセット
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/net/StateSync.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief StateSync可視化サンプルシーン
///
/// 2つのStateSyncManagerをHost/Clientとして表示し、
/// バージョンベースの差分同期を可視化する。
class SampleStateSync : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		resetState();
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float /*dt*/) override
	{
		const auto* input = getData().inputProvider;

		// ESCでメニューに戻る
		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		// Rでリセット
		if (input->isKeyJustPressed(KeyCode::R))
		{
			resetState();
			return;
		}

		// 1/2/3: Hostのスロットを更新
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			updateHostSlot(1);
		}
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			updateHostSlot(2);
		}
		if (input->isKeyJustPressed(KeyCode::NUM3))
		{
			updateHostSlot(3);
		}

		// Space: 差分同期を実行
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			syncHostToClient();
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
		renderer->clearBackground(sgc::Colorf{0.06f, 0.06f, 0.1f, 1.0f});

		// タイトル
		text->drawTextCentered(
			"StateSync Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.4f, 0.9f, 0.6f, 1.0f});

		// 区切り線
		renderer->drawLine(
			sgc::Vec2f{sw * 0.5f, 70.0f},
			sgc::Vec2f{sw * 0.5f, sh - 40.0f},
			2.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.5f});

		// ── 左側: Host ──
		drawNodePanel(renderer, text,
			"HOST", 20.0f, 70.0f, sw * 0.5f - 40.0f,
			m_host, m_hostValues,
			sgc::Colorf{0.3f, 0.8f, 1.0f, 1.0f});

		// ── 右側: Client ──
		drawNodePanel(renderer, text,
			"CLIENT", sw * 0.5f + 20.0f, 70.0f, sw * 0.5f - 40.0f,
			m_client, m_clientValues,
			sgc::Colorf{1.0f, 0.7f, 0.3f, 1.0f});

		// 同期矢印（中央）
		const float arrowY = 250.0f;
		renderer->drawLine(
			sgc::Vec2f{sw * 0.5f - 30.0f, arrowY},
			sgc::Vec2f{sw * 0.5f + 30.0f, arrowY},
			3.0f, sgc::Colorf{0.5f, 1.0f, 0.5f, 0.8f});
		renderer->drawTriangle(
			sgc::Vec2f{sw * 0.5f + 30.0f, arrowY},
			sgc::Vec2f{sw * 0.5f + 20.0f, arrowY - 8.0f},
			sgc::Vec2f{sw * 0.5f + 20.0f, arrowY + 8.0f},
			sgc::Colorf{0.5f, 1.0f, 0.5f, 0.8f});
		text->drawTextCentered(
			"Sync", 14.0f,
			sgc::Vec2f{sw * 0.5f, arrowY - 16.0f},
			sgc::Colorf{0.5f, 1.0f, 0.5f, 0.8f});

		// 同期ログ
		const float logY = sh - 180.0f;
		renderer->drawRect(
			sgc::AABB2f{{10.0f, logY}, {sw - 10.0f, sh - 40.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.5f});
		text->drawText("Sync Log:", 16.0f,
			sgc::Vec2f{20.0f, logY + 6.0f},
			sgc::Colorf{0.8f, 0.8f, 0.8f, 1.0f});

		const int maxLines = 5;
		const int startIdx = (static_cast<int>(m_syncLog.size()) > maxLines)
			? static_cast<int>(m_syncLog.size()) - maxLines : 0;
		for (int i = startIdx; i < static_cast<int>(m_syncLog.size()); ++i)
		{
			const float lineY = logY + 28.0f
				+ static_cast<float>(i - startIdx) * 20.0f;
			text->drawText(m_syncLog[i], 14.0f,
				sgc::Vec2f{30.0f, lineY},
				sgc::Colorf{0.6f, 0.9f, 0.6f, 1.0f});
		}

		// 操作説明
		text->drawText(
			"[1/2/3] Update Host Slot  [Space] Sync  [R] Reset  [Esc] Back",
			14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr int SLOT_COUNT = 3;  ///< スロット数

	sgc::StateSyncManager m_host;        ///< ホスト側マネージャ
	sgc::StateSyncManager m_client;      ///< クライアント側マネージャ
	int m_hostValues[SLOT_COUNT]{};      ///< ホスト側の論理値
	int m_clientValues[SLOT_COUNT]{};    ///< クライアント側の論理値
	int m_updateCounter{0};              ///< 更新カウンタ
	std::vector<std::string> m_syncLog;  ///< 同期ログ

	/// @brief 状態をリセットする
	void resetState()
	{
		m_host.clear();
		m_client.clear();
		for (int i = 0; i < SLOT_COUNT; ++i)
		{
			m_hostValues[i] = 0;
			m_clientValues[i] = 0;
		}
		m_updateCounter = 0;
		m_syncLog.clear();
		m_syncLog.push_back("System initialized.");
	}

	/// @brief ホスト側のスロットを更新する
	/// @param slotId スロットID (1-3)
	void updateHostSlot(int slotId)
	{
		const auto id = static_cast<std::uint32_t>(slotId);
		++m_updateCounter;
		const int newValue = m_updateCounter * 10 + slotId;
		m_hostValues[slotId - 1] = newValue;

		// int値をバイト列に変換
		std::vector<std::byte> data(sizeof(int));
		std::memcpy(data.data(), &newValue, sizeof(int));
		m_host.updateSlot(id, std::move(data));

		m_syncLog.push_back(
			"Host: Slot " + std::to_string(slotId)
			+ " -> " + std::to_string(newValue));
	}

	/// @brief HostからClientへ差分同期する
	void syncHostToClient()
	{
		const auto clientVersions = m_client.versionMap();
		const auto changed = m_host.getChangedSlots(clientVersions);

		if (changed.empty())
		{
			m_syncLog.push_back("Sync: No changes to sync.");
			return;
		}

		for (const auto& slot : changed)
		{
			m_client.applyRemoteSlot(slot);

			// クライアント側の表示値を更新
			if (slot.data.size() >= sizeof(int) && slot.id >= 1
				&& slot.id <= static_cast<std::uint32_t>(SLOT_COUNT))
			{
				int value = 0;
				std::memcpy(&value, slot.data.data(), sizeof(int));
				m_clientValues[slot.id - 1] = value;
			}
		}

		m_syncLog.push_back(
			"Sync: " + std::to_string(changed.size()) + " slot(s) synced.");
	}

	/// @brief ノードパネルを描画する
	static void drawNodePanel(
		sgc::IRenderer* renderer, sgc::ITextRenderer* text,
		const std::string& name, float x, float y, float w,
		const sgc::StateSyncManager& mgr, const int values[],
		const sgc::Colorf& accentColor)
	{
		// パネル背景
		renderer->drawRect(
			sgc::AABB2f{{x, y}, {x + w, y + 280.0f}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.4f});
		renderer->drawRectFrame(
			sgc::AABB2f{{x, y}, {x + w, y + 280.0f}},
			2.0f, accentColor.withAlpha(0.6f));

		// ノード名
		text->drawText(name, 24.0f,
			sgc::Vec2f{x + 10.0f, y + 8.0f}, accentColor);

		text->drawText(
			"Slots: " + std::to_string(mgr.slotCount()), 16.0f,
			sgc::Vec2f{x + 10.0f, y + 40.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// バージョンマップを取得
		const auto versions = mgr.versionMap();

		// スロット表示
		for (int i = 0; i < SLOT_COUNT; ++i)
		{
			const auto slotId = static_cast<std::uint32_t>(i + 1);
			const float slotY = y + 70.0f + static_cast<float>(i) * 65.0f;

			// スロット枠
			renderer->drawRect(
				sgc::AABB2f{{x + 10.0f, slotY}, {x + w - 10.0f, slotY + 55.0f}},
				sgc::Colorf{0.08f, 0.08f, 0.12f, 0.8f});
			renderer->drawRectFrame(
				sgc::AABB2f{{x + 10.0f, slotY}, {x + w - 10.0f, slotY + 55.0f}},
				1.0f, accentColor.withAlpha(0.3f));

			// スロットID
			text->drawText(
				"Slot " + std::to_string(i + 1), 18.0f,
				sgc::Vec2f{x + 20.0f, slotY + 4.0f},
				sgc::Colorf::white());

			// バージョン番号
			auto vIt = versions.find(slotId);
			const std::uint64_t ver = (vIt != versions.end()) ? vIt->second : 0;
			text->drawText(
				"v" + std::to_string(ver), 14.0f,
				sgc::Vec2f{x + w - 60.0f, slotY + 6.0f},
				sgc::Colorf{0.9f, 0.9f, 0.4f, 1.0f});

			// 値
			text->drawText(
				"Value: " + std::to_string(values[i]), 16.0f,
				sgc::Vec2f{x + 20.0f, slotY + 28.0f},
				sgc::Colorf{0.6f, 0.8f, 0.6f, 1.0f});
		}
	}
};
