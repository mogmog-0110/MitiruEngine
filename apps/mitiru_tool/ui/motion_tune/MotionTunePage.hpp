#pragma once
// motion_tune の窓 1 枚の宣言。
// ゲームが宣言した調整値 (assets/tuning/*.json) をスライダーと数で調整し、「試す / 残す / 捨てる」(ADR 0035、0082) で実行中のゲームと JSON に渡す。
// 同じ入力の記録を流し直し、元の値と調整した値の軌跡を重ねて表示する。
// RmlUi と窓に依存しないため、GPU なしの試験ですべての操作を実行できる。
// 窓を開く引数は --page "motion_tune?schema=<論理パス>&root=<プロジェクト>&script=<台本>"。
//   schema  調整値のスキーマの論理パス (必須。書式は TuneSchema.hpp)
//   root    プロジェクトのルート (省略時は作業フォルダから上へ mitiru.toml を探す)
//   script  操作の台本 (試験と撮影用。書式は MotionTuneScript.cpp)
//   report  状態を JSON で書き出すファイル (試験用。操作とゲームからの返事のたびに書き直す)
// ゲームから開けば、host が HTTP API を開いて port を --http-port で渡す (ToolHttpDemand)。
// 手で開くときは、ゲームを --http-port または MITIRU_AI=1 で起動し、窓の port は mitiru_tool の --http-port で渡す。

#include "../ToolPage.hpp"
#include "../PageUtil.hpp"
#include "../stage_edit/StageFiles.hpp"
#include "TraceView.hpp"
#include "TuneModel.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::tool
{

class MotionTunePage final : public ToolPage
{
public:
	explicit MotionTunePage(const PageContext& ctx);

	void start() override;
	void onAction(std::string_view name, const nlohmann::json& payload) override;
	void onHttp(const HttpResponse& res) override;
	void tick(double now) override;
	[[nodiscard]] bool wantsSnapshot() const override { return false; }

	[[nodiscard]] const motiontune::TuneModel* model() const noexcept { return m_model.get(); }
	[[nodiscard]] const std::string& status() const noexcept { return m_status; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] bool connected() const noexcept { return m_connected; }
	[[nodiscard]] const motiontune::Trace* baseTrace() const noexcept { return m_hasBase ? &m_base : nullptr; }
	[[nodiscard]] const motiontune::Trace* tunedTrace() const noexcept { return m_hasTuned ? &m_tuned : nullptr; }
	[[nodiscard]] bool idle() const noexcept { return m_inflight == 0 && !m_traceWanted && m_traceAt < 0.0; }
	[[nodiscard]] bool scriptDone() const noexcept { return m_scriptPos >= m_script.size() && m_scriptWait <= 0; }

private:
	void load();
	void fail(const std::string& message);
	void pushAll();
	void pushRows();
	void pushStatus();
	void pushTrace();
	void writeReport() const;

	void send(const char* tag, const char* method, const char* path, std::string body);
	bool onQuiet(int status, const nlohmann::json& body);
	void onCommit(const HttpResponse& res, const nlohmann::json& body);
	void onRing(int status, const nlohmann::json& body);
	void onTraceResponse(const HttpResponse& res, const nlohmann::json& body);

	void set(const std::string& name, const std::string& text);
	void tryOut();
	void keep();
	void discard();
	void commit(const char* tag, const nlohmann::json& values, std::string okText);
	void requestTrace();
	void sendTraces(int ringSize);
	[[nodiscard]] std::vector<int> visibleRows() const;

	void loadScript(const std::string& path);
	void scriptStep();
	void runScriptLine(const std::string& line);

	ToolView* m_view;
	ToolHttp* m_http;
	std::string m_schemaLogical;
	std::string m_rootHint;
	std::string m_scriptPath;
	std::string m_reportPath;
	std::string m_error;
	std::string m_status;
	std::string m_traceNote;
	motiontune::TuneSchema m_schema;
	std::unique_ptr<motiontune::TuneModel> m_model;
	std::vector<std::unique_ptr<stageedit::StageFiles>> m_files;
	std::vector<std::string> m_fileErrors;
	PollTimer m_poll;
	nlohmann::json m_lastQuiet;
	motiontune::Trace m_base;
	motiontune::Trace m_tuned;
	bool m_hasBase = false;
	bool m_hasTuned = false;
	bool m_traceWithEdits = false;
	int m_tab = 0;
	std::string m_filter;
	double m_now = 0.0;
	double m_traceAt = -1.0;
	bool m_traceWanted = false;
	int m_inflight = 0;
	int m_traceOutstanding = 0;
	int m_traceSerial = 0;
	bool m_traceFailed = false;
	std::unordered_map<std::string, nlohmann::json> m_sent;   ///< 送った commit の中身 (返事が来たら live に写す)
	bool m_imageDirty = true;
	bool m_connected = false;
	bool m_loaded = false;
	std::vector<std::string> m_script;
	std::size_t m_scriptPos = 0;
	int m_scriptWait = 0;
};

} // namespace mitiru::tool
