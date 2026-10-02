#include "Pages.hpp"

#include <array>
#include <utility>

namespace mitiru::tool
{

namespace
{

using Maker = std::unique_ptr<ToolPage> (*)(const PageContext&);

constexpr std::array<std::pair<std::string_view, Maker>, 10> kPages = { {
	{ "inspect", makeInspectPage },
	{ "input", makeInputPage },
	{ "scene", makeScenePage },
	{ "perf", makePerfPage },
	{ "mixer", makeMixerPage },
	{ "rewind", makeRewindPage },
	{ "replay", makeReplayPage },
	{ "scene_view", makeSceneViewPage },
	{ "why_view", makeWhyViewPage },
	{ "frame_view", makeFrameViewPage },
} };

} // namespace

bool isBuiltinPage(std::string_view name)
{
	for (const auto& [page, maker] : kPages) { if (page == name) { return true; } }
	return false;
}

bool pageRequestsScrub(std::string_view name)
{
	return name == "rewind";
}

bool pageUsesHttp(std::string_view name)
{
	return name == "scene_view" || name == "why_view" || name == "frame_view";
}

std::unique_ptr<ToolPage> makePage(std::string_view name, const PageContext& ctx)
{
	for (const auto& [page, maker] : kPages) { if (page == name) { return maker(ctx); } }
	return makeGameDefinedPage(ctx);
}

} // namespace mitiru::tool
