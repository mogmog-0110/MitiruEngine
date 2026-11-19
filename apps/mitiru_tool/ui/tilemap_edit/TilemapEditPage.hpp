#pragma once
// RmlUi とウィンドウから切り離しているため、GPU なしの試験で全操作を実行できる。引数と使い方は docs/TILEMAP_EDIT.md に記載している。

#include "../ToolPage.hpp"
#include "../stage_edit/StageFiles.hpp"
#include "TileDoc.hpp"
#include "TilePalette.hpp"
#include "TileSchema.hpp"
#include "TileView.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mitiru::tool
{

class TilemapEditPage final : public ToolPage
{
public:
	explicit TilemapEditPage(const PageContext& ctx);

	void start() override;
	void onAction(std::string_view name, const nlohmann::json& payload) override;
	void onPointer(std::string_view elementId, const PointerEvent& ev) override;
	void onKey(const KeyEvent& ev) override;
	void tick(double now) override;
	[[nodiscard]] std::vector<std::string> pointerTargets() const override { return { "map", "tiles" }; }
	[[nodiscard]] bool wantsAllButtons() const override { return true; }
	[[nodiscard]] bool wantsWheel() const override { return true; }
	[[nodiscard]] bool wantsSnapshot() const override { return false; }

	enum class Tool { Paint, Select };
	[[nodiscard]] const tilemapedit::TileDoc& doc() const noexcept { return m_doc; }
	[[nodiscard]] const tilemapedit::TileView& view() const noexcept { return *m_viewport; }
	[[nodiscard]] const std::string& status() const noexcept { return m_status; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] bool tried() const noexcept { return m_tried; }
	[[nodiscard]] int layer() const noexcept { return m_layer; }
	[[nodiscard]] Tool tool() const noexcept { return m_tool; }
	[[nodiscard]] const tilemapedit::Cell& brush() const noexcept { return m_brush; }
	[[nodiscard]] const std::optional<tilemapedit::Rect>& selection() const noexcept { return m_selection; }
	[[nodiscard]] const tilemapedit::Clip& clip() const noexcept { return m_clip; }
	[[nodiscard]] bool pasting() const noexcept { return m_pasting; }
	[[nodiscard]] bool scriptDone() const noexcept { return m_scriptPos >= m_script.size() && m_scriptWait <= 0; }

private:
	enum class DragKind { None, Paint, Select, Place, Pan, RightPending };
	struct Drag
	{
		DragKind kind = DragKind::None;
		double downX = 0.0;
		double downY = 0.0;
		double lastX = 0.0;
		double lastY = 0.0;
		int lastCellX = 0;
		int lastCellY = 0;
		int anchorX = 0;
		int anchorY = 0;
		bool moved = false;
	};

	void load();
	[[nodiscard]] std::optional<DecodedImage> loadTilesetImage(std::string& problem) const;
	void fail(const std::string& message);
	void pushAll();
	void pushPanel();
	void pushStatus();
	void pushImages();
	void changed(bool gestureEnd);
	void resetEditState();

	void tryOut();
	void keep();
	void discard();
	void undoRedo(bool undo);
	void openFile(const std::string& name);
	void chooseLayer(int index);

	void copySelection();
	void startPaste();
	void flip(bool horizontal);
	void clearSelection();
	void cancelPaste();

	void mapPointer(const PointerEvent& ev, double sx, double sy);
	void mapDown(int button, double sx, double sy);
	void mapMove(double sx, double sy);
	void tilesPointer(const PointerEvent& ev, double sx, double sy);
	void paintCell(int x, int y);
	void paintLine(int x0, int y0, int x1, int y1);
	void pickAt(int x, int y);
	void endDrag();
	[[nodiscard]] std::pair<int, int> clampedCell(double sx, double sy) const;
	[[nodiscard]] tilemapedit::Overlay overlay() const;

	void loadScript(const std::string& path);
	void scriptStep();
	void runScriptLine(const std::string& line);

	ToolView* m_view;
	std::string m_schemaLogical;
	std::string m_fileName;
	std::string m_rootHint;
	std::string m_scriptPath;
	std::string m_error;
	std::string m_status;
	std::string m_notice;
	tilemapedit::TileSchema m_schema;
	stageedit::StageFiles m_files;
	tilemapedit::TileDoc m_doc;
	std::unique_ptr<tilemapedit::TilePalette> m_palette;
	std::unique_ptr<tilemapedit::TileView> m_viewport;
	int m_layer = 0;
	Tool m_tool = Tool::Paint;
	tilemapedit::Cell m_brush;
	std::optional<tilemapedit::Rect> m_selection;
	tilemapedit::Clip m_clip;
	bool m_pasting = false;
	int m_ghostX = 0;
	int m_ghostY = 0;
	Drag m_drag;
	std::vector<std::string> m_script;
	std::size_t m_scriptPos = 0;
	int m_scriptWait = 0;
	bool m_tried = false;
	std::uint64_t m_autoRevision = 0;   ///< 最後に試した (または開いた) ときの文書の版
	bool m_auto = false;
	bool m_imagesDirty = true;
	bool m_loaded = false;
};

} // namespace mitiru::tool
