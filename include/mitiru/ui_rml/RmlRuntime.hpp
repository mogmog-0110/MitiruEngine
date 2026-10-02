#pragma once
// RmlUi はプロセスに 1 つの状態 (時計・ファイル窓口・読み込んだ書体) を持つので、それをここで 1 回だけ用意する。
// 時計はエンジンが毎フレーム渡す値をそのまま返す (壁時計を読まないので replay で UI の遷移まで同じになる)。

#include <cstddef>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace mitiru::ui_rml
{

class RmlRuntime
{
public:
	static RmlRuntime& instance();

	[[nodiscard]] bool ready() const noexcept { return m_ready; }
	void setTime(double seconds) noexcept;
	[[nodiscard]] double time() const noexcept;
	/// RmlUi が出した警告と誤り (RCSS の構文の誤りなど) の数。読み込みの前後で比べる
	[[nodiscard]] std::size_t warningCount() const noexcept;

	/// エンジン同梱の書体と、文書の近くの fonts/ (assets/ui/fonts, assets/fonts) にある書体を読む。
	/// 読み済みのファイルは飛ばす。同梱の丸ゴシックは "M PLUS Rounded 1c" (400 / 700 / 900) で引ける。
	void loadFonts(const std::filesystem::path& documentDir);

	[[nodiscard]] std::string nextContextName();

	/// RCSS / RML から href="mitiru:base.rcss" で読める、エンジン同梱の UI 部品の置き場。
	[[nodiscard]] const std::filesystem::path& engineUiDir() const noexcept { return m_engineUiDir; }

	RmlRuntime(const RmlRuntime&) = delete;
	RmlRuntime& operator=(const RmlRuntime&) = delete;

	struct BundledFace;

private:
	RmlRuntime();
	~RmlRuntime();
	bool loadFont(const std::filesystem::path& file, const BundledFace* bundled);

	struct Interfaces;
	std::unique_ptr<Interfaces> m_interfaces;
	std::set<std::string> m_loadedFonts;
	std::filesystem::path m_engineFontDir;
	std::filesystem::path m_engineUiDir;
	int m_contextSerial = 0;
	bool m_ready = false;
};

} // namespace mitiru::ui_rml
