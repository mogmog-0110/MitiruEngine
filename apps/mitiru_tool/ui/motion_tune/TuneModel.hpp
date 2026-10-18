#pragma once
// motion_tune が持つ値。各欄に file、live、edit の 3 値を持つ。
// file は元の JSON の値で、「残す」と書き換わる。
// live は実行中のゲームの GameMemory にある値で、「試す」と書き換わる。
// edit は窓で編集中の値。
// 窓にもゲームにも依存しないため、スキーマの読み込みから書式を保った書き戻しまでを窓なしで試験できる。

#include "TuneSchema.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::tool::motiontune
{

struct Entry
{
	std::string name;       ///< GameMemory の欄の名前
	std::string pointer;    ///< JsonText の位置
	std::string key;
	std::string groupLabel;
	std::string hint;
	std::string kind;
	int file = 0;
	int group = 0;
	double min = 0.0;
	double max = 1.0;
	double fileValue = 0.0;
	double liveValue = 0.0;
	double editValue = 0.0;
	bool hasFile = false;
	bool hasLive = false;
};

/// float に丸めて同じかを調べる。JSON の 0.12 とゲームの float が返す 0.11999999731779099 を同じ値とみなす
[[nodiscard]] bool sameValue(double a, double b) noexcept;

class TuneModel
{
public:
	explicit TuneModel(const TuneSchema& schema);

	/// file の JSON を読み、各欄の file と edit を決める。足りない欄は hasFile=false。読めなければ error を返す
	bool loadFile(int file, const std::string& text, std::string& error);
	/// GET /api/ai/state?quiet=1 の応答を live に写す。写せた欄の数を返す
	int setLive(const nlohmann::json& quiet);
	/// 窓で値を直す。範囲に収め、整数と真偽値は丸める。欄がなければ false
	bool set(const std::string& name, double value);
	[[nodiscard]] const Entry* find(const std::string& name) const;
	[[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
	[[nodiscard]] const TuneSchema& schema() const noexcept { return *m_schema; }

	/// edit が file と違う欄。「残す」で JSON に書く
	[[nodiscard]] std::vector<int> unsaved() const;
	/// edit が live と違う欄。「試す」でゲームへ入れる
	[[nodiscard]] std::vector<int> untried() const;
	/// live が file と違う欄。ゲームに入っているが JSON には残していない
	[[nodiscard]] std::vector<int> tried() const;

	/// 欄ごとの値を {名前: 値} にする。真偽値は true / false
	[[nodiscard]] nlohmann::json valuesOf(const std::vector<int>& indexes, bool useFile) const;
	/// 元の JSON の書式を保ち、edit が file と違う欄だけを書き込む。file の欄がなければ失敗する
	[[nodiscard]] std::optional<std::string> render(int file, const std::string& text, std::string& error) const;

	void markKept(int file);    ///< その file の欄の file = edit (JSON へ書けた後)
	void revertEdits();         ///< edit = file
	void markLive(const nlohmann::json& sent);   ///< ゲームが受けた {名前: 値} を live に写す (送った時点の値で、その後の編集は混ぜない)

private:
	const TuneSchema* m_schema;
	std::vector<Entry> m_entries;
	std::unordered_map<std::string, int> m_byName;
};

} // namespace mitiru::tool::motiontune
