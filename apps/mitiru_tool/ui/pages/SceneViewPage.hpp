#pragma once
// scene_view のページ。操作と HTTP は SceneViewPage.cpp、表示の値づくりは SceneViewPage_View.cpp。

#include "Pages.hpp"
#include "SceneViewEditor.hpp"

#include "../PageUtil.hpp"

#include <deque>
#include <optional>

namespace mitiru::tool
{

class SceneViewPage final : public ToolPage
{
public:
	explicit SceneViewPage(const PageContext& ctx);

	void start() override;
	void onSnapshot(const Snapshot& snap, bool ready) override;
	void onAction(std::string_view name, const nlohmann::json& payload) override;
	void onPointer(std::string_view elementId, const PointerEvent& ev) override;
	void onKey(const KeyEvent& ev) override;
	void onHttp(const HttpResponse& res) override;
	void tick(double now) override;
	/// 絵そのもの (枠は pointer-events: none で重ねてあるので、押した位置は絵の座標で取れる)
	[[nodiscard]] std::vector<std::string> pointerTargets() const override { return { "shot" }; }

private:
	struct PendingPut
	{
		nlohmann::json fields;
		std::string okText;
		std::string failPrefix;
	};

	void requestBranchState();
	void setPort(const std::string& text);
	void finishPointer();
	void putState(nlohmann::json fields, const std::string& busy, std::string okText, std::string failPrefix);
	void onPut(int status, const Snapshot& body);
	void undo();
	void redo();
	void generateCandidates(const std::string& field);
	void onCandidates(int status, const Snapshot& body);
	void pickCandidate(int slot);
	void commit();
	void discard();
	void onCommitted(int status, const Snapshot& body);
	void onDiscarded(int status, const Snapshot& body);
	void clearPending();
	void onShot(const Snapshot& body);
	void onBranchState(int status, const Snapshot& body);

	[[nodiscard]] bool isBranchDirty(const std::string& key) const;
	[[nodiscard]] nlohmann::json boxes() const;
	[[nodiscard]] nlohmann::json marquee() const;
	[[nodiscard]] nlohmann::json inspectorRows() const;
	[[nodiscard]] nlohmann::json candidateChips() const;
	[[nodiscard]] nlohmann::json gateRuns() const;
	void publish();

	ToolView* m_view;
	ToolHttp* m_http;
	SceneViewEditor m_editor;
	PollTimer m_shotPoll;
	PollTimer m_statePoll;
	std::deque<PendingPut> m_puts;
	std::optional<nlohmann::json> m_pending;   ///< 直近の PUT で試した値 (残す / 捨てるの対象)
	std::string m_port;
	std::string m_status;
	std::string m_candidateField;
	std::vector<double> m_candidates;
	std::string m_requestedField;
	std::vector<double> m_requestedValues;
	std::string m_baseFrame;   ///< 候補がどの frame から分岐したか
	Snapshot m_branch = Snapshot::object();
	Snapshot m_gateRuns = Snapshot::array();
	int m_spaceW = 0;   ///< 枠の座標系 (ゲームの論理画面) の大きさ。絵がまだ無ければ 0
	int m_spaceH = 0;
	bool m_ready = false;
};

} // namespace mitiru::tool
