#pragma once
// title 属性を持つ要素の説明を、要素のそばに 1 つだけ出す。出すきっかけは最後に使った入力で決まる:
// マウスなら要素の上で止めたとき (ホバー)、キーやパッドなら要素にフォーカスが来たとき。
// 説明の箱 (class m-tooltip の div) は、出すときに要素の文書の末尾へ足す。title の無い文書には何も足さない。
//
// 箱の大きさは配置してみないと分からないので、2 回に分ける: まず左上に隠して置き、次の更新で測ってから
// 要素の下 (入らなければ上) に置いて見せる。時間は UI の時計で数えるので、replay でも同じ絵になる。

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace mitiru::ui_rml
{

/// 最後に使った入力を、説明を出すきっかけにする
enum class TipSource : unsigned char { Mouse, Focus };

class RmlTooltip
{
public:
	static constexpr const char* kBoxClass = "m-tooltip";
	static constexpr const char* kVisibleClass = "is-visible";
	static constexpr double kMouseDelaySec = 0.5;   ///< マウスが止まってから出るまで
	static constexpr double kFocusDelaySec = 0.3;   ///< 十字キーで次々に動かす間は出さず、止まってから出す
	static constexpr float kGapDp = 8.0f;           ///< 要素と箱の間
	static constexpr float kMarginDp = 8.0f;        ///< 画面の端との間

	/// 入力の種類が替わったら、同じ要素でも待ち時間を数え直す
	void setSource(TipSource source) noexcept
	{
		if (source == m_source) { return; }
		m_source = source;
		m_dismissed = false;
		m_owner.reset();
	}

	/// まだ説明が出ていなければ、マウスが止まってから待ち時間を数え直す。出ている説明は引っ込めない
	void mouseMoved() noexcept { m_restartTimer = true; }

	/// クリック、文字入力、戻る操作で引っ込める。要素が替わるまでは出さない
	void dismiss()
	{
		m_dismissed = true;
		hide();
	}

	void reset()
	{
		m_owner.reset();
		m_box.reset();
		m_phase = Phase::Idle;
		m_shown.clear();
		m_pending.clear();
		m_dismissed = false;
	}

	/// 説明を出していない間は空
	[[nodiscard]] const std::string& shownText() const noexcept { return m_shown; }

	/// 毎フレーム、Context::Update の前に呼ぶ。docs にはゲームの文書と重ねた文書を渡す
	void update(Rml::Context& ctx, std::span<Rml::ElementDocument* const> docs, double now)
	{
		Rml::Element* owner = tipOwner(subject(ctx), docs);
		if (owner != m_owner.get())
		{
			m_owner = owner != nullptr ? owner->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();
			m_since = now;
			m_dismissed = false;
			hide();
		}
		if (std::exchange(m_restartTimer, false) && m_phase == Phase::Idle && m_source == TipSource::Mouse) { m_since = now; }
		if (owner == nullptr || m_dismissed) { return; }

		const std::string text = translate(owner->GetAttribute<Rml::String>("title", ""));
		const double delay = m_source == TipSource::Mouse ? kMouseDelaySec : kFocusDelaySec;
		switch (m_phase)
		{
		case Phase::Idle:
			if (now - m_since >= delay) { place(*owner, text); }
			break;
		case Phase::Measuring:
			if (text != m_pending) { place(*owner, text); }
			else { reveal(ctx, *owner); }
			break;
		case Phase::Shown:
			if (text != m_shown) { place(*owner, text); }
			else { follow(ctx, *owner); }
			break;
		}
	}

private:
	enum class Phase : unsigned char { Idle, Measuring, Shown };

	[[nodiscard]] Rml::Element* subject(Rml::Context& ctx) const
	{
		Rml::Element* e = m_source == TipSource::Mouse ? ctx.GetHoverElement() : ctx.GetFocusElement();
		return (e != nullptr && e->GetOwnerDocument() == e) ? nullptr : e;
	}

	/// subject から根へ向かって最初に title を持つ要素を探す。docs の外の文書と説明の箱は対象にしない
	[[nodiscard]] static Rml::Element* tipOwner(Rml::Element* subject, std::span<Rml::ElementDocument* const> docs)
	{
		for (Rml::Element* e = subject; e != nullptr; e = e->GetParentNode())
		{
			if (e->IsClassSet(kBoxClass)) { return nullptr; }
			if (e->GetOwnerDocument() == e) { return nullptr; }
			if (e->GetAttribute<Rml::String>("title", "").empty()) { continue; }
			const bool allowed = std::find(docs.begin(), docs.end(), e->GetOwnerDocument()) != docs.end();
			return allowed && e->IsVisible(true) ? e : nullptr;
		}
		return nullptr;
	}

	[[nodiscard]] static std::string translate(const std::string& raw)
	{
		Rml::String out;
		Rml::SystemInterface* sys = Rml::GetSystemInterface();
		if (sys == nullptr) { return raw; }
		sys->TranslateString(out, raw);
		return out;
	}

	[[nodiscard]] Rml::Element* ensureBox(Rml::ElementDocument& doc)
	{
		Rml::Element* box = m_box.get();
		if (box != nullptr && box->GetOwnerDocument() == &doc) { return box; }
		if (box != nullptr && box->GetParentNode() != nullptr) { box->GetParentNode()->RemoveChild(box); }
		Rml::ElementPtr made = doc.CreateElement("div");
		made->SetClassNames(kBoxClass);
		made->SetAttribute("aria-hidden", "true");   // 読み上げには、要素の title が名前として届く
		box = doc.AppendChild(std::move(made));
		m_box = box->GetObserverPtr();
		return box;
	}

	void hide()
	{
		if (Rml::Element* box = m_box.get()) { box->SetClass(kVisibleClass, false); }
		m_phase = Phase::Idle;
		m_shown.clear();
		m_pending.clear();
	}

	// 左上に隠して文字を入れ、次の更新で大きさを測る
	void place(Rml::Element& owner, const std::string& text)
	{
		Rml::ElementDocument* doc = owner.GetOwnerDocument();
		if (doc == nullptr || text.empty()) { return; }
		Rml::Element* box = ensureBox(*doc);
		box->SetClass(kVisibleClass, false);
		box->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		box->SetProperty("left", "0px");
		box->SetProperty("top", "0px");
		m_left = 0.0f;
		m_top = 0.0f;
		m_pending = text;
		m_shown.clear();
		m_phase = Phase::Measuring;
	}

	void reveal(Rml::Context& ctx, Rml::Element& owner)
	{
		Rml::Element* box = m_box.get();
		if (box == nullptr || box->GetBox().GetSize(Rml::BoxArea::Border).x <= 0.0f) { return; }
		if (!follow(ctx, owner, /*force=*/true)) { return; }
		box->SetClass(kVisibleClass, true);
		m_shown = m_pending;
		m_phase = Phase::Shown;
	}

	// 要素の下中央に置き、入らなければ上に置く。横は画面内に収める。箱の左上の原点は、現在位置との差から測る
	bool follow(Rml::Context& ctx, Rml::Element& owner, bool force = false)
	{
		Rml::Element* box = m_box.get();
		if (box == nullptr) { return false; }
		const float dp = ctx.GetDensityIndependentPixelRatio();
		const Rml::Vector2f screen(ctx.GetDimensions());
		const Rml::Vector2f boxSize = box->GetBox().GetSize(Rml::BoxArea::Border);
		const Rml::Vector2f at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f origin(at.x - m_left, at.y - m_top);
		const Rml::Vector2f ownerAt = owner.GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f ownerSize = owner.GetBox().GetSize(Rml::BoxArea::Border);

		const float margin = kMarginDp * dp;
		const float gap = kGapDp * dp;
		float x = ownerAt.x + (ownerSize.x - boxSize.x) * 0.5f;
		float y = ownerAt.y + ownerSize.y + gap;
		if (y + boxSize.y > screen.y - margin) { y = ownerAt.y - gap - boxSize.y; }
		x = std::max(margin, std::min(x, screen.x - boxSize.x - margin));
		y = std::max(margin, y);

		const float left = std::floor(x - origin.x);
		const float top = std::floor(y - origin.y);
		if (force || left != m_left || top != m_top)
		{
			box->SetProperty("left", std::to_string(static_cast<int>(left)) + "px");
			box->SetProperty("top", std::to_string(static_cast<int>(top)) + "px");
			m_left = left;
			m_top = top;
		}
		return true;
	}

	TipSource m_source = TipSource::Mouse;
	Phase m_phase = Phase::Idle;
	Rml::ObserverPtr<Rml::Element> m_owner;
	Rml::ObserverPtr<Rml::Element> m_box;
	double m_since = 0.0;
	float m_left = 0.0f;   ///< 箱に今書いてある left / top (px)
	float m_top = 0.0f;
	bool m_dismissed = false;
	bool m_restartTimer = false;
	std::string m_pending;   ///< 箱に入れた文。測り終わるまで見せない
	std::string m_shown;
};

} // namespace mitiru::ui_rml
