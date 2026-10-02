#pragma once
// ページが snapshot や /api の応答から値を拾うための小道具。無い所を引いても例外にしない。

#include "SnapshotFormat.hpp"

#include <initializer_list>
#include <string_view>

namespace mitiru::tool
{

/// path をたどった先。途中が無ければ nullptr。
inline const Snapshot* findAt(const Snapshot& root, std::initializer_list<std::string_view> path)
{
	const Snapshot* node = &root;
	for (std::string_view key : path)
	{
		if (!node->is_object()) { return nullptr; }
		const auto it = node->find(key);
		if (it == node->end()) { return nullptr; }
		node = &*it;
	}
	return node;
}

/// JS の真偽 (null / false / 0 / "" は偽、オブジェクトと配列は空でも真)。
inline bool truthy(const Snapshot* v)
{
	if (v == nullptr || v->is_null()) { return false; }
	if (v->is_boolean()) { return v->get<bool>(); }
	if (v->is_number()) { return v->get<double>() != 0.0; }
	if (v->is_string()) { return !v->get<std::string>().empty(); }
	return true;
}

inline double numberOr(const Snapshot* v, double fallback)
{
	return (v != nullptr && v->is_number()) ? v->get<double>() : fallback;
}

inline std::string stringOr(const Snapshot* v, std::string fallback)
{
	return (v != nullptr && v->is_string()) ? v->get<std::string>() : std::move(fallback);
}

/// URL の問い合わせ部分に入れる形 (encodeURIComponent と同じく英数字と -_.!~*'() 以外を %XX にする)。
inline std::string urlEncode(std::string_view s)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	for (const char ch : s)
	{
		const auto c = static_cast<unsigned char>(ch);
		const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		                   || std::string_view("-_.!~*'()").find(static_cast<char>(c)) != std::string_view::npos;
		if (plain) { out += static_cast<char>(c); continue; }
		out += '%';
		out += kHex[c >> 4];
		out += kHex[c & 0x0F];
	}
	return out;
}

/// 応答の本文を JSON として読む。読めなければ discarded。
inline Snapshot parseBody(const std::string& body)
{
	return Snapshot::parse(body, nullptr, false);
}

/// 応答が成功で、本文に error が無いか。
inline bool responseOk(int status, const Snapshot& body)
{
	return status >= 200 && status < 300 && !body.is_discarded() && !(body.is_object() && body.contains("error"));
}

/// /api/ai/state の branch から「まだ残す / 捨てるを選んでいない分岐」の帯の文字を作る (無ければ空)。
/// scene_view と why_view が同じ帯を出す (ADR 0035 4-3)。
inline std::string branchBannerText(const Snapshot* branch)
{
	const Snapshot* fields = branch != nullptr ? findAt(*branch, { "fields" }) : nullptr;
	if (!truthy(branch) || !truthy(findAt(*branch, { "active" })) || fields == nullptr || !fields->is_array() || fields->empty())
	{
		return {};
	}
	return "分岐中: " + std::to_string(fields->size()) + " フィールド（残す / 捨てる）";
}

/// ゲームの /api に届かなかった時の文。
inline std::string connectionFailedText(int port)
{
	return "接続失敗: 応答が無い (127.0.0.1:" + std::to_string(port) + " が listen していますか?)";
}

/// 一定の間隔で問い合わせる。前の応答が返るまでは次を送らない (ゲームが遅い時に列が伸びない)。
struct PollTimer
{
	double interval = 0.6;
	double next = 0.0;
	bool inFlight = false;

	[[nodiscard]] bool due(double now)
	{
		if (inFlight || now < next) { return false; }
		next = now + interval;
		inFlight = true;
		return true;
	}
	void done() noexcept { inFlight = false; }
};

/// payload の文字列。無ければ空。
inline std::string payloadString(const nlohmann::json& payload, const char* key)
{
	const auto it = payload.find(key);
	if (it == payload.end()) { return {}; }
	return it->is_string() ? it->get<std::string>() : it->dump();
}

} // namespace mitiru::tool
