// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string_view>

#include <mitiru/core/InlineMacro.hpp>

namespace mitiru::detail
{

/// DLL から来た固定長の文字列は null 終端を信用しない。
template <std::size_t N>
[[nodiscard]] std::string_view uiBoundedView(const char (&s)[N]) noexcept
{
	return std::string_view{s, static_cast<std::size_t>(std::find(s, s + N, '\0') - s)};
}

/// hud.set の値を UI の data model へ写す。値が変わらなければ何もしない。
inline void pushStateToRmlUi(ui_rml::RmlUiHost& ui, const module::FrameIntents& intents)
{
	const std::int32_t n = std::min<std::int32_t>(intents.statePushCount,
		static_cast<std::int32_t>(sizeof(intents.statePushes) / sizeof(intents.statePushes[0])));
	for (std::int32_t i = 0; i < n; ++i)
	{
		const auto& item = intents.statePushes[i];
		const std::string_view key = uiBoundedView(item.key);
		switch (item.kind)
		{
		case 1: ui.setInt(key, item.intVal); break;
		case 2: ui.setFloat(key, item.floatVal); break;
		case 3: ui.setBool(key, item.intVal != 0); break;
		case 4: ui.setText(key, uiBoundedView(item.strVal)); break;
		default: break;
		}
	}
}

} // namespace mitiru::detail

// ── main window の UI (RmlUi) のクラス外定義 ─────────────────────────

MITIRU_INLINE void mitiru::Engine::initializeRmlUi(const EngineConfig& config)
{
	if (config.uiDocument.empty()) { return; }
#ifdef _WIN32
	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	if (dx12 == nullptr)
	{
		std::fprintf(stderr, "[mitiru] UI (RmlUi) は DX12 の描画先が要る。%s は出さずに続ける"
		                     " (headless で UI ごと撮るなら --headless-3d)\n", config.uiDocument.c_str());
		return;
	}
	std::string error;
	if (!m_rmlUi.start(dx12->nativeDevice(), dx12->commandQueue(), config.uiDocument,
	                   m_logicalWidth, m_logicalHeight, error))
	{
		std::fprintf(stderr, "[mitiru] UI (RmlUi) を始められなかった: %s (UI 無しで続ける)\n", error.c_str());
	}
#else
	std::fprintf(stderr, "[mitiru] UI (RmlUi) はこのプラットフォームでは動かない: %s\n", config.uiDocument.c_str());
#endif
}

MITIRU_INLINE void mitiru::Engine::feedUiInput(const module::InputSnapshot& snap)
{
	if (!m_rmlUi.active()) { return; }
	// マウス・ホイール・パッド・変換中の文字は snapshot から作る (台本と replay の操作が UI にも届く)。
	ui_rml::UiPointer pointer;
	pointer.x = snap.mouseX;
	pointer.y = snap.mouseY;
	for (int i = 0; i < 3; ++i) { pointer.buttons[i] = snap.mouseButtonsDown[i] != 0; }
	pointer.wheel = snap.mouseWheel * 120.0f;
	ui_rml::UiExtraInput extra;
	extra.pad = { snap.gamepadButtonsDown, snap.gamepadAxes[module::gamepad::LeftStickX],
	              snap.gamepadAxes[module::gamepad::LeftStickY] };
	extra.imeComposition = std::string_view(snap.imeComposition,
		std::min<std::size_t>(snap.imeCompositionLen, sizeof(snap.imeComposition)));
	extra.imeCursor = snap.imeCursor;
	std::span<const platform::Win32KeyMessage> keys;
#ifdef _WIN32
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get())) { keys = win32->takeKeyMessages(); }
#endif
	m_rmlUi.processInput(pointer, keys, extra);
	for (const ui_rml::UiAction& a : m_rmlUi.takeActions())
	{
		if (!pushModuleActionEvent(a.name, a.payloadJson))
		{
			debug::warnOnce("ui.action.dropped", "UI の操作を game へ渡せなかった (action の列が満杯)");
		}
	}
}

MITIRU_INLINE void mitiru::Engine::tickUiComposite()
{
	MITIRU_ZONE_NAMED("Engine::UiComposite");
	if (!m_rmlUi.active() || !m_device) { return; }

	// RML / RCSS を保存したら文書を読み直す。data model の値は残るので、すぐ今の値で出る。
	if (!m_uiWatchInit)
	{
		m_uiWatchInit = true;
		auto watcher = std::make_unique<asset::FileWatcher>();
		const std::string& doc = m_rmlUi.documentPath();
		const auto dir = std::filesystem::path(std::u8string(doc.begin(), doc.end())).parent_path();
		if (watcher->watchDirectory(dir, { ".rml", ".rcss" })) { m_uiWatcher = std::move(watcher); }
	}
	if (m_uiWatcher && !m_uiWatcher->poll().empty()) { m_rmlUi.reloadDocument(); }

	// UI の時計は決定論の実行ではフレーム数そのもの。壁時計を読まないので replay で遷移の途中まで同じ絵になる。
	const double seconds = m_clock->isDeterministic()
		? static_cast<double>(m_clock->frameNumber()) / static_cast<double>(m_clock->targetTps())
		: static_cast<double>(m_clock->elapsed());
	m_rmlUi.update(seconds);
#ifdef _WIN32
	// 描画先は実バックバッファ (2D の MSAA / LoFi の中間 RT は present の段で外れている)。
	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	if (dx12 == nullptr) { return; }
	ID3D12Resource* target = nullptr;
	if (auto* swap = dx12->getSwapChain()) { target = swap->getBackBufferResource(swap->currentBackBufferIndex()); }
	else if (auto* offscreen = dx12->currentBackBuffer()) { target = offscreen->nativeResource(); }
	if (target != nullptr) { m_rmlUi.render(target, m_window->width(), m_window->height()); }
#endif
}
