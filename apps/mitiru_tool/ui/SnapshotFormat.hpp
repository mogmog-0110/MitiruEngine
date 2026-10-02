#pragma once
// SharedSnapshot の JSON を、RML がそのまま並べられる表の形 (配列) に直す。
// 文字の書き方は小数 2 桁、要約の区切り、64 字で切る、で揃える。

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::tool
{

using Snapshot = nlohmann::ordered_json;   // ファイルに書かれた順を保つ (HTML 版の Object.keys と同じ並び)

/// JS の String(x) と同じ書き方 (60.0 は "60"、文字列は引用符無し、null は "null")。
[[nodiscard]] std::string jsString(const Snapshot& v);

/// kv 表の値の欄。整数はそのまま、小数は 2 桁、配列は中身か件数、オブジェクトは 1 行の要約。
[[nodiscard]] std::string kvValue(const Snapshot& v);

/// MITIRU_ENUM / MITIRU_FIELD_RANGE / MITIRU_FIELD_UI_RANGE / MITIRU_FIELD_GROUP の書式
/// (FieldDescriptor.elemType の合成文字列) を読んだ結果。
struct ReflectMeta
{
	enum class Widget { None, Enum, Range };
	Widget widget = Widget::None;
	std::vector<std::string> names;   ///< Enum の名前
	double min = 0.0;
	double max = 0.0;
	std::string group;                ///< 空なら畳まない
	[[nodiscard]] bool any() const noexcept { return widget != Widget::None || !group.empty(); }
};

[[nodiscard]] ReflectMeta parseReflectMeta(std::string_view elemType);

struct KvFilter
{
	std::vector<std::string> only;   ///< 空でなければこの key の節だけ
	std::vector<std::string> skip;   ///< この key の節は出さない
};

/// {section:{title,state,order?,meta?}} を節の配列にする。
/// 各節は {title, items:[行 | 畳んだ組]}。行は {group:false, k, has_k, v, w ("text"|"enum"|"range"), min, max, val}、
/// 組は {group:true, name, open, rows:[行]}。openGroups は利用者が開いた組の名前 (既定は閉じる)。
[[nodiscard]] nlohmann::json kvSections(const Snapshot& snap, const KvFilter& filter,
                                        const std::set<std::string, std::less<>>& openGroups);

/// 節の数 (HTML 版の kv.count。0 なら「待っている」表示)。
[[nodiscard]] std::size_t kvSectionCount(const Snapshot& snap, const KvFilter& filter);

/// JSON を開閉つきの木の行にする。行は {indent:[..depth 個..], key, leaf, open, val, hint, path}。
/// collapsed は利用者が閉じた節の path (既定は全部開く)。閉じた節の子は行に出さない。
[[nodiscard]] nlohmann::json treeRows(const Snapshot& root, const std::string& rootLabel,
                                      const std::set<std::string, std::less<>>& collapsed);

/// 小数を固定桁で書く (JS の toFixed)。
[[nodiscard]] std::string toFixed(double v, int digits);

} // namespace mitiru::tool
