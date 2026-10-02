#pragma once
// <liveimage id="shot"/>: C++ が渡した RGBA の絵をそのまま貼る要素。scene_view がゲームの画面
// (/api/ai/frame の PNG) を映すのに使う。<img src> はファイル名で texture を覚えるので、毎回変わる
// 絵を貼ると古い texture が溜まる。こちらは 1 枚を差し替えるだけ。
// 大きさは絵の画素数を intrinsic にするので、RCSS で width: 100% にすれば縦横比を保って縮む。

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace mitiru::tool
{

class LiveImageElement final : public Rml::Element
{
public:
	explicit LiveImageElement(const Rml::String& tag) : Rml::Element(tag) {}

	static void registerInstancer()
	{
		static Rml::ElementInstancerGeneric<LiveImageElement> instancer;
		Rml::Factory::RegisterElementInstancer("liveimage", &instancer);
	}

	void setPixels(int width, int height, std::shared_ptr<const std::vector<std::uint8_t>> rgba)
	{
		if (width <= 0 || height <= 0 || !rgba || rgba->size() < static_cast<std::size_t>(width) * height * 4) { return; }
		const bool resized = (width != m_width || height != m_height);
		m_width = width;
		m_height = height;
		m_pixels = std::move(rgba);
		m_textureDirty = true;
		if (resized) { DirtyLayout(); }
	}

	[[nodiscard]] int imageWidth() const noexcept { return m_width; }
	[[nodiscard]] int imageHeight() const noexcept { return m_height; }

protected:
	bool GetIntrinsicDimensions(Rml::Vector2f& dimensions, float& ratio) override
	{
		if (m_width <= 0) { return false; }
		dimensions = { static_cast<float>(m_width), static_cast<float>(m_height) };
		ratio = dimensions.x / dimensions.y;
		return true;
	}

	void OnResize() override { m_geometryDirty = true; }

	void OnRender() override
	{
		if (!m_pixels) { return; }
		Rml::RenderManager* rm = GetRenderManager();
		if (rm == nullptr) { return; }
		if (m_textureDirty)
		{
			m_textureDirty = false;
			// RmlUi は texture を捨てて作り直すことがあるので、絵は値で持たせる (共有なので複製はしない)。
			m_texture = rm->MakeCallbackTexture(
				[pixels = m_pixels, w = m_width, h = m_height](const Rml::CallbackTextureInterface& ti) {
					return ti.GenerateTexture({ reinterpret_cast<const Rml::byte*>(pixels->data()), pixels->size() }, { w, h });
				});
		}
		if (m_geometryDirty) { rebuild(*rm); }
		m_geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border), m_texture);
	}

private:
	void rebuild(Rml::RenderManager& rm)
	{
		m_geometryDirty = false;
		Rml::Mesh mesh;
		const Rml::RenderBox box = GetRenderBox(Rml::BoxArea::Content);
		Rml::MeshUtilities::GenerateQuad(mesh, box.GetFillOffset(), box.GetFillSize(),
		                                 Rml::ColourbPremultiplied(255, 255, 255, 255), { 0.0f, 0.0f }, { 1.0f, 1.0f });
		m_geometry = rm.MakeGeometry(std::move(mesh));
	}

	std::shared_ptr<const std::vector<std::uint8_t>> m_pixels;
	int m_width = 0;
	int m_height = 0;
	Rml::CallbackTexture m_texture;
	Rml::Geometry m_geometry;
	bool m_textureDirty = false;
	bool m_geometryDirty = true;
};

} // namespace mitiru::tool
