#pragma once

/// @file InspectAssetsHost.hpp
/// @brief DLL が `MITIRU_INSPECT_ASSETS` で渡す資産 (木の JSON、ナビメッシュ) を host が写し、ツール窓へ見せる
/// @details 中身はツール窓の snapshot の隣のフォルダにファイルで置き、snapshot には小さな一覧 (種類・名前・ファイル・大きさ)
///          だけを載せる。毎フレーム書く snapshot に何百 KB のナビメッシュを混ぜないため。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::module
{

/// DLL が壊れた大きさを返しても host のメモリを使い切らない上限
inline constexpr std::uint64_t kMaxInspectAssetBytes = 64ull * 1024ull * 1024ull;

struct CopiedInspectAsset
{
	std::string kind;
	std::string name;
	std::vector<std::uint8_t> bytes;
};

namespace detail
{
template <std::size_t N>
[[nodiscard]] std::string boundedText(const char (&s)[N])
{
	std::size_t n = 0;
	while (n < N && s[n] != '\0') { ++n; }
	return std::string(s, n);
}

/// ファイル名に使う種類。英数字と _ 以外は _ にする (DLL の文字列をそのままパスにしない)
[[nodiscard]] inline std::string fileSafe(const std::string& s)
{
	std::string out = s.empty() ? std::string("asset") : s;
	for (char& c : out)
	{
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
		if (!ok) { c = '_'; }
	}
	return out;
}
} // namespace detail

/// @brief fn が返す一覧を写す。中身の無いもの、大きすぎるものは落とす
[[nodiscard]] inline std::vector<CopiedInspectAsset> copyInspectAssets(ModuleInspectAssetsFn fn)
{
	std::vector<CopiedInspectAsset> out;
	if (fn == nullptr) { return out; }
	InspectAsset raw[kMaxInspectAssets]{};
	const int n = std::clamp(static_cast<int>(fn(raw, kMaxInspectAssets)), 0, kMaxInspectAssets);
	for (int i = 0; i < n; ++i)
	{
		const InspectAsset& a = raw[i];
		if (a.data == nullptr || a.size == 0 || a.size > kMaxInspectAssetBytes) { continue; }
		const auto* p = static_cast<const std::uint8_t*>(a.data);
		out.push_back({detail::boundedText(a.kind), detail::boundedText(a.name), std::vector<std::uint8_t>(p, p + a.size)});
	}
	return out;
}

/// @brief 写した資産をフォルダへ置き、snapshot の "assets" に載せる一覧を作る。フォルダは手放すときに消す
class PublishedInspectAssets
{
public:
	/// generation はファイル名の頭に付ける。写し替えるたびに名前が変わるので、ツール窓は名前の違いで読み直す
	PublishedInspectAssets(const std::vector<CopiedInspectAsset>& assets, std::filesystem::path dir, std::uint32_t generation = 0)
		: m_dir(std::move(dir))
	{
		std::error_code ec;
		std::filesystem::remove_all(m_dir, ec);
		if (assets.empty() || !std::filesystem::create_directories(m_dir, ec)) { return; }
		for (std::size_t i = 0; i < assets.size(); ++i)
		{
			const CopiedInspectAsset& a = assets[i];
			const auto file = m_dir / (std::to_string(generation) + "_" + std::to_string(i) + "_" + detail::fileSafe(a.kind) + ".bin");
			std::ofstream f(file, std::ios::binary);
			f.write(reinterpret_cast<const char*>(a.bytes.data()), static_cast<std::streamsize>(a.bytes.size()));
			if (!f) { continue; }
			const std::u8string u = file.generic_u8string();
			m_list.push_back({{"kind", a.kind}, {"name", a.name}, {"file", std::string(u.begin(), u.end())},
			                  {"size", a.bytes.size()}});
		}
	}

	~PublishedInspectAssets()
	{
		std::error_code ec;
		std::filesystem::remove_all(m_dir, ec);
	}

	PublishedInspectAssets(const PublishedInspectAssets&) = delete;
	PublishedInspectAssets& operator=(const PublishedInspectAssets&) = delete;

	/// 種類・名前・ファイル (UTF-8 のパス)・大きさの配列
	[[nodiscard]] const nlohmann::json& list() const noexcept { return m_list; }

private:
	std::filesystem::path m_dir;
	nlohmann::json m_list = nlohmann::json::array();
};

/// @brief snapshot (snapshotPath) の隣に置く資産のフォルダ
[[nodiscard]] inline std::filesystem::path inspectAssetDirFor(const std::filesystem::path& snapshotPath)
{
	std::filesystem::path dir = snapshotPath;
	dir.replace_extension();
	dir += "_assets";
	return dir;
}

} // namespace mitiru::module
