#pragma once

/// @file HostEnvironmentWatch.hpp
/// @brief この PC の環境情報 (モニタ、音声出力機器、リフレッシュレート、HDR、OS の設定、キーの刻印) を Engine へ渡す (ABI v62、ADR 0099)。
/// @details ゲームが名前で指定したモニタと機器への切り替えと、Alt+Enter による全画面の切り替えも扱う。環境情報は変化したときだけ読み直す。
/// モニタ、DPI、OS の設定の変化はウィンドウの通知、機器の抜き差しは 1 秒ごとの一覧の比較、ウィンドウのモニタ間移動は毎フレームの MonitorFromWindow で検出する。
/// 読み取った内容は録画されないが、snapshot の値は録画され、再生時に記録した値へ置き換わる。
/// 人が見ない実行 (スクリプト、撮影、再生) では OS を読まず、空の読みを 1 度だけ渡す。PC ごとに snapshot が違うと、同じ入力でも結果が違って見えるため。

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HostAudioOutput.hpp"
#include "HostShip.hpp"

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/input/KeyPosition.hpp>
#include <mitiru/module/detail/HostEnvironment62.hpp>
#include <mitiru/platform/win32/OsAccessibility.hpp>
#ifdef _WIN32
#include <mitiru/platform/win32/KeyLayoutLabel.hpp>
#include <mitiru/platform/win32/MonitorList.hpp>
#include <mitiru/platform/win32/Win32Window.hpp>
#endif

namespace mitiru::host
{

class HostEnvironmentWatch
{
public:
	HostEnvironmentWatch(HostShip& ship, HostAudioOutput& audio) noexcept : m_ship(ship), m_audio(audio) {}
	HostEnvironmentWatch(const HostEnvironmentWatch&) = delete;
	HostEnvironmentWatch& operator=(const HostEnvironmentWatch&) = delete;

	/// @param attended 人がウィンドウを見て遊ぶ起動か。false なら OS を読まず、空の読みだけを渡す
	void configure(bool attended) noexcept { m_attended = attended; }

	/// @brief onFrameStart から毎フレーム呼ぶ
	void onFrame(Engine& engine)
	{
		if (!m_attended)
		{
			if (!m_sentEmpty)
			{
				module::detail::HostEnvironment env;
				env.osAccessibility = platform::forcedOsAccessibility().value_or(0);
				engine.setHostEnvironment(env);
				m_sentEmpty = true;
			}
			return;
		}
#ifdef _WIN32
		auto* window = dynamic_cast<Win32Window*>(engine.window());
		watchWindow(window);
		restoreSavedMonitor(window);
		if (m_toggleFullscreen) { m_toggleFullscreen = false; toggleFullscreen(engine, window); }
		if (needsRebuild(engine, window)) { publish(engine, window); }
#else
		if (needsRebuild(engine)) { publish(engine); }
#endif
	}

	/// @brief ゲームが Hud::windowMonitor で指定したモニタへ切り替える。空なら主モニタを使う。名前には表示した名前を使う
	void onMonitorRequest(std::string_view name)
	{
#ifdef _WIN32
		if (!m_attended || m_engine == nullptr) { return; }
		auto* window = dynamic_cast<Win32Window*>(m_engine->window());
		if (window == nullptr) { return; }
		const std::optional<std::size_t> hit = name.empty() ? primaryIndex() : module::detail::findPublished(m_published.monitors, name);
		if (!hit)
		{
			debug::verboseOnce("host_env.monitor." + std::string(name), "頼まれたモニタ " + std::string(name) + " が見つからないので、ウィンドウは今の画面のままです");
			return;
		}
		const std::size_t source = m_published.monitors.source[*hit];
		if (source >= m_monitors.size()) { return; }
		const HMONITOR target = m_monitors[source].handle;
		// 毎フレーム同じ指定が来ても、ウィンドウが動いたときだけ列挙し直す。モニタの列挙は軽くない
		if (monitorHandleOf(window) != target)
		{
			if (!window->moveToMonitor(target))
			{
				debug::verboseOnce("host_env.monitor_move", "ウィンドウをモニタ " + m_published.monitors.shown[*hit] + " へ移せなかったので、今の画面のままです");
				return;   // ウィンドウが動いていないのに、移ったことにして設定へ残さない
			}
			m_dirty = true;
		}
		m_ship.rememberMonitor(name.empty() ? std::string() : m_published.monitors.shown[*hit]);
#else
		(void)name;
#endif
	}

	/// @brief ゲームが Hud::audioDevice で指定した音声出力機器へ切り替える。空なら OS の既定を使う。名前には表示した名前を使う
	void onAudioDeviceRequest(std::string_view name)
	{
		if (!m_attended) { return; }
		std::string wanted;
		if (!name.empty())
		{
			const auto hit = module::detail::findPublished(m_published.audioDevices, name);
			if (!hit)
			{
				debug::verboseOnce("host_env.audio." + std::string(name), "頼まれた出力機器 " + std::string(name) + " が見つからないので、機器は今のままです");
				return;
			}
			wanted = m_published.audioDevices.full[*hit];   // 名前を持たない機器は空になり、OS の既定と同じ扱いになる
		}
		// 毎フレーム同じ指定が来ても、audio_device.json を書き直さない
		if (wanted == m_audio.current()) { return; }
		m_audio.chooseNamed(wanted);
		m_dirty = true;
	}

	/// @brief Engine の作成後、runModule の前に呼ぶ
	void attach(Engine& engine) noexcept { m_engine = &engine; }

private:
	using Clock = std::chrono::steady_clock;

	[[nodiscard]] bool needsRebuild(Engine& engine
#ifdef _WIN32
	                                , Win32Window* window
#endif
	)
	{
		bool rebuild = m_dirty.exchange(false) || !m_sentFirst;
		if (engine.config().keyMode != m_keyMode) { rebuild = true; }
		const Clock::time_point now = Clock::now();
		if (now >= m_audioPollAt)
		{
			m_audioPollAt = now + std::chrono::seconds(1);
			if (++m_audioPolls % 5 == 0) { m_audio.refreshDevices(); }
			int selected = -1;
			if (m_audio.deviceNames(selected) != m_audioNames || selected != m_audioSelected) { rebuild = true; }
		}
#ifdef _WIN32
		if (window != nullptr && monitorHandleOf(window) != m_windowMonitor) { rebuild = true; }
		if (platform::currentKeyboardLayoutId() != m_layout) { rebuild = true; }
#endif
		return rebuild;
	}

#ifdef _WIN32
	/// ウィンドウの通知で読み直しのフラグを立てる。ウィンドウと同じスレッドから呼ばれるが、フラグは atomic にする
	void watchWindow(Win32Window* window)
	{
		if (window == nullptr || m_hooked) { return; }
		m_hooked = true;
		window->setSettingChangeHandler([this](UINT, WPARAM, LPARAM) { m_dirty = true; });
		window->setDisplayChangeHandler([this](int, int) { m_dirty = true; });
		window->setDpiChangedHandler([this](UINT) { m_dirty = true; });
		// Alt+Enter はウィンドウ層で受け、Enter はゲームにも UI にも届かない。切り替えは次のフレームの頭
		window->setAltEnterHandler([this] { m_toggleFullscreen = true; });
	}

	[[nodiscard]] static HMONITOR monitorHandleOf(const Win32Window* window) noexcept
	{
		return MonitorFromWindow(reinterpret_cast<HWND>(window->nativeHandle()), MONITOR_DEFAULTTONEAREST);
	}

	/// 最初のフレームで、前回選んだモニタへウィンドウを移す。見つからなければ現在の画面から動かさない
	void restoreSavedMonitor(Win32Window* window)
	{
		if (m_restored || window == nullptr) { return; }
		m_restored = true;
		const std::string& saved = m_ship.settings().graphics.monitorName;
		if (saved.empty()) { return; }
		m_monitors = platform::enumerateMonitors();
		std::vector<std::string> names;
		for (const platform::MonitorInfo& m : m_monitors) { names.push_back(m.name); }
		const module::detail::PublishedNames list = module::detail::publishNames(names, -1);
		const auto hit = module::detail::findPublished(list, saved);
		if (!hit)
		{
			debug::verboseOnce("host_env.saved_monitor", "前に選んだモニタ " + saved + " が見つからないので、今の画面で始めます");
			return;
		}
		const platform::MonitorInfo& target = m_monitors[list.source[*hit]];
		if (target.handle != monitorHandleOf(window)) { (void)window->moveToMonitor(target.handle); }
	}

	void toggleFullscreen(Engine& engine, Win32Window* window)
	{
		if (window == nullptr) { return; }
		if (window->isFullscreen())
		{
			m_fullscreenAspectMilli = window->fullscreenAspectMilli();
			engine.setFullscreen(false);
		}
		else
		{
			engine.setFullscreen(true, m_fullscreenAspectMilli);
		}
		m_ship.rememberWindowMode(window->isFullscreen());
	}

	[[nodiscard]] std::optional<std::size_t> primaryIndex() const
	{
		for (std::size_t i = 0; i < m_published.monitors.source.size(); ++i)
		{
			const std::size_t source = m_published.monitors.source[i];
			if (source < m_monitors.size() && m_monitors[source].primary) { return i; }
		}
		return std::nullopt;
	}

	/// 位置で割り当てているときだけ、現在の配列からキーの刻印を取得する。HostShip の keyLabelFn と同じ
	void fillKeyLabels(module::detail::HostEnvironment& env, input::KeyMode mode)
	{
		if (mode != input::KeyMode::Position) { return; }
		for (const auto& key : input::detail::kPositionKeys) { module::detail::setKeyLabel(env, key.vk, platform::layoutKeyLabel(key.vk)); }
	}

	void publish(Engine& engine, Win32Window* window)
	{
		module::detail::HostEnvironment env;
		m_monitors = platform::enumerateMonitors();
		const HMONITOR here = window != nullptr ? monitorHandleOf(window) : nullptr;
		std::vector<std::string> names;
		int selectedMonitor = -1;
		for (std::size_t i = 0; i < m_monitors.size(); ++i)
		{
			names.push_back(m_monitors[i].name);
			if (m_monitors[i].handle == here) { selectedMonitor = static_cast<int>(i); }
		}
		env.monitors = module::detail::publishNames(names, selectedMonitor);
		if (selectedMonitor >= 0)
		{
			env.refreshMilliHz = m_monitors[static_cast<std::size_t>(selectedMonitor)].refreshMilliHz;
			env.hdrCapable = m_monitors[static_cast<std::size_t>(selectedMonitor)].hdrCapable ? 1 : 0;
		}
		env.osAccessibility = platform::readOsAccessibility();
		m_keyMode = engine.config().keyMode;
		fillKeyLabels(env, m_keyMode);
		m_layout = platform::currentKeyboardLayoutId();
		m_windowMonitor = here;
		finish(engine, env);
	}
#else
	void publish(Engine& engine)
	{
		module::detail::HostEnvironment env;
		m_keyMode = engine.config().keyMode;
		finish(engine, env);
	}
#endif

	/// 音声出力機器を加えて Engine へ渡す。指定時に表示名から元の名前へ戻せるよう、表示した一覧を保持する
	void finish(Engine& engine, module::detail::HostEnvironment& env)
	{
		m_audioNames = m_audio.deviceNames(m_audioSelected);
		env.audioDevices = module::detail::publishNames(m_audioNames, m_audioSelected);
		m_published = env;
		engine.setHostEnvironment(env);
		m_sentFirst = true;
	}

	HostShip& m_ship;
	HostAudioOutput& m_audio;
	Engine* m_engine = nullptr;
	bool m_attended = false;
	bool m_sentEmpty = false;
	bool m_sentFirst = false;
	bool m_restored = false;
	bool m_hooked = false;
	bool m_toggleFullscreen = false;
	std::atomic<bool> m_dirty{false};
	input::KeyMode m_keyMode = input::KeyMode::Position;
	int m_fullscreenAspectMilli = 0;
	module::detail::HostEnvironment m_published;   ///< 最後に Engine へ渡した読み (頼まれた名前を元へ戻すのに使う)
	Clock::time_point m_audioPollAt{};
	int m_audioPolls = 0;
	std::vector<std::string> m_audioNames;
	int m_audioSelected = -1;
#ifdef _WIN32
	std::vector<platform::MonitorInfo> m_monitors;
	HMONITOR m_windowMonitor = nullptr;
	std::uintptr_t m_layout = 0;
#endif
};

}  // namespace mitiru::host
