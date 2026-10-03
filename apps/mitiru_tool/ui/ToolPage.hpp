#pragma once
// ツール窓 1 枚分の振る舞い。RmlUi にも窓にも触らないので、GPU 無しのテストで全部の分岐を回せる。
// 値は ToolView へ写すだけ、UI からの操作は onAction / onPointer / onKey で受ける。ゲームへ届くのは
// ToolSignals (巻き戻しの要求) と ToolHttp (ゲームの /api への問い合わせ) の 2 つだけ。

#include "SnapshotFormat.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::tool
{

/// 値を RML の data model "tool" へ写す口。key "snap" は RML から {{ snap.x }} で引ける。
class ToolView
{
public:
	virtual ~ToolView() = default;
	virtual void set(std::string_view key, nlohmann::json value) = 0;
	/// <liveimage id="..."> の中身を差し替える (RGBA8、行の詰め無し)。
	virtual void setImage(std::string_view elementId, int width, int height, std::vector<std::uint8_t> rgba) = 0;
};

/// 監視中のゲームへ送る巻き戻しの要求。ScrubControlChannel に書くだけで、ゲームの状態には触らない。
class ToolSignals
{
public:
	virtual ~ToolSignals() = default;
	virtual void scrub(int offsetFromNewest) = 0;
	virtual void resume() = 0;
};

/// 窓のキーボードの受け持ち。ドックした窓は開いた時もクリックされた時もフォーカスを取らないので、
/// キーで操作したいページは、人が窓をクリックしてゲームを止めた時に take を、再開した時に giveBack を頼む。
class ToolKeyboard
{
public:
	virtual ~ToolKeyboard() = default;
	virtual void take() = 0;
	/// この窓がキーを持っている時だけ、ドック先のゲームの窓へ返す
	virtual void giveBack() = 0;
};

struct HttpRequest
{
	std::string tag;      ///< 応答をどの処理へ返すかの名前
	std::string method;   ///< "GET" / "PUT" / "POST"
	std::string path;     ///< "/api/ai/state" など
	std::string body;     ///< JSON (GET は空)
};

struct HttpResponse
{
	std::string tag;
	int status = 0;       ///< 0 = 接続できなかった
	std::string body;
};

/// ゲームの HTTP API (127.0.0.1:<port>) への問い合わせ。応答は後のフレームで返る。
class ToolHttp
{
public:
	virtual ~ToolHttp() = default;
	virtual void send(HttpRequest request) = 0;
	virtual void setPort(int port) = 0;
	[[nodiscard]] virtual int port() const = 0;
	[[nodiscard]] virtual std::vector<HttpResponse> takeResponses() = 0;
};

struct PointerEvent
{
	enum class Kind { Down, Move, Up };
	Kind kind = Kind::Down;
	float x = 0.0f;       ///< 要素の左上からの位置 (要素の座標)
	float y = 0.0f;
	float width = 0.0f;   ///< 要素の大きさ
	float height = 0.0f;
	bool ctrl = false;
	bool shift = false;
};

struct KeyEvent
{
	enum class Key { Other, Left, Right, Home, End, Escape, Z, Y };
	Key key = Key::Other;
	bool ctrl = false;
	bool shift = false;
};

struct PageContext
{
	ToolView* view = nullptr;
	ToolSignals* signals = nullptr;   ///< 巻き戻しを頼めるのは rewind だけ (他のページには渡さない)
	ToolKeyboard* keyboard = nullptr; ///< signals と同じく rewind だけ。撮影 (--capture) では無い
	ToolHttp* http = nullptr;         ///< ゲームの /api を叩くページ (why_view / frame_view / scene_view) だけ
	std::string query;                ///< "--page scene?tab=memory" の "tab=memory"
	std::optional<std::string> mtrrPath;
};

class ToolPage
{
public:
	virtual ~ToolPage() = default;
	/// 文書を読んだ直後 (読み直しも含む)。今の値を全部写し直す。
	virtual void start() {}
	/// SharedSnapshot の最新。ready は一度でも読めたか。
	virtual void onSnapshot(const Snapshot& snap, bool ready) { (void)snap; (void)ready; }
	virtual void onAction(std::string_view name, const nlohmann::json& payload) { (void)name; (void)payload; }
	virtual void onPointer(std::string_view elementId, const PointerEvent& ev) { (void)elementId; (void)ev; }
	virtual void onKey(const KeyEvent& ev) { (void)ev; }
	virtual void onHttp(const HttpResponse& res) { (void)res; }
	/// 毎フレーム。now は秒 (問い合わせの間隔を数える)。
	virtual void tick(double now) { (void)now; }
	/// マウスを受ける要素の id。ドラッグの移動と離しは文書全体から拾う。
	[[nodiscard]] virtual std::vector<std::string> pointerTargets() const { return {}; }
	/// SharedSnapshot を読むか (replay は .mtrr を読むので読まない)
	[[nodiscard]] virtual bool wantsSnapshot() const { return true; }
};

/// ページ名から振る舞いを作る。知らない名前はゲームが自分で書いたページとして snap をそのまま渡す。
std::unique_ptr<ToolPage> makePage(std::string_view name, const PageContext& ctx);

/// ページ名が分かっているもの (assets/<name>.rml を同梱している) か。
[[nodiscard]] bool isBuiltinPage(std::string_view name);

/// 巻き戻しを頼めるページか (rewind だけ。ほかの観測窓には ToolSignals を渡さない)。
[[nodiscard]] bool pageRequestsScrub(std::string_view name);

/// ゲームの HTTP API を叩くページか (scene_view / why_view / frame_view)。
[[nodiscard]] bool pageUsesHttp(std::string_view name);

} // namespace mitiru::tool
