#pragma once

/// @file StateStore.hpp
/// @brief MitiruCefBridge の上に乗せる型付き双方向 state bridge (G-05)
///
/// **動機。** 生の bridge (`cefQuery` + `executeJavaScript`) は message-passing
/// を提供するがパターンは提供しない。どのゲームも以下を再発明する:
///   - 「C++ の stat が変わったので新値を JS に push して HUD bar を更新する」
///   - 「JS の button が押されたので型付き C++ handler を呼ぶ」
///   - 「C++ が一回限りの event を発火したい (notification, animation trigger)」
///
/// `StateStore` はこの 3 つをまとめて包む。薄い追加 (additive) layer であり、
/// 既存の `cefQuery` handler はそのまま動き、`executeJavaScript` も残る。
/// JS 側の companion は `web/mitiru_runtime/mitiru_cef_state.js` にある。
///
/// **使い方 (C++ 側):**
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
/// constructor は意図的に callback ベース (`MitiruCefContext&` ではない) にして
/// あり、CEF browser を起動せず store を unit test できる。よくあるケース向けに
/// engine は factory helper `MitiruCefContext::makeStateStore()` を後述する。

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

/// @brief string_view で heterogeneous lookup するための透過ハッシュ。
/// @details std::string キーの map を、string_view から std::string を確保せずに
///          find できる (C++20 の透過 lookup)。std::string / const char* も
///          string_view に変換して同一ハッシュを返す。
struct TransparentStringHash
{
	using is_transparent = void;
	[[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
	{
		return std::hash<std::string_view>{}(sv);
	}
};

/// @brief 保持 state の map 型 (透過 lookup 対応)。
using StateMap =
	std::unordered_map<std::string, json, TransparentStringHash, std::equal_to<>>;

/// @brief 型付き C++↔JS reactive state + event bridge。
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

	// ── C++ → JS: 保持される key-value state ───────────────────────

	/// @brief 値を保存し `window.mitiru.onStateChange` に broadcast する。
	/// @details `json(value)` で nlohmann::json が受け付ける型は何でも可:
	///          bool, int, float, double, std::string, json object/array 等。
	template <typename T>
	void set(std::string_view key, const T& value)
	{
		const json encoded = value;
		std::string snapshot;
		{
			std::lock_guard lock(m_mutex);
			// 既存 key は find で当てて std::string 確保を避ける (透過 lookup)。
			if (auto it = m_state.find(key); it != m_state.end())
			{
				it->second = encoded;
			}
			else
			{
				m_state.emplace(std::string(key), encoded);
			}
			snapshot = encoded.dump();
		}
		pushJs("_onChange", keyJson(key), snapshot);
	}

	/// @brief set() と同じく m_state を即時更新するが、JS push は溜めて
	///        flushBatch() で 1 回の executeJavaScript にまとめる。
	/// @details 毎フレーム多数の key を push する HUD の per-key IPC を 1 回に畳む。
	///          値の dump は error_handler=replace で行い、ゲーム由来の非 UTF-8 byte が
	///          throw して batch 全体を巻き添えにしないようにする (不正 byte は U+FFFD)。
	///          flushBatch() を呼ぶまで JS には届かない。m_state (get/snapshot の真値) は
	///          即時更新される。
	template <typename T>
	void setBatched(std::string_view key, const T& value)
	{
		const json encoded = value;
		{
			std::lock_guard lock(m_mutex);
			if (auto it = m_state.find(key); it != m_state.end())
			{
				// 値が不変なら push しない: batch に積まず flushBatch を空で終わらせる。
				// 静的 HUD フレームは cross-process IPC ゼロになる。
				if (it->second == encoded) { return; }
				it->second = encoded;
			}
			else
			{
				m_state.emplace(std::string(key), encoded);
			}
		}
		constexpr auto kReplace = json::error_handler_t::replace;
		const std::string keyEnc = json(std::string(key)).dump(-1, ' ', false, kReplace);
		const std::string valEnc = encoded.dump(-1, ' ', false, kReplace);
		if (!m_pendingBatch.empty()) { m_pendingBatch += ','; }
		m_pendingBatch.reserve(m_pendingBatch.size() + keyEnc.size() + valEnc.size() + 3);
		m_pendingBatch += '[';
		m_pendingBatch += keyEnc;
		m_pendingBatch += ',';
		m_pendingBatch += valEnc;
		m_pendingBatch += ']';
	}

	/// @brief 溜めた setBatched の変更を 1 回の executeJavaScript で flush する。
	/// @details pending が空なら何もしない。新しい mitiru_cef_state.js では
	///          `_onChangeBatch` を 1 回呼び、古いキャッシュ JS (batch 関数なし) では
	///          `_onChange` を JS 側ループで per-key 適用する — どちらでも IPC は 1 回。
	void flushBatch()
	{
		if (m_pendingBatch.empty()) { return; }
		std::string pairs;
		pairs.swap(m_pendingBatch);
		if (!m_executeJs) { return; }
		std::string code;
		code.reserve(200 + pairs.size());
		code += "if(window.mitiru&&window.mitiru._state){var _b=[";
		code += pairs;
		code += "];var _s=window.mitiru._state;"
		        "if(_s._onChangeBatch){_s._onChangeBatch(_b);}"
		        "else if(_s._onChange){for(var _i=0;_i<_b.length;_i++){"
		        "_s._onChange(_b[_i][0],_b[_i][1]);}}}";
		m_executeJs(code);
	}

	/// @brief 最後に set した値を取得する。
	template <typename T>
	[[nodiscard]] std::optional<T> get(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_state.find(key);
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

	/// @brief 生 JSON へのアクセス (例: logging / debug snapshot 用)。
	[[nodiscard]] std::optional<json> getJson(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_state.find(key);
		if (it == m_state.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	[[nodiscard]] bool has(std::string_view key) const
	{
		std::lock_guard lock(m_mutex);
		return m_state.contains(key);
	}

	void clearState()
	{
		std::lock_guard lock(m_mutex);
		m_state.clear();
	}

	/// @brief 保持中の全 key→value を page に再 push する。
	/// @details OnLoadEnd hook から呼ぶことで、読み込み直後 (または hot-reload
	///          直後) の page が、読み込み完了前に set されていた全 state を
	///          即座に受け取れる。冪等 — JS binder は宣言的で重複配信を安全に
	///          処理する。
	///
	/// **使い方:**
	/// ```cpp
	///   ctx.setLoadEndCallback([&](std::string_view) {
	///       store.replayRetainedState();
	///   });
	/// ```
	void replayRetainedState()
	{
		StateMap snapshot;
		{
			std::lock_guard lock(m_mutex);
			snapshot = m_state;
		}
		for (const auto& [key, value] : snapshot)
		{
			pushJs("_onChange", keyJson(key), value.dump());
		}
	}

	// ── Debug snapshot: view state の export / import ──────────────

	/// @brief 現在保持中の全 key→value ペアを JSON ファイルに書き出す。
	///
	/// **範囲:** これは *観測可能な push 済み state* を snapshot する — `set()`
	/// で set され現在 store に保持されている `view.*` 値 (とその他の key)。
	/// ゲーム内部の `GameMemory` は **取得しない**; それは engine から不透明。
	/// 完全な gameplay time-travel には、ゲームが `GameMemory` を別途
	/// serialize し、この snapshot と協調する必要がある。これは UI が
	/// *表示している* ものの復元に使う。
	///
	/// ファイル形状: フラットな JSON object  `{ "view.hp": 80, "view.x": 120, ... }`
	///
	/// @param path  書き出し先ファイルパス (UTF-8、新規作成または上書き)。
	/// @return 成功時 `true`; I/O error 時 `false`。
	///
	/// **使い方:**
	/// ```cpp
	///   if (!store.saveSnapshot("debug/scene_snapshot.json"))
	///       log::error("saveSnapshot failed");
	/// ```
	[[nodiscard]] bool saveSnapshot(const std::string& path) const
	{
		std::ofstream file(path, std::ios::out | std::ios::trunc);
		if (!file.is_open())
		{
			return false;
		}
		file << snapshotJson(2);
		return file.good();
	}

	/// @brief 保持中の全 key を JSON object 文字列に serialize する。
	/// @details saveSnapshot() と同内容だが in-memory で返す — replay-as-test
	///          (axis 4) が、ゲームの観測可能な push 済み `view.*` state を
	///          per-frame / 最終 assertion blob として捕捉するのに使う。新たな
	///          DLL ABI hook は不要 (ゲームは HUD 用に既にこれを push している)。
	[[nodiscard]] std::string snapshotJson(int indent = -1) const
	{
		json doc = json::object();
		{
			std::lock_guard lock(m_mutex);
			for (const auto& [key, value] : m_state)
			{
				doc[key] = value;
			}
		}
		return doc.dump(indent);
	}

	/// @brief `saveSnapshot()` が書いた snapshot ファイルを読み込んで復元する。
	///
	/// ファイル内の各 key は保持 map に書き込まれ、`set()` や
	/// `replayRetainedState()` と同じ `_onChange` 経路で即座に page へ再 push
	/// される。これにより page reload なしで UI が snapshot state を即時反映する。
	///
	/// **範囲の注意:** snapshot の読み込みは UI 表示 state のみを復元する。
	/// `GameMemory` は復元 **しない**。gameplay logic が次フレームで engine
	/// state を読むと、`GameMemory` が保持している値をそのまま見る。
	///
	/// @param path  読み込むファイルパス (UTF-8)。
	/// @return 成功時 `true`; I/O または JSON parse error 時 `false`。
	///
	/// **使い方:**
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

		// lock の外で push するため entry を snapshot する (replayRetainedState
		// と同パターン — mutex 保持中に JS dispatch を走らせてはいけない)。
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

	/// @brief ゲームの観測可能な interface を表す JSON 記述を返す。
	///
	/// store に現在保持中の全 state key を best-effort な type tag 付きで報告し、
	/// `onAction` で登録された全 action 名を報告する。`onActionFallback` が
	/// 設定されている場合、open-ended な forwarder を示す sentinel entry `"*"`
	/// が actions 配列に追加される。
	///
	/// 返る形状:
	/// ```json
	/// {
	///   "stateKeys": [
	///     {"name": "view.points", "type": "number"},
	///     {"name": "view.shop",   "type": "object"}
	///   ],
	///   "actions": ["tap", "buyClick", "*"]
	/// }
	/// ```
	/// Type tag: `"number"` `"string"` `"object"` `"array"` `"bool"` `"null"`
	///
	/// Thread-safe: 読み取りの間 store mutex を取得する。
	///
	/// **使い方:**
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

	// ── C++ → JS: 一回限りの event ────────────────────────────────

	/// @brief 名前付き event を `window.mitiru.on(name, ...)` listener に発火する。
	/// @details 保持されない — 後から subscribe した listener は取りこぼす。
	///          保持される値には `set()` を使う。
	void emit(std::string_view eventName, const json& payload = json::object())
	{
		pushJs("_onEvent", keyJson(eventName), payload.dump());
	}

	// ── JS → C++: 型付き action dispatch ─────────────────────────

	/// @brief `window.mitiru.dispatch(action, payload)` 用の handler を登録する。
	/// @details handler は CEF UI thread 上で走る (MitiruCefBridge handler と
	///          同じ threading ルール)。応答するには json 値を返す;
	///          fire-and-forget なら `{}` または `json()` を返す。
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

	/// @brief 個別の `onAction()` 登録が無い action 向けの catch-all handler。
	///        `(action_name, payload)` を受け取り、任意の json (または
	///        fire-and-forget なら `{}`) を返してよい。
	/// @details
	/// engine が module-mode (ADR 0005) で使う: DLL は DLL boundary 越しに
	/// C++ handler を登録できないため、engine は到来した action を次フレームの
	/// `InputSnapshot` に `ActionEvent` として queue する fallback を設置する。
	/// これにより engine が事前に一覧を知らなくても DLL が任意の action 名に
	/// 反応できる。
	void onActionFallback(FallbackActionFn fn)
	{
		std::lock_guard lock(m_mutex);
		m_fallbackAction = std::move(fn);
	}

	// ── internal (test 用に public) ───────────────────────────

	/// @brief `{action, payload}` JSON blob から dispatch を駆動する。
	/// @details "state.dispatch" cefQuery handler から呼ばれる。CEF 無しでも
	///          unit test が routing を試せるようここで公開している。
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
		/// mitiru_cef_state.js より前に読み込まれた page に対するガード。
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

	/// @brief key を JSON 文字列リテラルとして serialize する (quote + escape 処理)。
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
	StateMap                                  m_state;
	std::unordered_map<std::string, ActionFn> m_actions;
	FallbackActionFn                          m_fallbackAction;
	/// setBatched が溜める JS array 要素 ("[k,v],[k,v],...")。host frame thread 専用。
	std::string                               m_pendingBatch;
};

} // namespace mitiru::cef
