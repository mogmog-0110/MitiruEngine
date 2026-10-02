#pragma once
// RmlUi の入力欄に IME の変換中の文字を見せる。変換中の文字は InputSnapshot::imeComposition (録画に乗る、
// game が受けるのと同じ値) から受け、選んでいる欄のキャレットの位置に下線つきで差し込む。
// 確定した文字は窓の WM_CHAR の列で届くので、キーを渡す前に差し込んだ文字を抜き、渡した後で入れ直す。
// 抜く・入れるの間に欄が出す change は入力ではないので、呼び出し側が操作として送らない。

#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/Types.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace mitiru::ui_rml
{

class RmlImeBridge final : public Rml::TextInputHandler
{
public:
	void OnActivate(Rml::TextInputContext* context) override { m_active = context; }

	void OnDeactivate(Rml::TextInputContext* context) override
	{
		if (m_active != context) { return; }
		removeComposition();
		m_active = nullptr;
	}

	void OnDestroy(Rml::TextInputContext* context) override
	{
		if (m_active != context) { return; }
		m_active = nullptr;
		m_length = 0;
		m_shown.clear();
	}

	/// 文字を受ける欄が選ばれているか。選ばれていれば、その欄の枠 (文脈の座標)。
	[[nodiscard]] bool focusedField(Rml::Rectanglef& bounds) const
	{
		return m_active != nullptr && m_active->GetBoundingBox(bounds);
	}

	[[nodiscard]] bool showing() const noexcept { return m_length > 0; }
	[[nodiscard]] bool sameAsShown(std::string_view composition) const noexcept { return composition == m_shown; }

	void removeComposition()
	{
		if (m_active == nullptr || m_length == 0) { m_length = 0; m_shown.clear(); return; }
		m_active->SetCompositionRange(0, 0);
		m_active->SetText("", m_start, m_start + m_length);
		m_active->SetCursorPosition(m_start);
		m_length = 0;
		m_shown.clear();
	}

	/// @param cursorBytes キャレットの位置 (composition の先頭からの byte 数)
	void showComposition(std::string_view composition, int cursorBytes)
	{
		if (m_active == nullptr || composition.empty()) { return; }
		int selStart = 0, selEnd = 0;
		m_active->GetSelectionRange(selStart, selEnd);
		m_active->SetText(toRml(composition), selStart, selEnd);
		m_start = selStart;
		m_length = characters(composition);
		m_shown.assign(composition);
		m_active->SetCompositionRange(m_start, m_start + m_length);
		const auto caretBytes = static_cast<std::size_t>(cursorBytes < 0 ? 0 : cursorBytes);
		m_active->SetCursorPosition(m_start + characters(composition.substr(0, std::min(caretBytes, composition.size()))));
	}

private:
	static Rml::StringView toRml(std::string_view s) { return Rml::StringView(s.data(), s.data() + s.size()); }

	static int characters(std::string_view utf8)
	{
		return static_cast<int>(Rml::StringUtilities::LengthUTF8(toRml(utf8)));
	}

	Rml::TextInputContext* m_active = nullptr;
	std::string m_shown;
	int m_start = 0;
	int m_length = 0;
};

} // namespace mitiru::ui_rml
