#pragma once

/// @file GpuProfiler.hpp
/// @brief GPU計測タイミング抽象化レイヤー
/// @details GPUの処理時間を計測するための抽象インターフェースと
///          テスト用のNullGpuTimer、RAIIスコープヘルパーを提供する。

#include <map>
#include <string>
#include <string_view>

namespace mitiru::debug
{

/// @brief GPUタイマーの抽象インターフェース
/// @details GPU処理時間の計測をAPI非依存で抽象化する。
///          Vulkan、D3D12、WebGPU等のバックエンドに対応可能。
class IGpuTimer
{
public:
	/// @brief デストラクタ
	virtual ~IGpuTimer() = default;

	/// @brief 計測を開始する
	/// @param name 計測区間の名前
	virtual void begin(std::string_view name) = 0;

	/// @brief 計測を終了する
	/// @param name 計測区間の名前
	virtual void end(std::string_view name) = 0;

	/// @brief 計測結果を取得する
	/// @return 計測区間名と所要時間（ミリ秒）のマップ
	[[nodiscard]] virtual std::map<std::string, float> getResults() const = 0;

	/// @brief 計測結果をリセットする
	virtual void reset() = 0;
};

/// @brief テスト用のNull GPUタイマー実装
/// @details 全ての操作をno-opで実装する。計測結果は常に0.0msを返す。
///
/// @code
/// mitiru::debug::NullGpuTimer timer;
/// timer.begin("ShadowPass");
/// // ... GPU処理 ...
/// timer.end("ShadowPass");
/// auto results = timer.getResults();
/// @endcode
class NullGpuTimer : public IGpuTimer
{
public:
	/// @brief 計測を開始する（no-op、名前を記録する）
	/// @param name 計測区間の名前
	void begin(std::string_view name) override
	{
		m_results[std::string(name)] = 0.0f;
	}

	/// @brief 計測を終了する（no-op）
	/// @param name 計測区間の名前
	void end(std::string_view) override
	{
	}

	/// @brief 計測結果を取得する
	/// @return 記録された区間名と0.0msのマップ
	[[nodiscard]] std::map<std::string, float> getResults() const override
	{
		return m_results;
	}

	/// @brief 計測結果をリセットする
	void reset() override
	{
		m_results.clear();
	}

	/// @brief 記録された計測区間の数を取得する（テスト用）
	/// @return 区間数
	[[nodiscard]] std::size_t sectionCount() const noexcept
	{
		return m_results.size();
	}

private:
	std::map<std::string, float> m_results;  ///< 計測結果
};

/// @brief GPUプロファイルスコープ（RAII）
/// @details コンストラクタでbegin()、デストラクタでend()を呼び出す。
///
/// @code
/// {
///     mitiru::debug::GpuProfileScope scope(&timer, "ForwardPass");
///     // ... GPU処理 ...
/// } // 自動的にend()が呼ばれる
/// @endcode
class GpuProfileScope
{
public:
	/// @brief コンストラクタ（計測開始）
	/// @param timer GPUタイマーへのポインタ（非所有、nullptrを許容）
	/// @param name 計測区間の名前
	GpuProfileScope(IGpuTimer* timer, std::string_view name)
		: m_timer(timer)
		, m_name(name)
	{
		if (m_timer)
		{
			m_timer->begin(m_name);
		}
	}

	/// @brief デストラクタ（計測終了）
	~GpuProfileScope()
	{
		if (m_timer)
		{
			m_timer->end(m_name);
		}
	}

	/// @brief コピー禁止
	GpuProfileScope(const GpuProfileScope&) = delete;

	/// @brief コピー代入禁止
	GpuProfileScope& operator=(const GpuProfileScope&) = delete;

	/// @brief ムーブ禁止
	GpuProfileScope(GpuProfileScope&&) = delete;

	/// @brief ムーブ代入禁止
	GpuProfileScope& operator=(GpuProfileScope&&) = delete;

private:
	IGpuTimer* m_timer;    ///< GPUタイマー（非所有）
	std::string m_name;    ///< 計測区間名
};

} // namespace mitiru::debug
