#pragma once

/// @file SampleMemory.hpp
/// @brief Arena/Poolアロケータのメモリ使用状況可視化デモ
///
/// ArenaAllocatorとPoolAllocatorの内部状態をリアルタイムで可視化する。
/// - Space: アリーナから割り当て
/// - 1: プールから割り当て
/// - 2: プールから解放（最後に割り当てたブロック）
/// - R: 両方リセット
/// - ESCキー: メニューに戻る

#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/memory/ArenaAllocator.hpp"
#include "sgc/memory/PoolAllocator.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief メモリアロケータ可視化サンプルシーン
///
/// ArenaAllocatorとPoolAllocatorのメモリ使用状況をバーグラフで表示する。
class SampleMemory : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		resetAllocators();
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
			resetAllocators();
			return;
		}

		// Space: アリーナから割り当て（ランダムサイズ）
		if (input->isKeyJustPressed(KeyCode::SPACE))
		{
			const std::size_t size = 32 + (m_allocCounter * 47 % 200);
			void* ptr = m_arena.allocate(size, 8);
			if (ptr)
			{
				m_arenaAllocLog.push_back(size);
			}
			++m_allocCounter;
		}

		// 1: プールから割り当て
		if (input->isKeyJustPressed(KeyCode::NUM1))
		{
			void* ptr = m_pool.allocate(POOL_BLOCK_SIZE);
			if (ptr)
			{
				m_poolAllocated.push_back(ptr);
			}
		}

		// 2: プールから解放（最後の1つ）
		if (input->isKeyJustPressed(KeyCode::NUM2))
		{
			if (!m_poolAllocated.empty())
			{
				m_pool.deallocate(m_poolAllocated.back());
				m_poolAllocated.pop_back();
			}
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
			"Memory Allocators Demo", 36.0f,
			sgc::Vec2f{sw * 0.5f, 30.0f},
			sgc::Colorf{0.9f, 0.7f, 0.3f, 1.0f});

		// ── アリーナアロケータ ──
		const float panelY = 80.0f;
		drawAllocatorPanel(
			renderer, text,
			"Arena Allocator", 20.0f, panelY,
			sw - 40.0f, 200.0f,
			sgc::Colorf{0.3f, 0.7f, 1.0f, 1.0f});

		// アリーナ使用率バー
		const float arenaBarX = 40.0f;
		const float arenaBarY = panelY + 40.0f;
		const float barW = sw - 80.0f;
		const float barH = 40.0f;

		drawMemoryBar(renderer, text,
			arenaBarX, arenaBarY, barW, barH,
			m_arena.used(), m_arena.capacity(),
			sgc::Colorf{0.2f, 0.6f, 1.0f, 0.8f});

		// アリーナ統計
		text->drawText(
			"Capacity: " + std::to_string(m_arena.capacity()) + " bytes", 16.0f,
			sgc::Vec2f{40.0f, arenaBarY + barH + 10.0f},
			sgc::Colorf::white());

		text->drawText(
			"Used: " + std::to_string(m_arena.used()) + " bytes", 16.0f,
			sgc::Vec2f{40.0f, arenaBarY + barH + 32.0f},
			sgc::Colorf{0.4f, 0.8f, 1.0f, 1.0f});

		text->drawText(
			"Remaining: " + std::to_string(m_arena.remaining()) + " bytes", 16.0f,
			sgc::Vec2f{40.0f, arenaBarY + barH + 54.0f},
			sgc::Colorf{0.4f, 1.0f, 0.4f, 1.0f});

		text->drawText(
			"Allocations: " + std::to_string(m_arenaAllocLog.size()), 16.0f,
			sgc::Vec2f{sw * 0.5f, arenaBarY + barH + 10.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// ── プールアロケータ ──
		const float poolPanelY = 300.0f;
		drawAllocatorPanel(
			renderer, text,
			"Pool Allocator", 20.0f, poolPanelY,
			sw - 40.0f, 220.0f,
			sgc::Colorf{1.0f, 0.6f, 0.3f, 1.0f});

		// プールブロック可視化（グリッド表示）
		const float gridX = 40.0f;
		const float gridY = poolPanelY + 40.0f;
		const float cellSize = 18.0f;
		const float cellGap = 3.0f;
		const int cols = static_cast<int>((sw - 80.0f) / (cellSize + cellGap));
		const std::size_t usedBlocks = m_poolAllocated.size();

		for (std::size_t i = 0; i < POOL_BLOCK_COUNT; ++i)
		{
			const int col = static_cast<int>(i) % cols;
			const int row = static_cast<int>(i) / cols;
			const float cx = gridX + static_cast<float>(col) * (cellSize + cellGap);
			const float cy = gridY + static_cast<float>(row) * (cellSize + cellGap);

			const bool isUsed = (i < usedBlocks);
			const auto cellColor = isUsed
				? sgc::Colorf{1.0f, 0.5f, 0.2f, 0.9f}
				: sgc::Colorf{0.15f, 0.15f, 0.2f, 0.8f};

			renderer->drawRect(
				sgc::AABB2f{{cx, cy}, {cx + cellSize, cy + cellSize}},
				cellColor);
			renderer->drawRectFrame(
				sgc::AABB2f{{cx, cy}, {cx + cellSize, cy + cellSize}},
				1.0f, sgc::Colorf{0.3f, 0.3f, 0.4f, 0.5f});
		}

		// プール統計
		const float statY = poolPanelY + 170.0f;
		text->drawText(
			"Block Size: " + std::to_string(POOL_BLOCK_SIZE) + " bytes", 16.0f,
			sgc::Vec2f{40.0f, statY},
			sgc::Colorf::white());

		text->drawText(
			"Total: " + std::to_string(m_pool.blockCount())
				+ "  Free: " + std::to_string(m_pool.freeCount())
				+ "  Used: " + std::to_string(m_pool.blockCount() - m_pool.freeCount()),
			16.0f,
			sgc::Vec2f{40.0f, statY + 22.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// 操作説明
		text->drawText(
			"[Space] Arena Alloc  [1] Pool Alloc  [2] Pool Free  [R] Reset  [Esc] Back",
			14.0f,
			sgc::Vec2f{10.0f, sh - 22.0f},
			sgc::Colorf{0.5f, 0.5f, 0.55f, 1.0f});
	}

private:
	static constexpr std::size_t ARENA_SIZE = 4096;       ///< アリーナ容量（バイト）
	static constexpr std::size_t POOL_BLOCK_SIZE = 64;     ///< プールブロックサイズ
	static constexpr std::size_t POOL_BLOCK_COUNT = 50;    ///< プールブロック数

	sgc::ArenaAllocator m_arena{ARENA_SIZE};                        ///< アリーナアロケータ
	sgc::PoolAllocator m_pool{POOL_BLOCK_SIZE, 8, POOL_BLOCK_COUNT}; ///< プールアロケータ
	std::vector<std::size_t> m_arenaAllocLog;   ///< アリーナ割り当てログ
	std::vector<void*> m_poolAllocated;          ///< プールから割り当てたポインタ
	int m_allocCounter{0};                       ///< 割り当てカウンタ（擬似ランダム用）

	/// @brief 両アロケータをリセットする
	void resetAllocators()
	{
		m_arena.reset();
		m_pool.reset();
		m_arenaAllocLog.clear();
		m_poolAllocated.clear();
		m_allocCounter = 0;
	}

	/// @brief アロケータパネルの外枠を描画する
	static void drawAllocatorPanel(
		sgc::IRenderer* renderer, sgc::ITextRenderer* text,
		const std::string& title,
		float x, float y, float w, float h,
		const sgc::Colorf& accentColor)
	{
		renderer->drawRect(
			sgc::AABB2f{{x, y}, {x + w, y + h}},
			sgc::Colorf{0.0f, 0.0f, 0.0f, 0.5f});
		renderer->drawRectFrame(
			sgc::AABB2f{{x, y}, {x + w, y + h}},
			2.0f, accentColor.withAlpha(0.6f));
		text->drawText(title, 20.0f,
			sgc::Vec2f{x + 10.0f, y + 8.0f}, accentColor);
	}

	/// @brief メモリ使用率バーを描画する
	static void drawMemoryBar(
		sgc::IRenderer* renderer, sgc::ITextRenderer* text,
		float x, float y, float w, float h,
		std::size_t used, std::size_t capacity,
		const sgc::Colorf& fillColor)
	{
		// 背景
		renderer->drawRect(
			sgc::AABB2f{{x, y}, {x + w, y + h}},
			sgc::Colorf{0.1f, 0.1f, 0.15f, 1.0f});

		// 使用部分
		const float ratio = (capacity > 0)
			? static_cast<float>(used) / static_cast<float>(capacity)
			: 0.0f;
		const float filledW = w * ratio;

		if (filledW > 0.0f)
		{
			renderer->drawRect(
				sgc::AABB2f{{x, y}, {x + filledW, y + h}},
				fillColor);
		}

		// 枠線
		renderer->drawRectFrame(
			sgc::AABB2f{{x, y}, {x + w, y + h}},
			1.0f, sgc::Colorf::white().withAlpha(0.5f));

		// パーセント表示
		const std::string pct = std::to_string(static_cast<int>(ratio * 100.0f)) + "%";
		text->drawTextCentered(pct, 18.0f,
			sgc::Vec2f{x + w * 0.5f, y + h * 0.5f},
			sgc::Colorf::white());
	}
};
