#pragma once

/// @file ScriptingHook.hpp
/// @brief 外部スクリプト言語統合インターフェース
/// @details Consumer 側で実装した任意のスクリプト言語ランタイムを VN システムに
///          接続するためのプラグインインターフェース。IScriptingEngine を実装する
///          ことで差し替え可能。Engine 本体は C++ gameplay 方針 (二言語依存を
///          避ける) により scripting 実装を同梱しない。
///          VNScriptBridge により VN システムの関数を自動登録し、FlagManager との
///          双方向同期を提供する。
///
/// @code
/// // 外部スクリプトエンジンのバインディング例 (consumer 側で IScriptingEngine 実装)
/// auto engine = std::make_unique<MyScriptingEngine>();
///
/// // VNブリッジに接続
/// mitiru::vn::VNScriptBridge bridge(std::move(engine), flagManager);
/// bridge.registerVNFunctions();
///
/// // シナリオスクリプトから呼び出し:
/// //   @script
/// //   show_character("sakura", "center")
/// //   play_bgm("morning.ogg")
/// //   set_flag("met_sakura", true)
/// //   @endscript
/// //
/// //   @eval affection >= 5
/// @endcode

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <mitiru/vn/FlagManager.hpp>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  データ型
// ════════════════════════════════════════════════════════════════════

/// @brief スクリプト実行結果の値型
using ScriptValue = std::variant<bool, int, float, std::string, std::nullptr_t>;

/// @brief スクリプト実行結果
struct ScriptResult
{
	bool success = false;						///< 実行成功か
	ScriptValue value = nullptr;				///< 戻り値
	std::string error;							///< エラーメッセージ（失敗時）

	/// @brief 成功結果を生成する
	[[nodiscard]] static ScriptResult ok(ScriptValue val = nullptr)
	{
		return {true, std::move(val), ""};
	}

	/// @brief エラー結果を生成する
	[[nodiscard]] static ScriptResult fail(const std::string& msg)
	{
		return {false, nullptr, msg};
	}

	/// @brief bool値として取得する
	[[nodiscard]] bool asBool(bool defaultValue = false) const
	{
		if (!success) { return defaultValue; }
		if (auto* b = std::get_if<bool>(&value)) { return *b; }
		if (auto* i = std::get_if<int>(&value)) { return *i != 0; }
		if (auto* f = std::get_if<float>(&value)) { return *f != 0.0f; }
		if (auto* s = std::get_if<std::string>(&value)) { return !s->empty(); }
		return defaultValue;
	}

	/// @brief int値として取得する
	[[nodiscard]] int asInt(int defaultValue = 0) const
	{
		if (!success) { return defaultValue; }
		if (auto* i = std::get_if<int>(&value)) { return *i; }
		if (auto* b = std::get_if<bool>(&value)) { return *b ? 1 : 0; }
		if (auto* f = std::get_if<float>(&value)) { return static_cast<int>(*f); }
		return defaultValue;
	}

	/// @brief float値として取得する
	[[nodiscard]] float asFloat(float defaultValue = 0.0f) const
	{
		if (!success) { return defaultValue; }
		if (auto* f = std::get_if<float>(&value)) { return *f; }
		if (auto* i = std::get_if<int>(&value)) { return static_cast<float>(*i); }
		if (auto* b = std::get_if<bool>(&value)) { return *b ? 1.0f : 0.0f; }
		return defaultValue;
	}

	/// @brief string値として取得する
	[[nodiscard]] std::string asString(const std::string& defaultValue = "") const
	{
		if (!success) { return defaultValue; }
		if (auto* s = std::get_if<std::string>(&value)) { return *s; }
		if (auto* b = std::get_if<bool>(&value)) { return *b ? "true" : "false"; }
		if (auto* i = std::get_if<int>(&value)) { return std::to_string(*i); }
		if (auto* f = std::get_if<float>(&value)) { return std::to_string(*f); }
		return defaultValue;
	}
};

/// @brief ネイティブ関数コールバック型
/// @details スクリプトエンジンからC++関数を呼び出す際のインターフェース。
///          引数はScriptValueのベクターで渡される。
using NativeFunction = std::function<ScriptResult(const std::vector<ScriptValue>& args)>;

// ════════════════════════════════════════════════════════════════════
//  IScriptingEngine。スクリプトエンジン抽象インターフェース
// ════════════════════════════════════════════════════════════════════

/// @brief スクリプトエンジンの抽象インターフェース
/// @details Lua, Python, JavaScript等の具体実装はこのインターフェースを実装する。
class IScriptingEngine
{
public:
	virtual ~IScriptingEngine() = default;

	/// @brief スクリプトコードを実行する
	/// @param code スクリプトソースコード
	/// @return 実行結果
	[[nodiscard]] virtual ScriptResult execute(const std::string& code) = 0;

	/// @brief 条件式を評価する
	/// @param expression 条件式
	/// @return 真偽値
	[[nodiscard]] virtual bool evaluateCondition(const std::string& expression) = 0;

	/// @brief 名前付き関数を呼び出す
	/// @param name 関数名
	/// @param args 引数リスト
	/// @return 実行結果
	[[nodiscard]] virtual ScriptResult callFunction(const std::string& name,
	                                                const std::vector<ScriptValue>& args) = 0;

	/// @brief ネイティブ（C++）関数を登録する
	/// @param name スクリプト側での関数名
	/// @param callback コールバック関数
	virtual void registerNativeFunction(const std::string& name, NativeFunction callback) = 0;

	/// @brief グローバル変数を設定する
	/// @param name 変数名
	/// @param value 値
	virtual void setGlobal(const std::string& name, ScriptValue value) = 0;

	/// @brief グローバル変数を取得する
	/// @param name 変数名
	/// @return 値（存在しない場合はnullopt）
	[[nodiscard]] virtual std::optional<ScriptValue> getGlobal(const std::string& name) const = 0;

	/// @brief スクリプトエンジンの状態をリセットする
	virtual void reset() = 0;
};

// ════════════════════════════════════════════════════════════════════
//  NullScriptingEngine。テスト用スタブ実装
// ════════════════════════════════════════════════════════════════════

/// @brief テスト用スクリプトエンジンスタブ
/// @details 実際のスクリプト言語ランタイムなしで動作する。
///          登録されたネイティブ関数の呼び出しとグローバル変数の管理を行い、
///          実行されたコマンドをログとして記録する。
class NullScriptingEngine final : public IScriptingEngine
{
public:
	[[nodiscard]] ScriptResult execute(const std::string& code) override
	{
		m_executionLog.push_back("execute: " + code);
		return ScriptResult::ok();
	}

	[[nodiscard]] bool evaluateCondition(const std::string& expression) override
	{
		m_executionLog.push_back("eval: " + expression);

		// 登録済みネイティブ関数で簡易評価を試行
		// "true" / "false" リテラルの直接判定
		if (expression == "true") { return true; }
		if (expression == "false") { return false; }

		// グローバル変数名の直接参照
		auto it = m_globals.find(expression);
		if (it != m_globals.end())
		{
			const auto& val = it->second;
			if (auto* b = std::get_if<bool>(&val)) { return *b; }
			if (auto* i = std::get_if<int>(&val)) { return *i != 0; }
			if (auto* f = std::get_if<float>(&val)) { return *f != 0.0f; }
			if (auto* s = std::get_if<std::string>(&val)) { return !s->empty(); }
		}

		return false;
	}

	[[nodiscard]] ScriptResult callFunction(const std::string& name,
	                                        const std::vector<ScriptValue>& args) override
	{
		m_executionLog.push_back("call: " + name + " (args: " + std::to_string(args.size()) + ")");

		auto it = m_nativeFunctions.find(name);
		if (it != m_nativeFunctions.end())
		{
			return it->second(args);
		}
		return ScriptResult::fail("function not found: " + name);
	}

	void registerNativeFunction(const std::string& name, NativeFunction callback) override
	{
		m_nativeFunctions[name] = std::move(callback);
		m_executionLog.push_back("register: " + name);
	}

	void setGlobal(const std::string& name, ScriptValue value) override
	{
		m_globals[name] = std::move(value);
	}

	[[nodiscard]] std::optional<ScriptValue> getGlobal(const std::string& name) const override
	{
		auto it = m_globals.find(name);
		if (it != m_globals.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	void reset() override
	{
		m_globals.clear();
		m_nativeFunctions.clear();
		m_executionLog.clear();
	}

	// ── テスト・デバッグ用 ────────────────────────────────────

	/// @brief 実行ログを取得する
	[[nodiscard]] const std::vector<std::string>& executionLog() const noexcept
	{
		return m_executionLog;
	}

	/// @brief 実行ログをクリアする
	void clearLog() noexcept { m_executionLog.clear(); }

	/// @brief 登録済みネイティブ関数名一覧を取得する
	[[nodiscard]] std::vector<std::string> registeredFunctions() const
	{
		std::vector<std::string> names;
		names.reserve(m_nativeFunctions.size());
		for (const auto& [name, _] : m_nativeFunctions)
		{
			names.push_back(name);
		}
		return names;
	}

private:
	std::unordered_map<std::string, ScriptValue> m_globals;
	std::unordered_map<std::string, NativeFunction> m_nativeFunctions;
	std::vector<std::string> m_executionLog;
};

// ════════════════════════════════════════════════════════════════════
//  VNScriptBridge。VNシステム連携ブリッジ
// ════════════════════════════════════════════════════════════════════

/// @brief VNシステムとスクリプトエンジンの連携ブリッジ
/// @details IScriptingEngineにVN操作関数を自動登録し、FlagManagerとの
///          双方向同期を提供する。ScenarioScriptの @script/@eval コマンドから
///          利用される。
///
/// ScenarioScript統合:
/// @code
/// @script
/// show_character("sakura", "center")
/// play_bgm("morning.ogg")
/// set_flag("met_sakura", true)
/// @endscript
///
/// @eval affection >= 5
/// @endcode
class VNScriptBridge
{
public:
	/// @brief VNコマンドコールバック型
	using ShowCharacterCallback = std::function<void(const std::string& name, const std::string& pos)>;
	using HideCharacterCallback = std::function<void(const std::string& name)>;
	using SetExpressionCallback = std::function<void(const std::string& name, const std::string& expr)>;
	using PlayBgmCallback = std::function<void(const std::string& file)>;
	using PlaySeCallback = std::function<void(const std::string& file)>;
	using StopBgmCallback = std::function<void()>;
	using ShowTextCallback = std::function<void(const std::string& speaker, const std::string& text)>;
	using ChangeBackgroundCallback = std::function<void(const std::string& file)>;

	/// @brief コンストラクタ
	/// @param engine スクリプトエンジン（所有権を移動）
	/// @param flags フラグマネージャ参照
	explicit VNScriptBridge(std::unique_ptr<IScriptingEngine> engine, FlagManager& flags)
		: m_engine(std::move(engine))
		, m_flags(flags)
	{
	}

	/// @brief スクリプトエンジンを取得する
	[[nodiscard]] IScriptingEngine* engine() const noexcept { return m_engine.get(); }

	/// @brief フラグマネージャを取得する
	[[nodiscard]] FlagManager& flags() noexcept { return m_flags; }

	// ── VN関数の自動登録 ──────────────────────────────────────

	/// @brief VN操作関数をスクリプトエンジンに一括登録する
	void registerVNFunctions()
	{
		if (!m_engine) { return; }

		// show_character(name, position)
		m_engine->registerNativeFunction("show_character",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.size() < 2) { return ScriptResult::fail("show_character requires 2 args"); }
				const auto name = extractString(args[0]);
				const auto pos = extractString(args[1]);
				if (m_onShowCharacter) { m_onShowCharacter(name, pos); }
				return ScriptResult::ok(true);
			});

		// hide_character(name)
		m_engine->registerNativeFunction("hide_character",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("hide_character requires 1 arg"); }
				const auto name = extractString(args[0]);
				if (m_onHideCharacter) { m_onHideCharacter(name); }
				return ScriptResult::ok(true);
			});

		// set_expression(name, expression)
		m_engine->registerNativeFunction("set_expression",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.size() < 2) { return ScriptResult::fail("set_expression requires 2 args"); }
				const auto name = extractString(args[0]);
				const auto expr = extractString(args[1]);
				if (m_onSetExpression) { m_onSetExpression(name, expr); }
				return ScriptResult::ok(true);
			});

		// play_bgm(file)
		m_engine->registerNativeFunction("play_bgm",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("play_bgm requires 1 arg"); }
				const auto file = extractString(args[0]);
				if (m_onPlayBgm) { m_onPlayBgm(file); }
				return ScriptResult::ok(true);
			});

		// play_se(file)
		m_engine->registerNativeFunction("play_se",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("play_se requires 1 arg"); }
				const auto file = extractString(args[0]);
				if (m_onPlaySe) { m_onPlaySe(file); }
				return ScriptResult::ok(true);
			});

		// stop_bgm()
		m_engine->registerNativeFunction("stop_bgm",
			[this]([[maybe_unused]] const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (m_onStopBgm) { m_onStopBgm(); }
				return ScriptResult::ok(true);
			});

		// show_text(speaker, text)
		m_engine->registerNativeFunction("show_text",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.size() < 2) { return ScriptResult::fail("show_text requires 2 args"); }
				const auto speaker = extractString(args[0]);
				const auto text = extractString(args[1]);
				if (m_onShowText) { m_onShowText(speaker, text); }
				return ScriptResult::ok(true);
			});

		// change_background(file)
		m_engine->registerNativeFunction("change_background",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("change_background requires 1 arg"); }
				const auto file = extractString(args[0]);
				if (m_onChangeBackground) { m_onChangeBackground(file); }
				return ScriptResult::ok(true);
			});

		// set_flag(name, value)
		m_engine->registerNativeFunction("set_flag",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.size() < 2) { return ScriptResult::fail("set_flag requires 2 args"); }
				const auto name = extractString(args[0]);
				m_flags.set(name, scriptValueToFlagValue(args[1]));
				return ScriptResult::ok(true);
			});

		// get_flag(name) -> value
		m_engine->registerNativeFunction("get_flag",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("get_flag requires 1 arg"); }
				const auto name = extractString(args[0]);
				auto val = m_flags.get(name);
				if (!val.has_value()) { return ScriptResult::ok(nullptr); }
				return ScriptResult::ok(flagValueToScriptValue(*val));
			});

		// check_flag(name) -> bool
		m_engine->registerNativeFunction("check_flag",
			[this](const std::vector<ScriptValue>& args) -> ScriptResult
			{
				if (args.empty()) { return ScriptResult::fail("check_flag requires 1 arg"); }
				const auto name = extractString(args[0]);
				return ScriptResult::ok(m_flags.getBool(name));
			});
	}

	// ── コールバック設定 ──────────────────────────────────────

	void onShowCharacter(ShowCharacterCallback cb) { m_onShowCharacter = std::move(cb); }
	void onHideCharacter(HideCharacterCallback cb) { m_onHideCharacter = std::move(cb); }
	void onSetExpression(SetExpressionCallback cb) { m_onSetExpression = std::move(cb); }
	void onPlayBgm(PlayBgmCallback cb) { m_onPlayBgm = std::move(cb); }
	void onPlaySe(PlaySeCallback cb) { m_onPlaySe = std::move(cb); }
	void onStopBgm(StopBgmCallback cb) { m_onStopBgm = std::move(cb); }
	void onShowText(ShowTextCallback cb) { m_onShowText = std::move(cb); }
	void onChangeBackground(ChangeBackgroundCallback cb) { m_onChangeBackground = std::move(cb); }

	// ── スクリプト実行 ────────────────────────────────────────

	/// @brief スクリプトブロックを実行する（@script ... @endscript）
	/// @param code スクリプトコード
	/// @return 実行結果
	[[nodiscard]] ScriptResult executeBlock(const std::string& code)
	{
		if (!m_engine) { return ScriptResult::fail("no scripting engine"); }

		// 実行前にフラグをスクリプトエンジンに同期
		syncFlagsToEngine();

		auto result = m_engine->execute(code);

		// 実行後にスクリプトエンジンからフラグを同期
		syncFlagsFromEngine();

		return result;
	}

	/// @brief 条件式を評価する（@eval expression）
	/// @param expression 条件式
	/// @return 真偽値
	[[nodiscard]] bool evaluateCondition(const std::string& expression)
	{
		if (!m_engine) { return false; }

		syncFlagsToEngine();
		return m_engine->evaluateCondition(expression);
	}

	/// @brief 名前付き関数を呼び出す
	/// @param name 関数名
	/// @param args 引数
	/// @return 実行結果
	[[nodiscard]] ScriptResult callFunction(const std::string& name,
	                                        const std::vector<ScriptValue>& args = {})
	{
		if (!m_engine) { return ScriptResult::fail("no scripting engine"); }
		return m_engine->callFunction(name, args);
	}

	// ── フラグ同期 ────────────────────────────────────────────

	/// @brief FlagManagerの全フラグをスクリプトエンジンに同期する
	void syncFlagsToEngine()
	{
		if (!m_engine) { return; }

		auto allFlags = m_flags.getAll();
		for (const auto& [key, value] : allFlags)
		{
			m_engine->setGlobal(key, flagValueToScriptValue(value));
		}
	}

	/// @brief スクリプトエンジンの既知変数をFlagManagerに同期する
	/// @details 事前に登録されたsync対象キーのみ同期する。
	void syncFlagsFromEngine()
	{
		if (!m_engine) { return; }

		for (const auto& key : m_syncKeys)
		{
			auto val = m_engine->getGlobal(key);
			if (val.has_value())
			{
				m_flags.set(key, scriptValueToFlagValue(*val));
			}
		}
	}

	/// @brief 双方向同期対象のキーを追加する
	/// @param key フラグキー名
	void addSyncKey(const std::string& key)
	{
		if (std::find(m_syncKeys.begin(), m_syncKeys.end(), key) == m_syncKeys.end())
		{
			m_syncKeys.push_back(key);
		}
	}

	/// @brief 双方向同期対象のキーを一括設定する
	/// @param keys フラグキー名リスト
	void setSyncKeys(std::vector<std::string> keys)
	{
		m_syncKeys = std::move(keys);
	}

	/// @brief 同期対象キー一覧を取得する
	[[nodiscard]] const std::vector<std::string>& syncKeys() const noexcept
	{
		return m_syncKeys;
	}

private:
	// ── 型変換ヘルパー ────────────────────────────────────────

	/// @brief ScriptValueから文字列を抽出する
	[[nodiscard]] static std::string extractString(const ScriptValue& val)
	{
		if (auto* s = std::get_if<std::string>(&val)) { return *s; }
		if (auto* b = std::get_if<bool>(&val)) { return *b ? "true" : "false"; }
		if (auto* i = std::get_if<int>(&val)) { return std::to_string(*i); }
		if (auto* f = std::get_if<float>(&val)) { return std::to_string(*f); }
		return "";
	}

	/// @brief FlagValueをScriptValueに変換する
	[[nodiscard]] static ScriptValue flagValueToScriptValue(const FlagValue& fv)
	{
		if (auto* b = std::get_if<bool>(&fv)) { return *b; }
		if (auto* i = std::get_if<int>(&fv)) { return *i; }
		if (auto* f = std::get_if<float>(&fv)) { return *f; }
		if (auto* s = std::get_if<std::string>(&fv)) { return *s; }
		return nullptr;
	}

	/// @brief ScriptValueをFlagValueに変換する
	[[nodiscard]] static FlagValue scriptValueToFlagValue(const ScriptValue& sv)
	{
		if (auto* b = std::get_if<bool>(&sv)) { return *b; }
		if (auto* i = std::get_if<int>(&sv)) { return *i; }
		if (auto* f = std::get_if<float>(&sv)) { return *f; }
		if (auto* s = std::get_if<std::string>(&sv)) { return *s; }
		return false; // nullptr_t -> false
	}

	// ── メンバ ────────────────────────────────────────────────

	std::unique_ptr<IScriptingEngine> m_engine;				///< スクリプトエンジン
	FlagManager& m_flags;									///< フラグマネージャ参照

	std::vector<std::string> m_syncKeys;					///< 双方向同期対象キー

	// VNコマンドコールバック
	ShowCharacterCallback m_onShowCharacter;
	HideCharacterCallback m_onHideCharacter;
	SetExpressionCallback m_onSetExpression;
	PlayBgmCallback m_onPlayBgm;
	PlaySeCallback m_onPlaySe;
	StopBgmCallback m_onStopBgm;
	ShowTextCallback m_onShowText;
	ChangeBackgroundCallback m_onChangeBackground;
};

} // namespace mitiru::vn
