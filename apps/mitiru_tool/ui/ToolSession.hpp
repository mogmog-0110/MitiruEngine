#pragma once
// ツール窓 1 枚の中身: ページ・RmlUi・データの出どころ (SharedSnapshot / ScrubControl / HTTP) をまとめる。
// 窓の有る無しを知らないので、本物の窓 (main.cpp) と windowless の撮影・テストで同じものを使う。

#include "HttpWorker.hpp"
#include "ToolPage.hpp"
#include "ToolUiHost.hpp"

#include <mitiru/observe/SharedSnapshot.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace mitiru::tool
{

struct ToolOptions
{
	std::string page = "perf";
	std::string query;                          ///< "scene?tab=memory" の "tab=memory"
	std::optional<int> pid;                     ///< 監視するゲーム (host) のプロセス
	std::optional<std::filesystem::path> file;  ///< snapshot の JSON を直接読む
	std::optional<std::string> mtrr;            ///< replay で開く録画
	int httpPort = 8090;                        ///< EngineHttpServer の既定と同じ
	std::filesystem::path document;             ///< ページの RML
};

/// SharedSnapshot のファイルを、書き換わった時だけ読む。書かれた順を保って読む。
class SnapshotSource
{
public:
	explicit SnapshotSource(std::filesystem::path path) : m_path(std::move(path)) {}
	[[nodiscard]] std::optional<Snapshot> poll();

private:
	std::filesystem::path m_path;
	std::filesystem::file_time_type m_lastWrite{};
	bool m_haveWrite = false;
};

class ToolSession
{
public:
	ToolSession() = default;
	ToolSession(const ToolSession&) = delete;
	ToolSession& operator=(const ToolSession&) = delete;

	/// @param keyboard 本物の窓だけが渡す (rewind のページへ届く)。session より長く生きること
	bool start(ID3D12Device* device, ID3D12CommandQueue* queue, const ToolOptions& options,
	           int width, int height, float dpRatio, std::string& error, ToolKeyboard* keyboard = nullptr);

	/// 1 フレーム分: snapshot (30Hz) と HTTP の応答を読み、入力を渡し、UI を進める。
	void frame(double now, const ToolPointer& pointer, std::span<const platform::Win32KeyMessage> keys);
	void render(ID3D12Resource* target, int width, int height) { m_ui.render(target, width, height); }
	void resize(int width, int height, float dpRatio) { m_ui.resize(width, height, dpRatio); }
	/// 描かないフレーム (最小化中) も読み手でいると知らせる。host は読み手が消えると巻き戻しの停止を解く
	void keepWatching() { if (m_beacon) { m_beacon->touch(m_everRead); } }

	[[nodiscard]] ToolUiHost& ui() noexcept { return m_ui; }
	[[nodiscard]] ToolPage* page() noexcept { return m_page.get(); }

private:
	void pollSnapshot(double now);

	ToolUiHost m_ui;
	std::unique_ptr<ToolSignals> m_signals;
	std::unique_ptr<HttpWorker> m_http;
	std::unique_ptr<ToolPage> m_page;
	std::optional<SnapshotSource> m_source;
	std::optional<observe::SnapshotWatchBeacon> m_beacon;   ///< 見ているゲームに、読み手がいると知らせる
	double m_nextPoll = 0.0;
	bool m_everRead = false;
};

/// 同梱ページ (exe の隣の assets/<page>.rml) か、ゲームが自分で置いたページ
/// (MITIRU_ASSET_ROOT/assets/<page>.rml) の RML の場所。無ければ空。
[[nodiscard]] std::filesystem::path findPageDocument(const std::filesystem::path& exeDir, const std::string& page);

} // namespace mitiru::tool
