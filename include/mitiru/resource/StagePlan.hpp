#pragma once

/// @file StagePlan.hpp
/// @brief ステージを切り替えるときに、手放す資産と先に読む資産を決める
/// @details 資産の一覧は 1 行 1 つのテキストで書く。`#` から後は読まない。種類を決めたいときは行の頭に
///          `model:` (glTF / FBX をスキンのモデルとして)、`clod:` (drawModel の世界のモデル)、`sound:` (音の id) を付ける。
///          付けなければ拡張子で決める (docs/STREAMING.md)。両方のステージにある資産は手放さず、読み直しもしない。

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::resource
{

struct StagePlan
{
	std::vector<std::string> release;   ///< 今のステージにだけある資産 (書いた順)
	std::vector<std::string> load;      ///< 次のステージにだけある資産 (書いた順)
};

[[nodiscard]] inline StagePlan planStageSwitch(const std::vector<std::string>& current, const std::vector<std::string>& next)
{
	const auto has = [](const std::vector<std::string>& list, const std::string& s) {
		return std::find(list.begin(), list.end(), s) != list.end();
	};
	StagePlan plan;
	for (const auto& s : current)
	{
		if (!has(next, s) && !has(plan.release, s)) { plan.release.push_back(s); }
	}
	for (const auto& s : next)
	{
		if (!has(current, s) && !has(plan.load, s)) { plan.load.push_back(s); }
	}
	return plan;
}

/// @brief 一覧のテキストを行に分ける。前後の空白と `#` から後を落とし、空の行は飛ばす
[[nodiscard]] inline std::vector<std::string> parseAssetList(std::string_view text)
{
	std::vector<std::string> out;
	std::size_t pos = 0;
	while (pos <= text.size())
	{
		const std::size_t end = std::min(text.find('\n', pos), text.size());
		std::string_view line = text.substr(pos, end - pos);
		line = line.substr(0, std::min(line.find('#'), line.size()));
		const auto first = line.find_first_not_of(" \t\r");
		if (first != std::string_view::npos)
		{
			const auto last = line.find_last_not_of(" \t\r");
			out.emplace_back(line.substr(first, last - first + 1));
		}
		pos = end + 1;
	}
	return out;
}

/// @brief 行の頭の種類 (`model:` など) と残りのパス。種類が無ければ kind は空
struct AssetListEntry
{
	std::string_view kind;
	std::string_view path;
};

[[nodiscard]] inline AssetListEntry splitAssetKind(std::string_view entry) noexcept
{
	for (const std::string_view kind : {std::string_view("model"), std::string_view("clod"), std::string_view("sound")})
	{
		if (entry.size() > kind.size() && entry.substr(0, kind.size()) == kind && entry[kind.size()] == ':')
		{
			std::string_view rest = entry.substr(kind.size() + 1);
			rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
			return {kind, rest};
		}
	}
	return {{}, entry};
}

} // namespace mitiru::resource
