#include <mitiru/ui_rml/RmlRuntime.hpp>

#include <mitiru/ui_rml/RmlBinderElements.hpp>
#include <mitiru/ui_rml/RmlFastOutline.hpp>
#include <mitiru/ui_rml/RmlSparkElement.hpp>
#include <mitiru/ui_rml/RmlTranslation.hpp>
#include <mitiru/ui_rml/RmlUiHost.hpp>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/platform/IClipboard.hpp>
#include <mitiru/text/UiFontFiles.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::ui_rml
{

namespace
{

namespace fs = std::filesystem;

constexpr std::string_view kEngineScheme = "mitiru:";
constexpr std::string_view kGlyphScheme = "glyph:";

} // namespace

// 同梱の丸ゴシックはファイルに入っている名前が太さごとに違う ("Rounded Mplus 1c" と "... Bold")。
// RCSS からは 1 つの名前と font-weight で選べるよう、名前を揃えて読む。Regular は代替書体も兼ね、
// 書体の指定に無い字 (仮名・漢字) をこれで出す。配布物 (mitiru_host の隣) には Regular と Bold だけを置く。
struct RmlRuntime::BundledFace
{
	const char* file;
	const char* family;
	int weight;
	bool fallback;
};

namespace
{

constexpr RmlRuntime::BundledFace kBundledFaces[] = {
	{ text::kUiFallbackFont, "M PLUS Rounded 1c", 400, true },
	{ "MPLUSRounded1c-Bold.ttf", "M PLUS Rounded 1c", 700, false },
	{ "MPLUSRounded1c-Black.ttf", "M PLUS Rounded 1c", 900, false },
};

fs::path fromUtf8(const Rml::String& s)
{
	return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string toUtf8(const fs::path& p)
{
	const std::u8string u = p.generic_u8string();
	return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// RmlUi 既定のファイル窓口は fopen で、日本語を含むパスを開けない。
class Utf8FileInterface final : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String& path) override
	{
#ifdef _WIN32
		FILE* f = _wfopen(fromUtf8(path).c_str(), L"rb");
#else
		FILE* f = std::fopen(path.c_str(), "rb");
#endif
		return reinterpret_cast<Rml::FileHandle>(f);
	}
	void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<FILE*>(file)); }
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
	{
		return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
	}
	bool Seek(Rml::FileHandle file, long offset, int origin) override
	{
		return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
	}
	size_t Tell(Rml::FileHandle file) override
	{
		return static_cast<size_t>(std::ftell(reinterpret_cast<FILE*>(file)));
	}
};

class SystemBridge final : public Rml::SystemInterface
{
public:
	double GetElapsedTime() override { return time; }

	int TranslateString(Rml::String& translated, const Rml::String& input) override
	{
		if (translator == nullptr)
		{
			translated = input;
			return 0;
		}
		return translateRmlText(*translator, input, translated);
	}

	// OS のクリップボードが無い・読めないときも、この process の中のコピーと貼り付けは通す
	void SetClipboardText(const Rml::String& text) override
	{
		localClipboard = text;
		// 書けなかったときは、直後の貼り付けで OS に残っていた古い中身が出ないよう、1 回だけ手元の写しを返す
		osMissedCopy = clipboard != nullptr && !clipboard->setText(text);
	}

	void GetClipboardText(Rml::String& text) override
	{
		if (clipboard != nullptr && !std::exchange(osMissedCopy, false))
		{
			if (std::optional<std::string> os = clipboard->getText())
			{
				text = std::move(*os);
				return;
			}
		}
		text = localClipboard;
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
	{
		if (type <= Rml::Log::LT_WARNING)
		{
			++warnings;
			console::notice("UI の RML か RCSS に直すところがあるので、該当するファイルを確かめてください (" + message + ")");
		}
		else
		{
			console::verbose("RmlUi の報告です (" + message + ")。");
		}
		return true;
	}

	void JoinPath(Rml::String& out, const Rml::String& documentPath, const Rml::String& path) override
	{
		if (path.rfind(kView3DImageScheme, 0) == 0)
		{
			out = path;   // 文書のフォルダを前に付けない (RenderInterface が副ビューの番号として読む)
			return;
		}
		if (path.rfind(kEngineScheme, 0) == 0)
		{
			out = toUtf8(engineUiDir / fromUtf8(path.substr(kEngineScheme.size())));
			return;
		}
		if (path.rfind(kGlyphScheme, 0) == 0)
		{
			out = toUtf8(glyphFile(std::string_view(path).substr(kGlyphScheme.size())));
			return;
		}
		Rml::SystemInterface::JoinPath(out, documentPath, path);
	}

	// 割り当てが無い操作は透明な絵にして、読めない画像の警告を毎回出さない。
	[[nodiscard]] fs::path glyphFile(std::string_view name) const
	{
		const std::string glyph = glyphName != nullptr ? glyphName(glyphCtx, name) : std::string();
		std::error_code ec;
		const fs::path file = fromUtf8(glyph + ".png");
		if (!glyph.empty() && !gameGlyphDir.empty() && fs::exists(gameGlyphDir / file, ec)) { return gameGlyphDir / file; }
		if (!glyph.empty() && fs::exists(engineGlyphDir / file, ec)) { return engineGlyphDir / file; }
		return engineGlyphDir / "none.png";
	}

	double time = 0.0;
	std::size_t warnings = 0;
	platform::IClipboard* clipboard = nullptr;
	std::string localClipboard;
	bool osMissedCopy = false;
	fs::path engineUiDir;
	fs::path engineGlyphDir;
	fs::path gameGlyphDir;
	const LocalizationManager* translator = nullptr;
	GlyphNameFn glyphName = nullptr;
	void* glyphCtx = nullptr;
};

} // namespace

struct RmlRuntime::Interfaces
{
	SystemBridge system;
	Utf8FileInterface files;
};

RmlRuntime& RmlRuntime::instance()
{
	static RmlRuntime runtime;
	return runtime;
}

RmlRuntime::RmlRuntime()
	: m_interfaces(std::make_unique<Interfaces>())
{
	m_engineFontDir = text::engineFontDir();
	m_engineUiDir = text::findEngineDir("assets/ui", "base.rcss");
	m_interfaces->system.engineUiDir = m_engineUiDir;
	m_interfaces->system.engineGlyphDir = text::findEngineDir("assets/glyphs", "none.png");
	Rml::SetSystemInterface(&m_interfaces->system);
	Rml::SetFileInterface(&m_interfaces->files);
	m_ready = Rml::Initialise();
	if (m_ready)
	{
		SparkElement::registerInstancer();
		FastOutlineInstancer::registerInstancer();
		registerBinderElements();
	}
}

RmlRuntime::~RmlRuntime()
{
	if (m_ready) { Rml::Shutdown(); }
}

void RmlRuntime::setTime(double seconds) noexcept
{
	m_interfaces->system.time = seconds;
}

double RmlRuntime::time() const noexcept
{
	return m_interfaces->system.time;
}

void RmlRuntime::setTranslator(const LocalizationManager* table) noexcept
{
	m_interfaces->system.translator = table;
}

const LocalizationManager* RmlRuntime::translator() const noexcept
{
	return m_interfaces->system.translator;
}

void RmlRuntime::setClipboard(platform::IClipboard* clipboard) noexcept
{
	m_interfaces->system.clipboard = clipboard;
}

platform::IClipboard* RmlRuntime::clipboard() const noexcept
{
	return m_interfaces->system.clipboard;
}

void RmlRuntime::setGlyphSource(GlyphNameFn fn, void* ctx, const fs::path& gameGlyphDir)
{
	m_interfaces->system.glyphName = fn;
	m_interfaces->system.glyphCtx = ctx;
	m_interfaces->system.gameGlyphDir = gameGlyphDir;
}

std::size_t RmlRuntime::warningCount() const noexcept
{
	return m_interfaces->system.warnings;
}

bool RmlRuntime::loadFont(const fs::path& file, const BundledFace* bundled)
{
	const std::string key = toUtf8(file);
	if (m_loadedFonts.contains(key)) { return true; }
	std::error_code ec;
	if (!fs::exists(file, ec)) { return false; }
	m_loadedFonts.insert(key);
	if (bundled == nullptr) { return Rml::LoadFontFace(key); }
	return Rml::LoadFontFace(key, bundled->family, Rml::Style::FontStyle::Normal,
	                         static_cast<Rml::Style::FontWeight>(bundled->weight), bundled->fallback);
}

namespace
{

struct SystemFace
{
	const char* winFile;
	const char* bundledFile;   // nullptr = 同梱の代わりは無い
	const char* family;
	int weight;
};

// RCSS は書体の候補を並べられないので、Windows に入っている書体を固定の名前で引けるようにし、無い環境
// (Windows 以外や削られた構成) では同梱の書体を同じ名前で読む。Segoe 系は配れないので同梱しない。
constexpr SystemFace kSystemFaces[] = {
	{ "segoeui.ttf",    "MPLUSRounded1c-Regular.ttf", "UI Sans",  400 },
	{ "seguisb.ttf",    "MPLUSRounded1c-Bold.ttf",    "UI Sans",  600 },
	{ "segoeuib.ttf",   "MPLUSRounded1c-Bold.ttf",    "UI Sans",  700 },
	{ "consola.ttf",    "IBMPlexMono-Regular.ttf",    "UI Mono",  400 },
	{ "consolab.ttf",   "IBMPlexMono-Regular.ttf",    "UI Mono",  700 },
	{ "SegoeIcons.ttf", nullptr,                      "UI Icons", 400 },
};

fs::path windowsFontDir()
{
	const char* root = std::getenv("WINDIR");
	return fs::path(root != nullptr ? root : "C:/Windows") / "Fonts";
}

} // namespace

void RmlRuntime::loadSystemFonts()
{
	std::error_code ec;
	const fs::path winDir = windowsFontDir();
	for (const SystemFace& face : kSystemFaces)
	{
		fs::path file = winDir / face.winFile;
		if (!fs::exists(file, ec))
		{
			// Windows 10 には Segoe Fluent Icons が無く、同じ字形の番号を持つ Segoe MDL2 Assets がある
			if (face.bundledFile == nullptr) { file = winDir / "segmdl2.ttf"; }
			else { file = m_engineFontDir / face.bundledFile; }
		}
		if (!fs::exists(file, ec)) { continue; }
		const std::string key = toUtf8(file) + "|" + face.family + "|" + std::to_string(face.weight);
		if (!m_loadedFonts.insert(key).second) { continue; }
		(void)Rml::LoadFontFace(toUtf8(file), face.family, Rml::Style::FontStyle::Normal,
		                        static_cast<Rml::Style::FontWeight>(face.weight), false);
	}
}

void RmlRuntime::loadFonts(const fs::path& documentDir)
{
	loadSystemFonts();
	for (const BundledFace& face : kBundledFaces)
	{
		if (!loadFont(m_engineFontDir / face.file, &face) && face.fallback)
		{
			console::noticef("同梱のフォント %s が見つからないので、配布物にこのファイルを入れてください", toUtf8(m_engineFontDir / face.file).c_str());
		}
	}
	for (const fs::path& dir : text::uiFontDirs(m_engineFontDir, documentDir))
	{
		for (const fs::path& f : text::fontFilesIn(dir)) { (void)loadFont(f, nullptr); }
	}
}

std::string RmlRuntime::nextContextName()
{
	return "mitiru_ui_" + std::to_string(++m_contextSerial);
}

} // namespace mitiru::ui_rml
