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

/// @brief VkPipelineのRAIIラッパー
/// @details グラフィックスパイプラインの生成・破棄をRAIIで管理する。
class VulkanGraphicsPipeline : public IPipeline
{
public:
	/// @brief コンストラクタ
	/// @param desc パイプライン生成パラメータ
	explicit VulkanGraphicsPipeline(const GraphicsPipelineDesc& desc)
		: m_desc(desc)
	{
		/// TODO: vkCreateGraphicsPipelines呼び出し
	}

	/// @brief デストラクタ
	~VulkanGraphicsPipeline() override
	{
		/// TODO: vkDestroyPipeline呼び出し
	}

	/// コピー禁止
	VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
	VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

	/// ムーブ禁止
	VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) = delete;
	VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) = delete;

	/// @brief パイプラインが有効かどうかを判定する
	/// @return スタブ実装のため常にfalse
	[[nodiscard]] bool isValid() const noexcept override
	{
		return false;
	}

	/// @brief 生成パラメータを取得する
	/// @return パイプライン記述子へのconst参照
	[[nodiscard]] const GraphicsPipelineDesc& desc() const noexcept
	{
		return m_desc;
	}

private:
	GraphicsPipelineDesc m_desc;  ///< 生成パラメータ
};

#endif // MITIRU_HAS_VULKAN

} // namespace mitiru::gfx
