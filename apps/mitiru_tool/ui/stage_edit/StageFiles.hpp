#pragma once
// stage_edit が読み書きするファイル 1 つ。ゲームが読む写し (deployed、--watch-assets が見る場所) と、
// プロジェクトの元ファイル (source、git で管理する場所) の 2 つを扱う。
//   試す   = deployed だけに書く。ゲームが asset.reloaded で読み直す。元ファイルは触らない
//   残す   = 元ファイルに書き (書く前を隠しファイル .<名前>.bak に残す)、deployed にも書く
//   捨てる = deployed を元ファイルの内容へ戻す
// ゲームの論理パス ("ooze_drive/assets/stages/r1_run.json") から場所を探す。

#include "EditSchema.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool::stageedit
{

class StageFiles
{
public:
	/// @param logical ゲームが読む論理パス
	/// @param projectRootHint 空なら MITIRU_PROJECT_ROOT か、cwd から上へ mitiru.toml を探す
	bool open(const std::string& logical, const SourceMap& map, const std::string& projectRootHint, std::string& error);

	/// 元ファイルがあればその内容、無ければ deployed の内容
	[[nodiscard]] const std::string& original() const noexcept { return m_original; }
	[[nodiscard]] bool hasSource() const noexcept { return m_source.has_value(); }
	[[nodiscard]] const std::filesystem::path& deployedPath() const noexcept { return m_deployed; }
	[[nodiscard]] const std::optional<std::filesystem::path>& sourcePath() const noexcept { return m_source; }
	/// deployed が元ファイルと違う (前の「試す」が残っている)
	[[nodiscard]] bool deployedDiffers() const;
	[[nodiscard]] const std::string& logical() const noexcept { return m_logical; }

	bool tryOut(const std::string& text, std::string& error);
	bool keep(const std::string& text, std::string& error);
	bool restore(std::string& error);

	[[nodiscard]] static std::optional<std::filesystem::path> findProjectRoot(const std::string& hint);
	/// ゲームが読む論理パスのフォルダにある JSON のファイル名 (隠しファイルを除く、名前順)
	[[nodiscard]] static std::vector<std::string> listJson(const std::string& dirLogical);

private:
	[[nodiscard]] static bool readAll(const std::filesystem::path& path, std::string& out);
	[[nodiscard]] static bool writeAtomic(const std::filesystem::path& path, const std::string& text, std::string& error);

	std::string m_logical;
	std::filesystem::path m_deployed;
	std::optional<std::filesystem::path> m_source;
	std::string m_original;
};

} // namespace mitiru::tool::stageedit
