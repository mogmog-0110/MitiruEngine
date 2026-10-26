#pragma once

// モデル (glTF) の材質が持つ画像 (基本色・法線・自発光)。材質ごとに 1 つの束 (bind group) にして、描画の前に結ぶ。
// 画像を持たない材質や読めなかった画像の所は、1 画素の既定の画像 (白、平らな法線、黒) を結ぶので、シェーダーは分岐しなくてよい。
// 金属・粗さの画像は使わない (半透明の Phong もトゥーンの段も、金属・粗さを読まない)。

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/render/GltfTypes.hpp>

#include "DawnContext.hpp"

namespace mitiru::render::dawn_detail
{

struct DawnMaterialMaps
{
	wgpu::BindGroup group;
	float mapFlags[4] = {};    // x = 基本色の画像 y = 法線の画像 z = 自発光の画像 w = 抜きのしきい値 (0 は抜かない)
	float mapParams[4] = {};   // x = 法線の強さ y = 1 なら最近傍で読む
	float emissive[4] = {};    // rgb = 自発光の係数
};

/// GPU に触れずに作れる 1 枚の画像 (縮小と mip の連鎖まで)。ワーカーで作り、メインスレッドが GPU に上げる
struct DawnImageData
{
	int width = 0;
	int height = 0;
	std::vector<std::vector<std::uint8_t>> mips;

	[[nodiscard]] bool valid() const noexcept { return !mips.empty(); }
};

/// 1 つの材質のワーカーで作れる部分。画像のほかは、束を作るのに要る指定だけを残す (デコード済みの画素は持ち越さない)
struct DawnPreparedMaterial
{
	DawnImageData base;
	DawnImageData normal;
	DawnImageData emissive;
	bool mask = false;
	float alphaCutoff = 0.5f;
	bool nearestFilter = false;
	float normalScale = 1.0f;
	float emissiveFactor[3] = {};
};

class DawnMaterialPool
{
public:
	explicit DawnMaterialPool(gfx::DawnContext& ctx) : m_ctx(ctx) {}

	[[nodiscard]] bool create();

	[[nodiscard]] const wgpu::BindGroupLayout& layout() const noexcept { return m_layout; }
	/// 画像を持たない描画に結ぶ束
	[[nodiscard]] const wgpu::BindGroup& defaultGroup() const noexcept { return m_defaultGroup; }

	/// 基本色だけを画像にした束 (法線は平ら、自発光は黒)。副ビューの出力を材質の基本色に結ぶのに使う
	[[nodiscard]] wgpu::BindGroup makeBaseGroup(const wgpu::TextureView& base) const { return makeGroup(base, m_flatNormal, m_black); }

	/// 画像の読み込みと縮小と mip の連鎖までの CPU の仕事。GPU にも pool の表にも触れないので、どのスレッドからでも呼べる。
	/// modelPath は外部 URI の起点
	[[nodiscard]] static DawnPreparedMaterial prepare(const GltfMaterialData& material, const std::string& modelPath);

	/// prepare の結果を GPU に上げて束にする。メインスレッドだけ。画像も、抜き・最近傍の指定も持たなければ nullptr。
	/// modelPath と materialIndex は同じ画像を上げ直さないための鍵に使う
	[[nodiscard]] std::shared_ptr<const DawnMaterialMaps> make(const DawnPreparedMaterial& prepared, const std::string& modelPath,
	                                                           std::size_t materialIndex);

private:
	enum class Role { Base, Normal, Emissive };
	[[nodiscard]] static DawnImageData imageFor(const CpuTexture& decoded, const std::string& uri, Role role, const std::string& modelPath);
	[[nodiscard]] static DawnImageData buildImage(const std::uint8_t* rgba, int width, int height, Role role);
	[[nodiscard]] wgpu::TextureView viewFor(const DawnImageData& image, Role role, const std::string& key);
	[[nodiscard]] wgpu::TextureView makeView(const DawnImageData& image, Role role) const;
	[[nodiscard]] wgpu::TextureView onePixel(const std::uint8_t rgba[4], bool srgb) const;
	[[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& base, const wgpu::TextureView& normal,
	                                        const wgpu::TextureView& emissive) const;

	gfx::DawnContext& m_ctx;
	wgpu::BindGroupLayout m_layout;
	wgpu::Sampler m_linear;
	wgpu::Sampler m_nearest;
	wgpu::TextureView m_white;
	wgpu::TextureView m_flatNormal;
	wgpu::TextureView m_black;
	wgpu::BindGroup m_defaultGroup;
	std::unordered_map<std::string, wgpu::TextureView> m_views;   ///< 鍵 = モデル + 材質 + 役目。読めなかった画像は空の view
};

} // namespace mitiru::render::dawn_detail
