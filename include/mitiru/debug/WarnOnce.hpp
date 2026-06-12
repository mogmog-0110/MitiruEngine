#pragma once

/// @file WarnOnce.hpp
/// @brief キー単位で 1 回だけ stderr に警告するヘルパ (R-01 級)
/// @details 「黙って壊れる」失敗経路 (音声/画像の読み込み失敗、intent 上限到達等) で
///          初回のみ 1 行出す。哲学: エラーは必要最小限 — 毎フレーム連呼しない。

#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace mitiru::debug
{

namespace detail
{
/// @brief warnOnce の発火済み key 集合 (process 単位の単一インスタンス)
struct WarnOnceState
{
	std::mutex mu;
	std::unordered_set<std::string> seen;

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
		if (!st.seen.emplace(std::string(key)).second) { return; }
	}
	std::fprintf(stderr, "[mitiru] %.*s\n", static_cast<int>(msg.size()), msg.data());
}

/// @brief テスト用: key が発火済みかを返す
[[nodiscard]] inline bool warnOnceFired(std::string_view key)
{
	auto& st = detail::WarnOnceState::instance();
	std::lock_guard lock(st.mu);
	return st.seen.count(std::string(key)) > 0;
}

/// @brief テスト用: 発火済み key を全て忘れる
inline void warnOnceResetForTest()
{
	auto& st = detail::WarnOnceState::instance();
	std::lock_guard lock(st.mu);
	st.seen.clear();
}

}  // namespace mitiru::debug
