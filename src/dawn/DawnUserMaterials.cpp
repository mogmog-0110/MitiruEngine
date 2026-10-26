#include "DawnUserMaterials.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/render/RenderFeatureNote.hpp>

namespace mitiru::render::dawn_detail
{

namespace
{

/// 焼いた .wgsl の先頭行「// mitiru-material <name> source=<16 桁>」から、元の内容のハッシュを取る。無ければ空
std::string bakedSourceHash(const std::string& wgsl)
{
	constexpr std::string_view kKey = "source=";
	const std::size_t eol = wgsl.find('\n');
	const std::string_view first(wgsl.data(), eol == std::string::npos ? wgsl.size() : eol);
	if (first.rfind("// mitiru-material ", 0) != 0) { return {}; }
	const std::size_t at = first.find(kKey);
	return at == std::string_view::npos ? std::string() : std::string(first.substr(at + kKey.size()));
}

std::string lowerSlashed(std::string s)
{
	for (char& c : s) { c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	return s;
}

bool endsWith(const std::string& s, const std::string& tail)
{
	return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

bool DawnUserMaterials::build(const std::string& name, Material& out, std::string& error, bool& stale)
{
	stale = false;
	auto src = loadUserMaterialDesc(name, error);
	if (!src) { return false; }
	const std::string wgslPath = userMaterialPath(name, ".wgsl");
	const auto wgsl = vfs::readAssetText(wgslPath);
	if (!wgsl)
	{
		error = wgslPath + " を読めません。python tools/bake_materials.py assets/materials で焼いてください";
		return false;
	}
	// .slang があるときだけ、焼いた後に書き換えていないか確かめる。出荷物に .slang は要らない
	if (const auto slang = vfs::readAssetText(src->slangPath))
	{
		src->slang = *slang;
		const std::string baked = bakedSourceHash(*wgsl);
		if (baked != userMaterialSourceHash(*src))
		{
			stale = true;
			debug::warnOnce("dawn.usermat.stale." + name, "自作の材質 " + name + " の .wgsl が .slang と合っていないので、bake_materials.py で焼き直してください");
		}
	}
	out.desc = std::move(src->desc);
	out.module = m_ctx.makeShader(*wgsl, "mitiru.usermat");
	for (int side = 0; side < 2; ++side) { m_pipelines.startUserScene(out.pipelines[side], out.module, side == 1); }
	return true;
}

int DawnUserMaterials::registerMaterial(const char* name)
{
	if (name == nullptr || name[0] == '\0') { return -1; }
	for (std::size_t i = 0; i < m_materials.size(); ++i)
	{
		if (m_materials[i].desc.name == name) { return static_cast<int>(i) + 1; }
	}
	if (m_failed.count(name) != 0) { return -1; }
	if (count() >= kMaxUserMaterials)
	{
		debug::warnOnce("dawn.usermat.limit", "自作の材質が上限を超えたので、登録する材質を減らしてください");
		return -1;
	}
	Material material;
	std::string error;
	bool stale = false;
	if (!build(name, material, error, stale))
	{
		m_failed.insert(name);
		debug::warnOnce("dawn.usermat.fail." + std::string(name), "自作の材質 " + std::string(name) + " を作れないので、-v で出る理由を確かめてください");
		debug::verboseOnce("dawn.usermat.why." + std::string(name), "自作の材質 " + std::string(name) + " の作成に失敗: " + error);
		return -1;
	}
	m_materials.push_back(std::move(material));
	return count();
}

void DawnUserMaterials::set(int id, const float* params, int count, float timeSec)
{
	int newId = 0;
	std::array<float, kMaxUserMaterialParams> newParams{};
	float newTime = 0.0f;
	if (id > 0 && id <= this->count())
	{
		newId = id;
		float packed[kMaxUserMaterialParams];
		packUserMaterialParams(m_materials[static_cast<std::size_t>(id) - 1].desc, params, count, packed);
		std::copy(std::begin(packed), std::end(packed), newParams.begin());
		newTime = std::isfinite(timeSec) ? timeSec : 0.0f;
	}
	else if (id > 0)
	{
		debug::verboseOnce("dawn.usermat.unknown_id", "登録していない番号の材質が頼まれたので、場面の陰影で描きます。");
	}
	if (newId != m_activeId || newParams != m_params || newTime != m_time) { ++m_version; }
	m_activeId = newId;
	m_params = newParams;
	m_time = newTime;
}

void DawnUserMaterials::fill(DrawCB& cb) const noexcept
{
	if (m_activeId <= 0) { return; }
	for (int i = 0; i < kMaxUserMaterialParams; ++i) { cb.userParams[i / 4][i % 4] = m_params[static_cast<std::size_t>(i)]; }
	cb.userInfo[0] = m_time;
}

const wgpu::RenderPipeline& DawnUserMaterials::pipeline(int id, bool cullNone)
{
	static const wgpu::RenderPipeline kNone;
	if (id <= 0 || id > count()) { return kNone; }
	return m_materials[static_cast<std::size_t>(id) - 1].pipelines[cullNone ? 1 : 0].get();
}

int DawnUserMaterials::reload(const std::string& changedPath)
{
	const std::string changed = lowerSlashed(changedPath);
	int reloaded = 0;
	std::vector<std::string> names;
	for (const Material& m : m_materials) { names.push_back(m.desc.name); }
	for (const std::string& f : m_failed) { names.push_back(f); }
	for (const std::string& name : names)
	{
		const bool hit = endsWith(changed, lowerSlashed(userMaterialPath(name, ".wgsl"))) ||
		                 endsWith(changed, lowerSlashed(userMaterialPath(name, ".mat.json"))) ||
		                 endsWith(changed, lowerSlashed(userMaterialPath(name, ".slang")));
		if (!hit) { continue; }
		const bool registered = std::any_of(m_materials.begin(), m_materials.end(), [&name](const Material& m) { return m.desc.name == name; });
		if (!registered && count() >= kMaxUserMaterials) { continue; }
		Material fresh;
		std::string error;
		bool stale = false;
		if (!build(name, fresh, error, stale))
		{
			debug::warnOnce("dawn.usermat.reload." + name + std::to_string(++m_reloadFailures),
			                "自作の材質 " + name + " を読み込み直せないので、-v で出る理由を確かめてください");
			console::verbosef("自作の材質 %s の読み込みに失敗: %s", name.c_str(), error.c_str());
			continue;
		}
		if (stale && registered)
		{
			// .slang か .mat.json だけが変わった。焼き直すまでは、前の .wgsl で描く
			console::verbosef("自作の材質 %s の .wgsl は焼き直しが要ります。python tools/bake_materials.py で焼いてください。", name.c_str());
			continue;
		}
		m_failed.erase(name);
		bool replaced = false;
		for (Material& m : m_materials)
		{
			if (m.desc.name == name) { m = std::move(fresh); replaced = true; break; }
		}
		if (!replaced) { m_materials.push_back(std::move(fresh)); }
		++m_version;
		++reloaded;
		console::verbosef("自作の材質 %s を読み込み直しました。", name.c_str());
	}
	return reloaded;
}

} // namespace mitiru::render::dawn_detail
