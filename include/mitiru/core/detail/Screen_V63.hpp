#pragma once
// mitiru::Screen の ABI v63 (ADR 0112) の入口。直接 include しない。core/Screen.hpp 経由。
//
// 組版 (縦書きとルビ) は host が setTypesetFont で繋いだ口を通る。DLL の inline が host の ScreenFont を直に触らない
// (型消去の関数ポインタで、setTrueTypeFont と同じ作り。ADR 0005)。口が無い画面 (8x8 ドットの書体) では横書きで描いて印を立て、
// host がフレームの頭に 1 度だけ知らせる。

#include <mitiru/module/detail/HostBoundary63.hpp>
#include <mitiru/render/IRenderer3D.hpp>

namespace mitiru
{

inline void Screen::drawTextRuby(const sgc::Rectf& rect, std::string_view text, const sgc::Colorf& color, float fontSize,
                                 float padX, float padY)
{
	if (m_typesetFunc != nullptr)
	{
		m_typesetFunc(m_typesetFont, *this, rect, text, color, fontSize, module::TextWriting::Horizontal, false, true, padX, padY);
		return;
	}
	m_pendingV62 |= module::detail::kPendingV63Ruby;
	drawTextInRect(rect, text, color, fontSize, TextAlignH::Left, TextAlignV::Top, padX, padY);
}

inline void Screen::lightLayers3D(std::uint32_t layers)
{
	m_lightLayers3D = layers;
	if (!has3D()) { return; }
	ensure3DFrame();
	m_renderer3D->setObjectLightLayers(layers);
}

} // namespace mitiru
