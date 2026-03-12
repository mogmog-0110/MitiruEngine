#include <catch2/catch_test_macros.hpp>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/ISwapChain.hpp>
#include <mitiru/gfx/IGpuFence.hpp>
#include <mitiru/gfx/IDescriptorHeap.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IShader.hpp>

// ============================================================
// Dx12 FRAME_COUNT constant
// ============================================================

/// @brief DX12で使用するトリプルバッファリングのフレーム数定数
static constexpr uint32_t DX12_FRAME_COUNT = 3;

TEST_CASE("Dx12Config - FRAME_COUNT is 3 for triple buffering", "[dx12]")
{
	REQUIRE(DX12_FRAME_COUNT == 3);
	REQUIRE(DX12_FRAME_COUNT >= 2);
}

// ============================================================
// Backend enum includes Dx12
// ============================================================

TEST_CASE("Dx12Config - Backend::Dx12 enum exists", "[dx12]")
{
	const auto backend = mitiru::gfx::Backend::Dx12;
	REQUIRE(backend == mitiru::gfx::Backend::Dx12);
	REQUIRE(backend != mitiru::gfx::Backend::Dx11);
	REQUIRE(backend != mitiru::gfx::Backend::Null);
	REQUIRE(backend != mitiru::gfx::Backend::Vulkan);
	REQUIRE(backend != mitiru::gfx::Backend::Auto);
}

// ============================================================
// ResourceState enum for D3D12 barriers
// ============================================================

TEST_CASE("Dx12Config - ResourceState enum values", "[dx12]")
{
	using mitiru::gfx::ResourceState;

	/// バリア遷移に必要な全状態が定義されていることを確認する
	REQUIRE(ResourceState::Common != ResourceState::RenderTarget);
	REQUIRE(ResourceState::RenderTarget != ResourceState::Present);
	REQUIRE(ResourceState::Present != ResourceState::ShaderResource);
	REQUIRE(ResourceState::ShaderResource != ResourceState::DepthWrite);
	REQUIRE(ResourceState::DepthWrite != ResourceState::CopyDest);
	REQUIRE(ResourceState::CopyDest != ResourceState::CopySrc);
	REQUIRE(ResourceState::CopySrc != ResourceState::UnorderedAccess);
}

TEST_CASE("Dx12Config - ResourceState::Common is 0", "[dx12]")
{
	REQUIRE(static_cast<uint32_t>(mitiru::gfx::ResourceState::Common) == 0);
}

// ============================================================
// DescriptorHeapType for D3D12
// ============================================================

TEST_CASE("Dx12Config - DescriptorHeapType enum values", "[dx12]")
{
	using mitiru::gfx::DescriptorHeapType;

	REQUIRE(DescriptorHeapType::CbvSrvUav != DescriptorHeapType::Rtv);
	REQUIRE(DescriptorHeapType::Rtv != DescriptorHeapType::Dsv);
	REQUIRE(DescriptorHeapType::Dsv != DescriptorHeapType::Sampler);
}

// ============================================================
// GpuDescriptorHandle / CpuDescriptorHandle
// ============================================================

TEST_CASE("Dx12Config - GpuDescriptorHandle default is invalid", "[dx12]")
{
	const mitiru::gfx::GpuDescriptorHandle handle;

	REQUIRE(handle.ptr == 0);
	REQUIRE(handle.isValid() == false);
}

TEST_CASE("Dx12Config - GpuDescriptorHandle with value is valid", "[dx12]")
{
	mitiru::gfx::GpuDescriptorHandle handle;
	handle.ptr = 0x12345678;

	REQUIRE(handle.isValid() == true);
}

TEST_CASE("Dx12Config - CpuDescriptorHandle default is invalid", "[dx12]")
{
	const mitiru::gfx::CpuDescriptorHandle handle;

	REQUIRE(handle.ptr == 0);
	REQUIRE(handle.isValid() == false);
}

TEST_CASE("Dx12Config - CpuDescriptorHandle with value is valid", "[dx12]")
{
	mitiru::gfx::CpuDescriptorHandle handle;
	handle.ptr = 0xABCD0000;

	REQUIRE(handle.isValid() == true);
}

// ============================================================
// FenceValue type
// ============================================================

TEST_CASE("Dx12Config - FenceValue is uint64_t", "[dx12]")
{
	mitiru::gfx::FenceValue val = 0;
	REQUIRE(val == 0);

	val = UINT64_MAX;
	REQUIRE(val == UINT64_MAX);

	/// フェンス値はインクリメント可能であること
	val = 42;
	++val;
	REQUIRE(val == 43);
}

// ============================================================
// IDevice D3D12 virtual methods (default implementations)
// ============================================================

TEST_CASE("Dx12Config - IDevice default currentFrameIndex is 0", "[dx12]")
{
	/// NullDeviceを使ってIDeviceのデフォルト実装を検証する
	class TestDevice : public mitiru::gfx::IDevice
	{
	public:
		[[nodiscard]] std::vector<std::uint8_t> readPixels(int, int) const override { return {}; }
		[[nodiscard]] mitiru::gfx::Backend backend() const noexcept override { return mitiru::gfx::Backend::Null; }
		void beginFrame() override {}
		void endFrame() override {}
		[[nodiscard]] std::unique_ptr<mitiru::gfx::IBuffer> createBuffer(
			mitiru::gfx::BufferType, std::uint32_t, bool, const void*) override { return nullptr; }
		[[nodiscard]] std::unique_ptr<mitiru::gfx::ICommandList> createCommandList() override { return nullptr; }
	};

	TestDevice device;
	REQUIRE(device.currentFrameIndex() == 0);
	REQUIRE(device.frameInFlightCount() == 1);
	REQUIRE(device.createFence() == nullptr);
	REQUIRE(device.createDescriptorHeap(
		mitiru::gfx::DescriptorHeapType::CbvSrvUav, 256) == nullptr);
}

// ============================================================
// ISwapChain D3D12 virtual methods
// ============================================================

TEST_CASE("Dx12Config - ISwapChain default currentBackBufferIndex is 0", "[dx12]")
{
	class TestSwapChain : public mitiru::gfx::ISwapChain
	{
	public:
		void present() override {}
		void resize(int, int) override {}
		[[nodiscard]] mitiru::gfx::IRenderTarget* backBuffer() noexcept override { return nullptr; }
	};

	TestSwapChain swapChain;
	REQUIRE(swapChain.currentBackBufferIndex() == 0);
	REQUIRE(swapChain.bufferCount() == 2);
}

// ============================================================
// ICommandList D3D12 virtual methods
// ============================================================

TEST_CASE("Dx12Config - ICommandList D3D12 methods have defaults", "[dx12]")
{
	class TestCmdList : public mitiru::gfx::ICommandList
	{
	public:
		void begin() override {}
		void end() override {}
		void setRenderTarget(mitiru::gfx::IRenderTarget*) override {}
		void clearRenderTarget(const sgc::Colorf&) override {}
		void setPipeline(mitiru::gfx::IPipeline*) override {}
		void setVertexBuffer(mitiru::gfx::IBuffer*) override {}
		void setIndexBuffer(mitiru::gfx::IBuffer*) override {}
		void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
		void draw(std::uint32_t, std::uint32_t) override {}
	};

	TestCmdList cmdList;

	/// D3D12固有メソッドのデフォルト実装が例外なしで呼び出せること
	cmdList.reset();
	cmdList.close();
	cmdList.setViewport(800.0f, 600.0f);
	cmdList.resourceBarrier(nullptr,
		mitiru::gfx::ResourceState::Common,
		mitiru::gfx::ResourceState::RenderTarget);
	cmdList.setRootSignature(nullptr);
	cmdList.setDescriptorHeaps(nullptr, 0);
	cmdList.setGraphicsRootDescriptorTable(0,
		mitiru::gfx::GpuDescriptorHandle{0});
	cmdList.setGraphicsRootCBV(0, 0);

	SUCCEED();
}

// ============================================================
// IBuffer D3D12 virtual methods
// ============================================================

TEST_CASE("Dx12Config - IBuffer default gpuVirtualAddress is 0", "[dx12]")
{
	class TestBuffer : public mitiru::gfx::IBuffer
	{
	public:
		[[nodiscard]] std::uint32_t size() const noexcept override { return 1024; }
		[[nodiscard]] mitiru::gfx::BufferType type() const noexcept override
		{
			return mitiru::gfx::BufferType::Vertex;
		}
	};

	TestBuffer buffer;
	REQUIRE(buffer.gpuVirtualAddress() == 0);
	REQUIRE(buffer.isValid() == true);

	/// デフォルトのupdate()が安全に呼び出せること
	int data = 42;
	buffer.update(&data, sizeof(data));

	SUCCEED();
}

// ============================================================
// IPipeline D3D12 virtual methods
// ============================================================

TEST_CASE("Dx12Config - IPipeline default rootSignature is nullptr", "[dx12]")
{
	class TestPipeline : public mitiru::gfx::IPipeline
	{
	public:
		[[nodiscard]] bool isValid() const noexcept override { return true; }
	};

	TestPipeline pipeline;
	REQUIRE(pipeline.rootSignature() == nullptr);
}

// ============================================================
// Constant buffer alignment
// ============================================================

TEST_CASE("Dx12Config - constant buffer 256-byte alignment calculation", "[dx12]")
{
	/// D3D12の定数バッファは256バイトアライメントが必要
	constexpr uint32_t D3D12_CONSTANT_BUFFER_ALIGNMENT = 256;

	auto align = [](uint32_t size) -> uint32_t
	{
		return (size + D3D12_CONSTANT_BUFFER_ALIGNMENT - 1) &
			~(D3D12_CONSTANT_BUFFER_ALIGNMENT - 1);
	};

	REQUIRE(align(1) == 256);
	REQUIRE(align(64) == 256);
	REQUIRE(align(255) == 256);
	REQUIRE(align(256) == 256);
	REQUIRE(align(257) == 512);
	REQUIRE(align(512) == 512);
	REQUIRE(align(1000) == 1024);
}

// ============================================================
// Triple buffering fence value tracking
// ============================================================

TEST_CASE("Dx12Config - fence value tracking for triple buffering", "[dx12]")
{
	/// フレームごとのフェンス値管理をシミュレートする
	uint64_t fenceValues[DX12_FRAME_COUNT] = {};
	uint64_t currentFenceValue = 0;

	/// フレーム0: submit + signal
	uint32_t frameIndex = 0;
	++currentFenceValue;
	fenceValues[frameIndex] = currentFenceValue;
	REQUIRE(fenceValues[0] == 1);

	/// フレーム1: submit + signal
	frameIndex = 1;
	++currentFenceValue;
	fenceValues[frameIndex] = currentFenceValue;
	REQUIRE(fenceValues[1] == 2);

	/// フレーム2: submit + signal
	frameIndex = 2;
	++currentFenceValue;
	fenceValues[frameIndex] = currentFenceValue;
	REQUIRE(fenceValues[2] == 3);

	/// フレーム0に戻る: 以前のフェンス値(1)の完了を待つ
	frameIndex = 0;
	/// GPU完了済みフェンス値 >= fenceValues[0] を確認する
	const uint64_t gpuCompleted = 1;
	REQUIRE(gpuCompleted >= fenceValues[frameIndex]);

	++currentFenceValue;
	fenceValues[frameIndex] = currentFenceValue;
	REQUIRE(fenceValues[0] == 4);
	REQUIRE(currentFenceValue == 4);
}

// ============================================================
// BufferType enum completeness
// ============================================================

TEST_CASE("Dx12Config - BufferType includes all GPU buffer types", "[dx12]")
{
	using mitiru::gfx::BufferType;

	REQUIRE(BufferType::Vertex != BufferType::Index);
	REQUIRE(BufferType::Index != BufferType::Constant);
	REQUIRE(BufferType::Vertex != BufferType::Constant);
}

// ============================================================
// Texture row pitch alignment
// ============================================================

TEST_CASE("Dx12Config - texture row pitch alignment calculation", "[dx12]")
{
	/// D3D12のテクスチャデータは256バイトアライメントが必要
	constexpr uint32_t D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256;

	auto alignRowPitch = [](uint32_t width) -> uint32_t
	{
		const uint32_t rowBytes = width * 4;  ///< RGBA8 = 4バイト/ピクセル
		return (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
			~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
	};

	/// 1ピクセル幅: 4バイト → 256バイトにアライメント
	REQUIRE(alignRowPitch(1) == 256);

	/// 64ピクセル幅: 256バイト → そのまま256
	REQUIRE(alignRowPitch(64) == 256);

	/// 65ピクセル幅: 260バイト → 512にアライメント
	REQUIRE(alignRowPitch(65) == 512);

	/// 1920ピクセル幅: 7680バイト → 7680 (256の倍数)
	REQUIRE(alignRowPitch(1920) == 7680);
}

// ============================================================
// Descriptor handle offset calculation
// ============================================================

TEST_CASE("Dx12Config - descriptor handle offset calculation", "[dx12]")
{
	/// デスクリプタヒープ内のオフセット計算をシミュレートする
	const uint64_t heapStart = 0x1000;
	const uint32_t descriptorSize = 32;

	/// N番目のデスクリプタのCPUハンドルを計算する
	auto getHandle = [&](uint32_t index) -> uint64_t
	{
		return heapStart + static_cast<uint64_t>(index) * descriptorSize;
	};

	REQUIRE(getHandle(0) == 0x1000);
	REQUIRE(getHandle(1) == 0x1020);
	REQUIRE(getHandle(2) == 0x1040);
	REQUIRE(getHandle(255) == 0x1000 + 255 * 32);

	/// インデックスからハンドルへの逆計算
	auto getIndex = [&](uint64_t handle) -> uint32_t
	{
		return static_cast<uint32_t>((handle - heapStart) / descriptorSize);
	};

	REQUIRE(getIndex(0x1000) == 0);
	REQUIRE(getIndex(0x1020) == 1);
	REQUIRE(getIndex(0x1040) == 2);
}
