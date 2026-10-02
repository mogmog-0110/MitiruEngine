#pragma once
// 同梱ページの作り口。makePage (PageFactory.cpp) が名前で選ぶ。

#include "../ToolPage.hpp"

#include <memory>

namespace mitiru::tool
{

std::unique_ptr<ToolPage> makeInspectPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeInputPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeScenePage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeGameDefinedPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makePerfPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeMixerPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeRewindPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeReplayPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeSceneViewPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeWhyViewPage(const PageContext& ctx);
std::unique_ptr<ToolPage> makeFrameViewPage(const PageContext& ctx);

} // namespace mitiru::tool
