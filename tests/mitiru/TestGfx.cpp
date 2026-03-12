#include <catch2/catch_test_macros.hpp>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/ITexture.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/IShader.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/null/NullDevice.hpp>
#include <mitiru/gfx/GfxFactory.hpp>

// ============================================================
// GfxTypes
// ============================================================

TEST_CASE("GfxTypes - VertexFormat enum values exist", "[gfx]")
{
	using mitiru::gfx::VertexFormat;

	REQUIRE(VertexFormat::Position2D == VertexFormat::Position2D);
	REQUIRE(VertexFormat::Position3D != VertexFormat::Position2D);
	REQUIRE(VertexFormat::PositionColor != VertexFormat::Position2D);
	REQUIRE(VertexFormat::PositionTexCoord != VertexFormat::Position2D);
}

TEST_CASE("GfxTypes - PixelFormat enum values exist", "[gfx]")
{
	using mitiru::gfx::PixelFormat;

	REQUIRE(PixelFormat::RGBA8 == PixelFormat::RGBA8);
	REQUIRE(PixelFormat::BGRA8 != PixelFormat::RGBA8);
	REQUIRE(PixelFormat::R8 != PixelFormat::RGBA8);
	REQUIRE(PixelFormat::Depth24Stencil8 != PixelFormat::RGBA8);
}

TEST_CASE("GfxTypes - BlendMode enum values exist", "[gfx]")
{
	using mitiru::gfx::BlendMode;

	REQUIRE(BlendMode::None == BlendMode::None);
	REQUIRE(BlendMode::Alpha != BlendMode::None);
	REQUIRE(BlendMode::Additive != BlendMode::None);
	REQUIRE(BlendMode::Multiplicative != BlendMode::None);
}

TEST_CASE("GfxTypes - Backend enum values exist", "[gfx]")
{
	using mitiru::gfx::Backend;

	REQUIRE(Backend::Auto == Backend::Auto);
	REQUIRE(Backend::Dx11 != Backend::Auto);
	REQUIRE(Backend::Dx12 != Backend::Auto);
	REQUIRE(Backend::Vulkan != Backend::Auto);
	REQUIRE(Backend::OpenGL != Backend::Auto);
	REQUIRE(Backend::Null != Backend::Auto);
}

TEST_CASE("GfxTypes - TextureDesc default values", "[gfx]")
{
	const mitiru::gfx::TextureDesc desc;

	REQUIRE(desc.width == 0);
	REQUIRE(desc.height == 0);
	REQUIRE(desc.format == mitiru::gfx::PixelFormat::RGBA8);
	REQUIRE(desc.renderTarget == false);
}

TEST_CASE("GfxTypes - TextureDesc custom values", "[gfx]")
{
	mitiru::gfx::TextureDesc desc;
	desc.width = 512;
	desc.height = 256;
	desc.format = mitiru::gfx::PixelFormat::BGRA8;
	desc.renderTarget = true;

	REQUIRE(desc.width == 512);
	REQUIRE(desc.height == 256);
	REQUIRE(desc.format == mitiru::gfx::PixelFormat::BGRA8);
	REQUIRE(desc.renderTarget == true);
}

TEST_CASE("GfxTypes - BufferDesc default values", "[gfx]")
{
	const mitiru::gfx::BufferDesc desc;

	REQUIRE(desc.usage == mitiru::gfx::BufferDesc::Usage::Vertex);
	REQUIRE(desc.sizeBytes == 0);
	REQUIRE(desc.dynamic == false);
}

TEST_CASE("GfxTypes - BufferDesc custom values", "[gfx]")
{
	mitiru::gfx::BufferDesc desc;
	desc.usage = mitiru::gfx::BufferDesc::Usage::Index;
	desc.sizeBytes = 4096;
	desc.dynamic = true;

	REQUIRE(desc.usage == mitiru::gfx::BufferDesc::Usage::Index);
	REQUIRE(desc.sizeBytes == 4096);
	REQUIRE(desc.dynamic == true);
}

// ============================================================
// NullDevice
// ============================================================

TEST_CASE("NullDevice - backend returns Null", "[gfx]")
{
	const mitiru::gfx::NullDevice device;
	REQUIRE(device.backend() == mitiru::gfx::Backend::Null);
}

TEST_CASE("NullDevice - readPixels returns correct size", "[gfx]")
{
	const mitiru::gfx::NullDevice device;
	const auto pixels = device.readPixels(4, 3);

	/// 4 * 3 * 4 (RGBA) = 48 bytes
	REQUIRE(pixels.size() == 48);
}

TEST_CASE("NullDevice - readPixels returns black pixels with full alpha", "[gfx]")
{
	const mitiru::gfx::NullDevice device;
	const auto pixels = device.readPixels(2, 2);

	/// 4 pixels, each RGBA = (0,0,0,255)
	REQUIRE(pixels.size() == 16);
	for (std::size_t i = 0; i < 4; ++i)
	{
		REQUIRE(pixels[i * 4 + 0] == 0);   /// R
		REQUIRE(pixels[i * 4 + 1] == 0);   /// G
		REQUIRE(pixels[i * 4 + 2] == 0);   /// B
		REQUIRE(pixels[i * 4 + 3] == 255); /// A
	}
}

TEST_CASE("NullDevice - readPixels with zero dimensions returns empty", "[gfx]")
{
	const mitiru::gfx::NullDevice device;
	const auto pixels = device.readPixels(0, 0);

	REQUIRE(pixels.empty());
}

TEST_CASE("NullDevice - readPixels with large dimensions", "[gfx]")
{
	const mitiru::gfx::NullDevice device;
	const auto pixels = device.readPixels(100, 100);

	REQUIRE(pixels.size() == 100u * 100u * 4u);
}

TEST_CASE("NullDevice - beginFrame and endFrame do not crash", "[gfx]")
{
	mitiru::gfx::NullDevice device;

	REQUIRE_NOTHROW(device.beginFrame());
	REQUIRE_NOTHROW(device.endFrame());
}

TEST_CASE("NullDevice - multiple frame cycles", "[gfx]")
{
	mitiru::gfx::NullDevice device;

	for (int i = 0; i < 10; ++i)
	{
		REQUIRE_NOTHROW(device.beginFrame());
		REQUIRE_NOTHROW(device.endFrame());
	}
}

// ============================================================
// GfxFactory
// ============================================================

TEST_CASE("GfxFactory - createDevice with Null backend returns valid device", "[gfx]")
{
	auto device = mitiru::gfx::createDevice(mitiru::gfx::Backend::Null);

	REQUIRE(device != nullptr);
	REQUIRE(device->backend() == mitiru::gfx::Backend::Null);
}

TEST_CASE("GfxFactory - createDevice with Auto backend returns valid device", "[gfx]")
{
	auto device = mitiru::gfx::createDevice(mitiru::gfx::Backend::Auto);

	REQUIRE(device != nullptr);
	/// Auto currently falls back to Null
	REQUIRE(device->backend() == mitiru::gfx::Backend::Null);
}

TEST_CASE("GfxFactory - createDevice with Dx11 backend throws", "[gfx]")
{
	REQUIRE_THROWS_AS(
		mitiru::gfx::createDevice(mitiru::gfx::Backend::Dx11),
		std::runtime_error);
}

TEST_CASE("GfxFactory - createDevice with Dx12 backend throws", "[gfx]")
{
	REQUIRE_THROWS_AS(
		mitiru::gfx::createDevice(mitiru::gfx::Backend::Dx12),
		std::runtime_error);
}

TEST_CASE("GfxFactory - createDevice with Vulkan backend throws", "[gfx]")
{
	REQUIRE_THROWS_AS(
		mitiru::gfx::createDevice(mitiru::gfx::Backend::Vulkan),
		std::runtime_error);
}

TEST_CASE("GfxFactory - createDevice with OpenGL backend throws", "[gfx]")
{
	REQUIRE_THROWS_AS(
		mitiru::gfx::createDevice(mitiru::gfx::Backend::OpenGL),
		std::runtime_error);
}

TEST_CASE("GfxFactory - created NullDevice can do full frame cycle", "[gfx]")
{
	auto device = mitiru::gfx::createDevice(mitiru::gfx::Backend::Null);
	REQUIRE(device != nullptr);

	REQUIRE_NOTHROW(device->beginFrame());
	const auto pixels = device->readPixels(8, 8);
	REQUIRE(pixels.size() == 8u * 8u * 4u);
	REQUIRE_NOTHROW(device->endFrame());
}

// ============================================================
// GfxTypes - BufferType and ShaderType enums
// ============================================================

TEST_CASE("GfxTypes - BufferType enum values exist", "[gfx]")
{
	using mitiru::gfx::BufferType;

	REQUIRE(BufferType::Vertex == BufferType::Vertex);
	REQUIRE(BufferType::Index != BufferType::Vertex);
	REQUIRE(BufferType::Constant != BufferType::Vertex);
}

TEST_CASE("GfxTypes - ShaderType enum values exist", "[gfx]")
{
	using mitiru::gfx::ShaderType;

	REQUIRE(ShaderType::Vertex == ShaderType::Vertex);
	REQUIRE(ShaderType::Pixel != ShaderType::Vertex);
	REQUIRE(ShaderType::Compute != ShaderType::Vertex);
}
