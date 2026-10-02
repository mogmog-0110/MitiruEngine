#pragma once

/// @file AssetPackSet.hpp
/// @brief 複数の .mtpak を依存順に重ね、1 つの VFS として読む PackSet。
///
/// v2 pack は、自分が依存する pack 名 (dependsOn) を持てる。PackSet::resolve は root pack を
/// 開き、dependsOn を rootFile と同じディレクトリで再帰的に解決して積み上げる。
/// 検索では「後から積んだ pack が優先」= root が最優先となり、依存の依存ほど優先度が低い。

#include <mitiru/asset/AssetPack.hpp>

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace mitiru::vfs
{

class PackSet
{
public:
	/// rootFile を開き、dependsOn を rootFile と同じディレクトリから再帰的に解決する。
	/// 循環依存や同名の重複解決は 1 回だけに抑える。見つからない依存は通知せず無視する
	/// (壊れた配布物でも root 自体は読めるようにするため)。root を開けなければ nullopt。
	[[nodiscard]] static std::optional<PackSet> resolve(const std::filesystem::path& rootFile)
	{
		auto root = AssetPack::open(rootFile);
		if (!root) { return std::nullopt; }

		PackSet set;
		std::unordered_set<std::string> visited;
		// root 自身をあらかじめ visited に入れておく。依存が root 名へ戻る場合
		// (直接の自己参照や A→B→A)、これが無いと root が「依存」として再度 open
		// されて out へ積まれた上で、末尾に本来の root がもう一度積まれ、二重登録になる。
		visited.insert(rootFile.stem().string());
		collectDeps(*root, rootFile.parent_path(), visited, set.m_packs);
		set.m_packs.push_back(std::move(*root));  // root は最後に積む = 最優先
		return set;
	}

	/// 後から積んだ pack (= root に近い側) を優先して読む。
	[[nodiscard]] std::optional<std::vector<uint8_t>> read(std::string_view path) const
	{
		for (auto it = m_packs.rbegin(); it != m_packs.rend(); ++it)
		{
			if (auto data = it->read(path)) { return data; }
		}
		return std::nullopt;
	}

	[[nodiscard]] bool contains(std::string_view path) const
	{
		for (const auto& p : m_packs)
		{
			if (p.contains(path)) { return true; }
		}
		return false;
	}

	[[nodiscard]] std::size_t packCount() const noexcept { return m_packs.size(); }

private:
	std::vector<AssetPack> m_packs;  // 先頭 = 依存の末端、末尾 = root (最優先)

	static void collectDeps(const AssetPack& pack, const std::filesystem::path& dir,
	                        std::unordered_set<std::string>& visited, std::vector<AssetPack>& out)
	{
		for (const auto& name : pack.dependsOn())
		{
			if (!visited.insert(name).second) { continue; }  // 循環 / 重複を回避
			std::filesystem::path depFile = dir / name;
			if (depFile.extension() != ".mtpak") { depFile += ".mtpak"; }
			auto dep = AssetPack::open(depFile);
			if (!dep) { continue; }
			collectDeps(*dep, dir, visited, out);  // 依存の依存を先に積む (root ほど後)
			out.push_back(std::move(*dep));
		}
	}
};

}  // namespace mitiru::vfs
