// 初回起動時 (projects.json が存在しない場合) に限り、zip に同梱された sample DLL を
// 一覧へ自動登録するための純粋なロジック。Win32 に依存しないため、単体テストができる。
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace mitiru_launcher
{

struct BundledSample
{
	std::string           name;
	std::filesystem::path dllPath;
};

/// hostExeDir/<sampleDirName>/<sampleDirName>.dll が存在すれば返す。
/// 実行ファイルの配置規約 (CMakeLists.txt の "<host_dir>/<sample>/<sample>.dll")
/// に合わせているだけで、副作用はない。
inline std::optional<BundledSample> findBundledSample(
	const std::filesystem::path& hostExeDir, const std::string& sampleDirName)
{
	if (hostExeDir.empty()) { return std::nullopt; }
	const auto dll = hostExeDir / sampleDirName / (sampleDirName + ".dll");
	std::error_code ec;
	if (!std::filesystem::exists(dll, ec) || ec) { return std::nullopt; }
	return BundledSample{sampleDirName, dll};
}

} // namespace mitiru_launcher
