#include <mitiru/ui_rml/RmlUiHost.hpp>

#include <mitiru/core/Localization.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/input/GlyphLookup.hpp>
#include <mitiru/ui_rml/RmlImeBridge.hpp>
#include <mitiru/ui_rml/RmlKeyTranslation.hpp>
#include <mitiru/ui_rml/RmlPadNavigation.hpp>
#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>
#include <mitiru/ui_rml/RmlRuntime.hpp>
#include <mitiru/ui_rml/RmlStateModel.hpp>

#include <RmlUi/Core.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace mitiru::ui_rml
{

namespace
{

constexpr const char* kModelName = "view";
constexpr std::string_view kEngineDocScheme = "mitiru:";

namespace fs = std::filesystem;

fs::path fromUtf8(std::string_view s)
{
	return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string toUtf8(const fs::path& p)
{
	const std::u8string u = p.generic_u8string();
	return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// "mitiru:" で始まる文書は engine の同梱物 (host の確認画面など)。
fs::path resolveDocument(std::string_view path)
{
	if (path.rfind(kEngineDocScheme, 0) == 0)
	{
		return RmlRuntime::instance().engineUiDir() / fromUtf8(path.substr(kEngineDocScheme.size()));
	}
	return fromUtf8(path);
}

// 訳の表は LocalizationManager の形 ("languages" と "strings") に、予備の言語 "fallback" を足せる。
void readStringsFile(LocalizationManager& loc, const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if (!in) { return; }
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const auto j = nlohmann::json::parse(text, nullptr, false);
	if (!j.is_object() || !loc.loadTranslationsFromString(text))
	{
		console::noticef("UI の訳の表 %s を JSON として読めません。書き方を確かめてください。", toUtf8(file).c_str());
		return;
	}
	if (j.contains("fallback") && j["fallback"].is_string()) { loc.setFallbackLanguage(j["fallback"].get<std::string>()); }
}

// host が絵柄の決め方を渡す前は、"glyph:pad:A" のような入力の名前だけを出す。
std::string inputNameGlyph(void*, std::string_view name)
{
	return input::glyphFor(name, {}, {}, input::InputDevice::KeyboardMouse, input::PadKind::Unknown);
}

void processMouse(Rml::Context& ctx, const UiPointer& p, Rml::Vector2i& lastPos, std::array<bool, 3>& lastButtons)
{
	const Rml::Vector2i pos(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)));
	if (pos != lastPos)
	{
		ctx.ProcessMouseMove(pos.x, pos.y, 0);
		lastPos = pos;
	}
	for (int b = 0; b < 3; ++b)
	{
		const bool down = p.buttons[b];
		if (down == lastButtons[static_cast<std::size_t>(b)]) { continue; }
		lastButtons[static_cast<std::size_t>(b)] = down;
		if (down) { ctx.ProcessMouseButtonDown(b, 0); }
		else      { ctx.ProcessMouseButtonUp(b, 0); }
	}
	// 窓のホイールは 1 目盛り 120 で奥が正、RmlUi は 1 行単位で手前 (下へ送る) が正。
	if (p.wheel != 0.0f) { ctx.ProcessMouseWheel(-p.wheel / 120.0f, 0); }
}

} // namespace

struct RmlUiHost::Impl
{
	RmlRenderInterfaceDx12 render;
	RmlImeBridge ime;
	RmlStateModel model;
	RmlKeyTranslator keys;
	RmlPadNavigator pad;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::string contextName;
	std::string documentPath;
	std::vector<UiAction> actions;
	Rml::Vector2i lastMouse = { -1, -1 };
	bool pendingLoad = true;
	std::array<bool, 3> lastButtons = {};
	LocalizationManager strings;
	std::string language;
	struct Overlay
	{
		std::string path;   ///< UTF-8 の実際のファイル
		Rml::ElementDocument* doc = nullptr;
		bool failed = false;
	};
	std::vector<Overlay> overlays;

	~Impl()
	{
		// 訳と絵柄の窓口はプロセスに 1 つ。後から始めた別の host が持っていれば、そのままにする
		RmlRuntime& rt = RmlRuntime::instance();
		if (rt.translator() == &strings)
		{
			rt.setTranslator(nullptr);
			rt.setGlyphSource(nullptr, nullptr, {});
		}
		if (context != nullptr)
		{
			Rml::RemoveContext(contextName);
			// 文脈が持っていた幾何とテクスチャをここで解放させる。render より先に片付けないと、
			// 後で RmlUi が、もう消えた render に解放を要求する。
			Rml::ReleaseRenderManagers();
		}
	}

	// 文書は最初の update で読む。RmlUi は読み込んだ時点で data-style などの式を評価するので、game が
	// 最初のフレームに送った値で最初の絵を作れる (まだ送られていない data-style の変数は 0 で評価する)。
	bool loadDocument()
	{
		pendingLoad = false;
		document = model.loadDocument(*context, documentPath);
		if (document == nullptr) { return false; }
		document->Show();
		return true;
	}

	void loadPending()
	{
		if (pendingLoad && !loadDocument())
		{
			console::noticef("UI の RML ファイル %s を読めません。パスと書き方を確かめてください。", documentPath.c_str());
		}
		for (Overlay& o : overlays)
		{
			if (o.doc == nullptr && !o.failed) { showOverlay(o); }
		}
	}

	void showOverlay(Overlay& o)
	{
		o.doc = model.loadDocument(*context, o.path);
		if (o.doc == nullptr)
		{
			o.failed = true;
			console::noticef("UI の RML ファイル %s を読めません。パスと書き方を確かめてください。", o.path.c_str());
			return;
		}
		o.doc->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
	}

	void loadStrings()
	{
		strings = LocalizationManager{};
		readStringsFile(strings, RmlRuntime::instance().engineUiDir() / "mitiru_strings.json");
		readStringsFile(strings, fromUtf8(documentPath).parent_path() / "strings.json");
		if (!language.empty()) { strings.setLanguage(language); }
	}

	// 訳は文書を読む時に入るので、言語を替えたら文書ごと読み直す。重ねた文書も同じ。
	void reloadAll()
	{
		if (document != nullptr) { document->Close(); }
		for (Overlay& o : overlays)
		{
			if (o.doc != nullptr) { o.doc->Close(); }
			o = Overlay{ o.path };
		}
		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();
		loadStrings();
		if (!loadDocument())
		{
			console::noticef("UI の RML ファイル %s を読めません。パスと書き方を確かめてください。", documentPath.c_str());
		}
		for (Overlay& o : overlays) { showOverlay(o); }
	}

	void refreshGlyphs()
	{
		std::vector<Rml::ElementDocument*> docs = { document };
		for (const Overlay& o : overlays) { docs.push_back(o.doc); }
		for (Rml::ElementDocument* d : docs)
		{
			if (d == nullptr) { continue; }
			Rml::ElementList images;
			d->GetElementsByTagName(images, "img");
			for (Rml::Element* e : images)
			{
				// 同じ src を書き直すと画像を読み直し、JoinPath で今の機器の絵柄を引き直す。
				const Rml::String src = e->GetAttribute<Rml::String>("src", "");
				if (src.rfind(kGlyphImageScheme, 0) == 0) { e->SetAttribute("src", src); }
			}
		}
	}

	void collectActions()
	{
		for (auto& a : model.takeActions()) { actions.push_back({ std::move(a.name), std::move(a.payloadJson) }); }
	}

	void translateKeys(std::span<const platform::Win32KeyMessage> messages)
	{
		Rml::Context& ctx = *context;
		keys.translate(messages, [&ctx](const RmlKeyEvent& ev) {
			switch (ev.kind)
			{
			case RmlKeyEvent::Kind::Down: ctx.ProcessKeyDown(ev.key, ev.modifiers); break;
			case RmlKeyEvent::Kind::Up:   ctx.ProcessKeyUp(ev.key, ev.modifiers); break;
			case RmlKeyEvent::Kind::Text: ctx.ProcessTextInput(ev.character); break;
			}
		});
	}

	// 確定した文字がキーの列で届く前に、欄に見せている変換中の文字を抜き、キーを渡した後で入れ直す。
	void processKeysAndComposition(std::span<const platform::Win32KeyMessage> messages, const UiExtraInput& extra)
	{
		const bool refresh = !messages.empty() || !ime.sameAsShown(extra.imeComposition);
		if (refresh)
		{
			model.setComposing(true);
			ime.removeComposition();
			model.setComposing(false);
		}
		translateKeys(messages);
		if (refresh)
		{
			model.setComposing(true);
			ime.showComposition(extra.imeComposition, extra.imeCursor);
			model.setComposing(ime.showing());
		}
	}

	// まだ何も選んでいなければ、最初の押下で先頭の要素を選ぶ (Tab と同じ)。
	void navigate(const UiPad& state)
	{
		pad.translate(state, [this](Rml::Input::KeyIdentifier key) {
			const Rml::Element* focus = context->GetFocusElement();
			const bool nothingFocused = focus == nullptr || focus->GetOwnerDocument() == focus;
			if (nothingFocused && key != Rml::Input::KI_RETURN) { key = Rml::Input::KI_TAB; }
			context->ProcessKeyDown(key, 0);
			context->ProcessKeyUp(key, 0);
		});
	}
};

RmlUiHost::RmlUiHost() = default;
RmlUiHost::~RmlUiHost() = default;

bool RmlUiHost::start(ID3D12Device* device, ID3D12CommandQueue* queue, const std::string& documentPath,
                      int logicalWidth, int logicalHeight, std::string& error)
{
	stop();
	RmlRuntime& rt = RmlRuntime::instance();
	if (!rt.ready()) { error = "RmlUi initialisation failed"; return false; }
	auto impl = std::make_unique<Impl>();
	if (!impl->render.initialize(device, queue)) { error = "RmlUi renderer: " + impl->render.error(); return false; }
	const fs::path doc = resolveDocument(documentPath);
	rt.loadFonts(doc.parent_path());
	impl->contextName = rt.nextContextName();
	impl->context = Rml::CreateContext(impl->contextName, { logicalWidth, logicalHeight }, &impl->render, &impl->ime);
	if (impl->context == nullptr || !impl->model.create(*impl->context, kModelName))
	{
		error = "RmlUi context creation failed";
		return false;
	}
	const std::u8string utf8 = doc.generic_u8string();
	impl->documentPath.assign(utf8.begin(), utf8.end());
	std::error_code ec;
	if (!std::filesystem::is_regular_file(doc, ec))
	{
		error = "RML document not found: " + impl->documentPath;
		return false;
	}
	impl->loadStrings();
	rt.setTranslator(&impl->strings);
	rt.setGlyphSource(&inputNameGlyph, nullptr, doc.parent_path() / "glyphs");
	m_impl = std::move(impl);
	return true;
}

void RmlUiHost::stop()
{
	m_impl.reset();
}

bool RmlUiHost::active() const noexcept
{
	return m_impl != nullptr;
}

const std::string& RmlUiHost::documentPath() const noexcept
{
	static const std::string empty;
	return m_impl ? m_impl->documentPath : empty;
}

void RmlUiHost::setInt(std::string_view key, int value)
{
	if (m_impl) { m_impl->model.setValue(key, nlohmann::json(value)); }
}

void RmlUiHost::setFloat(std::string_view key, float value)
{
	if (m_impl) { m_impl->model.setValue(key, nlohmann::json(value)); }
}

void RmlUiHost::setBool(std::string_view key, bool value)
{
	if (m_impl) { m_impl->model.setValue(key, nlohmann::json(value)); }
}

void RmlUiHost::setText(std::string_view key, std::string_view value)
{
	if (m_impl) { m_impl->model.setValue(key, parseStateValue(value)); }
}

void RmlUiHost::processInput(const UiPointer& pointer, std::span<const platform::Win32KeyMessage> keys)
{
	processInput(pointer, keys, UiExtraInput{});
}

void RmlUiHost::processInput(const UiPointer& pointer, std::span<const platform::Win32KeyMessage> keys,
                             const UiExtraInput& extra)
{
	if (!m_impl) { return; }
	processMouse(*m_impl->context, pointer, m_impl->lastMouse, m_impl->lastButtons);
	m_impl->processKeysAndComposition(keys, extra);
	m_impl->navigate(extra.pad);
	m_impl->collectActions();
}

bool RmlUiHost::focusedTextField(float rect[4]) const
{
	Rml::Rectanglef bounds;
	if (!m_impl || !m_impl->ime.focusedField(bounds)) { return false; }
	rect[0] = bounds.Left();
	rect[1] = bounds.Top();
	rect[2] = bounds.Width();
	rect[3] = bounds.Height();
	return true;
}

void RmlUiHost::update(double seconds)
{
	if (!m_impl) { return; }
	RmlRuntime::instance().setTime(seconds);
	m_impl->loadPending();
	m_impl->model.update(*m_impl->context);
	m_impl->collectActions();
}

void RmlUiHost::render(ID3D12Resource* target, int width, int height)
{
	if (!m_impl) { return; }
	if (!m_impl->render.beginFrame(target, width, height, m_impl->context->GetDimensions()))
	{
		if (!m_impl->render.error().empty())
		{
			debug::verboseOnce("rmlui.frame.skipped", "UI のこのフレームを描けませんでした (" + m_impl->render.error() + ")。");
		}
		return;
	}
	m_impl->context->Render();
	m_impl->render.endFrame();
}

void RmlUiHost::reloadDocument()
{
	if (m_impl) { m_impl->reloadAll(); }
}

void RmlUiHost::setUiScale(float scale)
{
	if (!m_impl || !(scale > 0.0f)) { return; }
	m_impl->context->SetDensityIndependentPixelRatio(scale);
}

std::vector<UiAction> RmlUiHost::takeActions()
{
	if (!m_impl) { return {}; }
	return std::exchange(m_impl->actions, {});
}

void RmlUiHost::setExternalImageSource(UiExternalImageFn fn, void* ctx)
{
	if (m_impl) { m_impl->render.setExternalImageSource(fn, ctx); }
}

void RmlUiHost::setLanguage(std::string_view code)
{
	if (!m_impl || code == m_impl->language) { return; }
	m_impl->language = std::string(code);
	m_impl->strings.setLanguage(code);
	if (!m_impl->pendingLoad) { m_impl->reloadAll(); }
}

void RmlUiHost::setGlyphSource(UiGlyphFn fn, void* ctx)
{
	if (!m_impl) { return; }
	RmlRuntime::instance().setGlyphSource(fn != nullptr ? fn : &inputNameGlyph, ctx,
	                                      fromUtf8(m_impl->documentPath).parent_path() / "glyphs");
	m_impl->refreshGlyphs();
}

void RmlUiHost::refreshGlyphs()
{
	if (m_impl) { m_impl->refreshGlyphs(); }
}

void RmlUiHost::openOverlay(std::string_view path)
{
	if (!m_impl) { return; }
	const std::string file = toUtf8(resolveDocument(path));
	if (file == m_impl->documentPath)
	{
		if (m_impl->document != nullptr) { m_impl->document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document); }
		return;
	}
	for (const auto& o : m_impl->overlays)
	{
		if (o.path == file) { return; }
	}
	m_impl->overlays.push_back({ file });
	if (!m_impl->pendingLoad) { m_impl->showOverlay(m_impl->overlays.back()); }
}

void RmlUiHost::closeOverlay(std::string_view path)
{
	if (!m_impl) { return; }
	const std::string file = toUtf8(resolveDocument(path));
	if (file == m_impl->documentPath)
	{
		if (m_impl->document != nullptr) { m_impl->document->Hide(); }
		return;
	}
	auto& list = m_impl->overlays;
	for (auto it = list.begin(); it != list.end(); ++it)
	{
		if (it->path != file) { continue; }
		if (it->doc != nullptr) { it->doc->Close(); }
		list.erase(it);
		return;
	}
}

} // namespace mitiru::ui_rml
