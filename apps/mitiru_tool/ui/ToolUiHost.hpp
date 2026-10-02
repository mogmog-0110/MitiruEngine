#pragma once
// ツール窓 1 枚の RmlUi。エンジンの DX12 デバイスと R1 の RenderInterface の上に、ページの RML を
// 1 枚だけ載せる。値は data model "tool" へ写すだけで、UI の操作 (dispatch・マウス・キー) は
// ToolPage へ渡す。UI から C++ の値を書き戻す経路は無い (RmlStateModel / JsonVariable と同じ)。

#include "ToolPage.hpp"

#include <mitiru/platform/win32/Win32KeyMessage.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;

namespace Rml
{
class ElementDocument;
}

namespace mitiru::tool
{

/// 窓のマウス。座標は描画先の実画素 (文脈も実画素で持ち、RCSS の dp で DPI を吸収する)。
struct ToolPointer
{
	float x = 0.0f;
	float y = 0.0f;
	bool buttons[3] = {};
	float wheel = 0.0f;   ///< 1 目盛り 120、奥が正
	bool ctrl = false;
	bool shift = false;
};

class ToolUiHost final : public ToolView
{
public:
	ToolUiHost();
	~ToolUiHost() override;
	ToolUiHost(const ToolUiHost&) = delete;
	ToolUiHost& operator=(const ToolUiHost&) = delete;

	/// @param dpRatio RCSS の 1dp が何画素か (窓の DPI / 96)
	bool start(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& document,
	           int width, int height, float dpRatio, std::string& error);
	void setPage(ToolPage* page) noexcept;
	void resize(int width, int height, float dpRatio);

	void processInput(const ToolPointer& pointer, std::span<const platform::Win32KeyMessage> keys);
	void update(double seconds);
	void render(ID3D12Resource* target, int width, int height);

	/// RML / RCSS が保存されていたら読み直す (data model の値は残る)。
	void reloadIfChanged();

	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] Rml::ElementDocument* document() const noexcept;

	void set(std::string_view key, nlohmann::json value) override;
	void setImage(std::string_view elementId, int width, int height, std::vector<std::uint8_t> rgba) override;

	struct Impl;

private:
	std::unique_ptr<Impl> m_impl;
};

} // namespace mitiru::tool
