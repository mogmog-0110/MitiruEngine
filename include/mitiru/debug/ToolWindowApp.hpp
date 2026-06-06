#pragma once

/// @file ToolWindowApp.hpp
/// @brief 独立ツール窓 exe の共通土台 (軸 5 modular sub-window、ADR 0004 / 0014)。
/// @details
/// 動作中のゲームの SharedSnapshot (`%TEMP%\mitiru_inspector_<pid>.json`) を 30Hz で
/// polling し、Saturn 配色の header + 「waiting」状態を描く `mitiru::Game` 基底。
/// 新しい観察系ツール窓は、この基底を継承して `windowTitle()` と `drawBody()` を
/// 実装するだけでよい (arg parse / poll / stale / chrome は基底が持つ)。
///
/// 注: ファイル駆動で SharedSnapshot を読まないツール (例: .mtrr replay scrubber) は
/// 別の関心事なので、この基底を無理に使わず独自の Game 実装にする。
///
/// @code
///   class MyTool final : public mitiru::debug::ToolWindowApp {
///       const char* windowTitle() const noexcept override { return "my tool"; }
///       void drawBody(mitiru::Screen& s, const nlohmann::json& snap) override { ... }
///   };
///   int main(int argc, char** argv) {
///       auto a = mitiru::debug::parseToolArgs(argc, argv);
///       if (!a.ok) { return 2; }
///       MyTool tool(a.pid, a.file);
///       return mitiru::debug::runToolWindow(tool, "MitiruEngine — my tool");
///   }
/// @endcode

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <mitiru/Mitiru.hpp>
#include <mitiru/observe/SharedSnapshot.hpp>

namespace mitiru::debug
{

// 全ツール窓で統一する基調色 (Apple-light / クリーン、2026-06-06 刷新)。
// 純白地 + ヘアライン区切り + グレー文字 + 1 色の青アクセント。塗りバーは廃止。
inline constexpr sgc::Colorf kToolBg{1.0f, 1.0f, 1.0f, 1.0f};             // 地: 純白
inline constexpr sgc::Colorf kToolPanel{0.965f, 0.965f, 0.975f, 1.0f};    // パネル/グラフ面 (薄グレー)
inline constexpr sgc::Colorf kToolHairline{0.886f, 0.886f, 0.906f, 1.0f}; // 区切りの細線
inline constexpr sgc::Colorf kToolInk{0.106f, 0.106f, 0.118f, 1.0f};      // 見出し/本文 (濃グレー)
inline constexpr sgc::Colorf kToolMuted{0.557f, 0.557f, 0.576f, 1.0f};    // 補足 (中グレー)
inline constexpr sgc::Colorf kToolAccent{0.039f, 0.518f, 1.0f, 1.0f};     // Apple blue #0A84FF
// 後方互換 (旧 filled header は廃止。直接参照する古いコード保険)。
inline constexpr sgc::Colorf kToolHeader = kToolBg;
inline constexpr sgc::Colorf kToolHeaderText = kToolInk;

/// @brief 全ツール窓共通の Apple-light ヘッダを描く。
/// @details 左に小さな青アクセント、太めの title、下に 1px ヘアライン。title の y は
///          明示指定で上端クリップを防ぐ。SharedSnapshot を読まない窓 (replay 等) も
///          これを呼べば見た目が揃う。
/// @return body 描画を始めてよい y。
inline float drawToolHeader(mitiru::Screen& screen, const char* title, float screenW)
{
	const float padX      = 20.0f;
	const float titleTop  = 18.0f;
	const float titleSize = screenW < 420.0f ? 18.0f : 20.0f;
	// 青アクセントの小さな点 (title 左)。
	screen.drawRect(sgc::Rectf{padX, titleTop + 4.0f, 9.0f, 9.0f}, kToolAccent);
	// title 本体。
	screen.drawTextInRect(sgc::Rectf{padX + 18.0f, titleTop, screenW - padX - 18.0f, titleSize + 8.0f},
	                      title, kToolInk, titleSize,
	                      mitiru::Screen::TextAlignH::Left, mitiru::Screen::TextAlignV::Top);
	// ヘアライン区切り。
	const float lineY = titleTop + titleSize + 16.0f;
	screen.drawRect(sgc::Rectf{0.0f, lineY, screenW, 1.0f}, kToolHairline);
	return lineY + 18.0f;
}

/// @brief 独立ツール窓 (観察系) の共通土台。
class ToolWindowApp : public mitiru::Game
{
public:
	ToolWindowApp(std::optional<int> producerPid, std::optional<std::string> filePath)
		: m_producerPid(producerPid)
	{
		if (filePath) { m_overridePath = std::filesystem::path(*filePath); }
	}

	void update(float dt) override
	{
		m_pollAccum += dt;
		if (hasInput() && input().isKeyJustPressed(mitiru::KeyCode::Escape))
		{
			if (auto* eng = engine()) { eng->requestStop(); }
			return;
		}
		if (m_pollAccum < 1.0f / 30.0f) { return; }
		m_pollAccum = 0.0f;
		poll();
	}

	void draw(mitiru::Screen& screen) final
	{
		m_screenW = static_cast<float>(screen.width());
		m_screenH = static_cast<float>(screen.height());
		screen.clear(kToolBg);
		drawHeader(screen);
		if (m_state) { drawBody(screen, *m_state); }
		else { drawWaiting(screen); }
	}

	[[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) final
	{
		return {outsideW, outsideH};
	}

protected:
	/// header に出すツール名 (例 "scene tree")。"MitiruEngine — " が前置される。
	[[nodiscard]] virtual const char* windowTitle() const noexcept = 0;

	/// producer の最新 snapshot が来ている時の本体描画。
	virtual void drawBody(mitiru::Screen& screen, const nlohmann::json& snapshot) = 0;

	// ── サブクラス用 helper ──────────────────────────────────────────────
	[[nodiscard]] float screenW() const noexcept { return m_screenW; }
	[[nodiscard]] float screenH() const noexcept { return m_screenH; }
	/// body 開始 y (ヘアラインヘッダの下端 + 余白)。
	[[nodiscard]] static constexpr float bodyTop() noexcept { return 78.0f; }
	/// 左パディング (全ツール共通の左端)。
	[[nodiscard]] static constexpr float padX() noexcept { return 20.0f; }
	/// 狭い窓では 1 段小さい font (SDF atlas 整合) を返す。
	[[nodiscard]] float scaledFont(float wide, float narrow) const noexcept
	{
		return m_screenW < 420.0f ? narrow : wide;
	}

	/// 左寄せ 1 行テキストを描き、次行の y を返す（行間ゆったりめ、被り防止）。
	float line(mitiru::Screen& screen, const std::string& s, float x, float y,
	           float size, sgc::Colorf col)
	{
		screen.drawTextInRect(sgc::Rectf{x, y, m_screenW - x - padX(), size + 10.0f},
		                      s.c_str(), col, size,
		                      mitiru::Screen::TextAlignH::Left,
		                      mitiru::Screen::TextAlignV::Top);
		return y + size + 13.0f;
	}

private:
	void poll()
	{
		if (m_overridePath)
		{
			try
			{
				if (!std::filesystem::exists(*m_overridePath)) { m_state = std::nullopt; return; }
				auto mt = std::filesystem::last_write_time(*m_overridePath);
				if (m_haveMtime && mt == m_lastMtime) { return; }
				std::ifstream in(*m_overridePath, std::ios::binary);
				if (!in) { return; }
				m_state = nlohmann::json::parse(in);
				m_lastMtime = mt;
				m_haveMtime = true;
			}
			catch (...) { /* 読み取り途中の truncate; 次 tick で retry */ }
			return;
		}
		if (!m_reader && m_producerPid) { m_reader.emplace(*m_producerPid); }
		if (!m_reader) { return; }
		if (auto j = m_reader->tryRead()) { m_state = std::move(*j); }
	}

	void drawHeader(mitiru::Screen& screen)
	{
		drawToolHeader(screen, windowTitle(), m_screenW);
	}

	void drawWaiting(mitiru::Screen& screen)
	{
		// Latin atlas のため ASCII 表示。
		const char* msg = (m_overridePath || m_producerPid)
			? "waiting for the game..."
			: "no source - pass <pid> or --file <path>";
		screen.drawTextInRect(sgc::Rectf{padX(), bodyTop(), m_screenW - padX() * 2.0f, 28.0f},
		                      msg, kToolMuted, 16.0f,
		                      mitiru::Screen::TextAlignH::Left,
		                      mitiru::Screen::TextAlignV::Top);
	}

	std::optional<int>                                m_producerPid;
	std::optional<std::filesystem::path>              m_overridePath;
	std::optional<mitiru::observe::SharedSnapshot::Reader> m_reader;
	std::optional<nlohmann::json>                     m_state;
	std::filesystem::file_time_type                   m_lastMtime{};
	bool                                              m_haveMtime{false};
	float                                             m_pollAccum{0.0f};
	float                                             m_screenW{360.0f};
	float                                             m_screenH{720.0f};
};

/// @brief 共通 arg parse: `<pid>` か `--file <path>`。ok=false なら usage 出力済みで終了すべき。
struct ToolArgs
{
	std::optional<int>         pid;
	std::optional<std::string> file;
	bool                       ok{true};
};

inline ToolArgs parseToolArgs(int argc, char* argv[])
{
	ToolArgs out;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--file" && i + 1 < argc) { out.file = argv[++i]; }
		else if (a == "-h" || a == "--help") { out.ok = false; }
		else
		{
			try { out.pid = std::stoi(a); }
			catch (...) { out.ok = false; }
		}
	}
	if (!out.pid && !out.file)
	{
		std::fputs("usage: <tool> <pid> | <tool> --file <path>\n", stderr);
		out.ok = false;
	}
	return out;
}

/// @brief 任意の mitiru::Game をツール窓 chrome (Saturn / 縦窓 / Latin font) で run する。
/// @details SharedSnapshot に乗らないツール (例 .mtrr replay scrubber) もこの bootstrap を
///          共有して、全ツール窓の見た目を統一する。
inline int runToolGame(mitiru::Game& game, const std::string& title,
                       int width = 360, int height = 720)
{
	mitiru::Engine engine;
	mitiru::EngineConfig cfg;
	cfg.title                = title.c_str();   // run() は同期。title は呼び出し中 生存。
	cfg.windowWidth          = width;
	cfg.windowHeight         = height;
	cfg.minWindowWidth       = 320;
	cfg.minWindowHeight      = 360;
	cfg.vsync                = true;
	cfg.enableCef            = false;
	cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
	cfg.useLogicalWindowSize = true;   // high-DPI で文字を crisp に保つ
	cfg.backgroundColor      = kToolBg;
	engine.run(game, cfg);
	return 0;
}

/// @brief 共通ブートストラップ: ToolWindowApp (観察系) 用の薄い別名。
inline int runToolWindow(ToolWindowApp& app, const std::string& title,
                         int width = 360, int height = 720)
{
	return runToolGame(app, title, width, height);
}

}  // namespace mitiru::debug
