#pragma once
// stage_edit の窓 1 枚の振る舞い。ステージの JSON を図の上で直し、「試す / 残す / 捨てる」(ADR 0035, 0082) で
// ゲームとファイルへ渡す。RmlUi にも窓にも触らないので、GPU 無しの試験で全部の操作を回せる。
// 窓を開く引数 (--page "stage_edit?schema=<論理パス>&file=<ステージ名>&root=<プロジェクト>"):
//   schema  編集スキーマの論理パス (必須)
//   file    ステージのファイル名 (スキーマの dir の下)。無ければ最初に見つかった物
//   root    プロジェクトの根 (無ければ作業フォルダから上へ mitiru.toml を探す)
//   script  操作の台本 (試験と撮影用)。1 行 1 命令で、座標は世界座標。書き方は StageEditScript.cpp

#include "../ToolPage.hpp"
#include "EditSchema.hpp"
#include "OrbitView.hpp"
#include "StageDoc.hpp"
#include "StageFiles.hpp"
#include "TopView.hpp"

#include <memory>
#include <optional>
#include <string>

namespace mitiru::tool
{

class StageEditPage final : public ToolPage
{
public:
	explicit StageEditPage(const PageContext& ctx);

	void start() override;
	void onAction(std::string_view name, const nlohmann::json& payload) override;
	void onPointer(std::string_view elementId, const PointerEvent& ev) override;
	void onKey(const KeyEvent& ev) override;
	void tick(double now) override;
	[[nodiscard]] std::vector<std::string> pointerTargets() const override { return { "top", "orbit" }; }
	[[nodiscard]] bool wantsAllButtons() const override { return true; }
	[[nodiscard]] bool wantsWheel() const override { return true; }
	[[nodiscard]] bool wantsSnapshot() const override { return false; }

	/// 試験が中身を確かめる口
	[[nodiscard]] const stageedit::StageDoc& doc() const noexcept { return m_doc; }
	[[nodiscard]] const stageedit::TopView& topView() const noexcept { return *m_top; }
	[[nodiscard]] const std::string& status() const noexcept { return m_status; }
	[[nodiscard]] bool tried() const noexcept { return m_tried; }
	[[nodiscard]] bool scriptDone() const noexcept { return m_scriptPos >= m_script.size() && m_scriptWait <= 0; }

private:
	enum class DragKind { None, Pan, Move, Handle, Orbit };
	struct Drag
	{
		DragKind kind = DragKind::None;
		double lastX = 0.0;
		double lastY = 0.0;
		double downX = 0.0;     ///< Move: 押した世界座標
		double downZ = 0.0;
		stageedit::Vec3 ref;    ///< Move: 動かす前の基準点 (刻みの基準)
		stageedit::Handle handle;
		bool moved = false;
	};

	void load();
	bool loadSchema(const std::string& logical);
	void fail(const std::string& message);
	void pushAll();
	void pushPanel();
	void pushStatus();
	void pushImages();
	void changed(bool gestureEnd);

	void tryOut();
	void keep();
	void discard();
	void adopt();
	void undoRedo(bool undo);
	void openFile(const std::string& name);

	void topPointer(const PointerEvent& ev, double sx, double sy);
	void orbitPointer(const PointerEvent& ev, double sx, double sy);
	void beginMove(int item, double wx, double wz);
	void dragTo(double wx, double wz);
	void endDrag();
	void nudgeKey(const KeyEvent& ev);
	void loadScript(const std::string& path);
	void scriptStep();
	void runScriptLine(const std::string& line);

	[[nodiscard]] std::string summaryOf(const stageedit::Item& item) const;
	[[nodiscard]] std::string labelOf(const stageedit::Item& item) const;

	ToolView* m_view;
	std::string m_query;
	std::string m_schemaLogical;
	std::string m_fileName;
	std::string m_rootHint;
	std::string m_error;
	stageedit::Schema m_schema;
	stageedit::StageFiles m_files;
	stageedit::StageDoc m_doc;
	std::unique_ptr<stageedit::TopView> m_top;
	std::unique_ptr<stageedit::OrbitView> m_orbit;
	std::string m_status;
	std::string m_generatedBy;
	std::string m_mode = "both";
	Drag m_drag;
	std::vector<std::string> m_script;
	std::size_t m_scriptPos = 0;
	int m_scriptWait = 0;
	std::string m_scriptPath;
	bool m_tried = false;
	bool m_auto = false;
	bool m_imagesDirty = true;
	bool m_loaded = false;
};

} // namespace mitiru::tool
