#pragma once
// HTML 版の data-m-tween / data-m-flash / data-m-toast と drag / drop の見た目を、RmlUi の要素と
// イベントで受ける。値は C++ が送ったものを data-attr-* で受けるだけで、UI から書き戻す経路は無い。
// 時間は RmlUi の時計 (決定論の実行ではフレーム数から作る) で数えるので、replay で途中の絵まで同じになる。
//
//   <tween data-attr-value="score" format="comma" ms="300"/>  値が変わると ms かけて数え上げる
//   <flash data-attr-watch="score">…</flash>                   値が変わると class m-flash を ms (既定 400) の間付ける
//   <toast data-attr-value="toast" ms="4000"/>                 {kind, message} が変わると文を出し、
//                                                              kind-<kind> と is-visible を付け、ms 後に is-visible を外す

#include "RmlFormatters.hpp"
#include "RmlInclude.hpp"
#include "RmlJsonVariable.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace mitiru::ui_rml
{

namespace detail
{

[[nodiscard]] inline double uiNow()
{
	Rml::SystemInterface* sys = Rml::GetSystemInterface();
	return sys != nullptr ? sys->GetElapsedTime() : 0.0;
}

[[nodiscard]] inline double attributeSeconds(const Rml::Element& e, int defaultMs)
{
	return static_cast<double>(std::max(1, e.GetAttribute<int>("ms", defaultMs))) / 1000.0;
}

/// 文字をそのまま中身にする。前と同じなら触らない (中身を差し替えると組み直しが走る)。
inline void setInnerText(Rml::Element& e, Rml::String& shown, const Rml::String& text)
{
	if (text == shown) { return; }
	shown = text;
	e.SetInnerRML(Rml::StringUtilities::EncodeRml(text));
}

} // namespace detail

class TweenElement final : public Rml::Element
{
public:
	explicit TweenElement(const Rml::String& tag) : Rml::Element(tag) {}

protected:
	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		const auto it = changed.find("value");
		if (it == changed.end()) { return; }
		const Rml::String raw = it->second.Get<Rml::String>();
		char* end = nullptr;
		const double target = std::strtod(raw.c_str(), &end);
		if (raw.empty() || end == raw.c_str())
		{
			// 数でない値は数え上げずにそのまま出す (bind.js も同じ)
			m_hasNumber = false;
			m_animating = false;
			detail::setInnerText(*this, m_text, raw);
			return;
		}
		// 最初の値は数え上げない。途中で次の値が来たら、今見えている値から数え直す。
		m_from = m_hasNumber ? m_shown : target;
		m_target = target;
		m_hasNumber = true;
		m_start = detail::uiNow();
		m_animating = m_from != m_target;
		show(m_from);
	}

	void OnUpdate() override
	{
		if (!m_animating) { return; }
		const double t = std::min(1.0, (detail::uiNow() - m_start) / detail::attributeSeconds(*this, 300));
		show(m_from + (m_target - m_from) * t);
		m_animating = t < 1.0;
	}

private:
	void show(double v)
	{
		m_shown = v;
		detail::setInnerText(*this, m_text, formatNumber(v, GetAttribute<Rml::String>("format", "")));
	}

	Rml::String m_text;
	double m_from = 0.0;
	double m_target = 0.0;
	double m_shown = 0.0;
	double m_start = 0.0;
	bool m_hasNumber = false;
	bool m_animating = false;
};

class FlashElement final : public Rml::Element
{
public:
	static constexpr const char* kClass = "m-flash";

	explicit FlashElement(const Rml::String& tag) : Rml::Element(tag) {}

protected:
	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		const auto it = changed.find("watch");
		if (it == changed.end()) { return; }
		const Rml::String raw = it->second.Get<Rml::String>();
		// 最初に届いた値は覚えるだけ (bind.js と同じく、変わった瞬間だけ光らせる)
		const bool first = !m_seen;
		m_seen = true;
		if (first || raw == m_last) { m_last = raw; return; }
		m_last = raw;
		m_phase = Phase::Restart;
	}

	// 付いたままの class を付け直しても animation は頭から始まらない。1 フレーム外してから付け直す。
	void OnUpdate() override
	{
		switch (m_phase)
		{
		case Phase::Restart:
			if (IsClassSet(kClass)) { SetClass(kClass, false); m_phase = Phase::AddNext; }
			else                    { start(); }
			break;
		case Phase::AddNext:
			start();
			break;
		case Phase::Active:
			if (detail::uiNow() - m_start >= detail::attributeSeconds(*this, 400))
			{
				SetClass(kClass, false);
				m_phase = Phase::Idle;
			}
			break;
		case Phase::Idle:
			break;
		}
	}

private:
	enum class Phase : unsigned char { Idle, Restart, AddNext, Active };

	void start()
	{
		SetClass(kClass, true);
		m_start = detail::uiNow();
		m_phase = Phase::Active;
	}

	Rml::String m_last;
	double m_start = 0.0;
	Phase m_phase = Phase::Idle;
	bool m_seen = false;
};

class ToastElement final : public Rml::Element
{
public:
	explicit ToastElement(const Rml::String& tag) : Rml::Element(tag) {}

protected:
	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		const auto it = changed.find("value");
		if (it == changed.end()) { return; }
		const Rml::String raw = it->second.Get<Rml::String>();
		if (raw == m_last) { return; }
		m_last = raw;
		const nlohmann::json v = parseStateValue(raw);
		if (!v.is_object()) { return; }
		const std::string message = stringField(v, "message", "");
		if (message.empty()) { return; }
		detail::setInnerText(*this, m_text, message);
		if (!m_kindClass.empty()) { SetClass(m_kindClass, false); }
		m_kindClass = "kind-" + stringField(v, "kind", "info");
		SetClass(m_kindClass, true);
		SetClass("is-visible", true);
		m_until = detail::uiNow() + detail::attributeSeconds(*this, 4000);
		m_visible = true;
	}

	void OnUpdate() override
	{
		if (m_visible && detail::uiNow() >= m_until)
		{
			SetClass("is-visible", false);
			m_visible = false;
		}
	}

private:
	// game が文字列以外を送っても例外にしない (json::value は型が違うと投げる)
	static std::string stringField(const nlohmann::json& obj, const char* key, const char* fallback)
	{
		const auto it = obj.find(key);
		return (it != obj.end() && it->is_string()) ? it->get<std::string>() : std::string(fallback);
	}

	Rml::String m_last;
	Rml::String m_text;
	Rml::String m_kindClass;
	double m_until = 0.0;
	bool m_visible = false;
};

/// 掴んでいる要素に m-dragging、落とし先 (data-event-dragdrop を持つ要素) の上にいる間は m-drop-hover を付ける。
/// 文書に 1 つ付ける (RmlStateModel::loadDocument が付ける)。
class DragClassListener final : public Rml::EventListener
{
public:
	void attach(Rml::ElementDocument& doc)
	{
		for (const Rml::EventId id : { Rml::EventId::Dragstart, Rml::EventId::Dragend,
		                               Rml::EventId::Dragover, Rml::EventId::Dragout, Rml::EventId::Dragdrop })
		{
			doc.AddEventListener(id, this, true);
		}
	}

	void ProcessEvent(Rml::Event& ev) override
	{
		Rml::Element* target = ev.GetTargetElement();
		if (target == nullptr) { return; }
		switch (ev.GetId())
		{
		case Rml::EventId::Dragstart: target->SetClass("m-dragging", true); break;
		case Rml::EventId::Dragend:   target->SetClass("m-dragging", false); break;
		case Rml::EventId::Dragover:  setDropHover(*target, true); break;
		case Rml::EventId::Dragout:
		case Rml::EventId::Dragdrop:  setDropHover(*target, false); break;
		default: break;
		}
	}

private:
	static void setDropHover(Rml::Element& e, bool on)
	{
		if (e.HasAttribute("data-event-dragdrop")) { e.SetClass("m-drop-hover", on); }
	}
};

inline void registerBinderElements()
{
	static Rml::ElementInstancerGeneric<TweenElement> tween;
	static Rml::ElementInstancerGeneric<FlashElement> flash;
	static Rml::ElementInstancerGeneric<ToastElement> toast;
	Rml::Factory::RegisterElementInstancer("tween", &tween);
	Rml::Factory::RegisterElementInstancer("flash", &flash);
	Rml::Factory::RegisterElementInstancer("toast", &toast);
	static IncludeInstancer include;
	Rml::Factory::RegisterElementInstancer("include", &include);
}

} // namespace mitiru::ui_rml
