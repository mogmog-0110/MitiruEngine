#pragma once

/// @file VulkanDescriptorPool.hpp
/// @brief Vulkanデスクリプタプール管理
/// @details VkDescriptorPoolとVkDescriptorSetの割り当てをRAIIで管理する。
///          MITIRU_HAS_VULKANが定義されている場合のみコンパイルされる。

#ifdef MITIRU_HAS_VULKAN

#include <stdexcept>
#include <vector>

#include <vulkan/vulkan.h>

namespace mitiru::gfx
{

/// @brief デスクリプタプール設定
/// @details デスクリプタプール生成パラメータを保持する。GPU不要でテスト可能。
struct DescriptorPoolConfig
{
	uint32_t maxSets = 0;                              ///< 最大デスクリプタセット数
	std::vector<VkDescriptorPoolSize> poolSizes;       ///< プールサイズ（タイプと個数）

	/// @brief 設定が有効か確認する
	/// @return maxSetsが0より大きくpoolSizesが空でなければtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return maxSets > 0 && !poolSizes.empty();
	}

	/// @brief ユニフォームバッファ用のデフォルト設定を作成する
	/// @param maxSets 最大セット数
	/// @param count デスクリプタ数
	/// @return DescriptorPoolConfig
	[[nodiscard]] static DescriptorPoolConfig uniformBuffer(uint32_t maxSets, uint32_t count)
	{
		DescriptorPoolConfig cfg;
		cfg.maxSets = maxSets;
		cfg.poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count });
		return cfg;
	}

	/// @brief コンバインドイメージサンプラー用のデフォルト設定を作成する
	/// @param maxSets 最大セット数
	/// @param count デスクリプタ数
	/// @return DescriptorPoolConfig
	[[nodiscard]] static DescriptorPoolConfig combinedImageSampler(uint32_t maxSets, uint32_t count)
	{
		DescriptorPoolConfig cfg;
		cfg.maxSets = maxSets;
		cfg.poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count });
		return cfg;
	}
};

/// @brief Vulkanデスクリプタプール RAIIラッパー
/// @details VkDescriptorPoolを管理し、デスクリプタセットの割り当てと
///          リセットを提供する。
///
/// @code
/// VulkanDescriptorPool pool;
/// pool.initialize(device, 100, poolSizes);
/// auto set = pool.allocateSet(layout);
/// pool.resetPool();
/// @endcode
class VulkanDescriptorPool
{
public:
	VulkanDescriptorPool() = default;

	~VulkanDescriptorPool()
	{
		destroy();
	}

	VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
	VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

	VulkanDescriptorPool(VulkanDescriptorPool&& other) noexcept
		: m_device(other.m_device)
		, m_pool(other.m_pool)
	{
		other.m_device = VK_NULL_HANDLE;
		other.m_pool = VK_NULL_HANDLE;
	}

	VulkanDescriptorPool& operator=(VulkanDescriptorPool&& other) noexcept
	{
		if (this != &other)
		{
			destroy();
			m_device = other.m_device;
			m_pool = other.m_pool;
			other.m_device = VK_NULL_HANDLE;
			other.m_pool = VK_NULL_HANDLE;
		}
		return *this;
	}

	/// @brief デスクリプタプールを初期化する
	/// @param device 論理デバイス
	/// @param maxSets 最大デスクリプタセット数
	/// @param poolSizes プールサイズリスト
	/// @throws std::runtime_error デスクリプタプール生成に失敗した場合
	void initialize(
		VkDevice device,
		uint32_t maxSets,
		const std::vector<VkDescriptorPoolSize>& poolSizes)
	{
		destroy();
		m_device = device;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = maxSets;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanDescriptorPool: vkCreateDescriptorPool failed");
		}
	}

	/// @brief デスクリプタセットを割り当てる
	/// @param layout デスクリプタセットレイアウト
	/// @return 割り当てられたVkDescriptorSet
	/// @throws std::runtime_error 割り当てに失敗した場合
	[[nodiscard]] VkDescriptorSet allocateSet(VkDescriptorSetLayout layout)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(m_device, &allocInfo, &set) != VK_SUCCESS)
		{
			throw std::runtime_error("VulkanDescriptorPool: vkAllocateDescriptorSets failed");
		}
		return set;
	}

	/// @brief プール内の全デスクリプタセットをリセットする
	/// @details フレームごとにデスクリプタセットを再利用する場合に使用する。
	void resetPool() noexcept
	{
		if (m_pool != VK_NULL_HANDLE)
		{
			vkResetDescriptorPool(m_device, m_pool, 0);
		}
	}

	/// @brief デスクリプタプールリソースを解放する
	void destroy() noexcept
	{
		if (m_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_device, m_pool, nullptr);
			m_pool = VK_NULL_HANDLE;
		}
		m_device = VK_NULL_HANDLE;
	}

	/// @brief プールハンドルを取得する
	/// @return VkDescriptorPool
	[[nodiscard]] VkDescriptorPool handle() const noexcept { return m_pool; }

	/// @brief 初期化済みか確認する
	/// @return プールが有効ならtrue
	[[nodiscard]] bool isInitialized() const noexcept { return m_pool != VK_NULL_HANDLE; }

private:
	VkDevice m_device = VK_NULL_HANDLE;            ///< 論理デバイス（非所有）
	VkDescriptorPool m_pool = VK_NULL_HANDLE;      ///< デスクリプタプール
};

} // namespace mitiru::gfx

#endif // MITIRU_HAS_VULKAN
