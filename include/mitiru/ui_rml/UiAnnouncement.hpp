#pragma once

/// @file UiAnnouncement.hpp
/// @brief RML で data-a11y を付けた要素の文字が出た・変わった時を、読み上げ用の通知に直す判定。
/// @details RmlUi にも Win32 にも依存しない。要素の文字は訳と data model の展開が済んだ描画時の文字なので、
///          言語を替えても画面の字幕と読み上げが食い違わない。通知は出力で、ゲームの状態にも録画にも入らない。

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::ui_rml
{

struct UiAnnouncement
{
	std::string text;
	bool assertive = false;   ///< data-a11y="assertive"。読み上げ中でも割り込んでよい
	std::string activity;     ///< 通知の種類の名前 (data-a11y-id)。読み上げソフト側で系統を分けられる
};

/// 1 回の走査で見つけた、画面に出ている data-a11y 付きの要素 1 つ。
struct ObservedAnnouncement
{
	std::uintptr_t element = 0;   ///< 要素を見分ける値 (比べるだけで、指す先は読まない)
	UiAnnouncement what;
};

class AnnouncementWatcher
{
public:
	/// @brief 今画面に出ている要素の一覧から、新しく出た・文字が変わった物だけを返す。
	/// @details 最初の走査 (読み上げソフトが起動した直後) は、すでに出ている文字を読み上げない。
	///          画面から消えた要素は忘れるので、同じ文字がもう一度出れば読み上げる。
	[[nodiscard]] std::vector<UiAnnouncement> observe(const std::vector<ObservedAnnouncement>& visible)
	{
		std::vector<UiAnnouncement> out;
		std::unordered_map<std::uintptr_t, std::string> next;
		next.reserve(visible.size());
		for (const ObservedAnnouncement& o : visible)
		{
			next.emplace(o.element, o.what.text);
			if (!m_primed || o.what.text.empty()) { continue; }
			const auto it = m_last.find(o.element);
			if (it == m_last.end() || it->second != o.what.text) { out.push_back(o.what); }
		}
		m_last = std::move(next);
		m_primed = true;
		return out;
	}

	/// @brief 読み上げソフトが止まった時に呼ぶ。次に動いた時は、また最初の走査から始める
	void reset() noexcept
	{
		m_last.clear();
		m_primed = false;
	}

private:
	std::unordered_map<std::uintptr_t, std::string> m_last;
	bool m_primed = false;
};

}  // namespace mitiru::ui_rml
