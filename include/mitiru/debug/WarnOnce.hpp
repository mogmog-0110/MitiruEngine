#pragma once

/// @file WarnOnce.hpp
/// @brief キーごとに 1 回だけ端末へ知らせるヘルパ
/// @details 音や画像が読めない、要求が上限を超えたといった、知らせないと気づけない失敗で使う。
///          毎フレーム同じ文を繰り返さないよう、キーごとに最初の 1 回だけ出す。
///          出し方は mitiru/debug/ConsoleOut.hpp に従う (warnOnce は常に、verboseOnce は詳細を出す実行でだけ)。

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/util/TransparentStringHash.hpp>

namespace mitiru::debug
{

namespace detail
{

/// @brief warnOnce で発火済みの key の集合 (process 単位で 1 つだけのインスタンス)
struct WarnOnceState
{
	std::mutex mu;
	// string_view のまま引く。発火済みの key を毎フレーム引いても std::string を作らない
	std::unordered_set<std::string, util::TransparentStringHash, std::equal_to<>> seen;

	static WarnOnceState& instance()
	{
		static WarnOnceState s;
		return s;
	}
};

/// @brief key が初めてなら覚えて true を返す
[[nodiscard]] inline bool claimOnce(std::string_view key)
{
	auto& st = WarnOnceState::instance();
	std::lock_guard lock(st.mu);
	if (st.seen.find(key) != st.seen.end()) { return false; }
	st.seen.emplace(key);
	return true;
}
}  // namespace detail

/// @brief 使う人が直す必要のある失敗を key ごとに 1 回だけ知らせる。msg は何が起きたかとどうすればよいかの 1〜2 文
/// @details 失敗経路でのみ呼ぶこと (成功路のホットパスで set lookup しない)。スレッド安全。
inline void warnOnce(std::string_view key, std::string_view msg)
{
	if (detail::claimOnce(key)) { console::notice(msg); }
}

/// @brief 中を調べる人向けの詳細を key ごとに 1 回だけ出す。詳細を出さない実行では key も消費しない
inline void verboseOnce(std::string_view key, std::string_view msg)
{
	if (console::isVerbose() && detail::claimOnce(key)) { console::notice(msg); }
}

/// @brief テスト用: key が発火済みかを返す
[[nodiscard]] inline bool warnOnceFired(std::string_view key)
{
	auto& st = detail::WarnOnceState::instance();
	std::lock_guard lock(st.mu);
	return st.seen.find(key) != st.seen.end();
}

/// @brief テスト用: 発火済み key を全て忘れる
inline void warnOnceResetForTest()
{
	auto& st = detail::WarnOnceState::instance();
	std::lock_guard lock(st.mu);
	st.seen.clear();
}

}  // namespace mitiru::debug
