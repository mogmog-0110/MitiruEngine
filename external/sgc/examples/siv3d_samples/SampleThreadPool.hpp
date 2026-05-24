#pragma once

/// @file SampleThreadPool.hpp
/// @brief ThreadPool（並列計算）ベンチマークサンプル
///
/// ThreadPoolのparallelForを使った並列計算と逐次計算の
/// 処理時間を比較するベンチマークデモ。
/// - Space: ベンチマーク実行
/// - Rキー: 結果リセット
/// - ESCキー: メニューに戻る

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/core/ThreadPool.hpp"
#include "sgc/core/Timer.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief ThreadPoolベンチマークサンプルシーン
///
/// 大規模配列の重い計算を逐次実行と並列実行で比較し、
/// 処理時間の差をグラフとともに可視化する。
class SampleThreadPool : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_pool = std::make_unique<sgc::ThreadPool>();
		m_sequentialMs = 0.0;
		m_parallelMs = 0.0;
		m_hasResult = false;
		m_running = false;
		m_data.resize(DATA_SIZE, 0.0f);
	}

	/// @brief 毎フレームの更新処理
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		(void)dt;
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		if (input->isKeyJustPressed(KeyCode::R))
		{
			m_sequentialMs = 0.0;
			m_parallelMs = 0.0;
			m_hasResult = false;
			return;
		}

		if (input->isKeyJustPressed(KeyCode::SPACE) && !m_running)
		{
			runBenchmark();
		}
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;
		const float sw = getData().screenWidth;

		r->clearBackground(sgc::Colorf{0.04f, 0.06f, 0.1f, 1.0f});

		// タイトル
		tr->drawText("ThreadPool - Parallel Benchmark", 22.0f,
			{10.0f, 10.0f}, sgc::Colorf{0.6f, 0.9f, 1.0f, 1.0f});
		tr->drawText(
			"[Space] Run Benchmark  [R] Reset  [Esc] Back",
			14.0f, {10.0f, 38.0f}, sgc::Colorf{0.6f, 0.6f, 0.6f, 1.0f});

		// スレッドプール情報
		drawPoolInfo(r, tr);

		// ベンチマーク結果
		if (m_hasResult)
		{
			drawResults(r, tr, sw);
		}
		else if (m_running)
		{
			tr->drawTextCentered("Running...", 28.0f,
				{sw * 0.5f, 300.0f},
				sgc::Colorf{1.0f, 0.8f, 0.2f, 1.0f});
		}
		else
		{
			tr->drawTextCentered("Press [Space] to start", 24.0f,
				{sw * 0.5f, 300.0f},
				sgc::Colorf{0.5f, 0.5f, 0.7f, 1.0f});
		}
	}

private:
	static constexpr std::size_t DATA_SIZE = 2000000;   ///< 計算対象の配列サイズ
	static constexpr int ITERATIONS = 20;               ///< 各要素の計算反復回数

	std::unique_ptr<sgc::ThreadPool> m_pool;   ///< スレッドプール
	std::vector<float> m_data;                 ///< 計算対象データ
	double m_sequentialMs{0.0};                ///< 逐次実行の所要時間(ms)
	double m_parallelMs{0.0};                  ///< 並列実行の所要時間(ms)
	bool m_hasResult{false};                   ///< 結果があるか
	bool m_running{false};                     ///< 実行中か

	/// @brief 重い計算を模倣する関数（sin/cos反復）
	/// @param val 入力値
	/// @return 計算結果
	static float heavyComputation(float val)
	{
		float result = val;
		for (int i = 0; i < ITERATIONS; ++i)
		{
			result = std::sin(result) * std::cos(result + 0.1f)
				+ std::sqrt(std::abs(result) + 1.0f);
		}
		return result;
	}

	/// @brief ベンチマークを実行する
	void runBenchmark()
	{
		m_running = true;

		// 初期データ設定
		for (std::size_t i = 0; i < m_data.size(); ++i)
		{
			m_data[i] = static_cast<float>(i) * 0.001f;
		}

		// 逐次実行
		{
			sgc::Stopwatch sw;
			sw.start();
			for (std::size_t i = 0; i < m_data.size(); ++i)
			{
				m_data[i] = heavyComputation(m_data[i]);
			}
			sw.stop();
			m_sequentialMs = sw.elapsedMilliseconds();
		}

		// データリセット
		for (std::size_t i = 0; i < m_data.size(); ++i)
		{
			m_data[i] = static_cast<float>(i) * 0.001f;
		}

		// 並列実行
		{
			sgc::Stopwatch sw;
			sw.start();
			m_pool->parallelFor(
				std::size_t{0}, m_data.size(),
				[this](std::size_t i)
				{
					m_data[i] = heavyComputation(m_data[i]);
				});
			sw.stop();
			m_parallelMs = sw.elapsedMilliseconds();
		}

		m_hasResult = true;
		m_running = false;
	}

	/// @brief スレッドプール情報を描画する
	void drawPoolInfo(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const float px = 20.0f;
		const float py = 70.0f;

		r->drawRect(sgc::AABB2f{{px, py}, {px + 320.0f, py + 80.0f}},
			sgc::Colorf{0.08f, 0.08f, 0.15f, 0.9f});
		r->drawRectFrame(sgc::AABB2f{{px, py}, {px + 320.0f, py + 80.0f}},
			2.0f, sgc::Colorf{0.3f, 0.5f, 0.8f, 0.8f});

		const std::size_t threads = m_pool ? m_pool->threadCount() : 0;
		tr->drawText(
			"Thread Count: " + std::to_string(threads), 18.0f,
			{px + 10.0f, py + 10.0f},
			sgc::Colorf{0.8f, 0.9f, 1.0f, 1.0f});

		tr->drawText(
			"Array Size: " + std::to_string(DATA_SIZE), 16.0f,
			{px + 10.0f, py + 38.0f},
			sgc::Colorf{0.7f, 0.7f, 0.8f, 1.0f});

		// スレッドインジケータ
		for (std::size_t i = 0; i < threads && i < 16; ++i)
		{
			const float cx = px + 240.0f + static_cast<float>(i % 8) * 18.0f;
			const float cy = py + 18.0f + (i >= 8 ? 20.0f : 0.0f);
			r->drawCircle({cx, cy}, 6.0f,
				sgc::Colorf{0.3f, 0.7f, 1.0f, 0.8f});
		}
	}

	/// @brief ベンチマーク結果を描画する
	void drawResults(
		sgc::IRenderer* r, sgc::ITextRenderer* tr, float sw) const
	{
		const float cx = sw * 0.5f;
		const float baseY = 200.0f;

		tr->drawTextCentered("Benchmark Results", 24.0f,
			{cx, baseY}, sgc::Colorf{1.0f, 1.0f, 1.0f, 1.0f});

		// 逐次実行バー
		const float barMaxW = 400.0f;
		const float barH = 36.0f;
		const float barX = cx - barMaxW * 0.5f;
		const double maxMs = (m_sequentialMs > m_parallelMs)
			? m_sequentialMs : m_parallelMs;

		// 逐次
		const float seqY = baseY + 50.0f;
		tr->drawText("Sequential:", 16.0f,
			{barX, seqY - 20.0f},
			sgc::Colorf{0.8f, 0.8f, 0.8f, 1.0f});
		const float seqW = (maxMs > 0.0)
			? static_cast<float>(m_sequentialMs / maxMs) * barMaxW
			: 0.0f;
		r->drawRect(sgc::AABB2f{{barX, seqY}, {barX + barMaxW, seqY + barH}},
			sgc::Colorf{0.1f, 0.1f, 0.15f, 0.8f});
		r->drawRect(sgc::AABB2f{{barX, seqY}, {barX + seqW, seqY + barH}},
			sgc::Colorf{1.0f, 0.4f, 0.3f, 0.8f});

		// 逐次時間テキスト
		char seqBuf[64];
		std::snprintf(seqBuf, sizeof(seqBuf), "%.1f ms", m_sequentialMs);
		tr->drawText(seqBuf, 16.0f,
			{barX + seqW + 8.0f, seqY + 8.0f},
			sgc::Colorf{1.0f, 0.5f, 0.4f, 1.0f});

		// 並列
		const float parY = baseY + 120.0f;
		tr->drawText("Parallel:", 16.0f,
			{barX, parY - 20.0f},
			sgc::Colorf{0.8f, 0.8f, 0.8f, 1.0f});
		const float parW = (maxMs > 0.0)
			? static_cast<float>(m_parallelMs / maxMs) * barMaxW
			: 0.0f;
		r->drawRect(sgc::AABB2f{{barX, parY}, {barX + barMaxW, parY + barH}},
			sgc::Colorf{0.1f, 0.1f, 0.15f, 0.8f});
		r->drawRect(sgc::AABB2f{{barX, parY}, {barX + parW, parY + barH}},
			sgc::Colorf{0.3f, 0.8f, 0.4f, 0.8f});

		char parBuf[64];
		std::snprintf(parBuf, sizeof(parBuf), "%.1f ms", m_parallelMs);
		tr->drawText(parBuf, 16.0f,
			{barX + parW + 8.0f, parY + 8.0f},
			sgc::Colorf{0.4f, 1.0f, 0.5f, 1.0f});

		// 速度比表示
		const float ratioY = baseY + 200.0f;
		if (m_parallelMs > 0.001)
		{
			const double speedup = m_sequentialMs / m_parallelMs;
			char ratioBuf[64];
			std::snprintf(ratioBuf, sizeof(ratioBuf), "Speedup: %.2fx", speedup);
			tr->drawTextCentered(ratioBuf, 28.0f,
				{cx, ratioY},
				sgc::Colorf{1.0f, 0.9f, 0.3f, 1.0f});
		}
	}
};
