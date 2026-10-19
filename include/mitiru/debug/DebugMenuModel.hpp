#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::debug
{

struct MenuRow
{
	std::uint8_t kind = module::kDebugMenuSection;
	std::uint8_t flags = 0;
	bool         builtin = false;
	std::int32_t id = 0;
	std::string  label;
	std::string  text;
	float        value = 0.0f;
	float        minValue = 0.0f;
	float        maxValue = 0.0f;
	float        step = 1.0f;
};

struct MenuInput
{
	bool upPressed = false, downPressed = false, leftPressed = false, rightPressed = false;
	bool upHeld = false, downHeld = false, leftHeld = false, rightHeld = false;
	bool pagePrev = false, pageNext = false, home = false, end = false;
	bool confirm = false, cancel = false, backspace = false;
	float wheel = 0.0f;
	std::string typed;
	bool pointerMoved = false;
	int hoverIndex = -1;
	int pickIndex = -1;
	int adjustIndex = -1;
	int adjustDirection = 0;
	float dt = 0.0f;
};

struct MenuCommand
{
	bool         builtin = false;
	std::int32_t id = 0;
	bool         hasValue = false;
	float        value = 0.0f;
};

class DebugMenuModel
{
public:
	static constexpr int   kVisibleRows = 12;
	static constexpr int   kPageRows = 10;
	static constexpr int   kWheelRows = 3;
	static constexpr int   kFilterMax = 24;
	static constexpr float kRepeatDelay = 0.4f;
	static constexpr float kRepeatEvery = 0.06f;

	void setRows(std::vector<MenuRow> rows)
	{
		bool hadSelected = false;
		bool selectedBuiltin = false;
		std::int32_t selectedId = 0;
		if (const MenuRow* row = selectedRow())
		{
			hadSelected = true;
			selectedBuiltin = row->builtin;
			selectedId = row->id;
		}

		const bool changed = !rowsEqual(m_rows, rows);
		m_rows = std::move(rows);
		rebuildVisible();

		bool restored = false;
		if (hadSelected)
		{
			for (int i = 0; i < selectableCount(); ++i)
			{
				const MenuRow& row = m_rows[m_visible[m_selectableVisible[i]]];
				if (row.builtin == selectedBuiltin && row.id == selectedId)
				{
					m_selected = i;
					restored = true;
					break;
				}
			}
		}
		if (!restored)
		{
			clampSelection();
		}
		ensureSelectionVisible();
		if (changed)
		{
			++m_revision;
		}
	}

	void open()
	{
		const bool changed = !m_open || !m_filter.empty() || m_selected != 0 || m_firstVisible != 0;
		m_open = true;
		m_filter.clear();
		m_selected = 0;
		m_firstVisible = 0;
		resetRepeats();
		rebuildVisible();
		if (changed)
		{
			++m_revision;
		}
	}

	void close()
	{
		if (m_open)
		{
			m_open = false;
			resetRepeats();
			++m_revision;
		}
	}

	[[nodiscard]] bool isOpen() const noexcept
	{
		return m_open;
	}

	void update(const MenuInput& in, std::vector<MenuCommand>& commands, bool& closeRequested)
	{
		closeRequested = false;
		updateFilter(in);

		const int vertical = repeatSteps(in.upHeld, in.downHeld, in.upPressed, in.downPressed,
		                                 in.dt, m_verticalRepeat);
		moveSelection(vertical);
		if (in.pagePrev) { moveSelection(-kPageRows); }
		if (in.pageNext) { moveSelection(kPageRows); }
		if (in.home) { setSelection(0); }
		if (in.end) { setSelection(selectableCount() - 1); }
		if (in.wheel > 0.0f) { moveSelection(-kWheelRows); }
		if (in.wheel < 0.0f) { moveSelection(kWheelRows); }

		if (in.pointerMoved && in.hoverIndex >= 0)
		{
			setSelection(in.hoverIndex);
		}

		const int horizontal = repeatSteps(in.leftHeld, in.rightHeld, in.leftPressed, in.rightPressed,
		                                   in.dt, m_horizontalRepeat);
		adjustSelected(horizontal, commands);

		if (in.pickIndex >= 0)
		{
			setSelection(in.pickIndex);
			confirmSelected(commands, closeRequested);
		}
		if (in.adjustIndex >= 0)
		{
			setSelection(in.adjustIndex);
			if (in.adjustDirection == -1 || in.adjustDirection == 1)
			{
				adjustSelected(in.adjustDirection, commands);
			}
		}
		if (in.confirm)
		{
			confirmSelected(commands, closeRequested);
		}
		if (in.cancel)
		{
			if (!m_filter.empty())
			{
				m_filter.clear();
				m_selected = 0;
				m_firstVisible = 0;
				rebuildVisible();
				++m_revision;
			}
			else
			{
				closeRequested = true;
			}
		}
	}

	[[nodiscard]] std::string rowsJson() const
	{
		nlohmann::json result = nlohmann::json::array();
		const int end = std::min(static_cast<int>(m_visible.size()), m_firstVisible + kVisibleRows);
		for (int visibleIndex = m_firstVisible; visibleIndex < end; ++visibleIndex)
		{
			const MenuRow& row = m_rows[m_visible[visibleIndex]];
			const int selectable = selectableIndexAtVisible(visibleIndex);
			const bool disabled = (row.flags & module::kDebugItemDisabled) != 0;
			result.push_back({
				{"t", row.label},
				{"k", row.kind},
				{"s", selectable >= 0 && selectable == m_selected},
				{"i", selectable},
				{"d", disabled},
				{"v", displayValue(row)},
				{"on", row.kind == module::kDebugMenuToggle && row.value != 0.0f},
				{"n", row.kind == module::kDebugMenuNumber},
			});
		}
		return result.dump();
	}

	[[nodiscard]] const std::string& filter() const noexcept
	{
		return m_filter;
	}

	[[nodiscard]] bool canScrollUp() const noexcept
	{
		return m_firstVisible > 0;
	}

	[[nodiscard]] bool canScrollDown() const noexcept
	{
		return m_firstVisible + kVisibleRows < static_cast<int>(m_visible.size());
	}

	[[nodiscard]] bool noMatch() const noexcept
	{
		return !m_filter.empty() && m_visible.empty();
	}

	[[nodiscard]] std::vector<std::pair<std::string, std::string>> pinnedTexts() const
	{
		std::vector<std::pair<std::string, std::string>> result;
		for (const MenuRow& row : m_rows)
		{
			if (row.kind == module::kDebugMenuText && (row.flags & module::kDebugItemPinned) != 0)
			{
				result.emplace_back(row.label, row.text);
			}
		}
		return result;
	}

	[[nodiscard]] int revision() const noexcept
	{
		return m_revision;
	}

private:
	struct RepeatState
	{
		int direction = 0;
		float elapsed = 0.0f;
		float next = kRepeatDelay;
	};

	[[nodiscard]] static bool selectable(const MenuRow& row) noexcept
	{
		const bool kind = row.kind == module::kDebugMenuButton ||
		                  row.kind == module::kDebugMenuToggle ||
		                  row.kind == module::kDebugMenuNumber ||
		                  row.kind == module::kDebugMenuChoice;
		return kind && (row.flags & module::kDebugItemDisabled) == 0;
	}

	[[nodiscard]] static bool rowsEqual(const std::vector<MenuRow>& a, const std::vector<MenuRow>& b)
	{
		if (a.size() != b.size()) { return false; }
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			const MenuRow& x = a[i];
			const MenuRow& y = b[i];
			if (x.kind != y.kind || x.flags != y.flags || x.builtin != y.builtin || x.id != y.id ||
			    x.label != y.label || x.text != y.text || x.value != y.value ||
			    x.minValue != y.minValue || x.maxValue != y.maxValue || x.step != y.step)
			{
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] static std::string lowerAscii(std::string value)
	{
		for (char& c : value)
		{
			if (c >= 'A' && c <= 'Z')
			{
				c = static_cast<char>(c - 'A' + 'a');
			}
			else if (c == '_')
			{
				c = '-';   // Shift を押さずに打てる '-' で、id の '_' にも当たるように
			}
		}
		return value;
	}

	[[nodiscard]] bool matches(const MenuRow& row) const
	{
		return m_filter.empty() || lowerAscii(row.label).find(lowerAscii(m_filter)) != std::string::npos;
	}

	void rebuildVisible()
	{
		m_visible.clear();
		m_selectableVisible.clear();
		for (int i = 0; i < static_cast<int>(m_rows.size());)
		{
			if (m_rows[i].kind != module::kDebugMenuSection)
			{
				if (matches(m_rows[i])) { appendVisible(i); }
				++i;
				continue;
			}

			const int section = i++;
			const int begin = i;
			while (i < static_cast<int>(m_rows.size()) && m_rows[i].kind != module::kDebugMenuSection) { ++i; }
			bool sectionMatches = m_filter.empty();
			for (int j = begin; j < i && !sectionMatches; ++j) { sectionMatches = matches(m_rows[j]); }
			if (!sectionMatches) { continue; }
			appendVisible(section);
			for (int j = begin; j < i; ++j)
			{
				if (matches(m_rows[j])) { appendVisible(j); }
			}
		}
		clampSelection();
		clampFirstVisible();
	}

	void appendVisible(int rowIndex)
	{
		const int visibleIndex = static_cast<int>(m_visible.size());
		m_visible.push_back(rowIndex);
		if (selectable(m_rows[rowIndex]))
		{
			m_selectableVisible.push_back(visibleIndex);
		}
	}

	void updateFilter(const MenuInput& in)
	{
		const std::string before = m_filter;
		for (char c : in.typed)
		{
			if (static_cast<int>(m_filter.size()) >= kFilterMax) { break; }
			m_filter.push_back(c);
		}
		if (in.backspace && !m_filter.empty())
		{
			m_filter.pop_back();
		}
		if (m_filter != before)
		{
			m_selected = 0;
			m_firstVisible = 0;
			rebuildVisible();
			++m_revision;
		}
	}

	[[nodiscard]] static int repeatSteps(bool negativeHeld, bool positiveHeld, bool negativePressed,
	                                     bool positivePressed, float dt, RepeatState& state)
	{
		const int heldDirection = negativeHeld == positiveHeld ? 0 : (negativeHeld ? -1 : 1);
		const int pressedDirection = negativePressed == positivePressed ? 0 : (negativePressed ? -1 : 1);
		if (heldDirection == 0)
		{
			state = {};
			return pressedDirection;
		}
		if (pressedDirection != 0 || state.direction != heldDirection)
		{
			state.direction = heldDirection;
			state.elapsed = 0.0f;
			state.next = kRepeatDelay;
			return pressedDirection != 0 ? pressedDirection : heldDirection;
		}

		state.elapsed += std::max(dt, 0.0f);
		int count = 0;
		while (state.elapsed >= state.next)
		{
			++count;
			state.next += kRepeatEvery;
		}
		return count * heldDirection;
	}

	void moveSelection(int delta)
	{
		if (delta == 0 || selectableCount() == 0) { return; }
		setSelection(std::clamp(m_selected + delta, 0, selectableCount() - 1));
	}

	void setSelection(int selection)
	{
		if (selectableCount() == 0) { selection = 0; }
		else { selection = std::clamp(selection, 0, selectableCount() - 1); }
		if (selection != m_selected)
		{
			m_selected = selection;
			++m_revision;
		}
		ensureSelectionVisible();
	}

	void clampSelection()
	{
		if (selectableCount() == 0) { m_selected = 0; }
		else { m_selected = std::clamp(m_selected, 0, selectableCount() - 1); }
	}

	void ensureSelectionVisible()
	{
		if (selectableCount() == 0)
		{
			setFirstVisible(0);
			return;
		}
		const int visibleIndex = m_selectableVisible[m_selected];
		int first = m_firstVisible;
		if (visibleIndex < first) { first = visibleIndex; }
		if (visibleIndex >= first + kVisibleRows) { first = visibleIndex - kVisibleRows + 1; }
		if (first == visibleIndex && first > 0 &&
		    m_rows[m_visible[first - 1]].kind == module::kDebugMenuSection)
		{
			--first;
		}
		setFirstVisible(first);
	}

	void setFirstVisible(int first)
	{
		const int maximum = std::max(0, static_cast<int>(m_visible.size()) - kVisibleRows);
		first = std::clamp(first, 0, maximum);
		if (first != m_firstVisible)
		{
			m_firstVisible = first;
			++m_revision;
		}
	}

	void clampFirstVisible()
	{
		m_firstVisible = std::clamp(m_firstVisible, 0,
		                            std::max(0, static_cast<int>(m_visible.size()) - kVisibleRows));
	}

	[[nodiscard]] int selectableCount() const noexcept
	{
		return static_cast<int>(m_selectableVisible.size());
	}

	[[nodiscard]] int selectableIndexAtVisible(int visibleIndex) const
	{
		const auto found = std::lower_bound(m_selectableVisible.begin(), m_selectableVisible.end(), visibleIndex);
		if (found == m_selectableVisible.end() || *found != visibleIndex) { return -1; }
		return static_cast<int>(found - m_selectableVisible.begin());
	}

	[[nodiscard]] const MenuRow* selectedRow() const
	{
		if (selectableCount() == 0 || m_selected < 0 || m_selected >= selectableCount()) { return nullptr; }
		return &m_rows[m_visible[m_selectableVisible[m_selected]]];
	}

	void adjustSelected(int amount, std::vector<MenuCommand>& commands) const
	{
		if (amount == 0) { return; }
		const MenuRow* row = selectedRow();
		if (row == nullptr) { return; }
		for (int i = 0; i < std::abs(amount); ++i)
		{
			adjust(*row, amount < 0 ? -1 : 1, commands);
		}
	}

	static void adjust(const MenuRow& row, int direction, std::vector<MenuCommand>& commands)
	{
		float value = row.value;
		if (row.kind == module::kDebugMenuNumber)
		{
			const float step = row.step > 0.0f ? row.step : 1.0f;
			value = std::clamp(row.value + step * static_cast<float>(direction), row.minValue, row.maxValue);
		}
		else if (row.kind == module::kDebugMenuChoice)
		{
			const int count = choiceCount(row.text);
			if (count <= 0) { return; }
			const int current = static_cast<int>(row.value);
			value = static_cast<float>((current + direction % count + count) % count);
		}
		else if (row.kind == module::kDebugMenuToggle)
		{
			value = direction < 0 ? 0.0f : 1.0f;
		}
		else
		{
			return;
		}
		if (value != row.value)
		{
			commands.push_back({row.builtin, row.id, true, value});
		}
	}

	void confirmSelected(std::vector<MenuCommand>& commands, bool& closeRequested) const
	{
		const MenuRow* row = selectedRow();
		if (row == nullptr) { return; }
		if (row->kind == module::kDebugMenuButton)
		{
			commands.push_back({row->builtin, row->id, false, 0.0f});
			closeRequested = closeRequested || (row->flags & module::kDebugItemCloseOnPick) != 0;
		}
		else if (row->kind == module::kDebugMenuToggle)
		{
			commands.push_back({row->builtin, row->id, true, row->value == 0.0f ? 1.0f : 0.0f});
		}
		else if (row->kind == module::kDebugMenuChoice)
		{
			const int count = choiceCount(row->text);
			if (count > 0)
			{
				const int current = static_cast<int>(row->value);
				commands.push_back({row->builtin, row->id, true, static_cast<float>((current + 1) % count)});
			}
		}
	}

	[[nodiscard]] static int choiceCount(const std::string& text)
	{
		if (text.empty()) { return 0; }
		return 1 + static_cast<int>(std::count(text.begin(), text.end(), '|'));
	}

	[[nodiscard]] static std::string choiceName(const std::string& text, int index)
	{
		if (index < 0) { return "?"; }
		std::size_t begin = 0;
		for (int i = 0; i < index; ++i)
		{
			begin = text.find('|', begin);
			if (begin == std::string::npos) { return "?"; }
			++begin;
		}
		const std::size_t end = text.find('|', begin);
		if (begin > text.size()) { return "?"; }
		return text.substr(begin, end == std::string::npos ? end : end - begin);
	}

	[[nodiscard]] static std::string numberText(float value)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(3) << value;
		std::string result = stream.str();
		while (!result.empty() && result.back() == '0') { result.pop_back(); }
		if (!result.empty() && result.back() == '.') { result.pop_back(); }
		if (result == "-0") { result = "0"; }
		return result;
	}

	[[nodiscard]] static std::string displayValue(const MenuRow& row)
	{
		switch (row.kind)
		{
		case module::kDebugMenuToggle: return row.value != 0.0f ? "On" : "Off";
		case module::kDebugMenuNumber: return numberText(row.value);
		case module::kDebugMenuChoice: return choiceName(row.text, static_cast<int>(row.value));
		case module::kDebugMenuText: return row.text;
		default: return {};
		}
	}

	void resetRepeats() noexcept
	{
		m_verticalRepeat = {};
		m_horizontalRepeat = {};
	}

	std::vector<MenuRow> m_rows;
	std::vector<int> m_visible;
	std::vector<int> m_selectableVisible;
	std::string m_filter;
	int m_selected = 0;
	int m_firstVisible = 0;
	int m_revision = 0;
	bool m_open = false;
	RepeatState m_verticalRepeat;
	RepeatState m_horizontalRepeat;
};

}  // namespace mitiru::debug
