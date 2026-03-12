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

/// @brief VkRenderPassのRAIIラッパー
/// @details レンダーパスの生成・破棄をRAIIで管理する。
class VulkanRenderPass
{
public:
	/// @brief コンストラクタ
	/// @param desc レンダーパス生成パラメータ
	explicit VulkanRenderPass(const RenderPassDesc& desc)
		: m_desc(desc)
	{
		/// TODO: vkCreateRenderPass呼び出し
	}

	/// @brief デストラクタ
	~VulkanRenderPass()
	{
		/// TODO: vkDestroyRenderPass呼び出し
	}

	/// コピー禁止
	VulkanRenderPass(const VulkanRenderPass&) = delete;
	VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

	/// ムーブ禁止
	VulkanRenderPass(VulkanRenderPass&&) = delete;
	VulkanRenderPass& operator=(VulkanRenderPass&&) = delete;

	/// @brief 生成パラメータを取得する
	/// @return レンダーパス記述子へのconst参照
	[[nodiscard]] const RenderPassDesc& desc() const noexcept
	{
		return m_desc;
	}

private:
	RenderPassDesc m_desc;  ///< 生成パラメータ
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
