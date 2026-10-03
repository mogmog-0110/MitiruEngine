#pragma once

/// @file HostShip.hpp
/// @brief 出荷に要る host の仕事 (セーブの置き場、利用者の設定、キー割り当て、Steamworks) を mitiru_host に繋ぐ。
/// @details 何も指定しなければ何もしない (開発中の mitiru_host は今までどおり作業フォルダの save/ を使う)。
///          ゲーム名が決まると (DLL の隣の ship.json か --game-name)、セーブは %APPDATA%/<game>/saves、設定は
///          %APPDATA%/<game>/settings.json になる。DLL の隣に input_actions.json があればキー割り当てが効く。
///          設定画面 (RmlUi) の操作は "settings.*" と "slots.*" の名前で host が受け、ゲームへは渡さない。
///          RML の <img src="glyph:jump"/> は、操作の割り当てと今使っている機器 (キーボードかパッドか) とパッドの
///          機種から絵柄を引く。前の実行のクラッシュ報告は HostCrashInbox が扱う ("crash.*")。

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "HostCrashInbox.hpp"

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/HostCrashHandler.hpp>
#include <mitiru/input/ActionRemapper.hpp>
#include <mitiru/input/GlyphLookup.hpp>
#include <mitiru/input/PadGlyphs.hpp>
#include <mitiru/save/UserDataDir.hpp>
#include <mitiru/settings/ApplySettings.hpp>
#include <mitiru/settings/SettingsFile.hpp>
#include <mitiru/steam/AchievementIntents.hpp>
#include <mitiru/steam/SteamService.hpp>

namespace mitiru::host
{

struct ShipArgs
{
	std::string gameName;      ///< --game-name
	std::string saveDir;       ///< --save-dir
	std::string settingsPath;  ///< --settings
	bool interactive = false;  ///< 人が遊ぶ起動 (headless・台本・リプレイ・キャプチャでない)。確認画面はこの時だけ出す
};

/// @brief DLL の隣の ship.json。出荷するゲームが置く。
struct ShipManifest
{
	std::string gameName;
	std::uint32_t gameVersion = 0;
	int autosaveSlots = 3;
	std::uint32_t steamAppId = 0;
	bool restartThroughSteam = false;
	std::string crashReportUrl;  ///< クラッシュ報告の送り先 (https)。空なら送らない
};

[[nodiscard]] inline ShipManifest readShipManifest(const std::filesystem::path& file)
{
	ShipManifest m;
	const auto bytes = save::readWholeFile(file);
	if (!bytes) { return m; }
	const auto j = nlohmann::json::parse(bytes->begin(), bytes->end(), nullptr, false);
	if (!j.is_object())
	{
		std::fprintf(stderr, "[mitiru_host] ship.json が JSON として読めない: %s\n", file.string().c_str());
		return m;
	}
	m.gameName = j.value("gameName", std::string());
	m.gameVersion = j.value("gameVersion", 0u);
	m.autosaveSlots = j.value("autosaveSlots", 3);
	m.steamAppId = j.value("steamAppId", 0u);
	m.restartThroughSteam = j.value("restartThroughSteam", false);
	m.crashReportUrl = j.value("crashReportUrl", std::string());
	return m;
}

class HostShip
{
public:
	/// @brief runModule の前に呼ぶ。Steam 経由で起動し直す時は false (host はすぐ終わる)。
	bool configure(const std::filesystem::path& dllPath, const ShipArgs& args, EngineConfig& cfg)
	{
		const auto dllDir = dllPath.parent_path();
		const ShipManifest ship = readShipManifest(dllDir / "ship.json");
		const std::string gameName = !args.gameName.empty() ? args.gameName : ship.gameName;
		const std::filesystem::path dataDir = gameName.empty() ? std::filesystem::path() : save::gameDataDir(gameName);
		if (!gameName.empty()) { debug::setCrashGameName(gameName); }
		cfg.gameVersion = ship.gameVersion;
		cfg.autosaveSlots = ship.autosaveSlots;
		if (!args.saveDir.empty()) { cfg.saveDir = args.saveDir; }
		else if (!dataDir.empty()) { cfg.saveDir = (dataDir / "saves").string(); }
		m_settingsPath = !args.settingsPath.empty() ? std::filesystem::path(args.settingsPath)
		               : (dataDir.empty() ? std::filesystem::path() : dataDir / "settings.json");
		loadSettings(cfg);
		if (!gameName.empty()) { m_crash.configure(ship.crashReportUrl, m_settings.privacy.crashReports, args.interactive, cfg); }
		loadActions(dllDir / "input_actions.json");
		m_active = !m_settingsPath.empty() || m_remapActive;
		cfg.uiActionFilter = [this](std::string_view name, std::string_view payload) { return onUiAction(name, payload); };
		return startSteam(ship);
	}

	/// @brief Engine を作った後、runModule の前に呼ぶ。
	void attach(Engine& engine)
	{
		m_engine = &engine;
		if (m_steam.available())
		{
			m_mirror = std::make_unique<steam::SteamCloudMirror>(m_steam);
			engine.setSaveMirror(m_mirror.get());
		}
	}

	[[nodiscard]] bool remapActive() const noexcept { return m_remapActive; }

	/// @brief moduleInputRemap から毎フレーム呼ぶ。組み替えた後の操作を actionsDown などにも書く (ABI v48)。
	void remap(module::InputSnapshot& snap)
	{
		if (!m_dllActionsChecked && m_engine != nullptr)
		{
			m_dllActionsChecked = true;
			loadDllActions(m_engine->moduleActionManifestJson());
		}
		m_device = input::lastUsedDevice(snap, m_device);
		if (!m_remapActive) { return; }
		m_remapper.apply(snap);
		const std::uint64_t down = m_remapper.downMask();
		snap.actionsDown = down;
		snap.actionsPressed = down & ~m_prevActions;
		snap.actionsReleased = m_prevActions & ~down;
		m_prevActions = down;
	}

	/// @brief on_update の後に毎回呼ぶ。ゲームの実績の依頼を Steam へ渡す (Steam が無ければ捨てる)。
	void onModuleFrame(const module::FrameIntents& intents) { (void)steam::applyAchievementIntents(m_steam, intents); }

	/// @brief onFrameStart から毎フレーム呼ぶ。
	void onFrame(Engine& engine)
	{
		m_steam.runCallbacks();
		pauseForOverlay(engine);
		updateGlyphs(engine);
		m_crash.onFrame(engine);
		if (!m_active) { return; }
		if (!m_started)
		{
			m_started = true;
			settings::applySettingsAfterStart(engine, m_settings);
			pushBindings(engine);
		}
		takeCapture();
		if (m_remapper.capturing() != m_lastCapturing)
		{
			m_lastCapturing = m_remapper.capturing();
			pushBindings(engine);
		}
		if (m_pendingSlotList) { pushSlotList(engine); }
		if (!(m_applied == m_settings)) { applyChanges(engine); }
	}

	[[nodiscard]] steam::SteamService& steam() noexcept { return m_steam; }

private:
	void loadSettings(EngineConfig& cfg)
	{
		if (m_settingsPath.empty()) { return; }
		const settings::SettingsLoad r = settings::loadUserSettings(m_settingsPath, m_settings);
		for (const auto& w : r.warnings) { std::fprintf(stderr, "[mitiru_host] settings: %s\n", w.c_str()); }
		m_settings = r.settings;
		m_applied = m_settings;
		settings::applyStartupSettings(cfg, m_settings);
		std::fprintf(stderr, "[mitiru_host] settings: %s%s\n", m_settingsPath.string().c_str(),
		             r.fromFile ? "" : " (無いので既定値。変えた時に作る)");
	}

	void loadActions(const std::filesystem::path& file)
	{
		const auto bytes = save::readWholeFile(file);
		if (!bytes) { return; }
		(void)useActionManifest(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
		                        "input_actions.json");
	}

	/// ゲーム DLL が MITIRU_ACTIONS で表を export していれば、DLL の隣の input_actions.json より優先する
	void loadDllActions(const char* json)
	{
		if (json == nullptr) { return; }
		if (useActionManifest(json, "MITIRU_ACTIONS")) { m_active = true; }
	}

	bool useActionManifest(std::string_view json, const char* origin)
	{
		input::ManifestLoad load = input::parseActionManifest(json);
		for (const auto& e : load.errors) { std::fprintf(stderr, "[mitiru_host] %s: %s\n", origin, e.c_str()); }
		if (!load.errors.empty())
		{
			std::fprintf(stderr, "[mitiru_host] %s に誤りがあるので、キー割り当ては使わない\n", origin);
			return false;
		}
		m_actions = std::move(load.manifest);
		configureRemapper();
		m_remapActive = true;
		return true;
	}

	void configureRemapper()
	{
		input::RemapOptions opt;
		opt.mouseSensitivity = m_settings.input.mouseSensitivity;
		opt.invertMouseY = m_settings.input.invertMouseY;
		opt.toggleActions = m_settings.input.toggleActions;
		if (const std::size_t dropped = m_remapper.configure(m_actions, m_settings.input.bindings, opt); dropped > 0)
		{
			std::fprintf(stderr, "[mitiru_host] キー割り当て: 上限 (1 操作 %zu 個) を超えた %zu 個は効かない\n",
			             input::kMaxBindingsPerAction, dropped);
		}
	}

	bool startSteam(const ShipManifest& ship)
	{
		const steam::SteamInitResult r = m_steam.init({ ship.steamAppId, ship.restartThroughSteam });
		if (r == steam::SteamInitResult::RestartRequired)
		{
			std::fprintf(stderr, "[mitiru_host] Steam 経由で起動し直す\n");
			return false;
		}
		if (r == steam::SteamInitResult::SteamNotRunning)
		{
			std::fprintf(stderr, "[mitiru_host] Steam に繋がらない。実績と Steam Cloud は使わずに続ける\n");
		}
		return true;
	}

	/// オーバーレイ (Shift+Tab) を開いている間は止める。止めたのが自分の時だけ戻す。
	void pauseForOverlay(Engine& engine)
	{
		const bool overlay = m_steam.overlayActive();
		if (overlay && !m_pausedByOverlay && !engine.isPaused())
		{
			engine.setPauseKind(EngineConfig::kPauseKindIngame);
			engine.setPaused(true);
			m_pausedByOverlay = true;
		}
		else if (!overlay && m_pausedByOverlay)
		{
			// 開いている間に F8 などで止め直された (種類が変わった) 時は、その停止を残す
			if (engine.config().pauseKind == EngineConfig::kPauseKindIngame) { engine.setPaused(false); }
			m_pausedByOverlay = false;
		}
	}

	void takeCapture()
	{
		const auto got = m_remapper.takeCaptured();
		if (!got || got->cancelled) { return; }
		const input::ActionDef* def = m_actions.find(got->actionId);
		if (def == nullptr) { return; }
		std::vector<input::InputSource> list = input::effectiveBindings(*def, m_settings.input.bindings);
		const auto at = static_cast<std::size_t>(got->index < 0 ? 0 : got->index);
		if (at < list.size()) { list[at] = got->source; }
		else if (list.size() < input::kMaxBindingsPerAction) { list.push_back(got->source); }
		else { list.back() = got->source; }
		m_settings.input.bindings[got->actionId] = std::move(list);
	}

	void applyChanges(Engine& engine)
	{
		const std::uint32_t changes = settings::diffSettings(m_applied, m_settings);
		settings::applyLiveSettings(engine, m_settings, changes);
		engine.noteSettingsChanged(changes);
		if (changes & settings::kChangeInput)
		{
			configureRemapper();
			pushBindings(engine);
			engine.uiHost().refreshGlyphs();
		}
		m_applied = m_settings;
		std::string error;
		if (!m_settingsPath.empty() && !settings::saveUserSettings(m_settingsPath, m_settings, &error))
		{
			std::fprintf(stderr, "[mitiru_host] settings を保存できない: %s\n", error.c_str());
		}
	}

	[[nodiscard]] input::PadKind padKind(const Engine& engine) const noexcept
	{
		for (int i = 0; i < input::kGamepadSlots; ++i)
		{
			if (engine.gamepads().connected(i)) { return engine.gamepads().info(i).kind; }
		}
		return input::PadKind::Unknown;
	}

	/// 操作ごとの割り当てを "view.settings_binding_<id>" へ (" / " 区切りの表示名)。重なりは settings_conflicts へ。
	void pushBindings(Engine& engine)
	{
		if (!m_remapActive) { return; }
		const input::PadKind kind = padKind(engine);
		for (const input::ActionDef& a : m_actions.actions)
		{
			std::string text;
			for (const auto& s : input::effectiveBindings(a, m_settings.input.bindings))
			{
				text += (text.empty() ? "" : " / ") + input::inputGlyph(s, kind).label;
			}
			engine.uiHost().setText("view.settings_binding_" + a.id, text);
		}
		std::string conflicts;
		for (const auto& c : input::findBindingConflicts(m_actions, m_settings.input.bindings))
		{
			conflicts += c.actionA + " / " + c.actionB + ": " + input::formatInputSource(c.source) + "\n";
		}
		engine.uiHost().setText("view.settings_conflicts", conflicts);
		engine.uiHost().setBool("view.settings_rebinding", m_remapper.capturing());
	}

	void pushSlotList(Engine& engine)
	{
		m_pendingSlotList = false;
		nlohmann::json list = nlohmann::json::array();
		for (const auto& s : engine.saveSlotStore().list())
		{
			list.push_back({ { "slot", s.slot }, { "status", save::slotStatusText(s.status) },
			                 { "chapter", s.meta.chapter }, { "savedAt", s.meta.savedAtUnixSec },
			                 { "playtime", s.meta.playtimeSec }, { "thumbnail", s.hasThumbnail } });
		}
		engine.uiHost().setText("view.save_slots", list.dump());
		if (!m_slotsToDelete.empty())
		{
			auto store = engine.saveSlotStore();
			for (const auto& slot : m_slotsToDelete) { (void)store.remove(slot); }
			m_slotsToDelete.clear();
			m_pendingSlotList = true;
		}
	}

	/// "glyph:" の絵柄。割り当ては利用者の設定を通した後のもの
	static std::string glyphOf(void* ctx, std::string_view name)
	{
		const auto& self = *static_cast<const HostShip*>(ctx);
		return input::glyphFor(name, self.m_actions, self.m_settings.input.bindings, self.m_device, self.m_glyphPadKind);
	}

	void updateGlyphs(Engine& engine)
	{
		auto& ui = engine.uiHost();
		if (!ui.active()) { return; }
		const input::PadKind kind = padKind(engine);
		if (m_glyphsHooked && kind == m_glyphPadKind && m_device == m_glyphDevice) { return; }
		m_glyphPadKind = kind;
		m_glyphDevice = m_device;
		if (!m_glyphsHooked) { ui.setGlyphSource(&HostShip::glyphOf, this); }
		else { ui.refreshGlyphs(); }
		m_glyphsHooked = true;
	}

	bool onUiAction(std::string_view name, std::string_view payloadJson)
	{
		if (m_crash.onUiAction(name, m_engine, m_settings.privacy.crashReports))
		{
			m_active = true;
			return true;
		}
		if (name.rfind("settings.", 0) != 0 && name.rfind("slots.", 0) != 0) { return false; }
		m_active = true;
		const auto p = nlohmann::json::parse(payloadJson, nullptr, false);
		const nlohmann::json payload = p.is_object() ? p : nlohmann::json::object();
		if (name == "settings.set") { setValue(payload); return true; }
		if (name == "settings.reset") { m_settings = settings::UserSettings{}; return true; }
		if (name == "settings.rebind")
		{
			(void)m_remapper.beginCapture(payload.value("action", std::string()), payload.value("index", 0));
			return true;
		}
		if (name == "settings.rebind_cancel") { m_remapper.cancelCapture(); return true; }
		if (name == "settings.reset_binding")
		{
			const std::string action = payload.value("action", std::string());
			if (action.empty()) { m_settings.input.bindings.clear(); }
			else { m_settings.input.bindings = input::resetBinding(m_settings.input.bindings, action); }
			return true;
		}
		if (name == "slots.list") { m_pendingSlotList = true; return true; }
		if (name == "slots.delete")
		{
			m_slotsToDelete.push_back(payload.value("slot", std::string()));
			m_pendingSlotList = true;
			return true;
		}
		return false;
	}

	void setValue(const nlohmann::json& payload)
	{
		std::string error;
		const auto next = settings::applySettingValue(m_settings, payload.value("key", std::string()),
		                                              payload.contains("value") ? payload["value"] : nlohmann::json(), &error);
		if (!error.empty()) { std::fprintf(stderr, "[mitiru_host] settings.set: %s\n", error.c_str()); }
		if (next) { m_settings = *next; }
	}

	settings::UserSettings m_settings;
	settings::UserSettings m_applied;
	std::filesystem::path m_settingsPath;
	input::ActionManifest m_actions;
	input::ActionRemapper m_remapper;
	bool m_remapActive = false;
	bool m_started = false;
	bool m_active = false;          ///< 設定かキー割り当てを使う時だけ毎フレームの仕事をする
	bool m_lastCapturing = false;
	bool m_pendingSlotList = false;
	std::vector<std::string> m_slotsToDelete;
	steam::SteamService m_steam;
	std::unique_ptr<steam::SteamCloudMirror> m_mirror;
	bool m_pausedByOverlay = false;
	Engine* m_engine = nullptr;
	bool m_dllActionsChecked = false;
	std::uint64_t m_prevActions = 0;
	input::InputDevice m_device = input::InputDevice::KeyboardMouse;
	input::InputDevice m_glyphDevice = input::InputDevice::KeyboardMouse;
	input::PadKind m_glyphPadKind = input::PadKind::Unknown;
	bool m_glyphsHooked = false;
	HostCrashInbox m_crash;
};

}  // namespace mitiru::host
