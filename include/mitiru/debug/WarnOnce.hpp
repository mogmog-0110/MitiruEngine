#pragma once

/// @file WarnOnce.hpp
/// @brief キー単位で 1 回だけ stderr に警告するヘルパ (R-01 級)
/// @details 「気づかないうちにおかしくなる」失敗経路 (音声/画像の読み込み失敗、intent 上限到達等) で
///          初回だけ 1 行出す。哲学: エラーは必要最小限。毎フレーム繰り返し出さない。

#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

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
}  // namespace detail

/// @brief key ごとに 1 回だけ `[mitiru] msg` を stderr へ出す。2 回目以降は no-op。
/// @details 失敗経路でのみ呼ぶこと (成功路のホットパスで set lookup しない)。スレッド安全。
inline void warnOnce(std::string_view key, std::string_view msg)
{
	auto& st = detail::WarnOnceState::instance();
	{
		std::lock_guard lock(st.mu);
		if (st.seen.find(key) != st.seen.end()) { return; }
		st.seen.emplace(key);
	}
	std::fprintf(stderr, "[mitiru] %.*s\n", static_cast<int>(msg.size()), msg.data());
}

/// @brief `warnOnce` の 3 行テンプレ版。「何が / なぜ / どうする」を強制することで、
///        原因不明のまま止まる失敗メッセージ (症状だけ書いて終わる warnOnce) を減らす。
/// @details key の発火判定は `warnOnce` と共有 (同じ key なら 1 回だけ)。
inline void warnOnceFix(std::string_view key, std::string_view what, std::string_view why, std::string_view fix)
{
	// 見出し付きの 3 行は帳票のようで読みにくかったので、1 文 + 対処の 2 行にする
	std::string msg;
	msg.reserve(what.size() + why.size() + fix.size() + 24);
	msg += what;
	if (!why.empty())
	{
		msg += " (";
		msg += why;
		msg += ")";
	}
	msg += "\n         → ";
	msg += fix;
	warnOnce(key, msg);
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
