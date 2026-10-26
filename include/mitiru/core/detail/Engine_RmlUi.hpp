// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>

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

/// 鳴らした音の字幕を UI の data model へ写す (view.caption_*)。行が変わった時だけ送る。
/// 録画に入る状態ではなく、音のステップから決まる表示なので、再生しても同じ行になる。
inline void pushCaptionsToRmlUi(
	ui_rml::RmlUiHost& ui, audio::sfx::CaptionTracker& tracker, const audio::sfx::CaptionPrefs& prefs)
{
	tracker.setPrefs(prefs);
	// UI がまだ無い間は変更を残す (先に消すと、UI が始まった後も次の字幕まで何も出ない)
	if (!ui.active() || !tracker.takeDirty()) { return; }
	const auto translate = [&ui](std::string_view key) { return ui.translate(key); };
	ui.setText("view.caption_lines", audio::sfx::captionLinesJson(tracker.lines(), prefs, translate));
	ui.setInt("view.caption_count", static_cast<int>(tracker.lines().size()));
	ui.setFloat("view.caption_scale", prefs.scale);
	// 下地の色は字幕の行そのものに掛ける (#rrggbbaa)。濃さは設定の不透明度
	char bg[16];
	const int alpha = static_cast<int>(std::lround(std::clamp(prefs.background, 0.0f, 1.0f) * 255.0f));
	std::snprintf(bg, sizeof(bg), "#0b0f1a%02x", alpha);
	ui.setText("view.caption_bg", bg);
	// 読み上げに渡す 1 行は、一番新しく鳴った音。危険な音 (alert) だけ割り込ませる。
	// 話者名などの設定を替えただけの時は送り直さない (同じ字幕をもう一度読ませないため)
	if (!tracker.takeLatestNew()) { return; }
	const audio::sfx::CaptionLine& latest = tracker.latest();
	ui.setText("view.caption_latest", audio::sfx::captionText(latest, prefs, translate));
	ui.setBool("view.caption_latest_alert", latest.kind == audio::sfx::CaptionKind::Alert);
}

} // namespace mitiru::detail

// ── main window の UI (RmlUi) のクラス外定義 ─────────────────────────

MITIRU_INLINE void mitiru::Engine::initializeRmlUi(const EngineConfig& config)
{
	if (config.uiDocument.empty()) { return; }
	(void)startRmlUiDocument(config.uiDocument);
}

MITIRU_INLINE bool mitiru::Engine::startUiOnDemand(const std::string& document)
{
	if (m_rmlUi.active()) { return true; }
	if (document.empty() || !m_device || !m_window) { return false; }
	m_config.uiDocument = document;
	return startRmlUiDocument(document);
}

MITIRU_INLINE bool mitiru::Engine::startRmlUiDocument(const std::string& document)
{
	if (document.empty()) { return false; }
	const bool started = startRmlUiBackend(document);
#ifdef _WIN32
	// 利用者が実際に打つ窓があるときだけ OS のクリップボードを使う。UI の入力欄に文字が届くのはその窓のキーだけで、
	// headless や台本の実行では貼り付けが起きない (replay に OS の中身が混ざらない)
	// (noActivate = 人が見ていない実行。カーソルの捕捉を除くのと同じに、ここでも除く)
	if (started && !mitiru::Win32Window::processNoActivate() && dynamic_cast<mitiru::Win32Window*>(m_window.get()) != nullptr)
	{
		m_rmlUi.useSystemClipboard(m_window->nativeHandle());
	}
#endif
	return started;
}

MITIRU_INLINE bool mitiru::Engine::startRmlUiBackend(const std::string& document)
{
#ifdef MITIRU_HAS_DAWN
	if (auto* dawn = dynamic_cast<gfx::DawnDevice*>(m_device.get()))
	{
		std::string error;
		if (!m_rmlUi.startDawn(*dawn, document, m_logicalWidth, m_logicalHeight, error))
		{
			console::noticef("UI の開始に失敗したので、UI なしで続けます (%s)", error.c_str());
			return false;
		}
		// <img src="view3d:N"/> は 3D レンダラの副ビュー slot N の出力を貼る。レンダラは作り直されることがあるので毎回引く
		m_rmlUi.setExternalImageSourceDawn([](void* ctx, int slot) {
			const auto* self = static_cast<const Engine*>(ctx);
			if (self->m_renderer3D == nullptr) { return ui_rml::UiExternalImageDawn{}; }
			const render::DawnViewImage img = render::dawnViewSlotImage(*self->m_renderer3D, slot);
			return ui_rml::UiExternalImageDawn{img.textureView, img.width, img.height};
		}, this);
		return true;
	}
#endif
#ifdef _WIN32
	auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get());
	if (dx12 == nullptr)
	{
		// 窓なしの 2D の実行で UI が出ないのは決まった動きなので、普通の実行では知らせない
		console::verbosef("UI の %s は DX12 で描くときだけ出るので、今回は出しません。"
		                  "headless で UI まで撮るときは --headless-3d を付けてください。", document.c_str());
		return false;
	}
	std::string error;
	if (!m_rmlUi.start(dx12->nativeDevice(), dx12->commandQueue(), document,
	                   m_logicalWidth, m_logicalHeight, error))
	{
		console::noticef("UI の開始に失敗したので、UI なしで続けます (%s)", error.c_str());
		return false;
	}
	// <img src="view3d:N"/> はゲームの副ビュー slot N の出力を貼る。レンダラは作り直されることがあるので毎回引く
	m_rmlUi.setExternalImageSource([](void* ctx, int slot) {
		const auto* self = static_cast<const Engine*>(ctx);
		const auto* r = dynamic_cast<const render::Renderer3D_DX12*>(self->m_renderer3D.get());
		if (r == nullptr) { return ui_rml::UiExternalImage{}; }
		const auto img = r->viewSlotImage(slot);
		return ui_rml::UiExternalImage{img.resource, static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM), img.width,
		                               img.height};
	}, this);
	return true;
#else
	console::verbosef("UI (RmlUi) はこのプラットフォームでは動かないので、%s は出しません。", document.c_str());
	return false;
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
		if (m_config.uiActionFilter && m_config.uiActionFilter(a.name, a.payloadJson)) { continue; }
		if (!pushModuleActionEvent(a.name, a.payloadJson))
		{
			debug::verboseOnce("ui.action.dropped",
				"UI の操作がたまりすぎて、game へ渡せなかった分を捨てました。UI から操作を送る回数を減らしてください。");
		}
	}
}

MITIRU_INLINE void mitiru::Engine::tickUiComposite()
{
	MITIRU_ZONE_NAMED("Engine::UiComposite");
	if (!m_rmlUi.active() || !m_device) { return; }

	// RML / RCSS と訳の表 (strings.json) を保存したら文書を読み直す。data model の値は残るので、すぐ今の値で出る。
	if (!m_uiWatchInit)
	{
		m_uiWatchInit = true;
		auto watcher = std::make_unique<asset::FileWatcher>();
		const std::string& doc = m_rmlUi.documentPath();
		const auto dir = std::filesystem::path(std::u8string(doc.begin(), doc.end())).parent_path();
		if (watcher->watchDirectory(dir, { ".rml", ".rcss", ".json" })) { m_uiWatcher = std::move(watcher); }
	}
	// 保存の監視は 0.25 秒に 1 回で足りる (人が保存してから見えるまでの間)。毎フレームの問い合わせは数十マイクロ秒
	if (m_uiWatcher)
	{
		const auto now = std::chrono::steady_clock::now();
		if (now - m_lastUiWatchPoll >= std::chrono::milliseconds(250))
		{
			m_lastUiWatchPoll = now;
			if (!m_uiWatcher->poll().empty()) { m_rmlUi.reloadDocument(); }
		}
	}

	// UI の時計は決定論の実行ではフレーム数そのもの。壁時計を読まないので replay で遷移の途中まで同じ絵になる。
	const double seconds = m_clock->isDeterministic()
		? static_cast<double>(m_clock->frameNumber()) / static_cast<double>(m_clock->targetTps())
		: static_cast<double>(m_clock->elapsed());
	m_rmlUi.setLanguage(m_config.language);
	m_rmlUi.update(seconds);
#ifdef MITIRU_HAS_DAWN
	if (auto* dawn = dynamic_cast<gfx::DawnDevice*>(m_device.get()))
	{
		m_rmlUi.renderDawn(dawn->width(), dawn->height());
		return;
	}
#endif
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
