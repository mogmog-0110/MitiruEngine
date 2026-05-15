#pragma once

/// @file VulkanPipeline.hpp
/// @brief Vulkanグラフィックスパイプライン設定
/// @details VkGraphicsPipelineの生成パラメータを定義する構造体群を提供する。
///          MITIRU_HAS_VULKANが定義されている場合のみ実際のVulkan APIを使用する。

#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/gfx/GfxTypes.hpp>
#include <mitiru/gfx/IPipeline.hpp>
#include <mitiru/gfx/IShader.hpp>

namespace mitiru::gfx
{

/// @brief プリミティブトポロジ
enum class PrimitiveTopology : std::uint8_t
{
	TriangleList = 0,   ///< 三角形リスト
	TriangleStrip,      ///< 三角形ストリップ
	LineList,           ///< 線分リスト
	LineStrip,          ///< 線分ストリップ
	PointList           ///< 点リスト
};

/// @brief ポリゴンフィルモード
enum class PolygonMode : std::uint8_t
{
	Fill = 0,     ///< 塗りつぶし
	Line,         ///< ワイヤーフレーム
	Point         ///< 頂点のみ
};

/// @brief カリングモード
enum class CullMode : std::uint8_t
{
	None = 0,     ///< カリングなし
	Front,        ///< フロントフェイスカリング
	Back          ///< バックフェイスカリング
};

/// @brief フロントフェイスの巻き方向
enum class FrontFace : std::uint8_t
{
	CounterClockwise = 0,  ///< 反時計回り
	Clockwise              ///< 時計回り
};

/// @brief 比較演算子
enum class CompareOp : std::uint8_t
{
	Never = 0,       ///< 常に不合格
	Less,            ///< 小さい場合
	Equal,           ///< 等しい場合
	LessOrEqual,     ///< 以下の場合
	Greater,         ///< 大きい場合
	NotEqual,        ///< 等しくない場合
	GreaterOrEqual,  ///< 以上の場合
	Always           ///< 常に合格
};

/// @brief 頂点入力属性記述子
struct VertexInputAttribute
{
	std::uint32_t location = 0;     ///< シェーダーロケーション
	std::uint32_t binding = 0;      ///< バインディングインデックス
	PixelFormat format = PixelFormat::RGBA8; ///< データフォーマット
	std::uint32_t offset = 0;       ///< バッファ内オフセット（バイト）
};

/// @brief 頂点入力バインディング記述子
struct VertexInputBinding
{
	std::uint32_t binding = 0;      ///< バインディングインデックス
	std::uint32_t stride = 0;       ///< 頂点ストライド（バイト）
	bool perInstance = false;       ///< インスタンスデータとして入力するか
};

/// @brief 頂点入力状態
struct VertexInputState
{
	std::vector<VertexInputBinding> bindings;     ///< バインディング記述子リスト
	std::vector<VertexInputAttribute> attributes; ///< 属性記述子リスト
};

/// @brief ラスタライザ状態
struct RasterizerState
{
	PolygonMode polygonMode = PolygonMode::Fill;  ///< ポリゴンフィルモード
	CullMode cullMode = CullMode::Back;           ///< カリングモード
	FrontFace frontFace = FrontFace::CounterClockwise; ///< フロントフェイス巻き方向
	bool depthClampEnable = false;                ///< 深度クランプの有効化
	bool depthBiasEnable = false;                 ///< 深度バイアスの有効化
	float depthBiasConstant = 0.0f;               ///< 定数深度バイアス
	float depthBiasSlope = 0.0f;                  ///< 傾斜深度バイアス
	float lineWidth = 1.0f;                       ///< 線幅
};

/// @brief マルチサンプリング状態
struct MultisampleState
{
	std::uint32_t sampleCount = 1;           ///< サンプル数（1, 2, 4, 8, ...）
	bool sampleShadingEnable = false;        ///< サンプルシェーディングの有効化
	float minSampleShading = 1.0f;           ///< 最小サンプルシェーディング率
};

/// @brief 深度ステンシル状態
struct DepthStencilState
{
	bool depthTestEnable = true;             ///< 深度テストの有効化
	bool depthWriteEnable = true;            ///< 深度書き込みの有効化
	CompareOp depthCompareOp = CompareOp::Less; ///< 深度比較演算子
	bool stencilTestEnable = false;          ///< ステンシルテストの有効化
};

/// @brief シェーダーステージ記述子
struct ShaderStageDesc
{
	ShaderType type = ShaderType::Vertex;    ///< シェーダー種別
	std::vector<std::uint8_t> code;          ///< SPIR-Vバイトコード
	std::string entryPoint = "main";         ///< エントリーポイント名
};

/// @brief プッシュ定数範囲
struct PushConstantRange
{
	ShaderType stageFlags = ShaderType::Vertex; ///< 使用するシェーダーステージ
	std::uint32_t offset = 0;                ///< オフセット（バイト）
	std::uint32_t size = 0;                  ///< サイズ（バイト）
};

/// @brief パイプラインレイアウト記述子
struct PipelineLayoutDesc
{
	std::vector<PushConstantRange> pushConstantRanges; ///< プッシュ定数範囲リスト
	std::uint32_t descriptorSetLayoutCount = 0;        ///< デスクリプタセットレイアウト数
};

/// @brief グラフィックスパイプライン生成パラメータ
/// @details VkGraphicsPipelineCreateInfoに対応する構成情報を保持する。
///
/// @code
/// GraphicsPipelineDesc desc;
/// desc.vertexInput.bindings.push_back({0, sizeof(Vertex), false});
/// desc.vertexInput.attributes.push_back({0, 0, PixelFormat::RGBA8, 0});
/// desc.rasterizer.cullMode = CullMode::Back;
/// desc.depthStencil.depthTestEnable = true;
/// desc.topology = PrimitiveTopology::TriangleList;
/// @endcode
struct GraphicsPipelineDesc
{
	std::vector<ShaderStageDesc> shaderStages;    ///< シェーダーステージリスト
	VertexInputState vertexInput;                 ///< 頂点入力状態
	PrimitiveTopology topology = PrimitiveTopology::TriangleList; ///< プリミティブトポロジ
	RasterizerState rasterizer;                   ///< ラスタライザ状態
	MultisampleState multisample;                 ///< マルチサンプリング状態
	DepthStencilState depthStencil;               ///< 深度ステンシル状態
	BlendMode blendMode = BlendMode::None;        ///< ブレンドモード
	PipelineLayoutDesc layout;                    ///< パイプラインレイアウト

	/// @brief 2Dスプライト描画用のデフォルト設定を生成する
	/// @return 2D描画向けパイプライン記述子
	[[nodiscard]] static GraphicsPipelineDesc default2D()
	{
		GraphicsPipelineDesc desc;
		desc.topology = PrimitiveTopology::TriangleList;
		desc.rasterizer.cullMode = CullMode::None;
		desc.rasterizer.polygonMode = PolygonMode::Fill;
		desc.depthStencil.depthTestEnable = false;
		desc.depthStencil.depthWriteEnable = false;
		desc.blendMode = BlendMode::Alpha;
		return desc;
	}

	/// @brief 3Dメッシュ描画用のデフォルト設定を生成する
	/// @return 3D描画向けパイプライン記述子
	[[nodiscard]] static GraphicsPipelineDesc default3D()
	{
		GraphicsPipelineDesc desc;
		desc.topology = PrimitiveTopology::TriangleList;
		desc.rasterizer.cullMode = CullMode::Back;
		desc.rasterizer.polygonMode = PolygonMode::Fill;
		desc.depthStencil.depthTestEnable = true;
		desc.depthStencil.depthWriteEnable = true;
		desc.blendMode = BlendMode::None;
		return desc;
	}
};

#ifdef MITIRU_HAS_VULKAN

#include <stdexcept>
#include <vulkan/vulkan.h>

/// @brief VkPipelineのRAIIラッパー
/// @details グラフィックスパイプラインの生成・破棄をRAIIで管理する。
///          GraphicsPipelineDescに含まれるSPIR-VコードからVkShaderModuleを生成し、
///          すべてのパイプラインステートを設定してvkCreateGraphicsPipelinesを呼び出す。
///
/// @code
/// VkDevice device = /* VulkanDevice から取得 */;
/// VkRenderPass renderPass = /* VulkanRenderPass::handle() */;
/// GraphicsPipelineDesc desc = GraphicsPipelineDesc::default3D();
/// // desc.shaderStages にSPIR-Vコードを設定する
/// VulkanGraphicsPipeline pipeline(device, renderPass, desc);
/// if (pipeline.isValid()) { /* 描画に使用 */ }
/// @endcode
class VulkanGraphicsPipeline : public IPipeline
{
public:
	/// @brief コンストラクタ
	/// @param device      論理デバイスハンドル
	/// @param renderPass  パイプラインで使用するレンダーパスハンドル
	/// @param desc        パイプライン生成パラメータ
	/// @throws std::runtime_error シェーダーモジュールまたはパイプラインの生成が失敗した場合
	VulkanGraphicsPipeline(VkDevice device, VkRenderPass renderPass, const GraphicsPipelineDesc& desc)
		: m_device(device)
		, m_desc(desc)
	{
		/// シェーダーモジュールを生成し、ステージ記述子を構築する
		std::vector<VkShaderModule>            shaderModules;
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
		shaderModules.reserve(desc.shaderStages.size());
		shaderStages.reserve(desc.shaderStages.size());

		for (const auto& stage : desc.shaderStages)
		{
			if (stage.code.empty())
			{
				continue;
			}

			VkShaderModuleCreateInfo moduleInfo{};
			moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = stage.code.size();
			moduleInfo.pCode    = reinterpret_cast<const std::uint32_t*>(stage.code.data());

			VkShaderModule shaderModule = VK_NULL_HANDLE;
			if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS)
			{
				destroyShaderModules(shaderModules, m_device);
				throw std::runtime_error("vkCreateShaderModule failed");
			}
			shaderModules.push_back(shaderModule);

			VkPipelineShaderStageCreateInfo stageInfo{};
			stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage  = toVkShaderStage(stage.type);
			stageInfo.module = shaderModule;
			stageInfo.pName  = stage.entryPoint.c_str();
			shaderStages.push_back(stageInfo);
		}

		/// 頂点入力状態
		std::vector<VkVertexInputBindingDescription>   bindingDescs;
		std::vector<VkVertexInputAttributeDescription> attrDescs;
		bindingDescs.reserve(desc.vertexInput.bindings.size());
		attrDescs.reserve(desc.vertexInput.attributes.size());

		for (const auto& b : desc.vertexInput.bindings)
		{
			VkVertexInputBindingDescription bd{};
			bd.binding   = b.binding;
			bd.stride    = b.stride;
			bd.inputRate = b.perInstance
				? VK_VERTEX_INPUT_RATE_INSTANCE
				: VK_VERTEX_INPUT_RATE_VERTEX;
			bindingDescs.push_back(bd);
		}
		for (const auto& a : desc.vertexInput.attributes)
		{
			VkVertexInputAttributeDescription ad{};
			ad.location = a.location;
			ad.binding  = a.binding;
			ad.format   = toVkVertexFormat(a.format);
			ad.offset   = a.offset;
			attrDescs.push_back(ad);
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount   = static_cast<std::uint32_t>(bindingDescs.size());
		vertexInputInfo.pVertexBindingDescriptions      = bindingDescs.empty() ? nullptr : bindingDescs.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrDescs.size());
		vertexInputInfo.pVertexAttributeDescriptions    = attrDescs.empty() ? nullptr : attrDescs.data();

		/// 入力アセンブリ
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology               = toVkTopology(desc.topology);
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		/// ビューポート・シザー（動的に設定する）
		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount  = 1;

		/// ラスタライザ
		const auto& rs = desc.rasterizer;
		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable        = rs.depthClampEnable ? VK_TRUE : VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode             = toVkPolygonMode(rs.polygonMode);
		rasterizer.cullMode                = toVkCullMode(rs.cullMode);
		rasterizer.frontFace               = toVkFrontFace(rs.frontFace);
		rasterizer.depthBiasEnable         = rs.depthBiasEnable ? VK_TRUE : VK_FALSE;
		rasterizer.depthBiasConstantFactor = rs.depthBiasConstant;
		rasterizer.depthBiasSlopeFactor    = rs.depthBiasSlope;
		rasterizer.lineWidth               = rs.lineWidth;

		/// マルチサンプリング
		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.rasterizationSamples = toVkSampleCount(desc.multisample.sampleCount);
		multisampling.sampleShadingEnable  = desc.multisample.sampleShadingEnable ? VK_TRUE : VK_FALSE;
		multisampling.minSampleShading     = desc.multisample.minSampleShading;

		/// 深度ステンシル
		const auto& ds = desc.depthStencil;
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable  = ds.depthTestEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = ds.depthWriteEnable ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp   = toVkCompareOp(ds.depthCompareOp);
		depthStencil.stencilTestEnable = ds.stencilTestEnable ? VK_TRUE : VK_FALSE;

		/// カラーブレンディング
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		if (desc.blendMode == BlendMode::Alpha)
		{
			colorBlendAttachment.blendEnable         = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
		}
		else if (desc.blendMode == BlendMode::Additive)
		{
			colorBlendAttachment.blendEnable         = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
		}
		else if (desc.blendMode == BlendMode::Multiplicative)
		{
			colorBlendAttachment.blendEnable         = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable   = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments    = &colorBlendAttachment;

		/// 動的ステート（ビューポートとシザーは実行時に設定する）
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates    = dynamicStates;

		/// パイプラインレイアウト
		std::vector<VkPushConstantRange> pushConstants;
		for (const auto& pcr : desc.layout.pushConstantRanges)
		{
			VkPushConstantRange vkPcr{};
			vkPcr.stageFlags = toVkShaderStage(pcr.stageFlags);
			vkPcr.offset     = pcr.offset;
			vkPcr.size       = pcr.size;
			pushConstants.push_back(vkPcr);
		}

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount         = desc.layout.descriptorSetLayoutCount;
		layoutInfo.pushConstantRangeCount = static_cast<std::uint32_t>(pushConstants.size());
		layoutInfo.pPushConstantRanges    = pushConstants.empty() ? nullptr : pushConstants.data();

		if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
		{
			destroyShaderModules(shaderModules, m_device);
			throw std::runtime_error("vkCreatePipelineLayout failed");
		}

		/// グラフィックスパイプラインを生成する
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount          = static_cast<std::uint32_t>(shaderStages.size());
		pipelineInfo.pStages             = shaderStages.empty() ? nullptr : shaderStages.data();
		pipelineInfo.pVertexInputState   = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState      = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState   = &multisampling;
		pipelineInfo.pDepthStencilState  = &depthStencil;
		pipelineInfo.pColorBlendState    = &colorBlending;
		pipelineInfo.pDynamicState       = &dynamicState;
		pipelineInfo.layout              = m_layout;
		pipelineInfo.renderPass          = renderPass;
		pipelineInfo.subpass             = 0;

		if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
		{
			destroyShaderModules(shaderModules, m_device);
			vkDestroyPipelineLayout(m_device, m_layout, nullptr);
			m_layout = VK_NULL_HANDLE;
			throw std::runtime_error("vkCreateGraphicsPipelines failed");
		}

		/// シェーダーモジュールはパイプライン生成後に不要になるため破棄する
		destroyShaderModules(shaderModules, m_device);
	}

	/// @brief デストラクタ
	~VulkanGraphicsPipeline() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			if (m_pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(m_device, m_pipeline, nullptr);
			}
			if (m_layout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(m_device, m_layout, nullptr);
			}
		}
	}

	/// コピー禁止
	VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
	VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

	/// ムーブ禁止
	VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) = delete;
	VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) = delete;

	/// @brief パイプラインが有効かどうかを判定する
	/// @return VkPipelineハンドルが有効ならtrue
	[[nodiscard]] bool isValid() const noexcept override
	{
		return m_pipeline != VK_NULL_HANDLE;
	}

	/// @brief 内部のVkPipelineハンドルを取得する
	/// @return VkPipelineハンドル
	[[nodiscard]] VkPipeline handle() const noexcept
	{
		return m_pipeline;
	}

	/// @brief 内部のVkPipelineLayoutハンドルを取得する
	/// @return VkPipelineLayoutハンドル
	[[nodiscard]] VkPipelineLayout layout() const noexcept
	{
		return m_layout;
	}

	/// @brief 生成パラメータを取得する
	/// @return パイプライン記述子へのconst参照
	[[nodiscard]] const GraphicsPipelineDesc& desc() const noexcept
	{
		return m_desc;
	}

private:
	void destroyShaderModules(std::vector<VkShaderModule>& modules, VkDevice device) noexcept
	{
		for (auto mod : modules)
		{
			if (mod != VK_NULL_HANDLE)
			{
				vkDestroyShaderModule(device, mod, nullptr);
			}
		}
		modules.clear();
	}

	[[nodiscard]] static VkShaderStageFlagBits toVkShaderStage(ShaderType type) noexcept
	{
		switch (type)
		{
		case ShaderType::Vertex:  return VK_SHADER_STAGE_VERTEX_BIT;
		case ShaderType::Pixel:   return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ShaderType::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
		}
		return VK_SHADER_STAGE_VERTEX_BIT;
	}

	[[nodiscard]] static VkPrimitiveTopology toVkTopology(PrimitiveTopology topo) noexcept
	{
		switch (topo)
		{
		case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		}
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}

	[[nodiscard]] static VkPolygonMode toVkPolygonMode(PolygonMode mode) noexcept
	{
		switch (mode)
		{
		case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
		case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
		case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
		}
		return VK_POLYGON_MODE_FILL;
	}

	[[nodiscard]] static VkCullModeFlags toVkCullMode(CullMode mode) noexcept
	{
		switch (mode)
		{
		case CullMode::None:  return VK_CULL_MODE_NONE;
		case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
		case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
		}
		return VK_CULL_MODE_NONE;
	}

	[[nodiscard]] static VkFrontFace toVkFrontFace(FrontFace face) noexcept
	{
		switch (face)
		{
		case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case FrontFace::Clockwise:        return VK_FRONT_FACE_CLOCKWISE;
		}
		return VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}

	[[nodiscard]] static VkCompareOp toVkCompareOp(CompareOp op) noexcept
	{
		switch (op)
		{
		case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
		case CompareOp::Less:           return VK_COMPARE_OP_LESS;
		case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
		case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
		case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
		}
		return VK_COMPARE_OP_ALWAYS;
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

	[[nodiscard]] static VkFormat toVkVertexFormat(PixelFormat fmt) noexcept
	{
		switch (fmt)
		{
		case PixelFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
		case PixelFormat::BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
		case PixelFormat::R8:    return VK_FORMAT_R8_UNORM;
		default:                 return VK_FORMAT_UNDEFINED;
		}
	}

	VkDevice             m_device   = VK_NULL_HANDLE;
	VkPipeline           m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout     m_layout   = VK_NULL_HANDLE;
	GraphicsPipelineDesc m_desc;
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
