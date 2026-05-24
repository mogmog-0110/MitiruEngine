#pragma once

/// @file StateStore.hpp
/// @brief Typed two-way state bridge layered on top of MitiruCefBridge (G-05)
///
/// **Motivation.** The raw bridge (`cefQuery` + `executeJavaScript`) gives
/// you message-passing but not a pattern. Every game re-invents:
///   - "C++ stat changed, push the new value to JS so the HUD bar updates"
///   - "JS button pressed, call into a typed C++ handler"
///   - "C++ wants to fire a one-shot event (notification, animation trigger)"
///
/// `StateStore` wraps all three. It is a thin, additive layer: existing
/// `cefQuery` handlers keep working, `executeJavaScript` still exists. The
/// JS companion lives in `web/mitiru_runtime/mitiru_cef_state.js`.
///
/// **Usage (C++ side):**
/// ```cpp
///   mitiru::cef::StateStore store(
///       [&](const std::string& js)          { ctx.executeJavaScript(js); },
///       [&](const std::string& name, auto f){ ctx.registerHandler(name, std::move(f)); });
///
///   store.set("stats.hp", 100);                          // broadcast
///   store.emit("event.raisingEnd", {{"score", 42}});     // one-shot
///   store.onAction("command.select", [&](const auto& p) {
///       selectCommand(p.at("id").get<std::string>());
///       return mitiru::cef::json{};                      // response (optional)
///   });
/// ```
///
/// **Usage (JS side):**
/// ```js
///   window.mitiru.onStateChange('stats.hp', v => hud.setHp(v));
///   window.mitiru.on('event.raisingEnd', p => animator.playEnd(p.score));
///   window.mitiru.dispatch('command.select', { id: 'pushup' });
/// ```
///
/// Constructor is deliberately callback-based (not `MitiruCefContext&`) so
/// the store is unit-testable without a running CEF browser. The engine
/// exposes a factory helper `MitiruCefContext::makeStateStore()` below for
/// the common case.

#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mitiru::cef
{

using json = ::nlohmann::json;

/// @brief Typed C++↔JS reactive state + event bridge.
class StateStore
{
public:
	using ActionFn           = std::function<json(const json& payload)>;
	using FallbackActionFn   = std::function<json(std::string_view action, const json& payload)>;
	using ExecuteJsFn        = std::function<void(const std::string& code)>;
	using HandlerFn          = std::function<std::string(std::string_view payload)>;
	using RegisterHandlerFn  = std::function<void(const std::string& name, HandlerFn fn)>;

	StateStore(ExecuteJsFn executeJs, RegisterHandlerFn registerHandler)
		: m_executeJs(std::move(executeJs))
		, m_registerHandler(std::move(registerHandler))
	{
		installDispatchHandler();
	}

	StateStore(const StateStore&)            = delete;
	StateStore& operator=(const StateStore&) = delete;

	// ── C++ → JS: retained key-value state ───────────────────────

	/// @brief Store a value and broadcast to `window.mitiru.onStateChange`.
	/// @details Any type nlohmann::json accepts via `json(value)` works:
	///          bool, int, float, double, std::string, json object/array, etc.
	template <typename T>
	void set(std::string_view key, const T& value)
	{
		const json encoded = value;
		std::string snapshot;
		{
			std::lock_guard lock(m_mutex);
			m_state[std::string(key)] = encoded;
			snapshot = encoded.dump();
		}
		pushJs("_onChange", keyJson(key), snapshot);
	}

	/// @brief Fetch the last-set value.
	template <typename T>
	[[nodiscard]] std::optional<T> get(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_state.find(std::string(key));
		if (it == m_state.end())
		{
			return std::nullopt;
		}
		try
		{
			return it->second.get<T>();
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}
	}

	/// @brief Raw JSON access (e.g. for logging / debug snapshots).
	[[nodiscard]] std::optional<json> getJson(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_state.find(std::string(key));
		if (it == m_state.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	[[nodiscard]] bool has(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		return m_state.contains(std::string(key));
	}

	void clearState()
	{
		std::lock_guard lock(m_mutex);
		m_state.clear();
	}

	/// @brief Re-push every retained key→value to the page.
	/// @details Call this from an OnLoadEnd hook so a freshly loaded (or
	///          hot-reloaded) page immediately receives all state that was
	///          set before it finished loading.  Idempotent — the JS binder
	///          is declarative and handles duplicate delivery safely.
	///
	/// **Usage:**
	/// ```cpp
	///   ctx.setLoadEndCallback([&](std::string_view) {
	///       store.replayRetainedState();
	///   });
	/// ```
	void replayRetainedState()
	{
		std::unordered_map<std::string, json> snapshot;
		{
			std::lock_guard lock(m_mutex);
			snapshot = m_state;
		}
		for (const auto& [key, value] : snapshot)
		{
			pushJs("_onChange", keyJson(key), value.dump());
		}
	}

	// ── Debug snapshot: export / import view state ──────────────

	/// @brief Write all currently-retained key→value pairs to a JSON file.
	///
	/// **Scope:** This snapshots the *observable pushed state* — the `view.*`
	/// values (and any other keys) that were set via `set()` and are currently
	/// retained in the store.  It does **not** capture the game's internal
	/// `GameMemory`; that is opaque to the engine.  Full gameplay time-travel
	/// requires the game to serialize `GameMemory` separately and coordinate
	/// with this snapshot.  Use this for restoring what the UI *displays*.
	///
	/// File shape: a flat JSON object  `{ "view.hp": 80, "view.x": 120, ... }`
	///
	/// @param path  File path to write (UTF-8, created or overwritten).
	/// @return `true` on success; `false` on I/O error.
	///
	/// **Usage:**
	/// ```cpp
	///   if (!store.saveSnapshot("debug/scene_snapshot.json"))
	///       log::error("saveSnapshot failed");
	/// ```
	[[nodiscard]] bool saveSnapshot(const std::string& path) const
	{
		json doc = json::object();
		{
			std::lock_guard lock(m_mutex);
			for (const auto& [key, value] : m_state)
			{
				doc[key] = value;
			}
		}
		std::ofstream file(path, std::ios::out | std::ios::trunc);
		if (!file.is_open())
		{
			return false;
		}
		file << doc.dump(2);
		return file.good();
	}

	/// @brief Load a snapshot file written by `saveSnapshot()` and restore it.
	///
	/// Each key from the file is written into the retained map and immediately
	/// re-pushed to the page via the same `_onChange` path that `set()` and
	/// `replayRetainedState()` use.  This makes the UI reflect the snapshot
	/// state instantly without a page reload.
	///
	/// **Scope caveat:** loading a snapshot restores UI display state only.
	/// It does NOT restore `GameMemory`.  If gameplay logic reads engine state
	/// on the next frame, it will still see whatever `GameMemory` holds.
	///
	/// @param path  File path to read (UTF-8).
	/// @return `true` on success; `false` on I/O or JSON parse error.
	///
	/// **Usage:**
	/// ```cpp
	///   if (!store.loadSnapshot("debug/scene_snapshot.json"))
	///       log::error("loadSnapshot failed");
	/// ```
	[[nodiscard]] bool loadSnapshot(const std::string& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			return false;
		}
		json doc;
		try
		{
			file >> doc;
		}
		catch (const std::exception&)
		{
			return false;
		}
		if (!doc.is_object())
		{
			return false;
		}

		// Snapshot the entries to push outside the lock (same pattern as
		// replayRetainedState — JS dispatch must not run while mutex is held).
		std::unordered_map<std::string, json> loaded;
		loaded.reserve(doc.size());
		for (const auto& [key, value] : doc.items())
		{
			loaded[key] = value;
		}
		{
			std::lock_guard lock(m_mutex);
			for (const auto& [key, value] : loaded)
			{
				m_state[key] = value;
			}
		}
		for (const auto& [key, value] : loaded)
		{
			pushJs("_onChange", keyJson(key), value.dump());
		}
		return true;
	}

	// ── Interface schema ────────────────────────────────────────

	/// @brief Returns a JSON description of the game's observable interface.
	///
	/// Reports every state key currently retained in the store with a
	/// best-effort type tag, and every action name registered via `onAction`.
	/// If an `onActionFallback` is installed a sentinel entry `"*"` is
	/// appended to the actions array to signal an open-ended forwarder.
	///
	/// Returned shape:
	/// ```json
	/// {
	///   "stateKeys": [
	///     {"name": "view.points", "type": "number"},
	///     {"name": "view.shop",   "type": "object"}
	///   ],
	///   "actions": ["tap", "buyClick", "*"]
	/// }
	/// ```
	/// Type tags: `"number"` `"string"` `"object"` `"array"` `"bool"` `"null"`
	///
	/// Thread-safe: acquires the store mutex for the duration of the read.
	///
	/// **Usage:**
	/// ```cpp
	///   std::cout << store.schemaJson() << '\n';
	/// ```
	[[nodiscard]] std::string schemaJson() const
	{
		std::lock_guard lock(m_mutex);

		json stateKeys = json::array();
		for (const auto& [key, value] : m_state)
		{
			std::string_view tag;
			if      (value.is_number())  { tag = "number"; }
			else if (value.is_string())  { tag = "string"; }
			else if (value.is_object())  { tag = "object"; }
			else if (value.is_array())   { tag = "array";  }
			else if (value.is_boolean()) { tag = "bool";   }
			else                         { tag = "null";   }

			stateKeys.push_back({{"name", key}, {"type", tag}});
		}

		json actions = json::array();
		for (const auto& [name, fn] : m_actions)
		{
			actions.push_back(name);
		}
		if (m_fallbackAction)
		{
			actions.push_back("*");
		}

		return json{{"stateKeys", std::move(stateKeys)},
		            {"actions",   std::move(actions)}}.dump();
	}

	// ── C++ → JS: one-shot event ────────────────────────────────

	/// @brief Fire a named event to `window.mitiru.on(name, ...)` listeners.
	/// @details Not retained — late-subscribing listeners miss it. For
	///          retained values use `set()`.
	void emit(std::string_view eventName, const json& payload = json::object())
	{
		pushJs("_onEvent", keyJson(eventName), payload.dump());
	}

	// ── JS → C++: typed action dispatch ─────────────────────────

	/// @brief Register a handler for `window.mitiru.dispatch(action, payload)`.
	/// @details The handler runs on the CEF UI thread (same threading rules
	///          as MitiruCefBridge handlers). Return a json value to respond;
	///          return `{}` or `json()` for fire-and-forget.
	void onAction(std::string_view action, ActionFn fn)
	{
		std::lock_guard lock(m_mutex);
		m_actions[std::string(action)] = std::move(fn);
	}

	void offAction(std::string_view action)
	{
		std::lock_guard lock(m_mutex);
		m_actions.erase(std::string(action));
	}

	/// @brief Catch-all handler for actions that have no specific `onAction()`
	///        registration. Receives `(action_name, payload)` and may return
	///        any json (or `{}` for fire-and-forget).
	/// @details
	/// Used by the engine in module-mode (ADR 0005): the DLL doesn't get to
	/// register C++ handlers from across the DLL boundary, so the engine
	/// installs a fallback that queues incoming actions as `ActionEvent`s
	/// into next frame's `InputSnapshot`. This lets the DLL react to any
	/// action name without engine knowing the list ahead of time.
	void onActionFallback(FallbackActionFn fn)
	{
		std::lock_guard lock(m_mutex);
		m_fallbackAction = std::move(fn);
	}

	// ── internal (public for testing) ───────────────────────────

	/// @brief Drive a dispatch from a `{action, payload}` JSON blob.
	/// @details Called from the "state.dispatch" cefQuery handler. Exposed
	///          here so unit tests can exercise routing without CEF.
	std::string dispatchFromJson(std::string_view payloadJson) const
	{
		json parsed;
		try { parsed = json::parse(payloadJson); }
		catch (const std::exception& e)
		{
			return errorJson(std::string("state.dispatch: invalid JSON: ") + e.what());
		}

		if (!parsed.is_object() || !parsed.contains("action"))
		{
			return errorJson("state.dispatch: missing 'action' field");
		}

		const auto action = parsed.at("action").get<std::string>();
		const json payload = parsed.value("payload", json::object());

		ActionFn         fn;
		FallbackActionFn fallback;
		{
			std::lock_guard lock(m_mutex);
			const auto it = m_actions.find(action);
			if (it != m_actions.end())
			{
				fn = it->second;
			}
			else
			{
				fallback = m_fallbackAction;
			}
		}

		try
		{
			if (fn)
			{
				return fn(payload).dump();
			}
			if (fallback)
			{
				return fallback(action, payload).dump();
			}
			return errorJson("state.dispatch: unknown action '" + action + "'");
		}
		catch (const std::exception& e)
		{
			return errorJson(std::string("state.dispatch: handler threw: ") + e.what());
		}
	}

private:
	void installDispatchHandler()
	{
		if (!m_registerHandler)
		{
			return;
		}
		m_registerHandler("state.dispatch",
			[this](std::string_view payload) -> std::string
			{
				return this->dispatchFromJson(payload);
			});
	}

	void pushJs(std::string_view method,
	            const std::string& keyJsonArg,
	            const std::string& valueJsonArg) const
	{
		if (!m_executeJs)
		{
			return;
		}
		/// Guard against pages that loaded before mitiru_cef_state.js.
		std::string code;
		code.reserve(128 + keyJsonArg.size() + valueJsonArg.size());
		code += "if (window.mitiru && window.mitiru._state) { window.mitiru._state.";
		code += method;
		code += "(";
		code += keyJsonArg;
		code += ", ";
		code += valueJsonArg;
		code += "); }";
		m_executeJs(code);
	}

	/// @brief Serialize a key as a JSON string literal (handles quoting + escapes).
	static std::string keyJson(std::string_view key)
	{
		return json(std::string(key)).dump();
	}

	static std::string errorJson(const std::string& message)
	{
		return json{{"error", message}}.dump();
	}

	ExecuteJsFn                               m_executeJs;
	RegisterHandlerFn                         m_registerHandler;
	mutable std::mutex                        m_mutex;
	std::unordered_map<std::string, json>     m_state;
	std::unordered_map<std::string, ActionFn> m_actions;
	FallbackActionFn                          m_fallbackAction;
};

} // namespace mitiru::cef
