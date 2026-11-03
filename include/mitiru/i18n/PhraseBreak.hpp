#pragma once

/// @file PhraseBreak.hpp
/// @brief 日本語の文を文節で区切る (BudouX の移植)。折り返しを文節の境目で起こすための印を打つ。
/// @details 点数は BudouX (google/budoux、Apache-2.0) の表で、上流の Python と同じ境目を返す
///          (tests/fixtures/budoux_ja_phrases.txt、1000 文)。計算は整数だけなので、同じ文は
///          どのビルド・どの機種でも同じ所で切れる。上流は基準点を -合計/2 (float) で持つが、
///          全部を 2 倍にして「2 × 点数の和 > 合計」で判定し、同じ結果を整数で出す。
///
/// 使い方 (docs/PHRASE_BREAK.md):
/// @code
/// const auto& ja = mitiru::i18n::PhraseModel::japanese();
/// std::string marked = ja.marked(text);          // 境目に U+200B を挟む。drawTextWrapped がそこで折る
/// ja.forEachPhrase(text, [](std::string_view p) { ... });
/// @endcode
/// RML では <phrase>…</phrase> (ui_rml/RmlPhraseElement.hpp)。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/i18n/LineBreak.hpp>
#include <mitiru/i18n/Utf8Text.hpp>
#include <mitiru/i18n/detail/BudouxJaData.hpp>

namespace mitiru::i18n
{

/// @brief 文節の境目に挟む印 (U+200B ZERO WIDTH SPACE) の UTF-8。字送り 0 で画面には出ない。
inline constexpr std::string_view kPhraseMarkUtf8 = "\xE2\x80\x8B";

class PhraseModel
{
public:
	/// @brief 同梱の日本語の表 (external/budoux/ja.json を tools/gen_budoux_model.py で写した物)。
	[[nodiscard]] static const PhraseModel& japanese()
	{
		static const PhraseModel model = fromTable(detail::kBudouxJaScores,
			sizeof(detail::kBudouxJaScores) / sizeof(detail::kBudouxJaScores[0]), detail::kBudouxJaTotal);
		return model;
	}

	/// @brief BudouX の models/*.json (group → {特徴: 点数}) から作る。別の言語や自分で学習した表に。
	[[nodiscard]] static std::optional<PhraseModel> fromJson(std::string_view json, std::string& error)
	{
		nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
		if (!j.is_object())
		{
			error = "phrase model: top level must be an object of groups";
			return std::nullopt;
		}
		PhraseModel m;
		for (const auto& [name, table] : j.items())
		{
			const int group = groupIndex(name);
			if (group < 0 || !table.is_object())
			{
				error = "phrase model: unknown group or non-object table '" + name + "'";
				return std::nullopt;
			}
			for (const auto& [key, score] : table.items())
			{
				// 上流の表は数千が上限。桁を抑えておけば、合計・2 倍・13 特徴の和が int64 で溢れない
				if (!score.is_number_integer() || score.get<std::int64_t>() > kMaxScore || score.get<std::int64_t>() < -kMaxScore)
				{
					error = "phrase model: score of '" + name + "." + key + "' is not an integer within +-1e12";
					return std::nullopt;
				}
				m.add(static_cast<std::uint8_t>(group), key, score.get<std::int64_t>());
			}
		}
		return m;
	}

	[[nodiscard]] bool empty() const noexcept { return m_scores.empty(); }

	/// @brief 文節の境目をバイト位置の昇順で out に足す (0 と utf8.size() は含まない)。
	void boundaries(std::string_view utf8, std::vector<std::size_t>& out) const
	{
		std::vector<std::size_t> offsets;
		offsets.reserve(utf8.size() + 1);
		for (std::size_t i = 0; i < utf8.size();)
		{
			int len = 1;
			static_cast<void>(decodeAt(utf8, i, len));
			offsets.push_back(i);
			i += static_cast<std::size_t>(len);
		}
		offsets.push_back(utf8.size());
		// offsets[i] は i 文字目の先頭。上流の sentence[i] と同じ添字で点数を引く
		const std::size_t n = offsets.size() - 1;
		const auto at = [&](std::uint8_t group, std::size_t from, std::size_t to) {
			return score(group, utf8.substr(offsets[from], offsets[to] - offsets[from]));
		};
		for (std::size_t i = 1; i < n; ++i)
		{
			std::int64_t doubled = -m_total;
			if (i > 2) { doubled += at(0, i - 3, i - 2); }
			if (i > 1) { doubled += at(1, i - 2, i - 1); }
			doubled += at(2, i - 1, i);
			doubled += at(3, i, i + 1);
			if (i + 1 < n) { doubled += at(4, i + 1, i + 2); }
			if (i + 2 < n) { doubled += at(5, i + 2, i + 3); }
			if (i > 1) { doubled += at(6, i - 2, i); }
			doubled += at(7, i - 1, i + 1);
			if (i + 1 < n) { doubled += at(8, i, i + 2); }
			if (i > 2) { doubled += at(9, i - 3, i); }
			if (i > 1) { doubled += at(10, i - 2, i + 1); }
			if (i + 1 < n) { doubled += at(11, i - 1, i + 2); }
			if (i + 2 < n) { doubled += at(12, i, i + 3); }
			if (doubled > 0) { out.push_back(offsets[i]); }
		}
	}

	[[nodiscard]] std::vector<std::size_t> boundaries(std::string_view utf8) const
	{
		std::vector<std::size_t> out;
		boundaries(utf8, out);
		return out;
	}

	/// @brief 文節ごとに fn(std::string_view) を呼ぶ。空の文では呼ばない。
	template <class Fn>
	void forEachPhrase(std::string_view utf8, Fn&& fn) const
	{
		std::vector<std::size_t> cuts;
		boundaries(utf8, cuts);
		std::size_t from = 0;
		for (const std::size_t cut : cuts)
		{
			fn(utf8.substr(from, cut - from));
			from = cut;
		}
		if (from < utf8.size()) { fn(utf8.substr(from)); }
	}

	/// @brief 境目に separator を挟んだ文字列。
	[[nodiscard]] std::string joined(std::string_view utf8, std::string_view separator) const
	{
		std::string out;
		out.reserve(utf8.size() + separator.size() * 8);
		bool first = true;
		forEachPhrase(utf8, [&](std::string_view phrase) {
			if (!first) { out.append(separator); }
			out.append(phrase);
			first = false;
		});
		return out;
	}

	/// @brief 境目に U+200B を挟む。改行はそのまま残る。
	[[nodiscard]] std::string marked(std::string_view utf8) const { return joined(utf8, kPhraseMarkUtf8); }

private:
	static constexpr std::int64_t kMaxScore = 1'000'000'000'000;

	struct KeyHash
	{
		using is_transparent = void;
		[[nodiscard]] std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	};

	static PhraseModel fromTable(const detail::BudouxScore* rows, std::size_t count, std::int64_t total)
	{
		PhraseModel m;
		m.m_scores.reserve(count);
		for (std::size_t i = 0; i < count; ++i) { m.add(rows[i].group, rows[i].key, rows[i].score, false); }
		m.m_total = total;
		return m;
	}

	[[nodiscard]] static int groupIndex(std::string_view name) noexcept
	{
		constexpr std::string_view kNames[] = {"UW1", "UW2", "UW3", "UW4", "UW5", "UW6",
		                                       "BW1", "BW2", "BW3", "TW1", "TW2", "TW3", "TW4"};
		for (int i = 0; i < 13; ++i) { if (kNames[i] == name) { return i; } }
		return -1;
	}

	void add(std::uint8_t group, std::string_view key, std::int64_t score, bool accumulate = true)
	{
		std::string k;
		k.reserve(key.size() + 1);
		k.push_back(static_cast<char>(group));
		k.append(key);
		m_scores[std::move(k)] = score;
		if (accumulate) { m_total += score; }
	}

	/// 2 倍した点数。特徴が表に無ければ 0。鍵は group の 1 byte + UTF-8 (3 文字でも 13 byte に収まる)。
	[[nodiscard]] std::int64_t score(std::uint8_t group, std::string_view feature) const noexcept
	{
		char buf[16];
		if (feature.size() + 1 > sizeof(buf)) { return 0; }
		buf[0] = static_cast<char>(group);
		feature.copy(buf + 1, feature.size());
		const auto it = m_scores.find(std::string_view(buf, feature.size() + 1));
		return it == m_scores.end() ? 0 : it->second * 2;
	}

	std::unordered_map<std::string, std::int64_t, KeyHash, std::equal_to<>> m_scores;
	std::int64_t m_total = 0;  // 全点数の和。上流の基準点 -合計/2 を 2 倍の整数で扱う
};

} // namespace mitiru::i18n
