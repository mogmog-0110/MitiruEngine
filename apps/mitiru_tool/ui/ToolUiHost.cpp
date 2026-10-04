#include "ToolUiHost.hpp"

#include "LiveImageElement.hpp"

#include <mitiru/asset/FileWatcher.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/ui_rml/RmlKeyTranslation.hpp>
#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>
#include <mitiru/ui_rml/RmlRuntime.hpp>
#include <mitiru/ui_rml/RmlStateModel.hpp>

#include <RmlUi/Core.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace mitiru::tool
{

namespace
{

constexpr const char* kModelName = "tool";

struct Image
{
	int width = 0;
	int height = 0;
	std::shared_ptr<const std::vector<std::uint8_t>> rgba;
};

KeyEvent::Key toolKey(int identifier)
{
	using namespace Rml::Input;
	switch (identifier)
	{
	case KI_LEFT:   return KeyEvent::Key::Left;
	case KI_RIGHT:  return KeyEvent::Key::Right;
	case KI_HOME:   return KeyEvent::Key::Home;
	case KI_END:    return KeyEvent::Key::End;
	case KI_ESCAPE: return KeyEvent::Key::Escape;
	case KI_Z:      return KeyEvent::Key::Z;
	case KI_Y:      return KeyEvent::Key::Y;
	default:        return KeyEvent::Key::Other;
	}
}

int modifierState(const ToolPointer& p)
{
	return (p.ctrl ? Rml::Input::KM_CTRL : 0) | (p.shift ? Rml::Input::KM_SHIFT : 0);
}

} // namespace

struct ToolUiHost::Impl
{
	// 押した要素の上で受けたマウスを、離すまでその要素の座標でページへ渡す (ドラッグが要素の外へ出ても続く)。
	class PointerListener final : public Rml::EventListener
	{
	public:
		PointerListener(Impl& owner, std::string id) : m_owner(owner), m_id(std::move(id)) {}
		void ProcessEvent(Rml::Event& ev) override { m_owner.onElementMouseDown(m_id, ev); }

	private:
		Impl& m_owner;
		std::string m_id;
	};

	class DocumentListener final : public Rml::EventListener
	{
	public:
		explicit DocumentListener(Impl& owner) : m_owner(owner) {}
		void ProcessEvent(Rml::Event& ev) override { m_owner.onDocumentEvent(ev); }

	private:
		Impl& m_owner;
	};

	ui_rml::RmlRenderInterfaceDx12 render;
	ui_rml::RmlStateModel model;
	ui_rml::RmlKeyTranslator keys;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::string contextName;
	std::string documentPath;
	std::filesystem::path documentFile;
	ToolPage* page = nullptr;
	bool pendingLoad = true;
	Rml::Vector2i lastMouse = { -1, -1 };
	std::array<bool, 3> lastButtons = {};
	std::map<std::string, Image, std::less<>> images;
	std::vector<std::pair<Rml::Element*, std::unique_ptr<PointerListener>>> pointerListeners;
	DocumentListener documentListener{ *this };
	bool documentListening = false;
	std::string captured;
	std::unique_ptr<asset::FileWatcher> watcher;

	~Impl()
	{
		if (context != nullptr)
		{
			Rml::RemoveContext(contextName);
			// 文脈が持っていた幾何とテクスチャをここで返させる (render より先に消えるので)。
			Rml::ReleaseRenderManagers();
		}
	}

	static PointerEvent pointerEvent(PointerEvent::Kind kind, Rml::Element& target, Rml::Event& ev)
	{
		const Rml::Vector2f origin = target.GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f size = target.GetBox().GetSize(Rml::BoxArea::Border);
		PointerEvent out;
		out.kind = kind;
		out.x = ev.GetParameter<float>("mouse_x", 0.0f) - origin.x;
		out.y = ev.GetParameter<float>("mouse_y", 0.0f) - origin.y;
		out.width = size.x;
		out.height = size.y;
		out.ctrl = ev.GetParameter<int>("ctrl_key", 0) != 0;
		out.shift = ev.GetParameter<int>("shift_key", 0) != 0;
		return out;
	}

	void onElementMouseDown(const std::string& id, Rml::Event& ev)
	{
		Rml::Element* target = document != nullptr ? document->GetElementById(id) : nullptr;
		if (target == nullptr || page == nullptr || ev.GetParameter<int>("button", 0) != 0) { return; }
		captured = id;
		page->onPointer(id, pointerEvent(PointerEvent::Kind::Down, *target, ev));
	}

	void onDocumentEvent(Rml::Event& ev)
	{
		if (page == nullptr) { return; }
		if (ev.GetId() == Rml::EventId::Keydown) { onKeyDown(ev); return; }
		if (captured.empty()) { return; }
		Rml::Element* target = document->GetElementById(captured);
		if (target == nullptr) { captured.clear(); return; }
		const bool up = ev.GetId() == Rml::EventId::Mouseup;
		page->onPointer(captured, pointerEvent(up ? PointerEvent::Kind::Up : PointerEvent::Kind::Move, *target, ev));
		if (up) { captured.clear(); }
	}

	void onKeyDown(Rml::Event& ev)
	{
		// 文字を打っている間のキーはページの操作にしない (scene_view の Esc / Ctrl+Z は入力欄の外だけ)。
		if (Rml::Element* t = ev.GetTargetElement(); t != nullptr && t->GetTagName() == "input") { return; }
		KeyEvent key;
		key.key = toolKey(ev.GetParameter<int>("key_identifier", 0));
		key.ctrl = ev.GetParameter<int>("ctrl_key", 0) != 0;
		key.shift = ev.GetParameter<int>("shift_key", 0) != 0;
		page->onKey(key);
	}

	// 要素は聞き手を生のポインタで持つので、聞き手を捨てる前に必ず外す。
	void detachListeners()
	{
		for (auto& [element, listener] : pointerListeners)
		{
			element->RemoveEventListener(Rml::EventId::Mousedown, listener.get());
		}
		pointerListeners.clear();
		if (documentListening && document != nullptr)
		{
			document->RemoveEventListener(Rml::EventId::Mousemove, &documentListener);
			document->RemoveEventListener(Rml::EventId::Mouseup, &documentListener);
			document->RemoveEventListener(Rml::EventId::Keydown, &documentListener);
		}
		documentListening = false;
		captured.clear();
	}

	void attachListeners()
	{
		detachListeners();
		if (document == nullptr || page == nullptr) { return; }
		for (const std::string& id : page->pointerTargets())
		{
			Rml::Element* el = document->GetElementById(id);
			if (el == nullptr) { continue; }
			auto listener = std::make_unique<PointerListener>(*this, id);
			el->AddEventListener(Rml::EventId::Mousedown, listener.get());
			pointerListeners.emplace_back(el, std::move(listener));
		}
		document->AddEventListener(Rml::EventId::Mousemove, &documentListener);
		document->AddEventListener(Rml::EventId::Mouseup, &documentListener);
		document->AddEventListener(Rml::EventId::Keydown, &documentListener);
		documentListening = true;
	}

	void applyImages()
	{
		if (document == nullptr) { return; }
		for (const auto& [id, image] : images)
		{
			if (auto* el = dynamic_cast<LiveImageElement*>(document->GetElementById(id)))
			{
				el->setPixels(image.width, image.height, image.rgba);
			}
		}
	}

	// 文書は最初の update で読む。読み込みの時点で data-style などの式を評価するので、ページが最初の値を
	// 写す前に読むと値の無い式が RCSS の構文エラーになる (RmlUiHost と同じ理由)。
	void loadDocument()
	{
		pendingLoad = false;
		document = model.loadDocument(*context, documentPath);
		if (document == nullptr)
		{
			std::fprintf(stderr, "mitiru_tool: RML %s を読めません。\n", documentPath.c_str());
			return;
		}
		document->Show();
		attachListeners();
		applyImages();
	}

	void deliverActions()
	{
		for (auto& a : model.takeActions())
		{
			if (page == nullptr) { continue; }
			auto payload = nlohmann::json::parse(a.payloadJson, nullptr, false);
			page->onAction(a.name, payload.is_discarded() ? nlohmann::json::object() : payload);
		}
	}

	void processMouse(const ToolPointer& p)
	{
		const int mods = modifierState(p);
		const Rml::Vector2i pos(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)));
		if (pos != lastMouse)
		{
			context->ProcessMouseMove(pos.x, pos.y, mods);
			lastMouse = pos;
		}
		for (int b = 0; b < 3; ++b)
		{
			const auto i = static_cast<std::size_t>(b);
			if (p.buttons[b] == lastButtons[i]) { continue; }
			lastButtons[i] = p.buttons[b];
			if (p.buttons[b]) { context->ProcessMouseButtonDown(b, mods); }
			else              { context->ProcessMouseButtonUp(b, mods); }
		}
		// 窓のホイールは 1 目盛り 120 で奥が正、RmlUi は 1 行単位で手前が正。
		if (p.wheel != 0.0f) { context->ProcessMouseWheel(-p.wheel / 120.0f, mods); }
	}
};

ToolUiHost::ToolUiHost() = default;
ToolUiHost::~ToolUiHost() = default;

bool ToolUiHost::start(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& document,
                       int width, int height, float dpRatio, std::string& error)
{
	m_impl.reset();
	ui_rml::RmlRuntime& rt = ui_rml::RmlRuntime::instance();
	if (!rt.ready()) { error = "RmlUi initialisation failed"; return false; }
	LiveImageElement::registerInstancer();
	auto impl = std::make_unique<Impl>();
	if (!impl->render.initialize(device, queue)) { error = "RmlUi renderer: " + impl->render.error(); return false; }
	std::error_code ec;
	if (!std::filesystem::is_regular_file(document, ec)) { error = "RML not found: " + document.generic_string(); return false; }
	rt.loadFonts(document.parent_path());
	impl->contextName = rt.nextContextName();
	impl->context = Rml::CreateContext(impl->contextName, { width, height }, &impl->render);
	if (impl->context == nullptr || !impl->model.create(*impl->context, kModelName))
	{
		error = "RmlUi context creation failed";
		return false;
	}
	impl->context->SetDensityIndependentPixelRatio(dpRatio);
	const std::u8string utf8 = document.generic_u8string();
	impl->documentPath.assign(utf8.begin(), utf8.end());
	impl->documentFile = document;
	m_impl = std::move(impl);
	return true;
}

void ToolUiHost::setPage(ToolPage* page) noexcept
{
	if (!m_impl) { return; }
	m_impl->page = page;
	m_impl->attachListeners();
}

void ToolUiHost::resize(int width, int height, float dpRatio)
{
	if (!m_impl) { return; }
	m_impl->context->SetDimensions({ width, height });
	m_impl->context->SetDensityIndependentPixelRatio(dpRatio);
}

bool ToolUiHost::active() const noexcept
{
	return m_impl != nullptr;
}

Rml::ElementDocument* ToolUiHost::document() const noexcept
{
	return m_impl ? m_impl->document : nullptr;
}

void ToolUiHost::processInput(const ToolPointer& pointer, std::span<const platform::Win32KeyMessage> keys)
{
	if (!m_impl) { return; }
	Rml::Context& ctx = *m_impl->context;
	m_impl->processMouse(pointer);
	m_impl->keys.translate(keys, [&ctx](const ui_rml::RmlKeyEvent& ev) {
		switch (ev.kind)
		{
		case ui_rml::RmlKeyEvent::Kind::Down: ctx.ProcessKeyDown(ev.key, ev.modifiers); break;
		case ui_rml::RmlKeyEvent::Kind::Up:   ctx.ProcessKeyUp(ev.key, ev.modifiers); break;
		case ui_rml::RmlKeyEvent::Kind::Text: ctx.ProcessTextInput(ev.character); break;
		}
	});
	m_impl->deliverActions();
}

void ToolUiHost::update(double seconds)
{
	if (!m_impl) { return; }
	ui_rml::RmlRuntime::instance().setTime(seconds);
	if (m_impl->pendingLoad) { m_impl->loadDocument(); }
	m_impl->model.update(*m_impl->context);
	m_impl->deliverActions();
}

void ToolUiHost::render(ID3D12Resource* target, int width, int height)
{
	if (!m_impl) { return; }
	if (!m_impl->render.beginFrame(target, width, height, m_impl->context->GetDimensions()))
	{
		if (!m_impl->render.error().empty())
		{
			console::verbosef("UI のフレームを描かずに飛ばしました (%s)。", m_impl->render.error().c_str());
		}
		return;
	}
	m_impl->context->Render();
	m_impl->render.endFrame();
}

void ToolUiHost::reloadIfChanged()
{
	if (!m_impl) { return; }
	if (!m_impl->watcher)
	{
		auto watcher = std::make_unique<asset::FileWatcher>();
		if (!watcher->watchDirectory(m_impl->documentFile.parent_path(), { ".rml", ".rcss" })) { return; }
		m_impl->watcher = std::move(watcher);
	}
	if (m_impl->watcher->poll().empty()) { return; }
	m_impl->detachListeners();
	if (m_impl->document != nullptr) { m_impl->document->Close(); }
	m_impl->document = nullptr;
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	m_impl->pendingLoad = true;
	if (m_impl->page != nullptr) { m_impl->page->start(); }
}

void ToolUiHost::set(std::string_view key, nlohmann::json value)
{
	if (!m_impl) { return; }
	std::string full = std::string(kModelName) + "." + std::string(key);
	(void)m_impl->model.setValue(full, std::move(value));
}

void ToolUiHost::setImage(std::string_view elementId, int width, int height, std::vector<std::uint8_t> rgba)
{
	if (!m_impl) { return; }
	auto& slot = m_impl->images[std::string(elementId)];
	slot = { width, height, std::make_shared<const std::vector<std::uint8_t>>(std::move(rgba)) };
	m_impl->applyImages();
}

} // namespace mitiru::tool
