#pragma once

/// @file RenderGraph.hpp
/// @brief 最小レンダーグラフ
/// @details レンダリングパスの順序管理と実行を行う。
///          Phase 1 ではシンプルな逐次実行のみ。

#include <functional>
#include <string>
#include <vector>

namespace mitiru::render
{

/// @brief レンダリングパス
/// @details 名前と実行コールバックを持つ1単位のレンダリング処理。
struct RenderPass
{
	std::string name;                    ///< パス名（デバッグ用）
	std::function<void()> execute;       ///< 実行コールバック
	bool enabled = true;                 ///< パスの有効/無効フラグ
};

/// @brief レンダーグラフ
/// @details レンダリングパスを登録し、順次実行する。
///          Phase 1 では登録順にそのまま実行する。
///
/// @code
/// mitiru::render::RenderGraph graph;
/// graph.addPass("Shadow", [&]() { renderShadows(); });
/// graph.addPass("Opaque", [&]() { renderOpaque(); });
/// graph.addPass("Transparent", [&]() { renderTransparent(); });
/// graph.addPass("PostProcess", [&]() { renderPostProcess(); });
/// graph.execute();
/// @endcode
class RenderGraph
{
public:
	/// @brief レンダリングパスを追加する
	/// @param name パス名
	/// @param executeFunc 実行コールバック
	void addPass(const std::string& name, std::function<void()> executeFunc)
	{
		m_passes.push_back(RenderPass{name, std::move(executeFunc), true});
	}

	/// @brief 指定パスの有効/無効を切り替える
	/// @param name パス名
	/// @param enabled 有効にするならtrue
	/// @return パスが見つかればtrue
	bool setPassEnabled(const std::string& name, bool enabled)
	{
		for (auto& pass : m_passes)
		{
			if (pass.name == name)
			{
				pass.enabled = enabled;
				return true;
			}
		}
		return false;
	}

	/// @brief 登録された全パスを順次実行する
	/// @return 実行されたパス数
	[[nodiscard]] int execute()
	{
		int executed = 0;

		for (const auto& pass : m_passes)
		{
			if (pass.enabled && pass.execute)
			{
				pass.execute();
				++executed;
			}
		}

		return executed;
	}

	/// @brief 登録されたパス数を取得する
	[[nodiscard]] std::size_t passCount() const noexcept
	{
		return m_passes.size();
	}

	/// @brief 全パスをクリアする
	void clear() noexcept
	{
		m_passes.clear();
	}

	/// @brief 登録されたパス一覧を取得する
	/// @return パス配列への定数参照
	[[nodiscard]] const std::vector<RenderPass>& passes() const noexcept
	{
		return m_passes;
	}

private:
	std::vector<RenderPass> m_passes;  ///< 登録されたレンダリングパス
};

} // namespace mitiru::render
