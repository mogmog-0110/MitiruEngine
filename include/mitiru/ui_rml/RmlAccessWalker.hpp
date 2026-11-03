#pragma once

/// @file RmlAccessWalker.hpp
/// @brief RmlUi の文書をたどって要素の事実 (ElementFacts) を集め、UiAccessTree を作る。読み上げソフトからの操作を要素へ届ける。
/// @details RmlUiHost.cpp だけが include する (RmlUi のヘッダーを Engine.hpp 側へ出さないため)。role と name の規則は
///          UiAccessTree.hpp の純関数にあり、ここは RmlUi の型から事実を読む仕事だけを持つ。
///          節の番号は要素ごとに ObserverPtr で寿命を見ながら払う。アドレスが別の要素に再利用されても、同じ番号は渡さない。

#include <mitiru/ui_rml/UiAccessTree.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mitiru::ui_rml
{

class RmlAccessWalker
{
public:
	struct Doc
	{
		Rml::ElementDocument* doc = nullptr;
		bool modal = false;   ///< 開いている間は操作がその文書へ行く
	};

	/// @brief 見えている文書から木を作る。見えている modal の文書があれば、その下の文書は含めない (操作が届かないため)。
	[[nodiscard]] UiAccessTree build(Rml::Context& context, const std::vector<Doc>& docs, const std::string& windowName)
	{
		++m_generation;
		m_byId.clear();
		Rml::Element* focus = context.GetFocusElement();
		const bool modalOpen = std::any_of(docs.begin(), docs.end(), [](const Doc& d) { return d.modal && visible(d.doc); });
		std::vector<FactsNode> roots;
		for (const Doc& d : docs)
		{
			if (!visible(d.doc) || (modalOpen && !d.modal)) { continue; }
			roots.push_back(collect(*d.doc, focus));
		}
		sweep();
		return buildAccessTree(windowName, roots);
	}

	/// @brief 読み上げソフトからの操作を要素に届ける。UI の状態は書き換えず、利用者が触ったときと同じ event を起こす。
	/// @return 届いたら true。要素が消えていた・無効だった・その操作を持たない要素なら false
	bool perform(const AccessRequest& request)
	{
		const auto it = m_byId.find(request.target);
		Rml::Element* e = it != m_byId.end() ? it->second.get() : nullptr;
		// 木を作った後に隠れた要素には届けない
		if (e == nullptr || !e->IsVisible(true)) { return false; }
		auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(e);
		if (control != nullptr && control->IsDisabled()) { return false; }
		switch (request.kind)
		{
		case AccessRequest::Kind::Focus: return e->Focus();
		case AccessRequest::Kind::Click: e->Click(); return true;
		case AccessRequest::Kind::SetValue:
			if (control == nullptr) { return false; }
			return setValue(*control, request.hasNumber ? formatNumber(request.number) : request.text);
		case AccessRequest::Kind::Increment:
		case AccessRequest::Kind::Decrement:
		{
			if (control == nullptr) { return false; }
			const double step = numberAttribute(*e, "step", 1.0);
			const double lo = numberAttribute(*e, "min", 0.0);
			const double hi = numberAttribute(*e, "max", 100.0);
			const double cur = std::strtod(control->GetValue().c_str(), nullptr);
			const double next = std::clamp(cur + (request.kind == AccessRequest::Kind::Increment ? step : -step), lo, hi);
			return setValue(*control, formatNumber(next));
		}
		}
		return false;
	}

private:
	struct Entry
	{
		Rml::ObserverPtr<Rml::Element> observer;
		std::uint64_t id = 0;
		std::uint64_t seen = 0;
	};

	std::uint64_t m_generation = 0;
	std::uint64_t m_nextId = kAccessRootId + 1;
	std::unordered_map<const Rml::Element*, Entry> m_ids;
	/// (親の番号, 位置) -> (番号, 最後に見た世代)
	std::map<std::pair<std::uint64_t, int>, std::pair<std::uint64_t, std::uint64_t>> m_runIds;
	std::unordered_map<std::uint64_t, Rml::ObserverPtr<Rml::Element>> m_byId;

	[[nodiscard]] static bool visible(const Rml::ElementDocument* d) { return d != nullptr && d->IsVisible(); }

	[[nodiscard]] static std::string squeeze(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());
		bool pending = false;
		for (const char c : in)
		{
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { pending = !out.empty(); continue; }
			if (pending) { out.push_back(' '); pending = false; }
			out.push_back(c);
		}
		return out;
	}

	[[nodiscard]] static std::string formatNumber(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%g", v);
		return buf;
	}

	[[nodiscard]] static double numberAttribute(const Rml::Element& e, const char* name, double fallback)
	{
		if (!e.HasAttribute(name)) { return fallback; }
		return static_cast<double>(e.GetAttribute<float>(name, static_cast<float>(fallback)));
	}

	[[nodiscard]] static bool setValue(Rml::ElementFormControl& control, const std::string& value)
	{
		// SetValue は属性を書くだけで change の event は起こさない。利用者が編集したときと同じ event を起こして、
		// data-event-change で書いた経路 (UI → C++ の通知) を通す。
		control.SetValue(value);
		Rml::Dictionary parameters;
		parameters["value"] = Rml::Variant(value);
		control.DispatchEvent("change", parameters);
		return true;
	}

	std::uint64_t idOf(Rml::Element& e)
	{
		auto it = m_ids.find(&e);
		if (it != m_ids.end() && it->second.observer.get() == &e)
		{
			it->second.seen = m_generation;
			m_byId[it->second.id] = it->second.observer;
			return it->second.id;
		}
		Entry fresh{ e.GetObserverPtr(), m_nextId++, m_generation };
		const std::uint64_t id = fresh.id;
		m_byId[id] = fresh.observer;
		m_ids.insert_or_assign(&e, std::move(fresh));
		return id;
	}

	// 親の番号 (ObserverPtr で寿命を見た番号) で引く。親のアドレスが再利用されても、別の親の文字に番号を渡さない
	std::uint64_t runIdOf(std::uint64_t parentId, int index)
	{
		auto& slot = m_runIds[{ parentId, index }];
		if (slot.first == 0) { slot.first = m_nextId++; }
		slot.second = m_generation;
		return slot.first;
	}

	void sweep()
	{
		for (auto it = m_ids.begin(); it != m_ids.end();)
		{
			it = it->second.seen == m_generation ? std::next(it) : m_ids.erase(it);
		}
		for (auto it = m_runIds.begin(); it != m_runIds.end();)
		{
			it = it->second.second == m_generation ? std::next(it) : m_runIds.erase(it);
		}
	}

	[[nodiscard]] static std::string attr(const Rml::Element& e, const char* name)
	{
		return e.GetAttribute<Rml::String>(name, "");
	}

	[[nodiscard]] FactsNode collect(Rml::Element& e, Rml::Element* focus)
	{
		FactsNode node;
		if (!e.IsVisible())
		{
			// 隠れた要素は子までたどらない (番号だけ払い、木には入らない)
			node.facts.tag = e.GetTagName();
			node.facts.id = idOf(e);
			node.facts.visible = false;
			return node;
		}
		node.facts = factsOf(e, focus);
		for (int i = 0; i < e.GetNumChildren(); ++i)
		{
			Rml::Element* child = e.GetChild(i);
			if (child == nullptr) { continue; }
			if (child->GetTagName() == "#text")
			{
				FactsNode run;
				run.facts.tag = "#text";
				run.facts.visible = child->IsVisible();
				run.facts.text = squeeze(static_cast<Rml::ElementText*>(child)->GetText());
				run.facts.id = runIdOf(node.facts.id, i);
				node.children.push_back(std::move(run));
			}
			else { node.children.push_back(collect(*child, focus)); }
		}
		return node;
	}

	[[nodiscard]] ElementFacts factsOf(Rml::Element& e, Rml::Element* focus)
	{
		ElementFacts f;
		f.id = idOf(e);
		f.tag = e.GetTagName();
		f.type = attr(e, "type");
		f.role = attr(e, "role");
		f.ariaLabel = attr(e, "aria-label");
		f.title = attr(e, "title");
		f.alt = attr(e, "alt");
		f.placeholder = attr(e, "placeholder");
		f.a11y = attr(e, "data-a11y");
		f.ariaHidden = attr(e, "aria-hidden");
		f.visible = e.IsVisible();
		f.hasClick = e.HasAttribute("data-event-click");
		f.tabbable = e.GetComputedValues().tab_index() != Rml::Style::TabIndex::None;
		f.focused = &e == focus;
		f.focusTrap = e.HasAttribute("focus-trap");
		f.checked = e.HasAttribute("checked");
		f.selected = e.IsPseudoClassSet("selected");

		Rml::Vector2f offset = e.GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f size = e.GetBox().GetSize(Rml::BoxArea::Border);
		f.bounds = { offset.x, offset.y, size.x, size.y };

		if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(&e))
		{
			f.disabled = control->IsDisabled();
			f.value = control->GetValue();
			f.readOnly = e.HasAttribute("readonly");
		}
		if (f.tag == "input" && f.type == "range")
		{
			f.hasRange = true;
			f.rangeMin = numberAttribute(e, "min", 0.0);
			f.rangeMax = numberAttribute(e, "max", 100.0);
			f.rangeStep = numberAttribute(e, "step", 1.0);
			f.rangeValue = std::strtod(f.value.c_str(), nullptr);
		}
		else if (f.tag == "progress")
		{
			f.hasRange = true;
			f.rangeMin = 0.0;
			f.rangeMax = numberAttribute(e, "max", 1.0);
			f.rangeValue = numberAttribute(e, "value", 0.0);
		}
		return f;
	}
};

}  // namespace mitiru::ui_rml
