// 初回起動 (projects.json が存在しない) に限り、zip 同梱の sample DLL を
// 一覧へ自動登録するための純ロジック。Win32 に依存しないので単体テストできる。
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

/// hostExeDir/<sampleDirName>/<sampleDirName>.dll が実在すれば返す。
/// 実行ファイル配置規約 (CMakeLists.txt の "<host_dir>/<sample>/<sample>.dll")
/// に合わせているだけで、副作用は無い。
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
