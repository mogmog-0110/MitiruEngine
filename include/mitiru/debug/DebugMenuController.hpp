#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/debug/DebugMenuBuiltins.hpp>
#include <mitiru/debug/DebugMenuInput.hpp>

namespace mitiru::debug
{

struct MenuView
{
	bool open = false;
	std::string rowsJson;
	std::string filter;
	bool noMatch = false;
	bool canScrollUp = false;
	bool canScrollDown = false;
	std::string info;
};

class IDebugMenuHost
{
public:
	virtual ~IDebugMenuHost() = default;
	virtual bool paused() const = 0;
	virtual void setPausedByMenu(bool pause) = 0;
	virtual void stepOneFrame() = 0;
	virtual float timeScale() const = 0;
	virtual void setTimeScale(float scale) = 0;
	virtual bool lofi() const = 0;
	virtual void toggleLofi() = 0;
	virtual void saveScreenshot() = 0;
	virtual void saveBugRecord() = 0;
	virtual void openLogFolder() = 0;
	virtual void openSaveFolder() = 0;
	virtual bool toolAvailable() const = 0;
	virtual void openTool(int builtinToolId) = 0;
	virtual bool pushGameAction(const std::string& name, const std::string& payloadJson) = 0;
	virtual void showView(const MenuView& view) = 0;
};

class DebugMenuController
{
public:
	using PullFn = std::function<bool(
		std::vector<module::DebugMenuItem>& items,
		std::uint32_t& flags)>;

	static constexpr int kClosedPullEvery = 30;
	static constexpr double kPointerIdleSec = 0.3;
	static constexpr double kFpsWindowSec = 0.5;

	void setEnabled(bool enabled) noexcept
	{
		m_enabled = enabled;
		if (!active())
		{
			requestClose();
		}
	}

	void setInert(bool inert) noexcept
	{
		m_inert = inert;
		if (!active())
		{
			requestClose();
		}
	}

	[[nodiscard]] bool active() const noexcept
	{
		return m_enabled && !m_inert &&
			(m_gameFlags & module::kDebugMenuDevToolsOff) == 0;
	}

	[[nodiscard]] std::uint32_t gameFlags() const noexcept
	{
		return m_gameFlags;
	}

	[[nodiscard]] bool gameWantsDevToolsOff() const noexcept
	{
		return (m_gameFlags & module::kDebugMenuDevToolsOff) != 0;
	}

	[[nodiscard]] bool gameKeepsScreenshotKey() const noexcept
	{
		return (m_gameFlags & module::kDebugMenuKeepScreenshotKey) != 0;
	}

	[[nodiscard]] bool isOpen() const noexcept
	{
		return m_model.isOpen();
	}

	void gate(module::InputSnapshot& snap, double nowSec)
	{
		const bool wasOpen = isOpen();
		if (!active())
		{
			if (wasOpen)
			{
				close(snap);
				hideInputFromGame(snap);
			}
			else
			{
				m_held.apply(snap);
			}
			rememberGateTime(nowSec);
			return;
		}

		if (!wasOpen)
		{
			if (DebugMenuInputReader::openRequested(snap))
			{
				m_model.open();
				m_hover = {};
				m_pick = {};
				m_adjust = {};
				hideInputFromGame(snap);
			}
			else
			{
				m_held.apply(snap);
			}
			rememberPointer(snap, nowSec);
			rememberGateTime(nowSec);
			return;
		}

		if (DebugMenuInputReader::openRequested(snap))
		{
			close(snap);
			hideInputFromGame(snap);
			rememberGateTime(nowSec);
			return;
		}

		MenuInput input = m_reader.read(snap, gateDelta(nowSec));
		rememberPointer(snap, nowSec);
		input.pointerMoved = nowSec - m_pointerMovedAt <= kPointerIdleSec;
		applyUiInput(input);

		std::vector<MenuCommand> commands;
		bool closeRequested = false;
		m_model.update(input, commands, closeRequested);
		for (const MenuCommand& command : commands)
		{
			m_commands.push_back(command);
		}
		if (closeRequested)
		{
			close(snap);
		}
		hideInputFromGame(snap);
		rememberGateTime(nowSec);
	}

	bool onUiAction(std::string_view name, std::string_view payloadJson)
	{
		UiInput* target = nullptr;
		if (name == "dbgmenu.hover") { target = &m_hover; }
		else if (name == "dbgmenu.pick") { target = &m_pick; }
		else if (name == "dbgmenu.adjust") { target = &m_adjust; }
		else { return false; }

		const nlohmann::json payload =
			nlohmann::json::parse(payloadJson, nullptr, false);
		if (!payload.is_object() || !payload.contains("i") ||
			!payload["i"].is_number_integer())
		{
			return true;
		}
		target->pending = true;
		target->index = payload["i"].get<int>();
		target->direction = 0;
		if (target == &m_adjust && payload.contains("d") &&
			payload["d"].is_number_integer())
		{
			target->direction = payload["d"].get<int>();
		}
		return true;
	}

	void onFrame(
		IDebugMenuHost& host,
		const PullFn& pull,
		std::uint64_t frame,
		double nowSec)
	{
		updateFps(nowSec);
		pullGameRows(pull, frame);
		if (!active() && isOpen())
		{
			m_model.close();
			m_reader.reset();
		}

		setCurrentRows(host);
		executeCommands(host);
		reconcilePause(host);
		showView(host);
		m_previousOpen = isOpen();
	}

private:
	struct UiInput
	{
		bool pending = false;
		int index = -1;
		int direction = 0;
	};

	[[nodiscard]] float gateDelta(double nowSec) const noexcept
	{
		if (!m_haveGateTime)
		{
			return 0.0f;
		}
		return static_cast<float>(std::clamp(nowSec - m_lastGateAt, 0.0, 0.1));
	}

	void rememberGateTime(double nowSec) noexcept
	{
		m_lastGateAt = nowSec;
		m_haveGateTime = true;
	}

	void rememberPointer(const module::InputSnapshot& snap, double nowSec) noexcept
	{
		if (!m_havePointer || snap.mouseX != m_mouseX || snap.mouseY != m_mouseY)
		{
			m_mouseX = snap.mouseX;
			m_mouseY = snap.mouseY;
			m_pointerMovedAt = nowSec;
			m_havePointer = true;
		}
	}

	void applyUiInput(MenuInput& input) noexcept
	{
		if (m_hover.pending)
		{
			input.hoverIndex = m_hover.index;
		}
		if (m_pick.pending)
		{
			input.pickIndex = m_pick.index;
		}
		if (m_adjust.pending)
		{
			input.adjustIndex = m_adjust.index;
			input.adjustDirection = m_adjust.direction;
		}
		m_hover = {};
		m_pick = {};
		m_adjust = {};
	}

	void requestClose() noexcept
	{
		if (isOpen())
		{
			m_model.close();
			m_reader.reset();
		}
	}

	void close(const module::InputSnapshot& snap) noexcept
	{
		m_held.capture(snap);
		m_model.close();
		m_reader.reset();
	}

	void updateFps(double nowSec) noexcept
	{
		if (!m_haveFpsWindow)
		{
			m_fpsWindowAt = nowSec;
			m_fpsFrames = 0;
			m_haveFpsWindow = true;
			return;
		}

		++m_fpsFrames;
		const double elapsed = nowSec - m_fpsWindowAt;
		if (elapsed >= kFpsWindowSec)
		{
			m_fps = static_cast<int>(
				std::lround(static_cast<double>(m_fpsFrames) / elapsed));
			m_fpsWindowAt = nowSec;
			m_fpsFrames = 0;
		}
	}

	void pullGameRows(const PullFn& pull, std::uint64_t frame)
	{
		const bool due = !m_pulled || isOpen() ||
			frame % static_cast<std::uint64_t>(kClosedPullEvery) == 0;
		if (!due)
		{
			return;
		}

		std::vector<module::DebugMenuItem> items;
		std::uint32_t flags = 0;
		if (!pull || !pull(items, flags))
		{
			items.clear();
			flags = 0;
		}
		m_gameRows = convertItems(items);
		m_gameFlags = flags;
		m_pulled = true;
	}

	[[nodiscard]] static std::vector<MenuRow> convertItems(
		const std::vector<module::DebugMenuItem>& items)
	{
		std::vector<MenuRow> rows;
		rows.reserve(items.size());
		for (const module::DebugMenuItem& item : items)
		{
			MenuRow row;
			row.kind = item.kind;
			row.flags = item.flags;
			row.id = item.id;
			row.label.assign(item.label, boundedLength(item.label));
			row.text.assign(item.text, boundedLength(item.text));
			row.value = item.value;
			row.minValue = item.minValue;
			row.maxValue = item.maxValue;
			row.step = item.step;
			rows.push_back(std::move(row));
		}
		return rows;
	}

	template<std::size_t Size>
	[[nodiscard]] static std::size_t boundedLength(
		const char (&text)[Size]) noexcept
	{
		std::size_t length = 0;
		while (length < Size && text[length] != '\0')
		{
			++length;
		}
		return length;
	}

	void setCurrentRows(IDebugMenuHost& host)
	{
		// 閉じていて情報の表示も無ければ行を見る人がいない。開く最初のフレームで組み直す
		if (!isOpen() && !m_showInfo && m_haveView && m_commands.empty()) { return; }
		std::vector<MenuRow> rows = m_gameRows;
		DebugMenuBuiltinState state;
		state.showInfo = m_showInfo;
		state.pauseWhileOpen = m_pauseWhileOpen;
		state.paused = host.paused();
		state.timeScale = host.timeScale();
		state.lofi = host.lofi();
		state.toolAvailable = host.toolAvailable();
		appendDebugMenuBuiltins(rows, state);
		m_model.setRows(std::move(rows));
	}

	void executeCommands(IDebugMenuHost& host)
	{
		bool sentGameCommand = false;
		for (auto command = m_commands.begin(); command != m_commands.end();)
		{
			if (command->builtin)
			{
				executeBuiltin(host, *command);
				command = m_commands.erase(command);
				continue;
			}
			if (sentGameCommand)
			{
				++command;
				continue;
			}
			sentGameCommand = true;
			if (sendGameCommand(host, *command))
			{
				command = m_commands.erase(command);
			}
			else
			{
				++command;
			}
		}
	}

	[[nodiscard]] static bool sendGameCommand(
		IDebugMenuHost& host,
		const MenuCommand& command)
	{
		nlohmann::json payload = {{"id", command.id}};
		payload["v"] = command.hasValue ? command.value : 0.0f;
		return host.pushGameAction("debug.menu", payload.dump());
	}

	void executeBuiltin(IDebugMenuHost& host, const MenuCommand& command)
	{
		switch (command.id)
		{
		case kBuiltinShowInfo:
			m_showInfo = command.value != 0.0f;
			break;
		case kBuiltinPauseWhileOpen:
			m_pauseWhileOpen = command.value != 0.0f;
			break;
		case kBuiltinStep:
			if (host.paused()) { host.stepOneFrame(); }
			break;
		case kBuiltinSpeed:
			setBuiltinSpeed(host, command.value);
			break;
		case kBuiltinLofi: host.toggleLofi(); break;
		case kBuiltinScreenshot: host.saveScreenshot(); break;
		case kBuiltinBugRecord: host.saveBugRecord(); break;
		case kBuiltinOpenLog: host.openLogFolder(); break;
		case kBuiltinOpenSave: host.openSaveFolder(); break;
		default:
			if (command.id >= kBuiltinToolInspector &&
				command.id <= kBuiltinToolSceneTree &&
				host.toolAvailable())
			{
				host.openTool(command.id);
			}
			break;
		}
	}

	static void setBuiltinSpeed(IDebugMenuHost& host, float value)
	{
		const int index = static_cast<int>(value);
		if (index >= 0 && index < static_cast<int>(kBuiltinSpeeds.size()))
		{
			host.setTimeScale(kBuiltinSpeeds[index]);
		}
	}

	void reconcilePause(IDebugMenuHost& host)
	{
		const bool shouldPause = isOpen() && active() && m_pauseWhileOpen;
		if (shouldPause && !m_pausedByMenu && !host.paused())
		{
			host.setPausedByMenu(true);
			m_pausedByMenu = true;
		}
		else if (!shouldPause && m_pausedByMenu)
		{
			host.setPausedByMenu(false);
			m_pausedByMenu = false;
		}
	}

	void showView(IDebugMenuHost& host)
	{
		MenuView view;
		view.open = isOpen();
		// 閉じている間は行を使わないので、毎フレーム JSON を組まない (確保が毎フレーム数百回増える)
		view.rowsJson = (view.open || !m_haveView) ? m_model.rowsJson() : m_lastView.rowsJson;
		view.filter = m_model.filter();
		view.noMatch = m_model.noMatch();
		view.canScrollUp = m_model.canScrollUp();
		view.canScrollDown = m_model.canScrollDown();
		view.info = makeInfo();
		if (!m_haveView || !sameView(view, m_lastView))
		{
			host.showView(view);
			m_lastView = std::move(view);
			m_haveView = true;
		}
	}

	[[nodiscard]] std::string makeInfo() const
	{
		if (!m_showInfo || !active())
		{
			return {};
		}
		std::string result = std::to_string(m_fps) + " fps";
		for (const auto& [label, text] : m_model.pinnedTexts())
		{
			result += "  " + label + " " + text;
		}
		return result;
	}

	[[nodiscard]] static bool sameView(
		const MenuView& left,
		const MenuView& right) noexcept
	{
		return left.open == right.open &&
			left.rowsJson == right.rowsJson &&
			left.filter == right.filter &&
			left.noMatch == right.noMatch &&
			left.canScrollUp == right.canScrollUp &&
			left.canScrollDown == right.canScrollDown &&
			left.info == right.info;
	}

	DebugMenuModel m_model;
	DebugMenuInputReader m_reader;
	HeldInputMask m_held;
	std::vector<MenuRow> m_gameRows;
	std::deque<MenuCommand> m_commands;
	std::uint32_t m_gameFlags = 0;
	bool m_enabled = true;
	bool m_inert = false;
	bool m_showInfo = false;
	bool m_pauseWhileOpen = true;
	bool m_pausedByMenu = false;
	bool m_previousOpen = false;
	bool m_pulled = false;
	bool m_haveGateTime = false;
	double m_lastGateAt = 0.0;
	bool m_havePointer = false;
	float m_mouseX = 0.0f;
	float m_mouseY = 0.0f;
	double m_pointerMovedAt = -std::numeric_limits<double>::infinity();
	UiInput m_hover;
	UiInput m_pick;
	UiInput m_adjust;
	bool m_haveFpsWindow = false;
	double m_fpsWindowAt = 0.0;
	int m_fpsFrames = 0;
	int m_fps = 0;
	bool m_haveView = false;
	MenuView m_lastView;
};

}  // namespace mitiru::debug
