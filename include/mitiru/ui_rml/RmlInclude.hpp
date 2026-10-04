#pragma once
// <include src="mitiru:talk.rml"/> の場所に、別ファイルの RML の断片をそのまま置く要素。
// 会話の窓のような定番の部品を、エンジン同梱の 1 ファイルから読めるようにする (見た目の RCSS も部品も
// 資産のファイルにあり、C++ は読み込むだけ)。src は文書の場所からの相対か、mitiru: (エンジンの UI 資産)。
// 断片の data-* 束縛は、要素が文書の data model に入ったときに RmlUi が付ける。

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/SystemInterface.h>

namespace mitiru::ui_rml
{

class IncludeInstancer final : public Rml::ElementInstancerGeneric<Rml::Element>
{
public:
	Rml::ElementPtr InstanceElement(Rml::Element* parent, const Rml::String& tag,
	                                const Rml::XMLAttributes& attributes) override
	{
		Rml::ElementPtr element = Rml::ElementInstancerGeneric<Rml::Element>::InstanceElement(parent, tag, attributes);
		const auto src = attributes.find("src");
		if (element == nullptr || src == attributes.end()) { return element; }
		Rml::String text;
		const Rml::String path = resolve(parent, src->second.Get<Rml::String>());
		if (Rml::GetFileInterface() == nullptr || !Rml::GetFileInterface()->LoadFile(path, text))
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "<include src=\"%s\">: 読めない (%s)",
			                  src->second.Get<Rml::String>().c_str(), path.c_str());
			return element;
		}
		element->SetInnerRML(text);
		return element;
	}

private:
	static Rml::String resolve(Rml::Element* parent, const Rml::String& src)
	{
		const Rml::ElementDocument* doc = parent != nullptr ? parent->GetOwnerDocument() : nullptr;
		Rml::String out = src;
		if (doc != nullptr && Rml::GetSystemInterface() != nullptr)
		{
			Rml::GetSystemInterface()->JoinPath(out, doc->GetSourceURL(), src);
		}
		return out;
	}
};

}  // namespace mitiru::ui_rml
