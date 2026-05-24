#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/FocusManager.hpp"

using namespace sgc::ui;

// ── Helper ──────────────────────────────────────────────────

static FocusableWidget makeWidget(FocusId id, float x, float y, float w = 100.0f, float h = 40.0f)
{
	return FocusableWidget{id, sgc::Rectf{x, y, w, h}, true, {}, {}, {}, {}};
}

static FocusInput noInput()
{
	return {};
}

// ── Tests ───────────────────────────────────────────────────

TEST_CASE("FocusManager - register widget increases count", "[ui][focus]")
{
	FocusManager fm;
	REQUIRE(fm.widgetCount() == 0);
	fm.registerWidget(makeWidget(1, 0, 0));
	REQUIRE(fm.widgetCount() == 1);
	fm.registerWidget(makeWidget(2, 0, 50));
	REQUIRE(fm.widgetCount() == 2);
}

TEST_CASE("FocusManager - unregister widget decreases count", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.registerWidget(makeWidget(2, 0, 50));
	fm.unregisterWidget(1);
	REQUIRE(fm.widgetCount() == 1);
}

TEST_CASE("FocusManager - update with no focus and no input returns no focus", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	auto result = fm.update(noInput());
	REQUIRE_FALSE(result.focusedId.has_value());
	REQUIRE_FALSE(result.activatedId.has_value());
	REQUIRE_FALSE(result.cancelPressed);
}

TEST_CASE("FocusManager - setFocus sets current focus", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.setFocus(1);
	REQUIRE(fm.currentFocus().has_value());
	REQUIRE(*fm.currentFocus() == 1);
}

TEST_CASE("FocusManager - clearFocus removes focus", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.setFocus(1);
	fm.clearFocus();
	REQUIRE_FALSE(fm.currentFocus().has_value());
}

TEST_CASE("FocusManager - isFocused returns correct state", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.registerWidget(makeWidget(2, 0, 50));
	fm.setFocus(1);
	REQUIRE(fm.isFocused(1));
	REQUIRE_FALSE(fm.isFocused(2));
}

TEST_CASE("FocusManager - directional navigation moves to nearest widget", "[ui][focus]")
{
	FocusManager fm;
	// 縦に3つ並べる
	fm.registerWidget(makeWidget(1, 50, 0, 100, 40));    // 上
	fm.registerWidget(makeWidget(2, 50, 60, 100, 40));   // 中
	fm.registerWidget(makeWidget(3, 50, 120, 100, 40));  // 下
	fm.setFocus(2);

	// 下方向
	FocusInput input{};
	input.down = true;
	auto result = fm.update(input);
	REQUIRE(result.focusedId.has_value());
	REQUIRE(*result.focusedId == 3);

	// 上方向
	FocusInput upInput{};
	upInput.up = true;
	result = fm.update(upInput);
	REQUIRE(result.focusedId.has_value());
	REQUIRE(*result.focusedId == 2);
}

TEST_CASE("FocusManager - tab cycles through widgets in order", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.registerWidget(makeWidget(2, 0, 50));
	fm.registerWidget(makeWidget(3, 0, 100));
	fm.setFocus(1);

	FocusInput input{};
	input.tabNext = true;
	auto result = fm.update(input);
	REQUIRE(*result.focusedId == 2);

	result = fm.update(input);
	REQUIRE(*result.focusedId == 3);

	// ラップアラウンド
	result = fm.update(input);
	REQUIRE(*result.focusedId == 1);
}

TEST_CASE("FocusManager - shift-tab cycles backwards", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.registerWidget(makeWidget(2, 0, 50));
	fm.registerWidget(makeWidget(3, 0, 100));
	fm.setFocus(1);

	FocusInput input{};
	input.tabPrev = true;
	auto result = fm.update(input);
	// 1の前は3（ラップアラウンド）
	REQUIRE(*result.focusedId == 3);

	result = fm.update(input);
	REQUIRE(*result.focusedId == 2);
}

TEST_CASE("FocusManager - confirm activates focused widget", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.setFocus(1);

	FocusInput input{};
	input.confirm = true;
	auto result = fm.update(input);
	REQUIRE(result.activatedId.has_value());
	REQUIRE(*result.activatedId == 1);
}

TEST_CASE("FocusManager - cancel sets cancelPressed", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.setFocus(1);

	FocusInput input{};
	input.cancel = true;
	auto result = fm.update(input);
	REQUIRE(result.cancelPressed);
}

TEST_CASE("FocusManager - disabled widget is skipped in navigation", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 50, 0, 100, 40));
	FocusableWidget disabled = makeWidget(2, 50, 60, 100, 40);
	disabled.enabled = false;
	fm.registerWidget(disabled);
	fm.registerWidget(makeWidget(3, 50, 120, 100, 40));
	fm.setFocus(1);

	// 下方向：2はdisabledなので3に飛ぶ
	FocusInput input{};
	input.down = true;
	auto result = fm.update(input);
	REQUIRE(*result.focusedId == 3);

	// タブでも2はスキップ
	fm.setFocus(1);
	FocusInput tabInput{};
	tabInput.tabNext = true;
	result = fm.update(tabInput);
	REQUIRE(*result.focusedId == 3);
}

TEST_CASE("FocusManager - explicit navUp override takes precedence", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 50, 0, 100, 40));
	fm.registerWidget(makeWidget(2, 50, 60, 100, 40));

	// 3のnavUpを明示的に1に設定（空間的には2が近い）
	FocusableWidget w3 = makeWidget(3, 50, 120, 100, 40);
	w3.navUp = 1;
	fm.registerWidget(w3);
	fm.setFocus(3);

	FocusInput input{};
	input.up = true;
	auto result = fm.update(input);
	REQUIRE(*result.focusedId == 1);
}

TEST_CASE("FocusManager - no crash when navigating with single widget", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.setFocus(1);

	FocusInput input{};
	input.down = true;
	auto result = fm.update(input);
	// フォーカスは1のまま（移動先がない）
	REQUIRE(*result.focusedId == 1);
}

TEST_CASE("FocusManager - clear removes all widgets", "[ui][focus]")
{
	FocusManager fm;
	fm.registerWidget(makeWidget(1, 0, 0));
	fm.registerWidget(makeWidget(2, 0, 50));
	fm.setFocus(1);
	fm.clear();
	REQUIRE(fm.widgetCount() == 0);
	REQUIRE_FALSE(fm.currentFocus().has_value());
}
