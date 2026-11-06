#pragma once

/// @file RmlRubyElement.hpp
/// @brief <ruby>親<rt>るび</rt></ruby>: 親文字の上にルビを置き、行間に収まらない分だけ行を高くする要素。
// 箱の組み方は RCSS（base.rcss の ruby / rt）に任せる。ruby を inline-block、rt を親文字の前に置く block にすると、箱の幅は親文字とルビの長い方になり、行はルビが行間の余りに収まらない分だけ高くなり、親文字の基線は隣の字に揃う。
// この要素は、表示用の rt を ruby の先頭に作り、ADR 0111 の drawTextRuby と同じ規則で均等に割り付ける。
// 表示用の rt は 1 字ずつ span に分け、flex の space-around で余りを各字の前後へ半分ずつ配る。
// 利用者が書いた rt は動かさず、display: none で隠して文字の元にする。RemoveChild は {{ }} の data view を外すため使わない。
// {{ }} の値が変わると、OnUpdate が隠れた rt の文字を表示用の rt に写す。RmlPhraseElement と同じ作り。

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleTypes.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mitiru::ui_rml
{

namespace ruby_detail
{

[[nodiscard]] inline std::uint32_t decodeAt(std::string_view s, std::size_t at, std::size_t& length) noexcept
{
	const auto b0 = static_cast<unsigned char>(s[at]);
	length = b0 < 0x80 ? 1 : b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : 4;
	if (at + length > s.size()) { length = s.size() - at; }
	if (length == 1) { return b0; }
	std::uint32_t cp = b0 & (0xFFu >> (length + 1));
	for (std::size_t i = 1; i < length; ++i) { cp = (cp << 6) | (static_cast<unsigned char>(s[at + i]) & 0x3Fu); }
	return cp;
}

// 濁点と半濁点の結合文字、異体字選択子は、前の字と合わせて 1 つの割り付け単位にする
[[nodiscard]] constexpr bool joinsPrevious(std::uint32_t cp) noexcept
{
	return cp == 0x3099 || cp == 0x309A || cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

/// @brief ルビの文字を、均等割り付けする単位に分ける。ASCII を含む読み (英字の読みなど) は割らずに 1 単位にする。
[[nodiscard]] inline std::vector<std::string_view> splitReading(std::string_view reading)
{
	std::vector<std::string_view> units;
	if (reading.empty()) { return units; }
	std::vector<std::pair<std::size_t, std::size_t>> clusters;
	for (std::size_t at = 0; at < reading.size();)
	{
		std::size_t length = 0;
		const std::uint32_t cp = decodeAt(reading, at, length);
		if (cp < 0x80) { return { reading }; }
		if (joinsPrevious(cp) && !clusters.empty()) { clusters.back().second += length; }
		else { clusters.emplace_back(at, length); }
		at += length;
	}
	for (const auto& [begin, length] : clusters) { units.push_back(reading.substr(begin, length)); }
	return units;
}

} // namespace ruby_detail

class RubyElement final : public Rml::Element
{
public:
	static constexpr const char* kAnnotationClass = "m-ruby-annotation";
	static constexpr const char* kUnitClass = "m-ruby-unit";

	explicit RubyElement(const Rml::String& tag) : Rml::Element(tag) {}

	static void registerInstancer()
	{
		static Rml::ElementInstancerGeneric<RubyElement> instancer;
		Rml::Factory::RegisterElementInstancer("ruby", &instancer);
	}

	/// @brief 今写しているルビの文字。試験用。
	[[nodiscard]] const std::string& readingText() const noexcept { return m_reading; }

protected:
	void OnUpdate() override
	{
		Rml::Element::OnUpdate();
		Rml::Element* source = sourceAnnotation();
		if (source == nullptr)
		{
			if (m_annotation) { dropAnnotation(); }
			return;
		}
		std::string reading;
		for (int i = 0; i < source->GetNumChildren(); ++i)
		{
			if (const auto* text = rmlui_dynamic_cast<const Rml::ElementText*>(source->GetChild(i))) { reading += text->GetText(); }
		}
		if (!m_annotation || reading != m_reading) { rebuild(*source, std::move(reading)); }
	}

private:
	// ポインタは ObserverPtr で持つ。SetInnerRML などで子が入れ替わると、以前の rt と同じアドレスに別の要素が置かれることがある
	[[nodiscard]] bool isOwnAnnotation(const Rml::Element* e) const { return m_annotation.get() == e && e != nullptr; }

	// 利用者が書いた最初の rt。2 つ目以降は使わず、警告する。ルビ 1 組ごとに <ruby> を分けて書く
	[[nodiscard]] Rml::Element* sourceAnnotation()
	{
		Rml::Element* first = nullptr;
		for (int i = 0; i < GetNumChildren(); ++i)
		{
			Rml::Element* child = GetChild(i);
			if (child->GetTagName() != "rt" || isOwnAnnotation(child)) { continue; }
			if (first == nullptr) { first = child; continue; }
			child->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
			if (!m_warned)
			{
				Rml::Log::Message(Rml::Log::LT_WARNING, "<ruby> に <rt> が 2 つ以上あります。最初の 1 つだけを使います。ルビの組ごとに <ruby> を分けてください。");
				m_warned = true;
			}
		}
		return first;
	}

	void dropAnnotation()
	{
		for (int i = 0; i < GetNumChildren(); ++i)
		{
			if (isOwnAnnotation(GetChild(i))) { RemoveChild(GetChild(i)); break; }
		}
		m_annotation = {};
		m_reading.clear();
	}

	// 利用者の rt は動かさず、display: none で隠して文字の元にする。RemoveChild は {{ }} の data view を外すため使わない。
	// 表示用の rt は先頭に作り、OnUpdate が毎フレーム文字を写す
	void rebuild(Rml::Element& source, std::string reading)
	{
		source.SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
		dropAnnotation();
		m_reading = std::move(reading);
		Rml::ElementPtr annotation = Rml::Factory::InstanceElement(this, "rt", "rt", Rml::XMLAttributes());
		// 利用者が rt に書いた class と style は引き継ぐ。id は重複するため写さない
		annotation->SetClassNames(source.GetClassNames());
		if (source.HasAttribute("style")) { annotation->SetAttribute("style", source.GetAttribute<Rml::String>("style", "")); }
		annotation->SetClass(kAnnotationClass, true);
		for (const std::string_view unit : ruby_detail::splitReading(m_reading))
		{
			Rml::ElementPtr span = Rml::Factory::InstanceElement(annotation.get(), "span", "span", Rml::XMLAttributes());
			span->SetClass(kUnitClass, true);
			// Factory::InstanceElementText を通すと [key] の翻訳と {{ }} の解釈がもう一度行われるため、文字要素へ直接入れる
			Rml::ElementPtr text = Rml::Factory::InstanceElement(span.get(), "#text", "#text", Rml::XMLAttributes());
			if (auto* node = rmlui_dynamic_cast<Rml::ElementText*>(text.get())) { node->SetText(Rml::String(unit)); }
			span->AppendChild(std::move(text));
			annotation->AppendChild(std::move(span));
		}
		m_annotation = InsertBefore(std::move(annotation), GetChild(0))->GetObserverPtr();
	}

	std::string m_reading;
	Rml::ObserverPtr<Rml::Element> m_annotation;
	bool m_warned = false;
};

} // namespace mitiru::ui_rml
