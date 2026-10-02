#pragma once

/// @file RenderGraph.hpp
/// @brief レンダーグラフ
/// @details 依存宣言なし（登録順のまま実行、Phase 1 互換）と、reads/writes を宣言して
///          compile() で依存順に並べ替える形の両方をサポートする。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace mitiru::render
{

/// @brief RenderGraph が扱うリソース識別子。RenderTargetPool の RtHandle 等、呼び出し側が採番する。
using RtId = std::uint32_t;

/// @brief レンダリングパス
/// @details 名前と実行コールバックを持つ 1 単位のレンダリング処理。
///          reads/writes が両方空のパスは依存追跡の対象外で、登録順のまま実行される。
struct RenderPass
{
	std::string name;                    ///< パス名（デバッグ用）
	std::function<void()> execute;       ///< 実行コールバック
	bool enabled = true;                 ///< パスの有効/無効フラグ
	std::vector<RtId> reads;             ///< 読み取る RT（このパスより前に writes 側を実行する必要がある）
	std::vector<RtId> writes;            ///< 書き込む RT
};

/// @brief レンダーグラフ
/// @details レンダリングパスを登録し、順次実行する。
///          reads/writes を宣言したパスは compile() でトポロジカルソートされる。
class RenderGraph
{
public:
	/// @brief 依存なしでレンダリングパスを追加する（Phase 1 互換、登録順のまま実行される）
	/// @param name パス名
	/// @param executeFunc 実行コールバック
	void addPass(const std::string& name, std::function<void()> executeFunc)
	{
		m_passes.push_back(RenderPass{name, std::move(executeFunc), true, {}, {}});
		m_compiled = false;
	}

	/// @brief 読み書きする RT を宣言してパスを追加する
	/// @param name パス名
	/// @param executeFunc 実行コールバック
	/// @param reads このパスが読み取る RT（他パスの writes と一致すると依存辺になる）
	/// @param writes このパスが書き込む RT
	void addPass(const std::string& name, std::function<void()> executeFunc,
	             std::initializer_list<RtId> reads,
	             std::initializer_list<RtId> writes)
	{
		m_passes.push_back(RenderPass{
			name, std::move(executeFunc), true,
			std::vector<RtId>(reads), std::vector<RtId>(writes)});
		m_compiled = false;
	}

	/// @brief reads/writes の依存関係から実行順を解決し、m_passes を並べ替える
	/// @details 他パスの reads から一度も参照されない writes 専用パスは無効化する（cull）。
	///          reads/writes が両方空のパスは cull 対象にならず、依存辺も持たない。
	/// @return 循環依存を検出した場合は false（m_passes は変更しない）
	[[nodiscard]] bool compile()
	{
		const std::size_t n = m_passes.size();
		std::vector<bool> culled(n, false);
		cullUnreferencedWrites(culled);

		std::vector<std::vector<std::size_t>> deps(n);
		buildDependencyEdges(culled, deps);

		std::vector<std::size_t> order;
		if (!topologicalSort(culled, deps, order))
		{
			return false;   // 循環依存
		}

		std::vector<RenderPass> reordered;
		reordered.reserve(n);
		for (std::size_t idx : order) { reordered.push_back(std::move(m_passes[idx])); }
		for (std::size_t i = 0; i < n; ++i)
		{
			if (!culled[i]) { continue; }
			m_passes[i].enabled = false;
			reordered.push_back(std::move(m_passes[i]));
		}
		m_passes = std::move(reordered);
		m_compiled = true;
		return true;
	}

	/// @brief 直前の compile() が成功したか
	[[nodiscard]] bool isCompiled() const noexcept { return m_compiled; }

	/// @brief 指定パスの有効/無効を切り替える
	/// @param name パス名
	/// @param enabled 有効にするなら true
	/// @return パスが見つかれば true
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
		m_compiled = false;
	}

	/// @brief 登録されたパス一覧を取得する
	/// @return パス配列への定数参照
	[[nodiscard]] const std::vector<RenderPass>& passes() const noexcept
	{
		return m_passes;
	}

private:
	/// @brief 他パスの reads から一度も参照されない writes 専用パスを culled[i]=true にする
	/// @details cull は連鎖する（A の唯一の読者 B が culled になれば A も culled になりうる）ため、
	///          変化が無くなるまで繰り返す。
	void cullUnreferencedWrites(std::vector<bool>& culled) const
	{
		bool changed = true;
		while (changed)
		{
			changed = false;
			std::unordered_set<RtId> liveReads;
			for (std::size_t i = 0; i < m_passes.size(); ++i)
			{
				if (culled[i]) { continue; }
				for (RtId id : m_passes[i].reads) { liveReads.insert(id); }
			}
			for (std::size_t i = 0; i < m_passes.size(); ++i)
			{
				if (culled[i] || m_passes[i].writes.empty()) { continue; }
				const bool anyConsumed = std::any_of(
					m_passes[i].writes.begin(), m_passes[i].writes.end(),
					[&](RtId id) { return liveReads.count(id) > 0; });
				if (!anyConsumed)
				{
					culled[i] = true;
					changed = true;
				}
			}
		}
	}

	/// @brief pass[i] が読む RtId を pass[j] が書いていれば「i は j に依存する」辺を張る
	void buildDependencyEdges(
		const std::vector<bool>& culled,
		std::vector<std::vector<std::size_t>>& deps) const
	{
		const std::size_t n = m_passes.size();
		for (std::size_t i = 0; i < n; ++i)
		{
			if (culled[i]) { continue; }
			for (RtId readId : m_passes[i].reads)
			{
				for (std::size_t j = 0; j < n; ++j)
				{
					if (i == j || culled[j]) { continue; }
					const auto& w = m_passes[j].writes;
					if (std::find(w.begin(), w.end(), readId) != w.end())
					{
						deps[i].push_back(j);
					}
				}
			}
		}
	}

	/// @brief Kahn 法によるトポロジカルソート
	/// @details 実行可能集合の中で最も元の登録順が早いものを選ぶため、依存辺の無いグラフでは
	///          登録順のまま並ぶ（addPass(name,fn) だけを使う既存呼び出しとの互換を保つ）。
	/// @return 循環依存があれば false
	[[nodiscard]] bool topologicalSort(
		const std::vector<bool>& culled,
		std::vector<std::vector<std::size_t>> deps,
		std::vector<std::size_t>& order) const
	{
		const std::size_t n = m_passes.size();
		const std::size_t liveCount = static_cast<std::size_t>(
			std::count(culled.begin(), culled.end(), false));

		std::vector<bool> done(n, false);
		order.reserve(liveCount);

		while (order.size() < liveCount)
		{
			std::size_t pick = n;
			for (std::size_t i = 0; i < n; ++i)
			{
				if (culled[i] || done[i] || !deps[i].empty()) { continue; }
				pick = i;
				break;
			}
			if (pick == n) { return false; }   // 実行可能なパスが無い = 循環依存

			done[pick] = true;
			order.push_back(pick);
			for (std::size_t i = 0; i < n; ++i)
			{
				if (culled[i] || done[i]) { continue; }
				auto& d = deps[i];
				d.erase(std::remove(d.begin(), d.end(), pick), d.end());
			}
		}
		return true;
	}

	std::vector<RenderPass> m_passes;  ///< 登録されたレンダリングパス
	bool m_compiled = false;           ///< 直前の compile() が成功したか
};

} // namespace mitiru::render
