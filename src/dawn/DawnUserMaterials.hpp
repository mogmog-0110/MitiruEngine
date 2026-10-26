#pragma once

// 自作の材質（ADR 0100）の Dawn 用宣言。
// assets/materials/<name>.wgsl と <name>.mat.json を読み、場面と同じ頂点で描く片面用と両面用のパイプラインを作る。
// WGSL は tools/bake_materials.py が Slang から生成し、利用者の material() を含む fsMain を持つ。
// 実行時には Slang を使わない。
// パイプラインは登録時に非同期で作り始め、最初の描画までに終わらなければ待つ。

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <mitiru/render/UserMaterial.hpp>

#include "DawnContext.hpp"
#include "DawnRenderTypes.hpp"
#include "DawnScenePipelines.hpp"

namespace mitiru::render::dawn_detail
{

class DawnUserMaterials
{
public:
	DawnUserMaterials(gfx::DawnContext& ctx, DawnScenePipelines& pipelines) : m_ctx(ctx), m_pipelines(pipelines) {}

	/// 同じ名前には同じ番号を返す。番号は 1 から始まる。作れなければ -1 を返し、理由は 1 度だけ出す。
	[[nodiscard]] int registerMaterial(const char* name);

	/// 以後の描画で使う材質を設定する。id が 0 または未登録なら、場面の陰影に戻る。
	void set(int id, const float* params, int count, float timeSec);
	void beginFrame() noexcept { m_activeId = 0; }
	[[nodiscard]] int active() const noexcept { return m_activeId; }
	/// 材質の値が変わるたびに増える。描画定数を使い回せるかの判定に含める。
	[[nodiscard]] std::uint64_t version() const noexcept { return m_version; }

	/// 現在の材質の値を定数の末尾（DrawCB::userParams、userInfo）へ書く。
	void fill(DrawCB& cb) const noexcept;

	/// id の材質のパイプラインを返す。作れていなければ空を返し、呼び出し側は場面の陰影で描く。
	[[nodiscard]] const wgpu::RenderPipeline& pipeline(int id, bool cullNone);

	/// changedPath を元にした材質を作り直し、その数を返す。
	int reload(const std::string& changedPath);

	[[nodiscard]] int count() const noexcept { return static_cast<int>(m_materials.size()); }

private:
	struct Material
	{
		UserMaterialDesc desc;
		wgpu::ShaderModule module;
		gfx::AsyncRenderPipeline pipelines[2];   ///< [0=背面を除く 1=両面]
	};

	/// stale は、.slang または .mat.json が生成後に書き換わり、.wgsl のハッシュと合わないときに true になる。
	[[nodiscard]] bool build(const std::string& name, Material& out, std::string& error, bool& stale);

	gfx::DawnContext& m_ctx;
	DawnScenePipelines& m_pipelines;
	std::vector<Material> m_materials;
	std::unordered_set<std::string> m_failed;
	int m_activeId = 0;
	std::array<float, kMaxUserMaterialParams> m_params{};
	float m_time = 0.0f;
	std::uint64_t m_version = 0;
	int m_reloadFailures = 0;
};

} // namespace mitiru::render::dawn_detail
