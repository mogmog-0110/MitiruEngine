#pragma once

/// @file VulkanDevice.hpp
/// @brief Vulkanバックエンド スタブ実装
/// @details Vulkan GPUデバイスのIDevice実装。
///          現時点ではスタブであり、全メソッドがノーオペレーションまたは空値を返す。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <cstdint>
#include <memory>
#include <vector>

#include <sgc/types/Color.hpp>

#include <mitiru/gfx/IBuffer.hpp>
#include <mitiru/gfx/ICommandList.hpp>
#include <mitiru/gfx/IDevice.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IRenderTarget.hpp>

namespace mitiru::gfx
{

/// @brief Vulkan用バッファ スタブ実装
/// @details VkBufferのラッパーとなるスタブ。サイズと種別のみ保持する。
class VulkanBuffer final : public IBuffer
{
public:
	/// @brief コンストラクタ
	/// @param bufferType バッファ種別
	/// @param sizeBytes サイズ（バイト）
	VulkanBuffer(BufferType bufferType, std::uint32_t sizeBytes) noexcept
		: m_type(bufferType)
		, m_size(sizeBytes)
	{
	}

	/// @brief バッファサイズを取得する
	[[nodiscard]] std::uint32_t size() const noexcept override
	{
		return m_size;
	}

	/// @brief バッファ種別を取得する
	[[nodiscard]] BufferType type() const noexcept override
	{
		return m_type;
	}

private:
	BufferType m_type;        ///< バッファ種別
	std::uint32_t m_size;     ///< サイズ（バイト）
};

/// @brief Vulkan用コマンドリスト スタブ実装
/// @details VkCommandBufferのラッパーとなるスタブ。全操作がノーオペレーション。
class VulkanCommandList final : public ICommandList
{
public:
	/// @brief コマンド記録を開始する（何もしない）
	void begin() override {}

	/// @brief コマンド記録を終了する（何もしない）
	void end() override {}

	/// @brief レンダーターゲットを設定する（何もしない）
	void setRenderTarget(IRenderTarget*) override {}

	/// @brief レンダーターゲットをクリアする（何もしない）
	void clearRenderTarget(const sgc::Colorf&) override {}

	/// @brief パイプライン状態を設定する（何もしない）
	void setPipeline(IPipeline*) override {}

	/// @brief 頂点バッファを設定する（何もしない）
	void setVertexBuffer(IBuffer*) override {}

	/// @brief インデックスバッファを設定する（何もしない）
	void setIndexBuffer(IBuffer*) override {}

	/// @brief インデックス付き描画を実行する（何もしない）
	void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}

	/// @brief 頂点のみで描画を実行する（何もしない）
	void draw(std::uint32_t, std::uint32_t) override {}
};

/// @brief Vulkan GPUデバイス スタブ実装
/// @details VkDevice / VkInstanceをラップするデバイス実装。
///          現時点ではNullDeviceと同等のスタブ実装。
///          将来のフェーズでVulkan SDKを使用した実装に置き換える。
///
/// @code
/// auto device = std::make_unique<VulkanDevice>();
/// device->beginFrame();
/// // Vulkan描画コマンド...
/// device->endFrame();
/// @endcode
class VulkanDevice final : public IDevice
{
public:
	/// @brief フレームバッファからピクセルを読み取る
	/// @param width 読み取り幅
	/// @param height 読み取り高さ
	/// @return 全て黒（RGBA=0,0,0,255）のピクセルデータ
	/// @note 将来のフェーズでvkCmdCopyImageToBufferを使用した実装に置き換える
	[[nodiscard]] std::vector<std::uint8_t> readPixels(
		int width, int height) const override
	{
		const auto pixelCount =
			static_cast<std::size_t>(width) *
			static_cast<std::size_t>(height);
		std::vector<std::uint8_t> data(pixelCount * 4, 0);

		/// アルファチャンネルを255に設定
		for (std::size_t i = 0; i < pixelCount; ++i)
		{
			data[i * 4 + 3] = 255;
		}

		return data;
	}

	/// @brief バックエンド種別を取得する
	/// @return Backend::Vulkan
	[[nodiscard]] Backend backend() const noexcept override
	{
		return Backend::Vulkan;
	}

	/// @brief フレーム開始処理（スタブ: 何もしない）
	/// @note 将来のフェーズでvkAcquireNextImageKHR等を呼び出す
	void beginFrame() override
	{
	}

	/// @brief フレーム終了処理（スタブ: 何もしない）
	/// @note 将来のフェーズでvkQueueSubmit / vkQueuePresentKHRを呼び出す
	void endFrame() override
	{
	}

	/// @brief GPUバッファを生成する（スタブ実装）
	/// @param bufferType バッファ種別
	/// @param sizeBytes サイズ（バイト）
	/// @return VulkanBufferインスタンス
	[[nodiscard]] std::unique_ptr<IBuffer> createBuffer(
		BufferType bufferType,
		std::uint32_t sizeBytes,
		bool /*dynamic*/,
		const void* /*initialData*/) override
	{
		return std::make_unique<VulkanBuffer>(bufferType, sizeBytes);
	}

	/// @brief コマンドリストを生成する（スタブ実装）
	/// @return VulkanCommandListインスタンス
	[[nodiscard]] std::unique_ptr<ICommandList> createCommandList() override
	{
		return std::make_unique<VulkanCommandList>();
	}
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
