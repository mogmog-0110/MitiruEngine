// scene_view (ADR 0035 O2/O3/O4/O5): ゲーム画面の絵に枠を重ね、ドラッグや候補で「試してから残すか捨てるか
// 選ぶ」分岐エディタ。この窓はゲームの状態を自分では書かない。PUT /api/ai/state (分岐で試す)・
// POST /api/ai/commit|discard (残す / 捨てる)・POST /api/ai/candidates (4 案を並走) をゲームの HTTP API に
// 頼むだけで、確定するかどうかはゲーム側の分岐の仕組みが持つ。

#include "SceneViewPage.hpp"

#include "../ImageDecode.hpp"

#include <cmath>

namespace mitiru::tool
{

namespace
{

// 選んだ数値 field を ±10% / ±25% した 4 案。0 付近は比率では動かないので絶対量に切り替える。
std::vector<double> candidateValues(double base)
{
	const double unit = std::fabs(base) > 1e-6 ? std::fabs(base) : 1.0;
	return { base - unit * 0.25, base - unit * 0.10, base + unit * 0.10, base + unit * 0.25 };
}

std::string errorOf(const Snapshot& body)
{
	return stringOr(findAt(body, { "error" }), "");
}

std::string failureText(int status, const Snapshot& body)
{
	if (status == 0) { return "game の /api に接続できません"; }
	const std::string err = errorOf(body);
	return err.empty() ? "HTTP " + std::to_string(status) : err;
}

} // namespace

SceneViewPage::SceneViewPage(const PageContext& ctx) : m_view(ctx.view), m_http(ctx.http)
{
	m_port = std::to_string(m_http != nullptr ? m_http->port() : 8090);
}

void SceneViewPage::start()
{
	m_view->set("port", m_port);
	publish();
}

void SceneViewPage::onSnapshot(const Snapshot& snap, bool ready)
{
	m_ready = ready;
	if (const Snapshot* gm = findAt(snap, { "gameMemory", "state" }); gm != nullptr && gm->is_object()) { m_editor.setGameMemory(*gm); }
	if (const Snapshot* objs = findAt(snap, { "sceneView", "state", "objects" }); objs != nullptr && objs->is_array())
	{
		m_editor.setObjects(sceneObjectsFrom(*objs));
	}
	if (const Snapshot* gate = findAt(snap, { "replayGate", "state" }); gate != nullptr)
	{
		const Snapshot* runs = findAt(*gate, { "runs" });
		m_gateRuns = (runs != nullptr && runs->is_array()) ? *runs : Snapshot::array();
	}
	publish();
}

void SceneViewPage::onAction(std::string_view name, const nlohmann::json& payload)
{
	if (name == "sv.commit")       { commit(); }
	else if (name == "sv.discard") { discard(); }
	else if (name == "sv.undo")    { undo(); }
	else if (name == "sv.redo")    { redo(); }
	else if (name == "sv.cand")    { generateCandidates(payloadString(payload, "k")); }
	else if (name == "sv.pick")    { pickCandidate(payload.value("slot", -1)); }
	else if (name == "sv.port")    { setPort(payloadString(payload, "v")); return; }
	else { return; }
	publish();
}

void SceneViewPage::onPointer(std::string_view, const PointerEvent& ev)
{
	if (m_spaceW <= 0 || ev.width <= 0.0f || ev.height <= 0.0f) { return; }
	const double x = static_cast<double>(ev.x) / ev.width * m_spaceW;
	const double y = static_cast<double>(ev.y) / ev.height * m_spaceH;
	if (ev.kind == PointerEvent::Kind::Down)
	{
		// 別の枠を選んだら前の候補一覧は消す (ゴースト自体は host が次の候補まで出し続ける)。
		m_candidateField.clear();
		m_candidates.clear();
		m_editor.pointerDown(x, y, ev.ctrl, ev.shift);
	}
	else if (ev.kind == PointerEvent::Kind::Move) { m_editor.pointerMove(x, y); }
	else { finishPointer(); }
	publish();
}

void SceneViewPage::onKey(const KeyEvent& ev)
{
	if (ev.key == KeyEvent::Key::Escape) { m_editor.clearSelection(); }
	else if (ev.ctrl && ev.key == KeyEvent::Key::Z && !ev.shift) { undo(); }
	else if (ev.ctrl && (ev.key == KeyEvent::Key::Y || (ev.key == KeyEvent::Key::Z && ev.shift))) { redo(); }
	else { return; }
	publish();
}

void SceneViewPage::onHttp(const HttpResponse& res)
{
	const Snapshot body = parseBody(res.body);
	if (res.tag == "sv.shot")         { m_shotPoll.done(); onShot(body); return; }
	if (res.tag == "sv.state")        { m_statePoll.done(); onBranchState(res.status, body); }
	else if (res.tag == "sv.put")     { onPut(res.status, body); }
	else if (res.tag == "sv.cands")   { onCandidates(res.status, body); }
	else if (res.tag == "sv.commit")  { onCommitted(res.status, body); }
	else if (res.tag == "sv.discard") { onDiscarded(res.status, body); }
	else { return; }
	publish();
}

// 画面の絵と分岐の印は ~0.6 秒ごとで足りる (host 側の撮影の間隔とだいたい合わせる程度)。
void SceneViewPage::tick(double now)
{
	if (m_http == nullptr) { return; }
	if (m_shotPoll.due(now)) { m_http->send({ "sv.shot", "GET", "/api/ai/frame?screenshot=1&width=800", "" }); }
	if (m_statePoll.due(now)) { requestBranchState(); }
}

void SceneViewPage::requestBranchState()
{
	m_http->send({ "sv.state", "GET", "/api/ai/state", "" });
}

void SceneViewPage::setPort(const std::string& text)
{
	m_port = text;
	int port = 0;
	try { port = std::stoi(text); } catch (...) { port = 0; }
	if (m_http != nullptr) { m_http->setPort(port > 0 ? port : 8090); }
}

void SceneViewPage::finishPointer()
{
	std::optional<MoveRequest> move = m_editor.pointerUp();
	if (const std::string notice = m_editor.takeNotice(); !notice.empty()) { m_status = notice; }
	if (!move) { return; }
	if (move->moved) { m_editor.pushUndo(move->label, move->fields); }
	putState(move->fields, "分岐を試行中…", "分岐を試行済み。残す/捨てるを選んでください", "分岐に失敗: ");
}

// PUT /api/ai/state は分岐 (複製 → 書き換え → 復元) で試すだけで、live の GameMemory は変わらない。
// 応答の値を「今そうしたらこうなる」の見た目として mini inspector に出す。
void SceneViewPage::putState(nlohmann::json fields, const std::string& busy, std::string okText, std::string failPrefix)
{
	m_status = busy;
	if (m_http == nullptr) { return; }
	m_http->send({ "sv.put", "PUT", "/api/ai/state", fields.dump() });
	m_puts.push_back({ std::move(fields), std::move(okText), std::move(failPrefix) });
}

void SceneViewPage::onPut(int status, const Snapshot& body)
{
	if (m_puts.empty()) { return; }
	PendingPut put = std::move(m_puts.front());
	m_puts.pop_front();
	if (status == 0 || body.is_discarded())
	{
		m_status = put.failPrefix + "game の /api に接続できません (port " + m_port + ")";
		return;
	}
	if (!responseOk(status, body)) { m_status = put.failPrefix + errorOf(body); return; }
	m_pending = std::move(put.fields);
	m_editor.setGameMemory(body);
	m_status = std::move(put.okText);
}

void SceneViewPage::undo()
{
	if (auto step = m_editor.undo())
	{
		const std::string label = "戻し (" + step->label + ")";
		putState(step->before, label + "…", label + "ました。残す/捨てるを選んでください", label + "に失敗: ");
	}
}

void SceneViewPage::redo()
{
	if (auto step = m_editor.redo())
	{
		const std::string label = "やり直し (" + step->label + ")";
		putState(step->after, label + "…", label + "ました。残す/捨てるを選んでください", label + "に失敗: ");
	}
}

void SceneViewPage::generateCandidates(const std::string& field)
{
	const Snapshot& gm = m_editor.gameMemory();
	const auto it = gm.find(field);
	if (field.empty() || it == gm.end() || !it->is_number() || m_http == nullptr) { return; }
	const std::vector<double> values = candidateValues(it->get<double>());
	nlohmann::json variants = nlohmann::json::array();
	for (const double v : values) { variants.push_back({ { "overrides", { { field, v } } } }); }
	const nlohmann::json body{ { "variants", std::move(variants) }, { "keys", "" }, { "frames", 60 } };
	m_status = "候補を計算中…";
	m_requestedField = field;
	m_requestedValues = values;
	m_http->send({ "sv.cands", "POST", "/api/ai/candidates", body.dump() });
}

void SceneViewPage::onCandidates(int status, const Snapshot& body)
{
	if (status == 0 || body.is_discarded() || !responseOk(status, body))
	{
		m_status = "候補の生成に失敗: " + failureText(status, body);
		return;
	}
	m_candidateField = m_requestedField;
	m_candidates = m_requestedValues;
	// 全 slot は同じ frame から分岐する (GameMemory は 1 つしかないので起点は共通)。
	const Snapshot* results = findAt(body, { "results" });
	const Snapshot* base = (results != nullptr && results->is_array() && !results->empty())
		? findAt((*results)[0], { "baseFrame" }) : nullptr;
	m_baseFrame = (base != nullptr && !base->is_null()) ? jsString(*base) : std::string();
	m_status = "候補 4 案をゴースト表示中 (色は slot 0〜3)。クリックで選ぶ";
}

// 案を選ぶ = その値で PUT /api/ai/state (ドラッグと同じ経路) → 残す / 捨てるを選べる状態にする。
void SceneViewPage::pickCandidate(int slot)
{
	if (slot < 0 || slot >= static_cast<int>(m_candidates.size())) { return; }
	nlohmann::json fields{ { m_candidateField, m_candidates[static_cast<std::size_t>(slot)] } };
	m_editor.pushUndo("候補を採用 " + m_candidateField, fields);
	putState(std::move(fields), "選んだ案を試行中…", "案を試行済み。残す/捨てるを選んでください", "分岐に失敗: ");
}

void SceneViewPage::commit()
{
	if (!m_pending || m_http == nullptr) { return; }
	m_status = "確定中…";
	m_http->send({ "sv.commit", "POST", "/api/ai/commit", m_pending->dump() });
}

void SceneViewPage::discard()
{
	if (!m_pending || m_http == nullptr) { return; }
	m_http->send({ "sv.discard", "POST", "/api/ai/discard", "" });
}

void SceneViewPage::onCommitted(int status, const Snapshot& body)
{
	m_status = responseOk(status, body) ? "残しました (live GameMemory に確定)" : "確定に失敗: " + errorOf(body);
	clearPending();
	requestBranchState();
}

void SceneViewPage::onDiscarded(int status, const Snapshot& body)
{
	// 捨てられなかった時は分岐がゲームに残っているので、残す / 捨てるを選べるままにする。
	if (!responseOk(status, body))
	{
		m_status = "捨てるのに失敗: " + failureText(status, body);
		return;
	}
	m_status = "捨てました (live は元のまま)";
	clearPending();
	requestBranchState();
}

void SceneViewPage::clearPending()
{
	m_pending.reset();
	m_editor.clearHistory();
	m_candidateField.clear();
	m_candidates.clear();
}

void SceneViewPage::onShot(const Snapshot& body)
{
	const Snapshot* png = findAt(body, { "screenshot", "pngBase64" });
	if (png == nullptr || !png->is_string()) { return; }
	std::optional<DecodedImage> img = decodePngBase64(png->get_ref<const std::string&>());
	if (!img) { return; }
	// 枠の座標はゲームの論理画面 (screen) の画素。絵は縮小して届くので、割合はそちらで割る。
	const int screenW = static_cast<int>(numberOr(findAt(body, { "screen", "width" }), 0.0));
	const int screenH = static_cast<int>(numberOr(findAt(body, { "screen", "height" }), 0.0));
	m_spaceW = screenW > 0 ? screenW : img->width;
	m_spaceH = screenH > 0 ? screenH : img->height;
	m_view->setImage("shot", img->width, img->height, std::move(img->rgba));
	publish();
}

void SceneViewPage::onBranchState(int status, const Snapshot& body)
{
	const bool ok = status >= 200 && status < 300 && !body.is_discarded();
	const Snapshot* branch = ok ? findAt(body, { "branch" }) : nullptr;
	m_branch = (branch != nullptr && branch->is_object()) ? *branch : Snapshot::object();
}

std::unique_ptr<ToolPage> makeSceneViewPage(const PageContext& ctx)
{
	return std::make_unique<SceneViewPage>(ctx);
}

} // namespace mitiru::tool
