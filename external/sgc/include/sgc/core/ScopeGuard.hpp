#pragma once

/// @file ScopeGuard.hpp
/// @brief RAIIスコープガード — スコープ終了時に自動でクリーンアップ処理を実行する
///
/// スコープを抜ける際に必ず実行したい処理（リソース解放、状態復元等）を
/// ラムダで登録する。dismiss() で実行をキャンセルすることも可能。

#include <concepts>
#include <type_traits>
#include <utility>

namespace sgc
{

/// @brief RAIIスコープガード
///
/// デストラクタで登録された呼び出し可能オブジェクトを実行する。
/// コピー不可・ムーブ可能。dismiss() で実行を抑制できる。
///
/// @tparam F 呼び出し可能オブジェクトの型（std::invocable を満たすこと）
///
/// @note 登録する関数は例外を投げてはならない。例外が発生した場合は
///       内部で捕捉され、静かに無視される。
///
/// @code
/// {
///     auto guard = sgc::ScopeGuard([&]{ resource.release(); });
///     // ... 処理 ...
///     guard.dismiss();  // 成功時はクリーンアップをスキップ
/// }
/// @endcode
template <std::invocable F>
class ScopeGuard
{
public:
	/// @brief ムーブコンストラクタ — 呼び出し可能オブジェクトを受け取って構築する
	/// @param func スコープ終了時に実行する関数（ムーブされる）
	explicit ScopeGuard(F&& func) noexcept(std::is_nothrow_move_constructible_v<F>)
		: m_func(std::move(func))
		, m_active(true)
	{
	}

	/// @brief コピーコンストラクタ — 呼び出し可能オブジェクトをコピーして構築する
	/// @param func スコープ終了時に実行する関数（コピーされる）
	explicit ScopeGuard(const F& func) noexcept(std::is_nothrow_copy_constructible_v<F>)
		: m_func(func)
		, m_active(true)
	{
	}

	/// @brief ムーブコンストラクタ — 所有権を移動する
	///
	/// 移動元のガードは無効化され、移動先のみが実行責任を持つ。
	///
	/// @param other 移動元のスコープガード
	ScopeGuard(ScopeGuard&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
		: m_func(std::move(other.m_func))
		, m_active(other.m_active)
	{
		other.m_active = false;
	}

	/// @brief コピー禁止
	ScopeGuard(const ScopeGuard&) = delete;

	/// @brief コピー代入禁止
	ScopeGuard& operator=(const ScopeGuard&) = delete;

	/// @brief ムーブ代入禁止（ガードの再代入は安全でないため）
	ScopeGuard& operator=(ScopeGuard&&) = delete;

	/// @brief デストラクタ — アクティブな場合に登録された関数を実行する
	///
	/// @note 関数が例外を投げた場合は内部で捕捉し、std::terminateを防止する。
	///       スコープガードのクリーンアップ処理は例外を投げるべきではない。
	~ScopeGuard()
	{
		if (m_active)
		{
			try
			{
				m_func();
			}
			catch (...)
			{
				// 例外を捕捉してstd::terminateを防止する。
				// クリーンアップ処理で例外を投げるべきではない。
			}
		}
	}

	/// @brief ガードの実行をキャンセルする
	///
	/// dismiss() を呼ぶと、スコープ終了時にクリーンアップ処理が実行されなくなる。
	void dismiss() noexcept
	{
		m_active = false;
	}

	/// @brief ガードがアクティブ（実行予定）かどうかを返す
	/// @return アクティブなら true、dismiss済みなら false
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_active;
	}

private:
	F m_func;       ///< スコープ終了時に実行する関数
	bool m_active;  ///< ガードが有効かどうか
};

/// @brief 推論ガイド — ラムダ等から直接 ScopeGuard を構築可能にする
template <typename F>
ScopeGuard(F) -> ScopeGuard<F>;

/// @brief スコープガードを生成するファクトリ関数
/// @tparam F 呼び出し可能オブジェクトの型
/// @param func スコープ終了時に実行する関数
/// @return 構築されたスコープガード
///
/// @code
/// auto guard = sgc::makeScopeGuard([&]{ cleanup(); });
/// @endcode
template <std::invocable F>
[[nodiscard]] auto makeScopeGuard(F&& func)
{
	return ScopeGuard<std::remove_cvref_t<F>>(std::forward<F>(func));
}

} // namespace sgc

/// @brief 無名スコープガードを生成するマクロ
///
/// 行番号を利用してユニークな変数名を自動生成する。
///
/// @code
/// SGC_SCOPE_EXIT([&]{ file.close(); });
/// @endcode
#define SGC_SCOPE_EXIT_CONCAT_(a, b) a##b
#define SGC_SCOPE_EXIT_CONCAT(a, b) SGC_SCOPE_EXIT_CONCAT_(a, b)
#define SGC_SCOPE_EXIT                                                         \
	auto SGC_SCOPE_EXIT_CONCAT(sgcScopeGuard_, __LINE__) =                    \
		::sgc::ScopeGuard
