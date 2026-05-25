#pragma once

/// @file ScriptRunner.hpp
/// @brief JSON 駆動の ADV 形式 script runner (G-04)。
///
/// MitiruEngine の architecture rule に従い data-driven: engine は script を
/// 解釈し、game が各 effect (text render、image show、choice offer、stat change)
/// の callback を提供する。engine 自身は描画しない — renderer 非依存。JSON schema
/// と callback 契約の全体は `docs/NARRATIVE_SCRIPT.md` を参照。
///
/// **設計判断 (v1):**
/// - `ChoiceScene` は終端: `execute()` は選ばれた `next` id を返す
///   (choice が無ければ空文字列)。外側の loop は game が回す — script 間の
///   内部 recursion は無い。
/// - `setFlag` / `statChange` は `GameContext` を in-place で変更する。callback は
///   変更*後*に発火するので `ctx` から更新済みの値を読める。
/// - 未知の scene `"type"`、必須 field 欠落、`execute()` での未知 script id、
///   ファイル間での script id 重複 → いずれも field-path 文脈付きで
///   `std::runtime_error` を throw する。
///
/// v1 非対応: loop、variable、text 補間 (`{name}`)、choice の `next` 以外の
/// 条件分岐。これらは 2 つ目の consumer が天井に当たった時点で v2 に入る。

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::narrative
{

using json = ::nlohmann::json;

// ── Scene の種別 ───────────────────────────────────────────────

struct TextScene
{
	std::string speaker;
	std::string text;
};

struct ImageScene
{
	std::string path;
	int         durationMs = 0;
};

struct ChoiceOption
{
	std::string label;
	std::string next;     ///< opaque id the game routes to via execute()
};

struct ChoiceScene
{
	std::vector<ChoiceOption> options;
};

struct WaitScene
{
	int ms = 0;
};

struct SetFlagScene
{
	std::string name;
	bool        value = false;
};

struct StatChangeScene
{
	std::string stat;
	int         delta = 0;
};

using Scene = std::variant<TextScene, ImageScene, ChoiceScene,
                           WaitScene, SetFlagScene, StatChangeScene>;

// ── Script + 実行 context ──────────────────────────────────

struct Script
{
	std::string        id;
	std::vector<Scene> scenes;
};

struct GameContext
{
	std::unordered_map<std::string, bool> flags;
	std::unordered_map<std::string, int>  stats;
};

/// @brief script 実行の結果。`chosenNext` が空でなければ game は
///        `execute(chosenNext, ctx)` を呼んで継続すべき。空なら
///        script は choice 無しで完了している。
struct ExecuteResult
{
	std::string chosenNext;
};

// ── ScriptRunner ────────────────────────────────────────────────

class ScriptRunner
{
public:
	using TextFn       = std::function<void(const std::string& speaker, const std::string& text)>;
	using ImageFn      = std::function<void(const std::string& path, int durationMs)>;
	using ChoiceFn     = std::function<std::string(const std::vector<std::string>& labels)>;
	using WaitFn       = std::function<void(int ms)>;
	using FlagFn       = std::function<void(const std::string& name, bool value)>;
	using StatChangeFn = std::function<void(const std::string& stat, int delta, int newValue)>;

	ScriptRunner() = default;

	// ── 読み込み ───────────────────────────────────────────────

	/// @brief inline な JSON blob から script を 1 つ parse する。
	/// @throws parse/schema 失敗時に std::runtime_error。
	void loadFromJson(std::string_view jsonText, std::string_view sourceLabel = "<inline>")
	{
		json parsed;
		try { parsed = json::parse(jsonText); }
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string("ScriptRunner: invalid JSON in ")
				+ std::string(sourceLabel) + ": " + e.what());
		}
		Script script = parseScript(parsed, sourceLabel);
		insertScript(std::move(script), sourceLabel);
	}

	/// @brief `dir` 内の全 `*.json` を読み込む。拡張子は大小無視で match。
	///        .json 以外は黙って skip。subdirectory も再帰する。
	/// @throws parse/schema 失敗 (filename 付き) または
	///         ファイル間の script id 重複時に std::runtime_error。
	void loadFromDirectory(const std::filesystem::path& dir)
	{
		if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
		{
			throw std::runtime_error(
				"ScriptRunner: directory not found: " + dir.string());
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
		{
			if (!entry.is_regular_file()) { continue; }
			const auto ext = entry.path().extension().string();
			std::string extLower;
			extLower.reserve(ext.size());
			for (char c : ext) { extLower.push_back(static_cast<char>(std::tolower(c))); }
			if (extLower != ".json") { continue; }

			std::ifstream f(entry.path());
			if (!f)
			{
				throw std::runtime_error(
					"ScriptRunner: cannot open " + entry.path().string());
			}
			std::string body((std::istreambuf_iterator<char>(f)),
			                  std::istreambuf_iterator<char>());
			loadFromJson(body, entry.path().filename().string());
		}
	}

	[[nodiscard]] const Script* findScript(std::string_view id) const
	{
		const auto it = m_scripts.find(std::string(id));
		return it == m_scripts.end() ? nullptr : &it->second;
	}

	[[nodiscard]] std::size_t scriptCount() const noexcept { return m_scripts.size(); }

	void clear() noexcept
	{
		m_scripts.clear();
	}

	// ── callback ─────────────────────────────────────────────

	void onTextDisplay(TextFn fn)       { m_onText  = std::move(fn); }
	void onImageShow(ImageFn fn)        { m_onImage = std::move(fn); }
	void onChoiceOffer(ChoiceFn fn)     { m_onChoice = std::move(fn); }
	void onWaitBegin(WaitFn fn)         { m_onWait = std::move(fn); }
	void onFlagSet(FlagFn fn)           { m_onFlag = std::move(fn); }
	void onStatChange(StatChangeFn fn)  { m_onStat = std::move(fn); }

	// ── 実行 ───────────────────────────────────────────────

	/// @brief script を実行し、各 scene で callback を発火する。
	/// @returns 最後の scene が ChoiceScene なら `chosenNext` を設定した
	///          ExecuteResult (それ以外は空文字列)。
	/// @throws 未知の script id 時に std::runtime_error。
	ExecuteResult execute(std::string_view scriptId, GameContext& ctx)
	{
		const auto it = m_scripts.find(std::string(scriptId));
		if (it == m_scripts.end())
		{
			throw std::runtime_error(
				"ScriptRunner: script '" + std::string(scriptId) + "' not found");
		}
		ExecuteResult result;
		for (const auto& scene : it->second.scenes)
		{
			std::visit([&](const auto& s) { this->dispatch(s, ctx, result); }, scene);
			/// ChoiceScene は script を終端する — 同じ list 内で choice の後ろに
			/// ある scene は設計上到達不能。
			if (std::holds_alternative<ChoiceScene>(scene))
			{
				break;
			}
		}
		return result;
	}

private:
	// ── parse ───────────────────────────────────────────────

	[[nodiscard]] static Script parseScript(const json& j, std::string_view sourceLabel)
	{
		if (!j.is_object())
		{
			throw std::runtime_error(std::string("ScriptRunner: ") + std::string(sourceLabel)
				+ ": root must be an object");
		}
		if (!j.contains("id") || !j.at("id").is_string())
		{
			throw std::runtime_error(std::string("ScriptRunner: ") + std::string(sourceLabel)
				+ ": missing string 'id' at root");
		}
		Script s;
		s.id = j.at("id").get<std::string>();

		if (!j.contains("scenes") || !j.at("scenes").is_array())
		{
			throw std::runtime_error(std::string("ScriptRunner: ") + std::string(sourceLabel)
				+ " script '" + s.id + "': missing array 'scenes'");
		}
		const auto& arr = j.at("scenes");
		s.scenes.reserve(arr.size());
		for (std::size_t i = 0; i < arr.size(); ++i)
		{
			s.scenes.push_back(parseScene(arr[i], s.id, i));
		}
		return s;
	}

	[[nodiscard]] static Scene parseScene(const json& j,
	                                      const std::string& scriptId,
	                                      std::size_t sceneIndex)
	{
		const auto prefix = [&](std::string_view field) {
			return "script '" + scriptId + "' scene[" + std::to_string(sceneIndex)
			     + "]: " + std::string(field);
		};
		if (!j.is_object())
		{
			throw std::runtime_error(prefix("must be an object"));
		}
		if (!j.contains("type") || !j.at("type").is_string())
		{
			throw std::runtime_error(prefix("missing string 'type'"));
		}
		const auto type = j.at("type").get<std::string>();

		if (type == "text")
		{
			TextScene t;
			t.speaker = j.value("speaker", std::string{});
			if (!j.contains("text") || !j.at("text").is_string())
			{
				throw std::runtime_error(prefix("'text' scene missing string 'text'"));
			}
			t.text = j.at("text").get<std::string>();
			return t;
		}
		if (type == "image")
		{
			ImageScene im;
			if (!j.contains("path") || !j.at("path").is_string())
			{
				throw std::runtime_error(prefix("'image' scene missing string 'path'"));
			}
			if (!j.contains("durationMs") || !j.at("durationMs").is_number_integer())
			{
				throw std::runtime_error(prefix("'image' scene missing integer 'durationMs'"));
			}
			im.path       = j.at("path").get<std::string>();
			im.durationMs = j.at("durationMs").get<int>();
			return im;
		}
		if (type == "choice")
		{
			ChoiceScene c;
			if (!j.contains("options") || !j.at("options").is_array() || j.at("options").empty())
			{
				throw std::runtime_error(prefix("'choice' scene missing non-empty array 'options'"));
			}
			for (std::size_t oi = 0; oi < j.at("options").size(); ++oi)
			{
				const auto& opt = j.at("options")[oi];
				if (!opt.is_object() || !opt.contains("label") || !opt.contains("next"))
				{
					throw std::runtime_error(prefix("'choice' option[")
						+ std::to_string(oi) + "] missing 'label' or 'next'");
				}
				c.options.push_back({
					opt.at("label").get<std::string>(),
					opt.at("next").get<std::string>(),
				});
			}
			return c;
		}
		if (type == "wait")
		{
			WaitScene w;
			if (!j.contains("ms") || !j.at("ms").is_number_integer())
			{
				throw std::runtime_error(prefix("'wait' scene missing integer 'ms'"));
			}
			w.ms = j.at("ms").get<int>();
			return w;
		}
		if (type == "setFlag")
		{
			SetFlagScene f;
			if (!j.contains("name") || !j.at("name").is_string())
			{
				throw std::runtime_error(prefix("'setFlag' scene missing string 'name'"));
			}
			if (!j.contains("value") || !j.at("value").is_boolean())
			{
				throw std::runtime_error(prefix("'setFlag' scene missing bool 'value'"));
			}
			f.name  = j.at("name").get<std::string>();
			f.value = j.at("value").get<bool>();
			return f;
		}
		if (type == "statChange")
		{
			StatChangeScene s;
			if (!j.contains("stat") || !j.at("stat").is_string())
			{
				throw std::runtime_error(prefix("'statChange' scene missing string 'stat'"));
			}
			if (!j.contains("delta") || !j.at("delta").is_number_integer())
			{
				throw std::runtime_error(prefix("'statChange' scene missing integer 'delta'"));
			}
			s.stat  = j.at("stat").get<std::string>();
			s.delta = j.at("delta").get<int>();
			return s;
		}
		throw std::runtime_error(prefix("unknown scene type '" + type + "'"));
	}

	void insertScript(Script&& script, std::string_view sourceLabel)
	{
		const auto [it, inserted] = m_scripts.emplace(script.id, std::move(script));
		if (!inserted)
		{
			throw std::runtime_error(std::string("ScriptRunner: duplicate script id '")
				+ it->second.id + "' in " + std::string(sourceLabel));
		}
	}

	// ── 処理振り分け (dispatch) ──────────────────────────────────────────────

	void dispatch(const TextScene& s, GameContext&, ExecuteResult&) const
	{
		if (m_onText) { m_onText(s.speaker, s.text); }
	}

	void dispatch(const ImageScene& s, GameContext&, ExecuteResult&) const
	{
		if (m_onImage) { m_onImage(s.path, s.durationMs); }
	}

	void dispatch(const ChoiceScene& s, GameContext&, ExecuteResult& out) const
	{
		std::vector<std::string> labels;
		labels.reserve(s.options.size());
		for (const auto& o : s.options) { labels.push_back(o.label); }
		std::string picked;
		if (m_onChoice)
		{
			picked = m_onChoice(labels);
		}
		/// label → next id を解決する。picked が空 or 未知 label なら先頭 option。
		const ChoiceOption* match = nullptr;
		for (const auto& o : s.options) { if (o.label == picked) { match = &o; break; } }
		if (!match) { match = &s.options.front(); }
		out.chosenNext = match->next;
	}

	void dispatch(const WaitScene& s, GameContext&, ExecuteResult&) const
	{
		if (m_onWait) { m_onWait(s.ms); }
	}

	void dispatch(const SetFlagScene& s, GameContext& ctx, ExecuteResult&) const
	{
		/// 先に変更し、その後 callback を発火 — callback は ctx.flags を見れる。
		ctx.flags[s.name] = s.value;
		if (m_onFlag) { m_onFlag(s.name, s.value); }
	}

	void dispatch(const StatChangeScene& s, GameContext& ctx, ExecuteResult&) const
	{
		auto& ref = ctx.stats[s.stat];
		ref += s.delta;
		if (m_onStat) { m_onStat(s.stat, s.delta, ref); }
	}

	// ── メンバ ───────────────────────────────────────────────

	std::unordered_map<std::string, Script> m_scripts;

	TextFn       m_onText;
	ImageFn      m_onImage;
	ChoiceFn     m_onChoice;
	WaitFn       m_onWait;
	FlagFn       m_onFlag;
	StatChangeFn m_onStat;
};

} // namespace mitiru::narrative
