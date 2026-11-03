#pragma once
// パッドで UI を操作する。十字キーと左スティックの倒し始めを上下左右のキー、A を Enter として RmlUi へ渡す
// (RmlUi は nav-* を持つ要素の間でフォーカスを動かし、tab-index: auto の要素は Enter で click する)。
// B は戻る (開いている select の一覧と confirm / prompt を閉じる。無ければ Escape のキー)、LB / RB は tabset の面を回す。
// InputSnapshot の押下状態とスティックだけから作るので、replay でも同じ所へフォーカスが動く。
//
// RmlFocusTrap は、focus-trap 属性を持つ要素 (ダイアログ) が見えている間、フォーカスをその中に閉じ込める
// (Xbox Accessibility Guideline 113)。現れたら中の最初の選べる要素を選び、消えたら前に選んでいた要素へ戻す。

#include "RmlUiHost.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementTabSet.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/ObserverPtr.h>

#include <cstdint>
#include <span>

namespace mitiru::ui_rml
{

/// 要素の属性。これを持つ要素が見えている間、フォーカスはその中から出ない
inline constexpr const char* kFocusTrapAttribute = "focus-trap";

struct PadNavCommand
{
	enum class Kind : unsigned char { Key, Back, TabPrev, TabNext };
	Kind kind = Kind::Key;
	Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;   ///< Kind::Key のとき
};

class RmlPadNavigator
{
public:
	static constexpr std::uint32_t kDPadUp = 0x0001;
	static constexpr std::uint32_t kDPadDown = 0x0002;
	static constexpr std::uint32_t kDPadLeft = 0x0004;
	static constexpr std::uint32_t kDPadRight = 0x0008;
	static constexpr std::uint32_t kLB = 0x0100;
	static constexpr std::uint32_t kRB = 0x0200;
	static constexpr std::uint32_t kA = 0x1000;
	static constexpr std::uint32_t kB = 0x2000;
	static constexpr float kStickThreshold = 0.5f;

	/// このフレームで押し始めたものだけを emit(const PadNavCommand&) で返す。押し続けても繰り返さない。
	template <class Emit>
	void translate(const UiPad& pad, Emit&& emit)
	{
		const std::uint32_t pressed = pad.buttonsDown & ~m_prevButtons;
		m_prevButtons = pad.buttonsDown;
		const Rml::Input::KeyIdentifier stick = stickKey(pad.stickX, pad.stickY);
		const bool stickTilted = stick != Rml::Input::KI_UNKNOWN && stick != m_prevStick;
		m_prevStick = stick;

		const struct { std::uint32_t bit; Rml::Input::KeyIdentifier key; } dpad[] = {
			{ kDPadUp, Rml::Input::KI_UP }, { kDPadDown, Rml::Input::KI_DOWN },
			{ kDPadLeft, Rml::Input::KI_LEFT }, { kDPadRight, Rml::Input::KI_RIGHT },
		};
		for (const auto& d : dpad)
		{
			if ((pressed & d.bit) != 0 || (stickTilted && stick == d.key)) { emit(PadNavCommand{ PadNavCommand::Kind::Key, d.key }); }
		}
		if ((pressed & kA) != 0) { emit(PadNavCommand{ PadNavCommand::Kind::Key, Rml::Input::KI_RETURN }); }
		if ((pressed & kB) != 0) { emit(PadNavCommand{ PadNavCommand::Kind::Back, Rml::Input::KI_UNKNOWN }); }
		if ((pressed & kLB) != 0) { emit(PadNavCommand{ PadNavCommand::Kind::TabPrev, Rml::Input::KI_UNKNOWN }); }
		if ((pressed & kRB) != 0) { emit(PadNavCommand{ PadNavCommand::Kind::TabNext, Rml::Input::KI_UNKNOWN }); }
	}

private:
	static Rml::Input::KeyIdentifier stickKey(float x, float y) noexcept
	{
		const float ax = x < 0.0f ? -x : x;
		const float ay = y < 0.0f ? -y : y;
		if (ax < kStickThreshold && ay < kStickThreshold) { return Rml::Input::KI_UNKNOWN; }
		if (ax >= ay) { return x > 0.0f ? Rml::Input::KI_RIGHT : Rml::Input::KI_LEFT; }
		return y > 0.0f ? Rml::Input::KI_UP : Rml::Input::KI_DOWN;
	}

	std::uint32_t m_prevButtons = 0;
	Rml::Input::KeyIdentifier m_prevStick = Rml::Input::KI_UNKNOWN;
};

namespace detail
{

[[nodiscard]] inline bool isInside(const Rml::Element& root, const Rml::Element* e) noexcept
{
	for (; e != nullptr; e = e->GetParentNode())
	{
		if (e == &root) { return true; }
	}
	return false;
}

[[nodiscard]] inline bool focusable(const Rml::Element& e)
{
	return e.GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto && !e.IsPseudoClassSet("disabled");
}

/// root の中で木の順に最初の、選べて見えている要素
[[nodiscard]] inline Rml::Element* firstFocusable(Rml::Element& root)
{
	for (int i = 0; i < root.GetNumChildren(); ++i)
	{
		Rml::Element* child = root.GetChild(i);
		if (child == nullptr || !child->IsVisible()) { continue; }
		if (focusable(*child)) { return child; }
		if (Rml::Element* deeper = firstFocusable(*child)) { return deeper; }
	}
	return nullptr;
}

[[nodiscard]] inline Rml::Element* tabSetOf(Rml::Element* e)
{
	for (; e != nullptr; e = e->GetParentNode())
	{
		if (e->GetTagName() == "tabset") { return e; }
	}
	return nullptr;
}

[[nodiscard]] inline Rml::Element* childByTag(Rml::Element& parent, const char* tag)
{
	for (int i = 0; i < parent.GetNumChildren(); ++i)
	{
		Rml::Element* child = parent.GetChild(i);
		if (child != nullptr && child->GetTagName() == tag) { return child; }
	}
	return nullptr;
}

} // namespace detail

/// 見えている文書の中で、focus-trap 属性を持ち表示中の要素のうち一番上に重なる物 (後の文書、後の要素が上)
[[nodiscard]] inline Rml::Element* findFocusTrap(std::span<Rml::ElementDocument* const> docs)
{
	Rml::Element* found = nullptr;
	for (Rml::ElementDocument* doc : docs)
	{
		if (doc == nullptr || !doc->IsVisible()) { continue; }
		Rml::ElementList traps;
		doc->QuerySelectorAll(traps, "[focus-trap]");
		for (Rml::Element* e : traps)
		{
			if (e->IsVisible(true)) { found = e; }
		}
	}
	return found;
}

/// 選んでいる要素を含む tabset (無ければ見えている文書の最初の tabset) の面を delta だけ回す。
/// フォーカスがその tabset の中にあれば、新しい面の見出し (tab) を選ぶ。回したら true。
/// within があれば (focus-trap で閉じ込めている要素)、探すのもその中だけ。ダイアログの裏の tabset は回さない
inline bool rotateTabs(Rml::Context& ctx, std::span<Rml::ElementDocument* const> docs, int delta, Rml::Element* within = nullptr)
{
	Rml::Element* focus = ctx.GetFocusElement();
	Rml::Element* set = detail::tabSetOf(focus);
	if (set != nullptr && within != nullptr && !detail::isInside(*within, set)) { set = nullptr; }
	if (set == nullptr && within != nullptr)
	{
		if (Rml::Element* e = within->QuerySelector("tabset"); e != nullptr && e->IsVisible(true)) { set = e; }
	}
	else if (set == nullptr)
	{
		for (Rml::ElementDocument* doc : docs)
		{
			if (doc == nullptr || !doc->IsVisible()) { continue; }
			if (Rml::Element* e = doc->QuerySelector("tabset"); e != nullptr && e->IsVisible(true)) { set = e; }
		}
	}
	if (set == nullptr) { return false; }
	auto* tabs = rmlui_dynamic_cast<Rml::ElementTabSet*>(set);
	if (tabs == nullptr) { return false; }
	const int n = tabs->GetNumTabs();
	if (n <= 0) { return false; }
	const int next = ((tabs->GetActiveTab() + delta) % n + n) % n;
	tabs->SetActiveTab(next);
	if (focus != nullptr && detail::isInside(*set, focus))
	{
		if (Rml::Element* heads = detail::childByTag(*set, "tabs"))
		{
			if (Rml::Element* head = heads->GetChild(next)) { head->Focus(); }
		}
	}
	return true;
}

class RmlFocusTrap
{
public:
	/// 入力を UI へ渡した後に毎回呼ぶ。trap が現れた・消えた・フォーカスが外へ出たのを見て直す
	void afterInput(Rml::Context& ctx, std::span<Rml::ElementDocument* const> docs)
	{
		Rml::Element* trap = findFocusTrap(docs);
		Rml::Element* focus = ctx.GetFocusElement();
		if (focus != nullptr && focus->GetOwnerDocument() == focus) { focus = nullptr; }
		if (trap == nullptr)
		{
			if (m_trap)
			{
				if (m_outside && m_outside->IsVisible(true)) { m_outside->Focus(); }
				m_trap.reset();
				m_outside.reset();
				m_inside.reset();
			}
			// trap の無い間に選んでいた要素。現れた時に autofocus が先に中を選んでいても、戻る先はこれ
			if (focus != nullptr) { m_lastFree = focus->GetObserverPtr(); }
			return;
		}
		const bool inside = detail::isInside(*trap, focus);
		if (trap != m_trap.get())
		{
			// 現れた (別の trap に替わった時は、最初の外の要素を覚えたままにする)
			if (!m_trap) { m_outside = (focus != nullptr && !inside) ? focus->GetObserverPtr() : m_lastFree; }
			m_trap = trap->GetObserverPtr();
			m_inside.reset();
			if (!inside) { focusFirst(*trap); }
			return;
		}
		if (inside)
		{
			m_inside = focus->GetObserverPtr();
			return;
		}
		if (m_inside && m_inside->IsVisible(true) && detail::isInside(*trap, m_inside.get())) { m_inside->Focus(); }
		else { focusFirst(*trap); }
	}

	[[nodiscard]] bool active() const noexcept { return static_cast<bool>(m_trap); }
	/// 今閉じ込めている要素。無ければ nullptr
	[[nodiscard]] Rml::Element* current() const noexcept { return m_trap.get(); }

private:
	void focusFirst(Rml::Element& trap)
	{
		Rml::Element* first = detail::firstFocusable(trap);
		if (first != nullptr && first->Focus()) { m_inside = first->GetObserverPtr(); }
	}

	Rml::ObserverPtr<Rml::Element> m_trap;      ///< 今閉じ込めている要素
	Rml::ObserverPtr<Rml::Element> m_outside;   ///< trap が現れる前に選んでいた要素 (消えたら戻す先)
	Rml::ObserverPtr<Rml::Element> m_inside;    ///< trap の中で最後に選んでいた要素 (外へ出たら戻す先)
	Rml::ObserverPtr<Rml::Element> m_lastFree;  ///< trap が無い間に最後に選んでいた要素
};

} // namespace mitiru::ui_rml
