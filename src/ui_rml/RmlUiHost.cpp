#include <mitiru/ui_rml/RmlUiHost.hpp>

#include <mitiru/ui_rml/RmlImeBridge.hpp>
#include <mitiru/ui_rml/RmlKeyTranslation.hpp>
#include <mitiru/ui_rml/RmlPadNavigation.hpp>
#include <mitiru/ui_rml/RmlRenderInterfaceDx12.hpp>
#include <mitiru/ui_rml/RmlRuntime.hpp>
#include <mitiru/ui_rml/RmlStateModel.hpp>

#include <RmlUi/Core.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace mitiru::ui_rml
{

namespace
{

constexpr const char* kModelName = "view";

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

	~Impl()
	{
		if (context != nullptr)
		{
			Rml::RemoveContext(contextName);
			// 文脈が持っていた幾何とテクスチャをここで解放させる。render より先に片付けないと、
			// 後で RmlUi が、もう消えた render に解放を要求する。
			Rml::ReleaseRenderManagers();
		}
	}

	// 文書は最初の update で読む。RmlUi は読み込んだ時点で data-style などの式を評価するので、game が
	// 最初の hud.set を送る前に読むと data-style-width="level + '%'" が値の無いまま評価され、RCSS の
	// 構文エラーを出す。
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
			std::fprintf(stderr, "[mitiru] UI (RmlUi): RML document could not be loaded: %s\n", documentPath.c_str());
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
	const std::filesystem::path doc(std::u8string(documentPath.begin(), documentPath.end()));
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
			std::fprintf(stderr, "[mitiru] UI (RmlUi) frame skipped: %s\n", m_impl->render.error().c_str());
		}
		return;
	}
	m_impl->context->Render();
	m_impl->render.endFrame();
}

void RmlUiHost::reloadDocument()
{
	if (!m_impl) { return; }
	if (m_impl->document != nullptr) { m_impl->document->Close(); }
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	if (!m_impl->loadDocument())
	{
		std::fprintf(stderr, "[mitiru] UI (RmlUi) reload failed: %s\n", m_impl->documentPath.c_str());
	}
}

std::vector<UiAction> RmlUiHost::takeActions()
{
	if (!m_impl) { return {}; }
	return std::exchange(m_impl->actions, {});
}

} // namespace mitiru::ui_rml
