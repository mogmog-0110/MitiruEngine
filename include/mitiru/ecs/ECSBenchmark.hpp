#pragma once

/// @file ECSBenchmark.hpp
/// @brief ECS パフォーマンスベンチマークハーネス
/// @details エンティティのイテレーション速度、コンポーネントのアクセスパターン、
///          システムスケジューリングの性能を計測するベンチマークフレームワーク。

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::ecs
{

/// @brief ベンチマーク結果の 1 エントリ
struct BenchmarkResult
{
	std::string name;                  ///< ベンチマーク名
	double totalMs = 0.0;             ///< 合計実行時間（ミリ秒）
	double averageMs = 0.0;           ///< 平均実行時間（ミリ秒）
	double minMs = 0.0;               ///< 最小実行時間（ミリ秒）
	double maxMs = 0.0;               ///< 最大実行時間（ミリ秒）
	std::uint64_t entityCount = 0;    ///< 処理エンティティ数
	std::uint64_t iterations = 0;     ///< イテレーション回数
	double entitiesPerSecond = 0.0;   ///< エンティティ/秒スループット
};

/// @brief ベンチマーク設定
struct BenchmarkConfig
{
	int warmupIterations = 5;         ///< ウォームアップ回数
	int measureIterations = 100;      ///< 計測イテレーション回数
	int entityCount = 10000;          ///< ベンチマーク用エンティティ数
	bool verbose = false;             ///< 詳細ログ出力フラグ
};

/// @brief ベンチマーク関数型
using BenchmarkFunc = std::function<void()>;

/// @brief ECS パフォーマンスベンチマークハーネス
/// @details 各種 ECS 操作の性能を計測し、結果をレポートする。
///          ウォームアップ→計測→統計集計の流れを自動で実行する。
class ECSBenchmark
{
public:
	/// @brief デフォルトコンストラクタ
	ECSBenchmark() = default;

	/// @brief デストラクタ
	~ECSBenchmark() = default;

	/// コピー禁止
	ECSBenchmark(const ECSBenchmark&) = delete;
	ECSBenchmark& operator=(const ECSBenchmark&) = delete;

	/// ムーブ許可
	ECSBenchmark(ECSBenchmark&&) noexcept = default;
	ECSBenchmark& operator=(ECSBenchmark&&) noexcept = default;

	/// @brief ベンチマーク設定を変更する
	/// @param config 新しい設定
	void configure(const BenchmarkConfig& config)
	{
		m_config = config;
	}

	/// @brief ベンチマークを登録する
	/// @param name ベンチマーク名
	/// @param entityCount 対象エンティティ数
	/// @param func 計測対象の関数
	void addBenchmark(const std::string& name,
	                  std::uint64_t entityCount,
	                  BenchmarkFunc func)
	{
		m_benchmarks.push_back({name, entityCount, std::move(func)});
	}

	/// @brief 全ベンチマークを実行する
	/// @return ベンチマーク結果一覧
	[[nodiscard]] std::vector<BenchmarkResult> runAll()
	{
		std::vector<BenchmarkResult> results;
		results.reserve(m_benchmarks.size());

		for (const auto& bench : m_benchmarks)
		{
			results.push_back(runSingle(bench));
		}

		return results;
	}

	/// @brief 結果を整形した文字列として取得する
	/// @param results ベンチマーク結果一覧
	/// @return 整形したレポート文字列
	[[nodiscard]] static std::string formatReport(
		const std::vector<BenchmarkResult>& results)
	{
		std::string report = "=== ECS Benchmark Report ===\n";
		for (const auto& r : results)
		{
			report += r.name + ": avg=" +
			          std::to_string(r.averageMs) + "ms" +
			          " min=" + std::to_string(r.minMs) + "ms" +
			          " max=" + std::to_string(r.maxMs) + "ms" +
			          " (" + std::to_string(static_cast<std::uint64_t>(r.entitiesPerSecond)) +
			          " entities/s)\n";
		}
		return report;
	}

private:
	/// @brief 登録されたベンチマーク情報
	struct BenchmarkEntry
	{
		std::string name;
		std::uint64_t entityCount = 0;
		BenchmarkFunc func;
	};

	/// @brief ベンチマークを 1 つ実行する
	/// @param entry ベンチマーク情報
	/// @return ベンチマーク結果
	[[nodiscard]] BenchmarkResult runSingle(const BenchmarkEntry& entry) const
	{
		// ウォームアップ
		for (int i = 0; i < m_config.warmupIterations; ++i)
		{
			entry.func();
		}

		// 計測
		std::vector<double> times;
		times.reserve(static_cast<std::size_t>(m_config.measureIterations));

		for (int i = 0; i < m_config.measureIterations; ++i)
		{
			const auto start = std::chrono::high_resolution_clock::now();
			entry.func();
			const auto end = std::chrono::high_resolution_clock::now();

			const double ms = std::chrono::duration<double, std::milli>(
				end - start).count();
			times.push_back(ms);
		}

		// 統計集計
		BenchmarkResult result;
		result.name = entry.name;
		result.entityCount = entry.entityCount;
		result.iterations = static_cast<std::uint64_t>(m_config.measureIterations);

		double total = 0.0;
		double minVal = times.empty() ? 0.0 : times[0];
		double maxVal = 0.0;

		for (const double t : times)
		{
			total += t;
			if (t < minVal)
			{
				minVal = t;
			}
			if (t > maxVal)
			{
				maxVal = t;
			}
		}

		result.totalMs = total;
		result.averageMs = times.empty() ? 0.0 : total / static_cast<double>(times.size());
		result.minMs = minVal;
		result.maxMs = maxVal;

		if (result.averageMs > 0.0)
		{
			result.entitiesPerSecond =
				static_cast<double>(entry.entityCount) / (result.averageMs * 0.001);
		}

		return result;
	}

	BenchmarkConfig m_config;                    ///< ベンチマーク設定
	std::vector<BenchmarkEntry> m_benchmarks;    ///< 登録済みベンチマーク一覧
};

} // namespace mitiru::ecs
