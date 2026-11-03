#pragma once

/// @file RmlPhraseElement.hpp
/// @brief <phrase>…</phrase>: 日本語の文を文節ごとの span に置き直し、RmlUi が文節の途中で折らないようにする要素。
// <phrase>吾輩は猫である。名前はまだ無い。</phrase> / <phrase>{{ talk_text }}</phrase>:
// 日本語の文を文節 (i18n/PhraseBreak.hpp、BudouX) で区切り、文節ごとの <span class="m-phrase-part"> に
// 置き直す。RmlUi は空白でしか行を折れず、空白の無い文は word-break: break-word で字の途中へ落ちる。
// U+200B を挟む案は、RmlUi が U+200B を折り目と見ず、書体に無い字として豆腐で描くので使えない
// (ElementText::GenerateLine と FontFaceHandleDefault::GetOrAppendGlyph)。
//
// span を display: inline のままにしても、エンジンの既定 (base.rcss の word-break: break-word) では何も変わらない:
// GenerateLine は break-word を「その要素の行がまだ空なら字の間で割る」と扱い、行の途中から始まる span の文字も
// 割ってしまう (撮影して確かめた)。span を inline-block にすると、1 つの箱として「入らなければ次の行」へ送られ、
// 枠より長い文節だけが箱の中で字の間に折れる。word-break: normal を span に付ける案は、長い文節が枠から
// はみ出すので使わない。
//
// 元の文 (文字の子) は消さずに display: none で隠す。{{ }} の data view は元の文字の要素を見ているので、
// 値が変わるたびに隠れた文が書き換わり、OnUpdate がそれを写し直す (data model の反映の後に呼ばれる)。
// 使わない RML には何も起きない (opt-in)。

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleTypes.h>

#include <mitiru/i18n/PhraseBreak.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::ui_rml
{

class PhraseElement final : public Rml::Element
{
public:
	static constexpr const char* kPartClass = "m-phrase-part";

	explicit PhraseElement(const Rml::String& tag) : Rml::Element(tag) {}

	static void registerInstancer()
	{
		static Rml::ElementInstancerGeneric<PhraseElement> instancer;
		Rml::Factory::RegisterElementInstancer("phrase", &instancer);
	}

	/// @brief 今写している文 (文節の span をつないだ物)。試験と読み上げ用。
	[[nodiscard]] const std::string& sourceText() const noexcept { return m_applied; }

protected:
	void OnUpdate() override
	{
		Rml::Element::OnUpdate();
		std::string source;
		for (int i = 0; i < GetNumChildren(); ++i)
		{
			if (const auto* text = rmlui_dynamic_cast<const Rml::ElementText*>(GetChild(i))) { source += text->GetText(); }
		}
		if (source != m_applied) { rebuild(std::move(source)); }
	}

private:
	void rebuild(std::string source)
	{
		m_applied = std::move(source);
		// 消すのは自分が作った span だけ (同じ class を利用者が付けた子は残す)
		for (Rml::Element* part : m_parts) { RemoveChild(part); }
		m_parts.clear();
		for (int i = 0; i < GetNumChildren(); ++i)
		{
			Rml::Element* child = GetChild(i);
			if (rmlui_dynamic_cast<Rml::ElementText*>(child) != nullptr)
			{
				child->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
			}
		}
		const i18n::PhraseModel& model = i18n::PhraseModel::japanese();
		model.forEachPhrase(m_applied, [&](std::string_view phrase) {
			Rml::ElementPtr part = Rml::Factory::InstanceElement(this, "span", "span", Rml::XMLAttributes());
			part->SetClass(kPartClass, true);
			part->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::InlineBlock));
			// Factory::InstanceElementText を通すと [key] の訳と {{ }} の解釈がもう一度走るので、文字の要素に直接入れる
			Rml::ElementPtr text = Rml::Factory::InstanceElement(part.get(), "#text", "#text", Rml::XMLAttributes());
			if (auto* node = rmlui_dynamic_cast<Rml::ElementText*>(text.get())) { node->SetText(Rml::String(phrase)); }
			part->AppendChild(std::move(text));
			m_parts.push_back(AppendChild(std::move(part)));
		});
	}

	std::string m_applied;
	std::vector<Rml::Element*> m_parts;
};

} // namespace mitiru::ui_rml
