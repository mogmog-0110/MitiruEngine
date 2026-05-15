#pragma once

/// @file VulkanRenderPass.hpp
/// @brief Vulkanレンダーパス設定
/// @details VkRenderPassの生成パラメータを定義する構造体群を提供する。
///          MITIRU_HAS_VULKANが定義されている場合のみ実際のVulkan APIを使用する。

#include <cstdint>
#include <vector>

#include <mitiru/gfx/GfxTypes.hpp>

namespace mitiru::gfx
{

/// @brief アタッチメントのロード操作
enum class AttachmentLoadOp : std::uint8_t
{
	Load = 0,   ///< 既存の内容を保持する
	Clear,      ///< 指定色でクリアする
	DontCare    ///< 内容を保証しない（最適化用）
};

/// @brief アタッチメントのストア操作
enum class AttachmentStoreOp : std::uint8_t
{
	Store = 0,  ///< 内容を保存する
	DontCare    ///< 内容を保存しない（最適化用）
};

/// @brief イメージレイアウト
enum class ImageLayout : std::uint8_t
{
	Undefined = 0,        ///< 未定義（初期状態）
	ColorAttachment,      ///< カラーアタッチメント
	DepthStencilAttachment, ///< 深度ステンシルアタッチメント
	ShaderReadOnly,       ///< シェーダー読み取り専用
	PresentSrc,           ///< プレゼント元
	TransferSrc,          ///< 転送元
	TransferDst           ///< 転送先
};

/// @brief アタッチメント記述子
/// @details レンダーパスのカラー・深度・リゾルブアタッチメントを記述する。
struct AttachmentDesc
{
	PixelFormat format = PixelFormat::RGBA8;           ///< ピクセルフォーマット
	std::uint32_t sampleCount = 1;                    ///< マルチサンプル数
	AttachmentLoadOp loadOp = AttachmentLoadOp::Clear; ///< ロード操作
	AttachmentStoreOp storeOp = AttachmentStoreOp::Store; ///< ストア操作
	AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::DontCare; ///< ステンシルロード操作
	AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::DontCare; ///< ステンシルストア操作
	ImageLayout initialLayout = ImageLayout::Undefined; ///< 初期レイアウト
	ImageLayout finalLayout = ImageLayout::PresentSrc;  ///< 最終レイアウト
};

/// @brief アタッチメント参照
/// @details サブパスが参照するアタッチメントのインデックスとレイアウトの組。
struct AttachmentReference
{
	std::uint32_t attachmentIndex = 0;                ///< アタッチメントインデックス
	ImageLayout layout = ImageLayout::ColorAttachment; ///< サブパス内でのレイアウト
};

/// @brief サブパス記述子
/// @details レンダーパス内の1つのサブパスが使用するアタッチメントを記述する。
struct SubpassDesc
{
	std::vector<AttachmentReference> colorAttachments;    ///< カラーアタッチメント参照
	std::vector<AttachmentReference> inputAttachments;    ///< 入力アタッチメント参照
	std::vector<AttachmentReference> resolveAttachments;  ///< リゾルブアタッチメント参照
	std::uint32_t depthStencilAttachment = UINT32_MAX;   ///< 深度ステンシルインデックス（UINT32_MAX = なし）
	ImageLayout depthStencilLayout = ImageLayout::DepthStencilAttachment; ///< 深度ステンシルレイアウト

	/// @brief 深度ステンシルアタッチメントが設定されているか確認する
	/// @return 設定されていればtrue
	[[nodiscard]] bool hasDepthStencil() const noexcept
	{
		return depthStencilAttachment != UINT32_MAX;
	}
};

/// @brief サブパス依存関係
/// @details サブパス間のパイプラインバリアを記述する。
struct SubpassDependency
{
	std::uint32_t srcSubpass = UINT32_MAX;    ///< 依存元サブパス（UINT32_MAX = 外部）
	std::uint32_t dstSubpass = 0;             ///< 依存先サブパス
	std::uint32_t srcStageMask = 0;           ///< 依存元パイプラインステージマスク
	std::uint32_t dstStageMask = 0;           ///< 依存先パイプラインステージマスク
	std::uint32_t srcAccessMask = 0;          ///< 依存元アクセスマスク
	std::uint32_t dstAccessMask = 0;          ///< 依存先アクセスマスク

	/// @brief 外部→最初のサブパスへの標準依存関係を生成する
	/// @return デフォルトの外部依存関係
	[[nodiscard]] static SubpassDependency externalToFirst() noexcept
	{
		SubpassDependency dep;
		dep.srcSubpass = UINT32_MAX;
		dep.dstSubpass = 0;
		return dep;
	}
};

/// @brief レンダーパス生成パラメータ
/// @details VkRenderPassCreateInfoに対応する構成情報を保持する。
///
/// @code
/// RenderPassDesc desc;
/// desc.attachments.push_back(AttachmentDesc{
///     .format = PixelFormat::BGRA8,
///     .loadOp = AttachmentLoadOp::Clear,
///     .finalLayout = ImageLayout::PresentSrc
/// });
///
/// SubpassDesc subpass;
/// subpass.colorAttachments.push_back({0, ImageLayout::ColorAttachment});
/// desc.subpasses.push_back(subpass);
/// @endcode
struct RenderPassDesc
{
	std::vector<AttachmentDesc> attachments;       ///< アタッチメント記述子リスト
	std::vector<SubpassDesc> subpasses;            ///< サブパス記述子リスト
	std::vector<SubpassDependency> dependencies;   ///< サブパス依存関係リスト

	/// @brief シンプルなカラーのみレンダーパスを生成する
	/// @param format カラーアタッチメントのフォーマット
	/// @return 1アタッチメント1サブパスのレンダーパス記述子
	[[nodiscard]] static RenderPassDesc simpleColor(
		PixelFormat format = PixelFormat::BGRA8)
	{
		RenderPassDesc desc;

		/// カラーアタッチメント
		AttachmentDesc colorAttach;
		colorAttach.format = format;
		colorAttach.loadOp = AttachmentLoadOp::Clear;
		colorAttach.storeOp = AttachmentStoreOp::Store;
		colorAttach.initialLayout = ImageLayout::Undefined;
		colorAttach.finalLayout = ImageLayout::PresentSrc;
		desc.attachments.push_back(colorAttach);

		/// サブパス
		SubpassDesc subpass;
		subpass.colorAttachments.push_back(
			AttachmentReference{0, ImageLayout::ColorAttachment});
		desc.subpasses.push_back(subpass);

		/// 外部依存関係
		desc.dependencies.push_back(SubpassDependency::externalToFirst());

		return desc;
	}

	/// @brief カラー＋深度レンダーパスを生成する
	/// @param colorFormat カラーアタッチメントのフォーマット
	/// @return カラー+深度の2アタッチメント1サブパスのレンダーパス記述子
	[[nodiscard]] static RenderPassDesc colorDepth(
		PixelFormat colorFormat = PixelFormat::BGRA8)
	{
		RenderPassDesc desc;

		/// カラーアタッチメント
		AttachmentDesc colorAttach;
		colorAttach.format = colorFormat;
		colorAttach.loadOp = AttachmentLoadOp::Clear;
		colorAttach.storeOp = AttachmentStoreOp::Store;
		colorAttach.initialLayout = ImageLayout::Undefined;
		colorAttach.finalLayout = ImageLayout::PresentSrc;
		desc.attachments.push_back(colorAttach);

		/// 深度アタッチメント
		AttachmentDesc depthAttach;
		depthAttach.format = PixelFormat::Depth24Stencil8;
		depthAttach.loadOp = AttachmentLoadOp::Clear;
		depthAttach.storeOp = AttachmentStoreOp::DontCare;
		depthAttach.initialLayout = ImageLayout::Undefined;
		depthAttach.finalLayout = ImageLayout::DepthStencilAttachment;
		desc.attachments.push_back(depthAttach);

		/// サブパス
		SubpassDesc subpass;
		subpass.colorAttachments.push_back(
			AttachmentReference{0, ImageLayout::ColorAttachment});
		subpass.depthStencilAttachment = 1;
		subpass.depthStencilLayout = ImageLayout::DepthStencilAttachment;
		desc.subpasses.push_back(subpass);

		/// 外部依存関係
		desc.dependencies.push_back(SubpassDependency::externalToFirst());

		return desc;
	}
};

#ifdef MITIRU_HAS_VULKAN

#include <stdexcept>
#include <vulkan/vulkan.h>

/// @brief VkRenderPassのRAIIラッパー
/// @details レンダーパスの生成・破棄をRAIIで管理する。
///          RenderPassDescをVkRenderPassCreateInfoに変換してvkCreateRenderPassを呼び出す。
///
/// @code
/// VkDevice device = /* VulkanDevice から取得 */;
/// RenderPassDesc desc = RenderPassDesc::simpleColor(PixelFormat::BGRA8);
/// VulkanRenderPass renderPass(device, desc);
/// VkRenderPass vkPass = renderPass.handle();
/// @endcode
class VulkanRenderPass
{
public:
	/// @brief コンストラクタ
	/// @param device 論理デバイスハンドル
	/// @param desc レンダーパス生成パラメータ
	/// @throws std::runtime_error vkCreateRenderPassが失敗した場合
	VulkanRenderPass(VkDevice device, const RenderPassDesc& desc)
		: m_device(device)
		, m_desc(desc)
	{
		/// アタッチメント記述子を変換する
		std::vector<VkAttachmentDescription> attachments;
		attachments.reserve(desc.attachments.size());
		for (const auto& a : desc.attachments)
		{
			VkAttachmentDescription vkAttach{};
			vkAttach.format         = toVkFormat(a.format);
			vkAttach.samples        = toVkSampleCount(a.sampleCount);
			vkAttach.loadOp         = toVkLoadOp(a.loadOp);
			vkAttach.storeOp        = toVkStoreOp(a.storeOp);
			vkAttach.stencilLoadOp  = toVkLoadOp(a.stencilLoadOp);
			vkAttach.stencilStoreOp = toVkStoreOp(a.stencilStoreOp);
			vkAttach.initialLayout  = toVkImageLayout(a.initialLayout);
			vkAttach.finalLayout    = toVkImageLayout(a.finalLayout);
			attachments.push_back(vkAttach);
		}

		/// サブパス記述子とそれが参照するアタッチメント参照を変換する
		/// VkSubpassDescriptionはポインタを保持するため、参照配列の寿命を管理する
		std::vector<std::vector<VkAttachmentReference>> colorRefs(desc.subpasses.size());
		std::vector<std::vector<VkAttachmentReference>> inputRefs(desc.subpasses.size());
		std::vector<std::vector<VkAttachmentReference>> resolveRefs(desc.subpasses.size());
		std::vector<VkAttachmentReference>              depthRefs(desc.subpasses.size());
		std::vector<VkSubpassDescription>               subpasses;
		subpasses.reserve(desc.subpasses.size());

		for (std::size_t i = 0; i < desc.subpasses.size(); ++i)
		{
			const auto& s = desc.subpasses[i];

			for (const auto& ref : s.colorAttachments)
			{
				colorRefs[i].push_back(
					VkAttachmentReference{ref.attachmentIndex, toVkImageLayout(ref.layout)});
			}
			for (const auto& ref : s.inputAttachments)
			{
				inputRefs[i].push_back(
					VkAttachmentReference{ref.attachmentIndex, toVkImageLayout(ref.layout)});
			}
			for (const auto& ref : s.resolveAttachments)
			{
				resolveRefs[i].push_back(
					VkAttachmentReference{ref.attachmentIndex, toVkImageLayout(ref.layout)});
			}

			depthRefs[i] = VkAttachmentReference{
				s.depthStencilAttachment,
				toVkImageLayout(s.depthStencilLayout)
			};

			VkSubpassDescription vkSubpass{};
			vkSubpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
			vkSubpass.colorAttachmentCount    = static_cast<std::uint32_t>(colorRefs[i].size());
			vkSubpass.pColorAttachments       = colorRefs[i].empty() ? nullptr : colorRefs[i].data();
			vkSubpass.inputAttachmentCount    = static_cast<std::uint32_t>(inputRefs[i].size());
			vkSubpass.pInputAttachments       = inputRefs[i].empty() ? nullptr : inputRefs[i].data();
			vkSubpass.pResolveAttachments     = resolveRefs[i].empty() ? nullptr : resolveRefs[i].data();
			vkSubpass.pDepthStencilAttachment = s.hasDepthStencil() ? &depthRefs[i] : nullptr;
			subpasses.push_back(vkSubpass);
		}

		/// サブパス依存関係を変換する
		std::vector<VkSubpassDependency> dependencies;
		dependencies.reserve(desc.dependencies.size());
		for (const auto& dep : desc.dependencies)
		{
			VkSubpassDependency vkDep{};
			vkDep.srcSubpass    = dep.srcSubpass;
			vkDep.dstSubpass    = dep.dstSubpass;
			vkDep.srcStageMask  = dep.srcStageMask;
			vkDep.dstStageMask  = dep.dstStageMask;
			vkDep.srcAccessMask = dep.srcAccessMask;
			vkDep.dstAccessMask = dep.dstAccessMask;
			dependencies.push_back(vkDep);
		}

		VkRenderPassCreateInfo createInfo{};
		createInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
		createInfo.pAttachments    = attachments.empty() ? nullptr : attachments.data();
		createInfo.subpassCount    = static_cast<std::uint32_t>(subpasses.size());
		createInfo.pSubpasses      = subpasses.empty() ? nullptr : subpasses.data();
		createInfo.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
		createInfo.pDependencies   = dependencies.empty() ? nullptr : dependencies.data();

		if (vkCreateRenderPass(m_device, &createInfo, nullptr, &m_renderPass) != VK_SUCCESS)
		{
			throw std::runtime_error("vkCreateRenderPass failed");
		}
	}

	/// @brief デストラクタ
	~VulkanRenderPass()
	{
		if (m_device != VK_NULL_HANDLE && m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(m_device, m_renderPass, nullptr);
		}
	}

	/// コピー禁止
	VulkanRenderPass(const VulkanRenderPass&) = delete;
	VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

	/// ムーブ禁止
	VulkanRenderPass(VulkanRenderPass&&) = delete;
	VulkanRenderPass& operator=(VulkanRenderPass&&) = delete;

	/// @brief 内部のVkRenderPassハンドルを取得する
	/// @return VkRenderPassハンドル
	[[nodiscard]] VkRenderPass handle() const noexcept
	{
		return m_renderPass;
	}

	/// @brief 生成パラメータを取得する
	/// @return レンダーパス記述子へのconst参照
	[[nodiscard]] const RenderPassDesc& desc() const noexcept
	{
		return m_desc;
	}

private:
	[[nodiscard]] static VkFormat toVkFormat(PixelFormat fmt) noexcept
	{
		switch (fmt)
		{
		case PixelFormat::RGBA8:           return VK_FORMAT_R8G8B8A8_UNORM;
		case PixelFormat::BGRA8:           return VK_FORMAT_B8G8R8A8_UNORM;
		case PixelFormat::R8:              return VK_FORMAT_R8_UNORM;
		case PixelFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
		}
		return VK_FORMAT_UNDEFINED;
	}

	[[nodiscard]] static VkSampleCountFlagBits toVkSampleCount(std::uint32_t count) noexcept
	{
		switch (count)
		{
		case 2:  return VK_SAMPLE_COUNT_2_BIT;
		case 4:  return VK_SAMPLE_COUNT_4_BIT;
		case 8:  return VK_SAMPLE_COUNT_8_BIT;
		case 16: return VK_SAMPLE_COUNT_16_BIT;
		default: return VK_SAMPLE_COUNT_1_BIT;
		}
	}

	[[nodiscard]] static VkAttachmentLoadOp toVkLoadOp(AttachmentLoadOp op) noexcept
	{
		switch (op)
		{
		case AttachmentLoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
		case AttachmentLoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
		case AttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		}
		return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	[[nodiscard]] static VkAttachmentStoreOp toVkStoreOp(AttachmentStoreOp op) noexcept
	{
		switch (op)
		{
		case AttachmentStoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
		case AttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}

	[[nodiscard]] static VkImageLayout toVkImageLayout(ImageLayout layout) noexcept
	{
		switch (layout)
		{
		case ImageLayout::Undefined:                return VK_IMAGE_LAYOUT_UNDEFINED;
		case ImageLayout::ColorAttachment:          return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case ImageLayout::DepthStencilAttachment:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case ImageLayout::ShaderReadOnly:           return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case ImageLayout::PresentSrc:               return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		case ImageLayout::TransferSrc:              return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ImageLayout::TransferDst:              return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}

	VkDevice       m_device     = VK_NULL_HANDLE;
	VkRenderPass   m_renderPass = VK_NULL_HANDLE;
	RenderPassDesc m_desc;
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
